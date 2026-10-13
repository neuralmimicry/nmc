use anyhow::{bail, Context, Result};
use axum::{
    body::Bytes,
    extract::{DefaultBodyLimit, Path, State},
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    routing::{get, post},
    Router,
};
use nmc_bmc_observer::{
    accept_events, fresh, inventory_matches, metrics, now, private_read, project, unwrap, Config,
    Enrollment, Store,
};
use serde_json::{json, Value};
use std::{
    collections::BTreeMap,
    path::PathBuf,
    process::Stdio,
    sync::{
        atomic::{AtomicI64, Ordering},
        Arc,
    },
    time::Duration,
};
use subtle::ConstantTimeEq;
use tokio::{io::AsyncReadExt, process::Command, sync::Mutex};

struct App {
    config: Config,
    store: Mutex<Store>,
    inventory_at: AtomicI64,
    metrics_token: String,
    event_tokens: BTreeMap<String, String>,
    rate: Mutex<BTreeMap<String, (i64, u32)>>,
}
async fn nmc(args: Vec<String>) -> Result<Value> {
    let mut child = Command::new("/usr/bin/nmc")
        .args(args)
        .args(["--output", "json"])
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .kill_on_drop(true)
        .spawn()?;
    let stdout = child.stdout.take().context("no typed stdout")?;
    let call = async {
        let mut bytes = Vec::new();
        stdout
            .take(2 * 1024 * 1024 + 1)
            .read_to_end(&mut bytes)
            .await?;
        if bytes.len() > 2 * 1024 * 1024 {
            bail!("typed response exceeds bounds");
        }
        let status = child.wait().await?;
        if !status.success() {
            bail!("typed command failed");
        }
        let start = bytes
            .iter()
            .position(|c| *c == b'{')
            .context("missing typed response")?;
        unwrap(serde_json::from_slice(&bytes[start..])?)
    };
    tokio::time::timeout(Duration::from_secs(120), call)
        .await
        .context("typed observation deadline")?
}
async fn reconcile(app: &App, b: &nmc_bmc_observer::Controller) -> Result<()> {
    let e = app.store.lock().await.state.controllers[&b.id]
        .enrollment
        .clone()
        .context("missing enrollment intent")?;
    let request = json!({"controller_id":b.id,"request_id":e.request_id,"change_id":e.request_id,"expected_inventory_revision":e.revision,"reason":"Continuum BMC event collection with retained periodic state reconciliation"});
    let response = nmc(vec![
        "device".into(),
        "event-subscribe".into(),
        "--request-json".into(),
        request.to_string(),
    ])
    .await;
    let mut store = app.store.lock().await;
    let enrollment = store
        .state
        .controllers
        .get_mut(&b.id)
        .unwrap()
        .enrollment
        .as_mut()
        .unwrap();
    enrollment.phase = if response.is_ok_and(|v| v["configured"] == true) {
        "configured"
    } else {
        "unknown"
    }
    .into();
    enrollment.checked_at = now();
    let record =
        json!({"kind":"enrollment_result","controller_id":b.id,"enrollment":enrollment,"at":now()});
    store.commit(record)
}
async fn tick(app: &App) -> Result<()> {
    // No state or credential lock is held while waiting on a BMC; metrics remain responsive.
    app.inventory_at.store(0, Ordering::Relaxed);
    let inv = nmc(vec!["device".into(), "inventory".into()]).await?;
    if inv["stale"] != false
        || !fresh(
            inv["generated_at_unix_ms"].as_i64().unwrap_or(0),
            now(),
            app.config.inventory_max_age_seconds * 1000,
        )
    {
        bail!("inventory unavailable or stale");
    }
    app.inventory_at.store(
        inv["generated_at_unix_ms"].as_i64().unwrap_or(0),
        Ordering::Relaxed,
    );
    for chunk in app.config.controllers.chunks(2) {
        if chunk.len() == 2 {
            let (a, b) = tokio::join!(
                observe_controller(app, &chunk[0], &inv),
                observe_controller(app, &chunk[1], &inv)
            );
            a?;
            b?;
        } else {
            observe_controller(app, &chunk[0], &inv).await?;
        }
    }
    Ok(())
}
async fn observe_controller(
    app: &App,
    b: &nmc_bmc_observer::Controller,
    inv: &Value,
) -> Result<()> {
    let valid = inventory_matches(b, &inv, now(), app.config.inventory_max_age_seconds * 1000);
    {
        let mut store = app.store.lock().await;
        let state = store.state.controllers.entry(b.id.clone()).or_default();
        state.identity_valid = valid;
        state.available = false;
    }
    if !valid {
        app.store
            .lock()
            .await
            .commit(json!({"kind":"identity_gap","controller_id":b.id,"at":now()}))?;
        return Ok(());
    }
    match nmc(vec![
        "device".into(),
        "diagnostics".into(),
        "--controller-id".into(),
        b.id.clone(),
    ])
    .await
    {
        Ok(d) if d["controller_id"] == b.id && d["protocol"] == b.protocol => {
            let p = project(&d, now());
            let mut enroll = false;
            {
                let mut store = app.store.lock().await;
                let s = store.state.controllers.get_mut(&b.id).unwrap();
                s.available = p["fresh"] == true;
                s.poll_at = now();
                s.diagnostic = p.clone();
                let registered = inv["controllers"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .find(|x| x["id"] == b.id)
                    .unwrap();
                if app.config.enroll_push
                    && registered["event_push_enabled"] == true
                    && p["push_supported"] == true
                    && b.event_token_env.is_some()
                {
                    if s.enrollment.is_none()
                        || s.enrollment.as_ref().is_some_and(|e| {
                            e.phase == "configured" && now() - e.checked_at >= 900000
                        })
                    {
                        let request_id = format!("bmc-events-{}", uuid::Uuid::new_v4());
                        s.enrollment = Some(Enrollment {
                            request_id,
                            revision: inv["revision"]
                                .as_str()
                                .context("missing inventory revision")?
                                .into(),
                            phase: "intent".into(),
                            checked_at: 0,
                        });
                    }
                    enroll = s
                        .enrollment
                        .as_ref()
                        .is_some_and(|e| e.phase == "intent" || e.phase == "unknown");
                }
                let e = s.enrollment.clone();
                store.commit(
                    json!({"kind":"observation","controller_id":b.id,"at":now(),"diagnostic":p}),
                )?;
                if enroll {
                    store.commit(json!({"kind":"enrollment_intent","controller_id":b.id,"enrollment":e,"at":now()}))?;
                }
            }
            if enroll {
                reconcile(app, b).await?;
            }
        }
        _ => {
            app.store
                .lock()
                .await
                .commit(json!({"kind":"collection_gap","controller_id":b.id,"at":now()}))?;
        }
    }
    Ok(())
}
fn authorized(headers: &HeaderMap, expected: &str) -> bool {
    let actual = headers
        .get("authorization")
        .and_then(|h| h.to_str().ok())
        .and_then(|s| s.strip_prefix("Bearer "))
        .unwrap_or("");
    expected.len() >= 32 && actual.as_bytes().ct_eq(expected.as_bytes()).into()
}
fn response(status: StatusCode, body: String) -> Response {
    let mut r = (status, body).into_response();
    r.headers_mut()
        .insert("cache-control", "no-store".parse().unwrap());
    r
}
async fn scrape(State(app): State<Arc<App>>, headers: HeaderMap) -> Response {
    if !authorized(&headers, &app.metrics_token) {
        return response(StatusCode::UNAUTHORIZED, String::new());
    }
    let store = app.store.lock().await;
    let mut r = response(
        StatusCode::OK,
        metrics(
            &app.config,
            &store.state,
            app.inventory_at.load(Ordering::Relaxed),
            now(),
        ),
    );
    r.headers_mut()
        .insert("content-type", "text/plain; version=0.0.4".parse().unwrap());
    r
}
async fn events(
    State(app): State<Arc<App>>,
    Path(id): Path<String>,
    headers: HeaderMap,
    bytes: Bytes,
) -> Response {
    let Some(token) = app.event_tokens.get(&id) else {
        return response(StatusCode::UNAUTHORIZED, String::new());
    };
    if !authorized(&headers, token) {
        return response(StatusCode::UNAUTHORIZED, String::new());
    }
    if headers
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .and_then(|v| v.split(';').next())
        != Some("application/json")
    {
        return response(StatusCode::UNSUPPORTED_MEDIA_TYPE, String::new());
    }
    let at = now();
    {
        let mut rate = app.rate.lock().await;
        let (start, count) = rate.entry(id.clone()).or_insert((at, 0));
        if at - *start >= 60000 {
            *start = at;
            *count = 0;
        }
        *count += 1;
        if *count > 120 {
            return response(StatusCode::TOO_MANY_REQUESTS, String::new());
        }
    }
    let Ok(body) = serde_json::from_slice(&bytes) else {
        return response(StatusCode::BAD_REQUEST, String::new());
    };
    let mut store = app.store.lock().await;
    match accept_events(
        &mut store,
        &app.config,
        &id,
        body,
        app.inventory_at.load(Ordering::Relaxed),
        at,
    ) {
        Ok(()) => response(StatusCode::NO_CONTENT, String::new()),
        Err(_) => response(StatusCode::BAD_REQUEST, String::new()),
    }
}
#[tokio::main]
async fn main() -> Result<()> {
    let args: Vec<_> = std::env::args().collect();
    if args.get(1).is_some_and(|s| s == "--version") {
        println!("nmc-bmc-observer {}", env!("CARGO_PKG_VERSION"));
        return Ok(());
    }
    let config_path = PathBuf::from(
        args.get(1)
            .context("usage: nmc-bmc-observer CONFIG [--once]")?,
    );
    let config: Config = serde_json::from_slice(&private_read(&config_path, 128 * 1024)?)?;
    config.validate()?;
    let once = args.get(2).is_some_and(|s| s == "--once");
    let store = Store::open(config.state_directory.clone())?;
    let metrics_token = if once {
        String::new()
    } else {
        String::from_utf8(private_read(&config.metrics_token_file, 1024)?)?
            .trim()
            .into()
    };
    if !once && metrics_token.len() < 32 {
        bail!("dedicated metrics credential required");
    }
    let event_tokens = config
        .controllers
        .iter()
        .filter_map(|b| {
            b.event_token_env
                .as_ref()
                .and_then(|name| std::env::var(name).ok())
                .map(|token| (b.id.clone(), token))
        })
        .collect();
    let app = Arc::new(App {
        config,
        store: Mutex::new(store),
        inventory_at: AtomicI64::new(0),
        metrics_token,
        event_tokens,
        rate: Mutex::new(BTreeMap::new()),
    });
    if once {
        tick(&app).await?;
        let store = app.store.lock().await;
        print!(
            "{}",
            metrics(
                &app.config,
                &store.state,
                app.inventory_at.load(Ordering::Relaxed),
                now()
            )
        );
        return Ok(());
    }
    let listener =
        tokio::net::TcpListener::bind((app.config.listen_address.as_str(), app.config.port))
            .await?;
    let observer = app.clone();
    tokio::spawn(async move {
        loop {
            if tick(&observer).await.is_err() {
                observer.inventory_at.store(0, Ordering::Relaxed);
                eprintln!("BMC observation gap; inspect private state journal");
            }
            tokio::time::sleep(Duration::from_secs(observer.config.interval_seconds as u64)).await;
        }
    });
    let router = router(app);
    axum::serve(listener, router).await?;
    Ok(())
}

fn router(app: Arc<App>) -> Router {
    Router::new()
        .route("/metrics", get(scrape))
        .route("/events/{id}", post(events))
        .layer(DefaultBodyLimit::max(65536))
        .layer(tower_http::timeout::TimeoutLayer::with_status_code(
            StatusCode::REQUEST_TIMEOUT,
            Duration::from_secs(10),
        ))
        .layer(tower::limit::ConcurrencyLimitLayer::new(32))
        .with_state(app)
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::{body::Body, http::Request};
    use std::os::unix::fs::PermissionsExt;
    use tower::ServiceExt;
    fn fixture() -> (tempfile::TempDir, Arc<App>) {
        let dir = tempfile::tempdir().unwrap();
        std::fs::set_permissions(dir.path(), std::fs::Permissions::from_mode(0o700)).unwrap();
        let config:Config=serde_json::from_value(json!({"controllers":[{"id":"bmc01","protocol":"ipmi","endpoint":"192.168.1.51","manages":["qc01"],"event_token_env":"BMC01_EVENTS"}],"enroll_push":false,"interval_seconds":60,"stale_seconds":180,"inventory_max_age_seconds":86400,"listen_address":"127.0.0.1","port":9105,"state_directory":dir.path(),"metrics_token_file":"/etc/nmc/bmc-token"})).unwrap();
        let mut store = Store::open(dir.path().into()).unwrap();
        store.state.controllers.insert(
            "bmc01".into(),
            nmc_bmc_observer::ControllerState {
                identity_valid: true,
                available: true,
                poll_at: now(),
                ..Default::default()
            },
        );
        let app = Arc::new(App {
            config,
            store: Mutex::new(store),
            inventory_at: AtomicI64::new(now()),
            metrics_token: "m".repeat(32),
            event_tokens: BTreeMap::from([("bmc01".into(), "p".repeat(32))]),
            rate: Mutex::new(BTreeMap::new()),
        });
        (dir, app)
    }
    async fn request(
        app: Arc<App>,
        path: &str,
        method: &str,
        token: &str,
        body: String,
    ) -> StatusCode {
        let request = Request::builder()
            .method(method)
            .uri(path)
            .header("authorization", format!("Bearer {token}"))
            .header("content-type", "application/json")
            .body(Body::from(body))
            .unwrap();
        router(app).oneshot(request).await.unwrap().status()
    }
    #[tokio::test]
    async fn metrics_and_push_credentials_are_isolated() {
        let (_dir, app) = fixture();
        assert_eq!(
            request(
                app.clone(),
                "/metrics",
                "GET",
                &"p".repeat(32),
                String::new()
            )
            .await,
            StatusCode::UNAUTHORIZED
        );
        assert_eq!(
            request(
                app.clone(),
                "/metrics",
                "GET",
                &"m".repeat(32),
                String::new()
            )
            .await,
            StatusCode::OK
        );
        let body = json!({"Context":"continuum-bmc/bmc01","Events":[{"EventId":"1"}]}).to_string();
        assert_eq!(
            request(
                app.clone(),
                "/events/bmc01",
                "POST",
                &"m".repeat(32),
                body.clone()
            )
            .await,
            StatusCode::UNAUTHORIZED
        );
        assert_eq!(
            request(
                app.clone(),
                "/events/other",
                "POST",
                &"p".repeat(32),
                body.clone()
            )
            .await,
            StatusCode::UNAUTHORIZED
        );
        assert_eq!(
            request(app, "/events/bmc01", "POST", &"p".repeat(32), body).await,
            StatusCode::NO_CONTENT
        );
    }
    #[tokio::test]
    async fn oversized_and_rate_limited_events_are_rejected() {
        let (_dir, app) = fixture();
        assert_eq!(
            request(
                app.clone(),
                "/events/bmc01",
                "POST",
                &"p".repeat(32),
                "x".repeat(65537)
            )
            .await,
            StatusCode::PAYLOAD_TOO_LARGE
        );
        app.rate.lock().await.insert("bmc01".into(), (now(), 120));
        let body = json!({"Context":"continuum-bmc/bmc01","Events":[{"EventId":"1"}]}).to_string();
        assert_eq!(
            request(app, "/events/bmc01", "POST", &"p".repeat(32), body).await,
            StatusCode::TOO_MANY_REQUESTS
        );
    }
}
