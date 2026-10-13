#!/usr/bin/env node
// Continuum's BMC observation plane. It never performs host or controller power actions.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {createHash, timingSafeEqual, randomUUID} from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {pathToFileURL} from 'node:url';
import {unwrap} from './availability-watchdog.mjs';
const execute = promisify(execFile);
const identifier = s => typeof s === 'string' && !['constructor', 'prototype', '__proto__'].includes(s) && /^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$/.test(s);
const digest = v => createHash('sha256').update(JSON.stringify(v)).digest('hex');
const bounded = v => typeof v === 'string' ? v.slice(0, 128) : '';
const epochFresh = (t, now, age) => Number.isSafeInteger(t) && t > 0 && t <= now && now - t <= age;
export function validateConfig(c) {
  if (!Array.isArray(c.controllers) || !c.controllers.length || c.controllers.length > 32
      || new Set(c.controllers.map(x => x.id)).size !== c.controllers.length
      || ![true, false].includes(c.enroll_push) || !Number.isInteger(c.interval_seconds)
      || c.interval_seconds < 60 || c.interval_seconds > 900 || !Number.isInteger(c.stale_seconds)
      || c.stale_seconds < c.interval_seconds * 2 || c.stale_seconds > 3600
      || !Number.isInteger(c.inventory_max_age_seconds) || c.inventory_max_age_seconds < 300 || c.inventory_max_age_seconds > 86400
      || !Number.isInteger(c.port) || c.port < 1024 || c.port > 65535
      || !['127.0.0.1', '0.0.0.0'].includes(c.listen_address)
      || !path.isAbsolute(c.state_directory) || !path.isAbsolute(c.metrics_token_file)) throw Error('Invalid BMC observation policy');
  for (const b of c.controllers) if (!identifier(b.id) || !['redfish', 'ipmi', 'turingpi'].includes(b.protocol)
      || typeof b.endpoint !== 'string' || !Array.isArray(b.manages) || !b.manages.length || !b.manages.every(identifier)
      || (b.event_token_env && !/^[A-Za-z_][A-Za-z0-9_]{0,127}$/.test(b.event_token_env))) throw Error('Pinned BMC identity and host mapping required');
  return c;
}
function privateFile(file, flags, mode = 0o600) {
  const fd = fs.openSync(file, flags | fs.constants.O_NOFOLLOW, mode);
  const st = fs.fstatSync(fd);
  if (!st.isFile() || st.nlink !== 1 || (st.mode & 0o077) || st.uid !== process.getuid()) { fs.closeSync(fd); throw Error('Unsafe BMC state/credential file'); }
  return fd;
}
export class Store {
  constructor(directory) {
    const st = fs.lstatSync(directory);
    if (!st.isDirectory() || st.isSymbolicLink() || st.uid !== process.getuid() || (st.mode & 0o077)) throw Error('Private BMC state directory required');
    this.directory = directory;
    this.state = {version: 1, controllers: {}};
    const file = path.join(directory, 'state.json');
    if (fs.existsSync(file)) {
      const fd = privateFile(file, fs.constants.O_RDONLY);
      try { if (fs.fstatSync(fd).size > 4 * 1024 * 1024) throw Error('BMC state exceeds bounds'); this.state = JSON.parse(fs.readFileSync(fd, 'utf8')); }
      finally { fs.closeSync(fd); }
      if (this.state.version !== 1 || !this.state.controllers || Array.isArray(this.state.controllers)
          || Object.keys(this.state.controllers).length > 32 || Object.keys(this.state.controllers).some(k => !identifier(k))) throw Error('Invalid BMC state');
    }
  }
  commit(record) {
    const log = path.join(this.directory, 'observations.jsonl');
    // Keep four bounded files. Prometheus owns time-series retention independently.
    if (fs.existsSync(log)) {
      const fd = privateFile(log, fs.constants.O_RDONLY); const size = fs.fstatSync(fd).size; fs.closeSync(fd);
      if (size > 16 * 1024 * 1024) {
        for (let i = 3; i >= 1; i--) {
          const old = `${log}.${i}`;
          if (fs.existsSync(old)) { const check = privateFile(old, fs.constants.O_RDONLY); fs.closeSync(check); if (i === 3) fs.unlinkSync(old); else fs.renameSync(old, `${log}.${i + 1}`); }
        }
        fs.renameSync(log, `${log}.1`);
      }
    }
    const fd = privateFile(log, fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_APPEND);
    try { fs.writeFileSync(fd, `${JSON.stringify(record)}\n`); fs.fsyncSync(fd); } finally { fs.closeSync(fd); }
    const temporary = path.join(this.directory, `.state-${randomUUID()}`);
    const sf = privateFile(temporary, fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL);
    try { fs.writeFileSync(sf, JSON.stringify(this.state)); fs.fsyncSync(sf); } finally { fs.closeSync(sf); }
    fs.renameSync(temporary, path.join(this.directory, 'state.json'));
    const dir = fs.openSync(this.directory, fs.constants.O_RDONLY); try { fs.fsyncSync(dir); } finally { fs.closeSync(dir); }
  }
}
export async function api(args) {
  return unwrap((await execute('/usr/bin/nmc', [...args, '--output', 'json'], {timeout: 120000, maxBuffer: 2 * 1024 * 1024})).stdout);
}
export function projectDiagnostic(d, now = Date.now()) {
  const p = {observed_at_unix_ms: d.observed_at_unix_ms, protocol: d.protocol, systems: [], firmware: [], sensors: [], logs_available: false, push_supported: d.event_service?.push_supported === true, faults: {}, log_ids: []};
  for (const s of (d.systems ?? []).slice(0, 32)) p.systems.push({id: bounded(s.id), available: s.available === true, power_state: bounded(s.power_state), health: bounded(s.status?.HealthRollup ?? s.status?.Health)});
  for (const [slot, power] of Object.entries(d.nodes ?? {}).slice(0, 4)) p.systems.push({id: bounded(slot), available: ['On', 'Off'].includes(power), power_state: bounded(power), health: ''});
  for (const m of (d.managers ?? []).slice(0, 32)) if (m.available === true) p.firmware.push({id: bounded(m.id), version: bounded(m.firmware_version), health: bounded(m.status?.Health)});
  if (d.protocol === 'ipmi') {
    p.systems.push({id: 'chassis', available: /System Power\s*:\s*(on|off)/i.test(d.chassis_status ?? ''), power_state: /System Power\s*:\s*on/i.test(d.chassis_status ?? '') ? 'On' : 'Off', health: ''});
    for (const [label, name] of [['Main Power Fault', 'power'], ['Cooling/Fan Fault', 'cooling'], ['Drive Fault', 'drive'], ['Power Control Fault', 'power_control']]) {
      const match = (d.chassis_status ?? '').match(new RegExp(`${label}\\s*:\\s*(true|false)`, 'i')); if (match) p.faults[name] = match[1].toLowerCase() === 'true';
    }
    if (d.sensors?.available === true) for (const line of d.sensors.output.split('\n').slice(0, 256)) {
      const parts = line.split('|').map(x => x.trim()); const value = Number(parts[1]);
      if (parts.length >= 4 && parts[1] !== '' && Number.isFinite(value) && /^[+-]?[0-9]+(?:\.[0-9]+)?$/.test(parts[1])) p.sensors.push({id: bounded(parts[0]), value, unit: bounded(parts[2]), healthy: parts[3] === 'ok'});
    }
    p.logs_available = d.system_event_log?.available === true;
    if (p.logs_available) p.log_ids = d.system_event_log.output.split('\n').filter(l => /^[0-9a-f]+\s*\|/i.test(l.trim())).slice(0, 20).map(l => digest(l));
  } else {
    p.logs_available = (d.logs ?? []).some(l => l.available === true);
    for (const l of (d.logs ?? []).slice(0, 4)) for (const e of (l.entries ?? []).slice(0, 20)) p.log_ids.push(digest([e.Id, e.Created, e.MessageId, e.Severity]));
  }
  p.fresh = epochFresh(p.observed_at_unix_ms, now, 120000) && d.inventory_stale === false;
  return p;
}
export class Observer {
  constructor(config, store, call = api, clock = Date.now) { this.config = validateConfig(config); this.store = store; this.call = call; this.clock = clock; this.inventoryFresh = false; this.busy = false; }
  async tick() {
    if (this.busy) return;
    this.busy = true;
    try {
      const inv = await this.call(['device', 'inventory']); const now = this.clock();
      this.inventoryFresh = inv.stale === false && epochFresh(inv.generated_at_unix_ms, now, this.config.inventory_max_age_seconds * 1000);
      for (const b of this.config.controllers) {
        let state = this.store.state.controllers[b.id] ?? {events_total: 0, seen_events: [], enrollment: null};
        this.store.state.controllers[b.id] = state;
        const registered = inv.controllers?.find(x => x.id === b.id);
        const mappingMatches = registered && registered.endpoint === b.endpoint && registered.protocol === b.protocol
          && JSON.stringify([...registered.manages].sort()) === JSON.stringify([...b.manages].sort())
          && b.manages.every(id => inv.devices?.some(d => d.id === id && d.controller_id === b.id));
        state.identity_valid = this.inventoryFresh && !!mappingMatches;
        state.available = false;
        if (!state.identity_valid) { this.store.commit({kind: 'identity_gap', controller_id: b.id, at: this.clock()}); continue; }
        try {
          const d = await this.call(['device', 'diagnostics', '--controller-id', b.id]);
          if (d.controller_id !== b.id || d.protocol !== b.protocol) throw Error('BMC diagnostic identity mismatch');
          const p = projectDiagnostic(d, this.clock());
          state.available = p.fresh; state.diagnostic = p; state.poll_at = this.clock();
          this.store.commit({kind: 'observation', controller_id: b.id, at: state.poll_at, diagnostic: p});
          if (this.config.enroll_push && registered.event_push_enabled === true && p.push_supported && b.event_token_env && (!state.enrollment || (state.enrollment.phase === 'configured' && this.clock() - (state.enrollment.checked_at ?? 0) >= 900000))) {
            state.enrollment = {request_id: `bmc-events-${randomUUID()}`, phase: 'intent'};
            this.store.commit({kind: 'enrollment_intent', controller_id: b.id, ...state.enrollment, at: this.clock()});
            await this.reconcileEnrollment(b, inv, state);
          } else if (state.enrollment?.phase === 'intent' || state.enrollment?.phase === 'unknown') {
            // Same request ID means the server only reconciles an ambiguous write.
            if (this.config.enroll_push && p.push_supported) await this.reconcileEnrollment(b, inv, state);
          }
        } catch { state.available = false; this.store.commit({kind: 'collection_gap', controller_id: b.id, at: this.clock()}); }
      }
    } catch { this.inventoryFresh = false; for (const s of Object.values(this.store.state.controllers)) { s.identity_valid = false; s.available = false; } }
    finally { this.busy = false; }
  }
  async reconcileEnrollment(b, inv, state) {
    // A changed inventory revision requires operator reconciliation of an ambiguous request.
    state.enrollment.revision ??= inv.revision;
    this.store.commit({kind: 'enrollment_reconcile', controller_id: b.id, ...state.enrollment, at: this.clock()});
    try {
      const response = await this.call(['device', 'event-subscribe', '--request-json', JSON.stringify({controller_id: b.id,
        request_id: state.enrollment.request_id, change_id: state.enrollment.request_id,
        expected_inventory_revision: state.enrollment.revision, reason: 'Continuum BMC event collection with retained periodic state reconciliation'})]);
      state.enrollment.phase = response.configured === true ? 'configured' : 'unknown'; state.enrollment.checked_at = this.clock();
    } catch { state.enrollment.phase = 'unknown'; }
    this.store.commit({kind: 'enrollment_result', controller_id: b.id, ...state.enrollment, at: this.clock()});
  }
  acceptEvents(id, body) {
    const b = this.config.controllers.find(x => x.id === id), state = this.store.state.controllers[id];
    if (!b || !state?.identity_valid || !this.inventoryFresh || !epochFresh(state.poll_at, this.clock(), this.config.stale_seconds * 1000)) throw Error('Event controller identity is not freshly reconciled');
    if (!body || body.Context !== `continuum-bmc/${id}` || !Array.isArray(body.Events) || !body.Events.length || body.Events.length > 32) throw Error('Invalid Redfish event envelope');
    const projected = body.Events.map(e => {
      if (!e || typeof e !== 'object' || typeof e.EventId !== 'string' || e.EventId.length > 256
        || !['string', 'undefined'].includes(typeof e.EventTimestamp) || (e.EventTimestamp?.length ?? 0) > 128) throw Error('Invalid Redfish event');
      return {id: e.EventId, time: bounded(e.EventTimestamp), type: bounded(e.EventType), severity: bounded(e.Severity), message_id: bounded(e.MessageId)};
    });
    for (const e of projected) {
      const hash = digest(e); if (state.seen_events.includes(hash)) continue;
      state.seen_events = [...state.seen_events, hash].slice(-512); state.events_total++;
      this.store.commit({kind: 'push_event', controller_id: id, at: this.clock(), event: e});
    }
    state.push_at = this.clock();
    this.store.commit({kind: 'push_receipt', controller_id: id, at: state.push_at});
  }
  metrics(now = this.clock()) {
    const lines = [`continuum_bmc_inventory_fresh ${this.inventoryFresh ? 1 : 0}`];
    const label = x => JSON.stringify(String(x));
    for (const b of this.config.controllers) {
      const s = this.store.state.controllers[b.id] ?? {}, p = s.diagnostic;
      const fresh = this.inventoryFresh && s.identity_valid && s.available && epochFresh(p?.observed_at_unix_ms, now, this.config.stale_seconds * 1000);
      const base = `controller_id=${label(b.id)},protocol=${label(b.protocol)}`;
      const metric = (name, value, extra = '') => lines.push(`continuum_bmc_${name}{${base}${extra ? ',' + extra : ''}} ${value}`);
      metric('available', fresh ? 1 : 0); metric('identity_valid', s.identity_valid && this.inventoryFresh ? 1 : 0);
      metric('last_observation_timestamp_seconds', (p?.observed_at_unix_ms ?? 0) / 1000);
      metric('event_push_supported', fresh && p?.push_supported ? 1 : 0);
      metric('event_push_configured', s.enrollment?.phase === 'configured' ? 1 : 0);
      metric('event_delivery_observed', s.push_at ? 1 : 0);
      metric('last_push_timestamp_seconds', (s.push_at ?? 0) / 1000); metric('push_events_total', s.events_total ?? 0);
      metric('log_collection_available', fresh && p?.logs_available ? 1 : 0);
      metric('polling_required', 1);
      if (!fresh) continue; // Never continue exporting a last known good sensor as current.
      for (const host of b.manages) metric('managed_host_info', 1, `host=${label(host)}`);
      for (const x of p.systems) { metric('system_state_known', x.available ? 1 : 0, `system=${label(x.id)}`); if (x.available) metric('power_on', x.power_state === 'On' ? 1 : 0, `system=${label(x.id)}`); if (x.health) metric('health_fault', x.health !== 'OK' ? 1 : 0, `resource=${label(x.id)}`); }
      for (const x of p.firmware) { metric('firmware_info', 1, `manager=${label(x.id)},version=${label(x.version)}`); if (x.health) metric('health_fault', x.health !== 'OK' ? 1 : 0, `resource=${label(x.id)}`); }
      for (const [fault, value] of Object.entries(p.faults)) metric('chassis_fault', value ? 1 : 0, `fault=${label(fault)}`);
      for (const x of p.sensors) { metric('sensor_value', x.value, `sensor=${label(x.id)},unit=${label(x.unit)}`); metric('sensor_healthy', x.healthy ? 1 : 0, `sensor=${label(x.id)}`); }
    }
    return `${lines.join('\n')}\n`;
  }
}
const equalToken = (actual, expected) => typeof expected === 'string' && expected.length >= 32 && Buffer.byteLength(actual) === Buffer.byteLength(expected) && timingSafeEqual(Buffer.from(actual), Buffer.from(expected));
export function createServer(observer, metricsToken, eventTokens = process.env) {
  const windows = new Map();
  const server = http.createServer(async (req, res) => {
    const send = (status, text) => { res.writeHead(status, {'Content-Type': status === 200 ? 'text/plain; version=0.0.4' : 'application/json', 'Cache-Control': 'no-store'}); res.end(text); };
    const token = req.headers.authorization?.startsWith('Bearer ') ? req.headers.authorization.slice(7) : '';
    if (req.method === 'GET' && req.url === '/metrics') {
      if (!equalToken(token, metricsToken)) { send(401, '{}'); return; }
      send(200, observer.metrics()); return;
    }
    const match = req.url?.match(/^\/events\/([A-Za-z0-9][A-Za-z0-9_.:-]{0,127})$/);
    const b = match && observer.config.controllers.find(x => x.id === match[1]);
    if (req.method !== 'POST' || !b || !equalToken(token, eventTokens[b.event_token_env])) { send(401, '{}'); return; }
    const now = observer.clock(), window = windows.get(b.id) ?? {start: now, count: 0};
    if (now - window.start >= 60000) { window.start = now; window.count = 0; } windows.set(b.id, window);
    if (++window.count > 120) { send(429, '{}'); return; }
    if (req.headers['content-type']?.split(';')[0] !== 'application/json') { send(415, '{}'); return; }
    if (Number(req.headers['content-length'] ?? 0) > 65536) { send(413, '{}'); req.resume(); return; }
    try {
      let length = 0; const chunks = [];
      for await (const chunk of req) { length += chunk.length; if (length > 65536) { send(413, '{}'); req.destroy(); return; } chunks.push(chunk); }
      observer.acceptEvents(b.id, JSON.parse(Buffer.concat(chunks).toString('utf8'))); send(204, '');
    } catch { send(400, '{}'); }
  });
  server.requestTimeout = 10000; server.headersTimeout = 10000; server.maxConnections = 32;
  return server;
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const config = validateConfig(JSON.parse(fs.readFileSync(process.argv[2], 'utf8')));
  const fd = privateFile(config.metrics_token_file, fs.constants.O_RDONLY);
  let token; try { token = fs.readFileSync(fd, 'utf8').trim(); } finally { fs.closeSync(fd); }
  if (token.length < 32) throw Error('Dedicated metrics bearer credential required');
  const observer = new Observer(config, new Store(config.state_directory));
  createServer(observer, token).listen(config.port, config.listen_address);
  for (;;) { try { await observer.tick(); } catch { process.stderr.write('BMC observation gap; inspect private state journal\n'); } await new Promise(resolve => setTimeout(resolve, config.interval_seconds * 1000)); }
}
