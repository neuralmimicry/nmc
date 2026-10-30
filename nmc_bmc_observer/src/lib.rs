use anyhow::{bail, ensure, Context, Result};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    os::unix::fs::{MetadataExt, OpenOptionsExt},
    path::{Path, PathBuf},
    time::{SystemTime, UNIX_EPOCH},
};

pub fn now() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as i64
}
pub fn fresh(t: i64, at: i64, age: i64) -> bool {
    t > 0 && t <= at && at - t <= age
}
fn id(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 128
        && s.as_bytes()[0].is_ascii_alphanumeric()
        && s.bytes()
            .all(|c| c.is_ascii_alphanumeric() || b"_.:-".contains(&c))
}
fn text(v: &Value) -> String {
    v.as_str().unwrap_or("").chars().take(128).collect()
}
fn hash(v: &Value) -> String {
    format!("{:x}", Sha256::digest(v.to_string().as_bytes()))
}
fn array(v: &Value) -> impl Iterator<Item = &Value> {
    v.as_array().into_iter().flatten()
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Controller {
    pub id: String,
    pub protocol: String,
    pub endpoint: String,
    pub manages: Vec<String>,
    #[serde(default)]
    pub event_token_env: Option<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Config {
    pub controllers: Vec<Controller>,
    pub enroll_push: bool,
    pub interval_seconds: i64,
    pub stale_seconds: i64,
    pub inventory_max_age_seconds: i64,
    pub listen_address: String,
    pub port: u16,
    pub state_directory: PathBuf,
    pub metrics_token_file: PathBuf,
}
impl Config {
    pub fn validate(&self) -> Result<()> {
        ensure!(
            !self.controllers.is_empty() && self.controllers.len() <= 32,
            "bounded explicit controllers required"
        );
        ensure!(
            (60..=900).contains(&self.interval_seconds)
                && (self.interval_seconds * 2..=3600).contains(&self.stale_seconds)
                && (300..=86400).contains(&self.inventory_max_age_seconds),
            "invalid observation intervals"
        );
        ensure!(
            self.port >= 1024
                && ["127.0.0.1", "0.0.0.0"].contains(&self.listen_address.as_str())
                && self.state_directory.is_absolute()
                && self.metrics_token_file.is_absolute(),
            "invalid listener or private paths"
        );
        let mut ids = std::collections::BTreeSet::new();
        for c in &self.controllers {
            ensure!(
                id(&c.id)
                    && ids.insert(&c.id)
                    && ["ipmi", "redfish", "turingpi"].contains(&c.protocol.as_str())
                    && !c.endpoint.is_empty()
                    && c.endpoint.len() <= 512
                    && !c.manages.is_empty()
                    && c.manages.len() <= 32
                    && c.manages.iter().all(|x| id(x)),
                "invalid pinned controller identity"
            );
            if let Some(name) = &c.event_token_env {
                ensure!(
                    name.len() <= 128
                        && !name.is_empty()
                        && (name.as_bytes()[0].is_ascii_alphabetic() || name.starts_with('_'))
                        && name.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_'),
                    "invalid credential reference"
                );
            }
        }
        Ok(())
    }
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Enrollment {
    pub request_id: String,
    pub revision: String,
    pub phase: String,
    pub checked_at: i64,
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ControllerState {
    pub identity_valid: bool,
    pub available: bool,
    pub poll_at: i64,
    pub push_at: i64,
    pub events_total: u64,
    pub seen_events: Vec<String>,
    pub diagnostic: Value,
    pub enrollment: Option<Enrollment>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct State {
    pub version: u8,
    pub controllers: BTreeMap<String, ControllerState>,
}
impl Default for State {
    fn default() -> Self {
        Self {
            version: 1,
            controllers: BTreeMap::new(),
        }
    }
}
fn private_file(path: &Path, mut options: OpenOptions) -> Result<File> {
    let file = options
        .custom_flags(libc::O_NOFOLLOW | libc::O_CLOEXEC)
        .mode(0o600)
        .open(path)?;
    let st = file.metadata()?;
    ensure!(
        st.is_file()
            && st.nlink() == 1
            && st.mode() & 0o077 == 0
            && st.uid() == unsafe { libc::geteuid() },
        "unsafe state or credential file"
    );
    Ok(file)
}
pub fn private_read(path: &Path, max: u64) -> Result<Vec<u8>> {
    let mut opts = OpenOptions::new();
    opts.read(true);
    let file = private_file(path, opts)?;
    ensure!(file.metadata()?.len() <= max, "private file exceeds bounds");
    let mut bytes = Vec::new();
    file.take(max + 1).read_to_end(&mut bytes)?;
    ensure!(bytes.len() as u64 <= max, "private file grew past bounds");
    Ok(bytes)
}
pub struct Store {
    pub directory: PathBuf,
    pub state: State,
    _lock: File,
}
impl Store {
    pub fn open(directory: PathBuf) -> Result<Self> {
        let st = fs::symlink_metadata(&directory)?;
        ensure!(
            st.is_dir()
                && !st.file_type().is_symlink()
                && st.mode() & 0o077 == 0
                && st.uid() == unsafe { libc::geteuid() },
            "private state directory required"
        );
        let mut opts = OpenOptions::new();
        opts.read(true).write(true).create(true);
        let lock = private_file(&directory.join("observer.lock"), opts)?;
        use std::os::fd::AsRawFd;
        ensure!(
            unsafe { libc::flock(lock.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } == 0,
            "another BMC observer holds the state lock"
        );
        let state_path = directory.join("state.json");
        let state: State = if state_path.try_exists()? {
            serde_json::from_slice(&private_read(&state_path, 4 * 1024 * 1024)?)?
        } else {
            State::default()
        };
        ensure!(
            state.version == 1
                && state.controllers.len() <= 32
                && state.controllers.iter().all(|(id_, s)| id(id_)
                    && s.seen_events.len() <= 512
                    && s.seen_events.iter().all(|h| h.len() == 64)),
            "invalid durable BMC state"
        );
        Ok(Self {
            directory,
            state,
            _lock: lock,
        })
    }
    pub fn commit(&self, record: Value) -> Result<()> {
        let log = self.directory.join("observations.jsonl");
        if log.try_exists()? {
            let mut opts = OpenOptions::new();
            opts.read(true);
            let file = private_file(&log, opts)?;
            if file.metadata()?.len() > 16 * 1024 * 1024 {
                for i in (1..=3).rev() {
                    let old = self.directory.join(format!("observations.jsonl.{i}"));
                    if old.try_exists()? {
                        let mut opts = OpenOptions::new();
                        opts.read(true);
                        private_file(&old, opts)?;
                        if i == 3 {
                            fs::remove_file(&old)?;
                        } else {
                            fs::rename(
                                &old,
                                self.directory.join(format!("observations.jsonl.{}", i + 1)),
                            )?;
                        }
                    }
                }
                fs::rename(&log, self.directory.join("observations.jsonl.1"))?;
            }
        }
        let mut opts = OpenOptions::new();
        opts.write(true).create(true).append(true);
        let mut file = private_file(&log, opts)?;
        writeln!(file, "{record}")?;
        file.sync_all()?;
        let temporary = self
            .directory
            .join(format!(".state-{}", uuid::Uuid::new_v4()));
        let mut opts = OpenOptions::new();
        opts.write(true).create_new(true);
        let mut file = private_file(&temporary, opts)?;
        file.write_all(&serde_json::to_vec(&self.state)?)?;
        file.sync_all()?;
        fs::rename(temporary, self.directory.join("state.json"))?;
        File::open(&self.directory)?.sync_all()?;
        Ok(())
    }
}
pub fn unwrap(mut v: Value) -> Result<Value> {
    for _ in 0..8 {
        if let Some(success) = v.get("success") {
            ensure!(
                success.as_bool() == Some(true) && v.get("data").is_some(),
                "typed operation rejected"
            );
            v = v["data"].take();
        } else {
            ensure!(v.is_object(), "invalid typed response");
            if let Some(status) = v.get("upstream_status") {
                ensure!(
                    status.as_i64().is_some_and(|s| (200..300).contains(&s)),
                    "upstream operation rejected"
                );
            }
            return Ok(v);
        }
    }
    bail!("typed response exceeds nesting bound")
}
pub fn inventory_matches(c: &Controller, inv: &Value, at: i64, max_age: i64) -> bool {
    if inv["stale"].as_bool() != Some(false)
        || !fresh(
            inv["generated_at_unix_ms"].as_i64().unwrap_or(0),
            at,
            max_age,
        )
    {
        return false;
    }
    let Some(b) = array(&inv["controllers"]).find(|b| b["id"] == c.id) else {
        return false;
    };
    let mut manages: Vec<_> = array(&b["manages"]).filter_map(Value::as_str).collect();
    manages.sort();
    let mut expected: Vec<_> = c.manages.iter().map(String::as_str).collect();
    expected.sort();
    b["endpoint"] == c.endpoint
        && b["protocol"] == c.protocol
        && manages == expected
        && c.manages.iter().all(|id_| {
            array(&inv["devices"]).any(|d| d["id"] == *id_ && d["controller_id"] == c.id)
        })
}
pub fn project(d: &Value, at: i64) -> Value {
    let mut systems:Vec<Value>=array(&d["systems"]).take(32).map(|s|json!({"id":text(&s["id"]),"available":s["available"]==true && (s["power_state"]=="On" || s["power_state"]=="Off"),"power_state":text(&s["power_state"]),"health":text(s["status"].get("HealthRollup").unwrap_or(&s["status"]["Health"]))})).collect();
    if let Some(nodes) = d["nodes"].as_object() {
        for (slot, power) in nodes.iter().take(4) {
            systems.push(json!({"id":slot,"available":power=="On" || power=="Off","power_state":text(power),"health":""}));
        }
    }
    let firmware:Vec<Value>=array(&d["managers"]).take(32).filter(|m|m["available"]==true).map(|m|json!({"id":text(&m["id"]),"version":text(&m["firmware_version"]),"health":text(&m["status"]["Health"])})).collect();
    let mut sensors = Vec::new();
    let mut faults = BTreeMap::new();
    let mut log_ids = Vec::new();
    let mut log_used_percent = None;
    let logs_available;
    if d["protocol"] == "ipmi" {
        if d["system_event_log"]["info_available"] == true {
            let re = regex::Regex::new(r"(?m)^Percent Used\s*:\s*(\d+)%").unwrap();
            log_used_percent = re
                .captures(d["system_event_log"]["info"].as_str().unwrap_or(""))
                .and_then(|m| m[1].parse::<u8>().ok())
                .filter(|p| *p <= 100);
        }
        let chassis = d["chassis_status"].as_str().unwrap_or("");
        let power = regex::Regex::new(r"(?i)System Power\s*:\s*(on|off)\b")
            .unwrap()
            .captures(chassis);
        systems.push(json!({"id":"chassis","available":power.is_some(),"power_state":if power.as_ref().map(|p|p[1].eq_ignore_ascii_case("on"))==Some(true){"On"}else{"Off"},"health":""}));
        for (label, name) in [
            ("Main Power Fault", "power"),
            ("Cooling/Fan Fault", "cooling"),
            ("Drive Fault", "drive"),
            ("Power Control Fault", "power_control"),
        ] {
            if let Some(m) = regex::Regex::new(&format!(r"(?i){label}\s*:\s*(true|false)\b"))
                .unwrap()
                .captures(chassis)
            {
                faults.insert(name, m[1].eq_ignore_ascii_case("true"));
            }
        }
        if d["sensors"]["available"] == true {
            for line in d["sensors"]["output"]
                .as_str()
                .unwrap_or("")
                .lines()
                .take(256)
            {
                let parts: Vec<_> = line.split('|').map(str::trim).collect();
                if parts.len() >= 4 {
                    if let Ok(value) = parts[1].parse::<f64>() {
                        if value.is_finite() {
                            sensors.push(json!({"id":parts[0].chars().take(128).collect::<String>(),"value":value,"unit":parts[2].chars().take(128).collect::<String>(),"healthy":parts[3]=="ok"}));
                        }
                    }
                }
            }
        }
        logs_available = d["system_event_log"]["available"] == true;
        if logs_available {
            let re = regex::Regex::new(r"(?i)^[0-9a-f]+\s*\|").unwrap();
            for line in d["system_event_log"]["output"]
                .as_str()
                .unwrap_or("")
                .lines()
                .filter(|s| re.is_match(s.trim()))
                .take(20)
            {
                log_ids.push(hash(&json!(line)));
            }
        }
    } else {
        logs_available = array(&d["logs"]).any(|l| l["available"] == true);
        for log in array(&d["logs"]).take(4) {
            for e in array(&log["entries"]).take(20) {
                log_ids.push(hash(&json!([
                    e["Id"],
                    e["Created"],
                    e["MessageId"],
                    e["Severity"]
                ])));
            }
        }
    }
    json!({"observed_at_unix_ms":d["observed_at_unix_ms"].as_i64().unwrap_or(0),"fresh":d["inventory_stale"]==false && fresh(d["observed_at_unix_ms"].as_i64().unwrap_or(0),at,120000),"systems":systems,"firmware":firmware,"sensors":sensors,"faults":faults,"logs_available":logs_available,"log_used_percent":log_used_percent,"log_ids":log_ids,"push_supported":d["event_service"]["push_supported"]==true,"push_capability_known":d["event_service"]["available"]==true || d["event_service"]["capability"]=="not_advertised"})
}
#[derive(Debug)]
pub struct PersistenceFailure;
impl std::fmt::Display for PersistenceFailure {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("BMC state durability unavailable")
    }
}
impl std::error::Error for PersistenceFailure {}
pub fn accept_events(
    store: &mut Store,
    c: &Config,
    id_: &str,
    body: Value,
    inventory_at: i64,
    at: i64,
) -> Result<()> {
    ensure!(
        c.controllers.iter().any(|b| b.id == id_),
        "unknown controller"
    );
    let s = store
        .state
        .controllers
        .get(id_)
        .context("no controller state")?;
    ensure!(
        s.identity_valid
            && fresh(inventory_at, at, c.inventory_max_age_seconds * 1000)
            && fresh(s.poll_at, at, c.stale_seconds * 1000),
        "event identity not freshly reconciled"
    );
    ensure!(
        body["Context"] == format!("continuum-bmc/{id_}"),
        "wrong event context"
    );
    let events = body["Events"].as_array().context("invalid event batch")?;
    ensure!(
        !events.is_empty() && events.len() <= 32,
        "event batch exceeds bounds"
    );
    let mut projected = Vec::new();
    for e in events {
        ensure!(
            e["EventId"]
                .as_str()
                .is_some_and(|x| !x.is_empty() && x.len() <= 256)
                && e.get("EventTimestamp")
                    .is_none_or(|x| x.as_str().is_some_and(|s| s.len() <= 128)),
            "invalid event identity"
        );
        projected.push(json!({"id":e["EventId"],"time":text(&e["EventTimestamp"]),"type":text(&e["EventType"]),"severity":text(&e["Severity"]),"message_id":text(&e["MessageId"])}));
    }
    for e in projected {
        let previous = store.state.controllers[id_].clone();
        let key = hash(&e);
        let s = store.state.controllers.get_mut(id_).unwrap();
        if s.seen_events.contains(&key) {
            continue;
        }
        s.seen_events.push(key);
        if s.seen_events.len() > 512 {
            s.seen_events.remove(0);
        }
        s.events_total = s
            .events_total
            .checked_add(1)
            .context("event counter exhausted")?;
        if let Err(error) =
            store.commit(json!({"kind":"push_event","controller_id":id_,"at":at,"event":e}))
        {
            store.state.controllers.insert(id_.into(), previous);
            return Err(error.context(PersistenceFailure));
        }
    }
    let previous = store.state.controllers[id_].push_at;
    store.state.controllers.get_mut(id_).unwrap().push_at = at;
    if let Err(error) = store.commit(json!({"kind":"push_receipt","controller_id":id_,"at":at})) {
        store.state.controllers.get_mut(id_).unwrap().push_at = previous;
        return Err(error.context(PersistenceFailure));
    }
    Ok(())
}
fn prom_label(value: &str) -> String {
    let clean: String = value
        .chars()
        .filter(|c| !c.is_control() || *c == '\n')
        .collect();
    format!(
        "\"{}\"",
        clean
            .replace('\\', "\\\\")
            .replace('\"', "\\\"")
            .replace('\n', "\\n")
    )
}
pub fn metrics(c: &Config, state: &State, inventory_at: i64, at: i64) -> String {
    let inventory_fresh = fresh(inventory_at, at, c.inventory_max_age_seconds * 1000);
    let mut out = String::new();
    for (name, help, type_) in [
        (
            "inventory_fresh",
            "Verified device registry is fresh",
            "gauge",
        ),
        (
            "available",
            "BMC observation is fresh and identity verified",
            "gauge",
        ),
        (
            "identity_valid",
            "Controller and managed hosts match the fresh registry",
            "gauge",
        ),
        (
            "last_observation_timestamp_seconds",
            "BMC observation time in Unix seconds",
            "gauge",
        ),
        (
            "event_push_supported",
            "Advertised Redfish Event delivery is supported",
            "gauge",
        ),
        (
            "event_push_capability_known",
            "Push capability has been verified for this transport",
            "gauge",
        ),
        (
            "event_push_configured",
            "Owned event subscription was reconciled",
            "gauge",
        ),
        (
            "event_delivery_observed",
            "Authenticated event delivery has been received",
            "gauge",
        ),
        (
            "last_push_timestamp_seconds",
            "Most recent authenticated event receipt in Unix seconds",
            "gauge",
        ),
        (
            "push_events_total",
            "Durably accepted distinct BMC events",
            "counter",
        ),
        (
            "log_collection_available",
            "BMC event history collection is available",
            "gauge",
        ),
        (
            "polling_required",
            "State and event history polling remains required",
            "gauge",
        ),
        (
            "managed_host_info",
            "Verified host to controller association",
            "gauge",
        ),
        ("system_state_known", "System power state is known", "gauge"),
        ("power_on", "System reports power on", "gauge"),
        ("health_fault", "BMC resource health is not OK", "gauge"),
        (
            "firmware_info",
            "Observed BMC manager firmware version",
            "gauge",
        ),
        ("chassis_fault", "BMC reports a chassis fault", "gauge"),
        (
            "sensor_value",
            "Numeric BMC sensor reading with unit label",
            "gauge",
        ),
        ("sensor_healthy", "BMC sensor status is OK", "gauge"),
        (
            "log_capacity_available",
            "BMC SEL capacity evidence is fresh",
            "gauge",
        ),
        (
            "log_used_percent",
            "BMC SEL capacity used, percent",
            "gauge",
        ),
    ] {
        out.push_str(&format!(
            "# HELP continuum_bmc_{name} {help}\n# TYPE continuum_bmc_{name} {type_}\n"
        ));
    }
    out.push_str(&format!(
        "continuum_bmc_inventory_fresh {}\n",
        u8::from(inventory_fresh)
    ));
    for b in &c.controllers {
        let empty = ControllerState::default();
        let s = state.controllers.get(&b.id).unwrap_or(&empty);
        let p = &s.diagnostic;
        let is_fresh = inventory_fresh
            && s.identity_valid
            && s.available
            && fresh(
                p["observed_at_unix_ms"].as_i64().unwrap_or(0),
                at,
                c.stale_seconds * 1000,
            );
        let base = format!(
            "controller_id={},protocol={}",
            prom_label(&b.id),
            prom_label(&b.protocol)
        );
        let mut metric = |name: &str, value: f64, extra: String| {
            out.push_str(&format!(
                "continuum_bmc_{name}{{{base}{}}} {value}\n",
                if extra.is_empty() {
                    String::new()
                } else {
                    format!(",{extra}")
                }
            ));
        };
        let mut plain = |name: &str, value: f64| metric(name, value, String::new());
        plain("available", u8::from(is_fresh) as f64);
        plain(
            "identity_valid",
            u8::from(inventory_fresh && s.identity_valid) as f64,
        );
        plain(
            "last_observation_timestamp_seconds",
            p["observed_at_unix_ms"].as_i64().unwrap_or(0) as f64 / 1000.,
        );
        plain(
            "event_push_supported",
            u8::from(is_fresh && p["push_supported"] == true) as f64,
        );
        plain(
            "event_push_capability_known",
            u8::from(is_fresh && p["push_capability_known"] == true) as f64,
        );
        plain(
            "event_push_configured",
            u8::from(
                s.enrollment
                    .as_ref()
                    .is_some_and(|e| e.phase == "configured"),
            ) as f64,
        );
        plain("event_delivery_observed", u8::from(s.push_at > 0) as f64);
        plain("last_push_timestamp_seconds", s.push_at as f64 / 1000.);
        plain("push_events_total", s.events_total as f64);
        plain(
            "log_collection_available",
            u8::from(is_fresh && p["logs_available"] == true) as f64,
        );
        plain("polling_required", 1.);
        plain(
            "log_capacity_available",
            u8::from(is_fresh && p["log_used_percent"].is_number()) as f64,
        );
        if is_fresh {
            if let Some(percent) = p["log_used_percent"].as_f64() {
                plain("log_used_percent", percent);
            }
        }
        if !is_fresh {
            continue;
        }
        for host in &b.manages {
            metric(
                "managed_host_info",
                1.,
                format!("host={}", prom_label(host)),
            );
        }
        for x in array(&p["systems"]) {
            let label = format!("system={}", prom_label(&text(&x["id"])));
            metric(
                "system_state_known",
                u8::from(x["available"] == true) as f64,
                label.clone(),
            );
            if x["available"] == true {
                metric("power_on", u8::from(x["power_state"] == "On") as f64, label);
            }
            let h = text(&x["health"]);
            if !h.is_empty() {
                metric(
                    "health_fault",
                    u8::from(h != "OK") as f64,
                    format!("resource={}", prom_label(&text(&x["id"]))),
                );
            }
        }
        for x in array(&p["firmware"]) {
            metric(
                "firmware_info",
                1.,
                format!(
                    "manager={},version={}",
                    prom_label(&text(&x["id"])),
                    prom_label(&text(&x["version"]))
                ),
            );
            let h = text(&x["health"]);
            if !h.is_empty() {
                metric(
                    "health_fault",
                    u8::from(h != "OK") as f64,
                    format!("resource={}", prom_label(&text(&x["id"]))),
                );
            }
        }
        if let Some(faults) = p["faults"].as_object() {
            for (fault, value) in faults {
                metric(
                    "chassis_fault",
                    u8::from(*value == true) as f64,
                    format!("fault={}", prom_label(fault)),
                );
            }
        }
        for x in array(&p["sensors"]) {
            metric(
                "sensor_value",
                x["value"].as_f64().unwrap_or(0.),
                format!(
                    "sensor={},unit={}",
                    prom_label(&text(&x["id"])),
                    prom_label(&text(&x["unit"]))
                ),
            );
            metric(
                "sensor_healthy",
                u8::from(x["healthy"] == true) as f64,
                format!("sensor={}", prom_label(&text(&x["id"]))),
            );
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;
    use tempfile::TempDir;
    fn config() -> Config {
        serde_json::from_value(json!({"controllers":[{"id":"bmc01","protocol":"ipmi","endpoint":"192.168.1.51","manages":["qc01"],"event_token_env":"BMC01_EVENTS"}],"enroll_push":false,"interval_seconds":60,"stale_seconds":180,"inventory_max_age_seconds":86400,"listen_address":"127.0.0.1","port":9105,"state_directory":"/var/lib/nmc/bmc-observer","metrics_token_file":"/etc/nmc/bmc-metrics-token"})).unwrap()
    }
    fn inv() -> Value {
        json!({"generated_at_unix_ms":1000000,"stale":false,"revision":"revision1","controllers":[{"id":"bmc01","protocol":"ipmi","endpoint":"192.168.1.51","manages":["qc01"]}],"devices":[{"id":"qc01","controller_id":"bmc01"}]})
    }
    fn diag() -> Value {
        json!({"protocol":"ipmi","observed_at_unix_ms":1000000,"inventory_stale":false,"chassis_status":"System Power : on\nMain Power Fault : false\nCooling/Fan Fault : true","sensors":{"available":true,"output":"CPU Temp | 52.000 | degrees C | ok\nFan | na | RPM | ns"},"system_event_log":{"available":false,"output":""}})
    }
    fn event() -> Value {
        json!({"Context":"continuum-bmc/bmc01","Events":[{"EventId":"1","EventTimestamp":"2026-10-13T10:00:00Z","EventType":"Alert","Severity":"Warning","Message":"secret-do-not-store","MessageId":"Hardware.Fault"}]})
    }
    fn store() -> (TempDir, Store) {
        let dir = TempDir::new().unwrap();
        fs::set_permissions(dir.path(), fs::Permissions::from_mode(0o700)).unwrap();
        let mut store = Store::open(dir.path().into()).unwrap();
        store.state.controllers.insert(
            "bmc01".into(),
            ControllerState {
                identity_valid: true,
                available: true,
                poll_at: 1000000,
                diagnostic: project(&diag(), 1000000),
                ..Default::default()
            },
        );
        (dir, store)
    }
    #[test]
    fn pinned_identity_drift_and_stale_inventory_block_observations() {
        let c = config();
        assert!(inventory_matches(
            &c.controllers[0],
            &inv(),
            1000000,
            86400000
        ));
        for path in ["stale", "endpoint", "mapping"] {
            let mut i = inv();
            match path {
                "stale" => i["stale"] = json!(true),
                "endpoint" => i["controllers"][0]["endpoint"] = json!("192.168.1.99"),
                _ => i["devices"][0]["controller_id"] = json!("other"),
            };
            assert!(!inventory_matches(&c.controllers[0], &i, 1000000, 86400000));
        }
    }
    #[test]
    fn sensors_faults_and_unavailable_logs_are_distinct() {
        let (_, s) = store();
        let m = metrics(&config(), &s.state, 1000000, 1000000);
        assert!(m.contains("sensor=\"CPU Temp\",unit=\"degrees C\"} 52"));
        assert!(m.contains("fault=\"cooling\"} 1"));
        assert!(m.contains(
            "continuum_bmc_log_collection_available{controller_id=\"bmc01\",protocol=\"ipmi\"} 0"
        ));
        assert!(!m.contains("sensor=\"Fan\""));
    }
    #[test]
    fn stale_samples_never_export_old_sensor_values_as_current() {
        let (_, s) = store();
        let m = metrics(&config(), &s.state, 1000000, 1180001);
        assert!(!m
            .lines()
            .any(|line| line.starts_with("continuum_bmc_sensor_value{")));
        assert!(m.contains("continuum_bmc_available{controller_id=\"bmc01\",protocol=\"ipmi\"} 0"));
    }
    #[test]
    fn unknown_power_is_explicitly_unavailable() {
        let mut d = diag();
        d["chassis_status"] = json!("System Power : unknown");
        assert_eq!(project(&d, 1000000)["systems"][0]["available"], false);
    }
    #[test]
    fn duplicate_events_survive_restart_and_raw_messages_are_omitted() {
        let (dir, mut s) = store();
        accept_events(&mut s, &config(), "bmc01", event(), 1000000, 1000000).unwrap();
        accept_events(&mut s, &config(), "bmc01", event(), 1000000, 1000000).unwrap();
        assert_eq!(s.state.controllers["bmc01"].events_total, 1);
        assert!(!fs::read_to_string(dir.path().join("observations.jsonl"))
            .unwrap()
            .contains("secret-do-not-store"));
        drop(s);
        let mut s = Store::open(dir.path().into()).unwrap();
        accept_events(&mut s, &config(), "bmc01", event(), 1000000, 1000000).unwrap();
        assert_eq!(s.state.controllers["bmc01"].events_total, 1);
    }
    #[test]
    fn wrong_controller_context_and_oversized_batch_are_rejected_atomically() {
        let (_, mut s) = store();
        let mut e = event();
        e["Context"] = json!("continuum-bmc/other");
        assert!(accept_events(&mut s, &config(), "bmc01", e, 1000000, 1000000).is_err());
        let mut e = event();
        e["Events"] = json!(vec![event()["Events"][0].clone(); 33]);
        assert!(accept_events(&mut s, &config(), "bmc01", e, 1000000, 1000000).is_err());
        let mut e = event();
        e["Events"] = json!([event()["Events"][0],{"EventId":3}]);
        assert!(accept_events(&mut s, &config(), "bmc01", e, 1000000, 1000000).is_err());
        assert_eq!(s.state.controllers["bmc01"].events_total, 0);
    }
    #[test]
    fn stale_mapping_cannot_receive_events() {
        let (_, mut s) = store();
        s.state.controllers.get_mut("bmc01").unwrap().identity_valid = false;
        assert!(accept_events(&mut s, &config(), "bmc01", event(), 1000000, 1000000).is_err());
    }
    #[test]
    fn symlink_hardlink_and_non_private_state_are_rejected() {
        let (dir, s) = store();
        drop(s);
        fs::write(dir.path().join("target"), "{}").unwrap();
        std::os::unix::fs::symlink(dir.path().join("target"), dir.path().join("state.json"))
            .unwrap();
        assert!(Store::open(dir.path().into()).is_err());
        fs::remove_file(dir.path().join("state.json")).unwrap();
        fs::set_permissions(dir.path().join("target"), fs::Permissions::from_mode(0o600)).unwrap();
        fs::hard_link(dir.path().join("target"), dir.path().join("state.json")).unwrap();
        assert!(Store::open(dir.path().into()).is_err());
        fs::remove_file(dir.path().join("state.json")).unwrap();
        fs::set_permissions(dir.path(), fs::Permissions::from_mode(0o755)).unwrap();
        assert!(Store::open(dir.path().into()).is_err());
    }
    #[test]
    fn configuration_rejects_unbounded_and_ambiguous_targets() {
        let mut c = config();
        c.validate().unwrap();
        c.controllers.push(c.controllers[0].clone());
        assert!(c.validate().is_err());
        c = config();
        c.interval_seconds = 1;
        assert!(c.validate().is_err());
    }
    #[test]
    fn unknown_redfish_power_cannot_be_reported_as_off() {
        let d = json!({"protocol":"redfish","observed_at_unix_ms":1000000,"inventory_stale":false,"systems":[{"id":"1","available":true,"power_state":"","status":{"Health":"OK"}}]});
        let p = project(&d, 1000000);
        assert_eq!(p["systems"][0]["available"], false);
    }
    #[test]
    fn unknown_transport_push_capability_is_not_reported_as_known_unsupported() {
        let p = project(&diag(), 1000000);
        assert_eq!(p["push_capability_known"], false);
        assert_eq!(p["push_supported"], false);
    }
    #[test]
    fn uncertain_enrollment_preserves_request_and_revision_on_restart() {
        let (dir, mut s) = store();
        s.state.controllers.get_mut("bmc01").unwrap().enrollment = Some(Enrollment {
            request_id: "original-request".into(),
            revision: "original-revision".into(),
            phase: "unknown".into(),
            checked_at: 1000000,
        });
        s.commit(json!({"kind":"enrollment_intent"})).unwrap();
        drop(s);
        let s = Store::open(dir.path().into()).unwrap();
        let e = s.state.controllers["bmc01"].enrollment.as_ref().unwrap();
        assert_eq!(e.request_id, "original-request");
        assert_eq!(e.revision, "original-revision");
        assert_eq!(e.phase, "unknown");
    }
    #[test]
    fn metric_metadata_and_label_escaping_are_valid() {
        let (_, s) = store();
        let output = metrics(&config(), &s.state, 1000000, 1000000);
        assert!(output.contains("# TYPE continuum_bmc_push_events_total counter"));
        assert!(output.contains("# HELP continuum_bmc_sensor_value"));
        assert_eq!(prom_label("CPU\t\"Temp\"\n"), "\"CPU\\\"Temp\\\"\\n\"");
    }
    #[test]
    fn unavailable_durable_journal_does_not_accept_or_forget_an_event() {
        let (dir, mut s) = store();
        let target = dir.path().join("target");
        fs::write(&target, "private").unwrap();
        std::os::unix::fs::symlink(&target, dir.path().join("observations.jsonl")).unwrap();
        let error =
            accept_events(&mut s, &config(), "bmc01", event(), 1000000, 1000000).unwrap_err();
        assert!(error.is::<PersistenceFailure>());
        assert_eq!(s.state.controllers["bmc01"].events_total, 0);
        assert!(s.state.controllers["bmc01"].seen_events.is_empty());
        fs::remove_file(dir.path().join("observations.jsonl")).unwrap();
        accept_events(&mut s, &config(), "bmc01", event(), 1000000, 1000000).unwrap();
        assert_eq!(s.state.controllers["bmc01"].events_total, 1);
    }
    #[test]
    fn failed_typed_envelopes_cannot_be_observations() {
        assert!(unwrap(json!({"success":false,"data":{"available":true}})).is_err());
        assert!(unwrap(json!({"success":true})).is_err());
        assert_eq!(
            unwrap(json!({"success":true,"data":{"success":true,"data":{"value":1}}})).unwrap(),
            json!({"value":1})
        );
    }
}
