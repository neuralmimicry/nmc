import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {Observer, Store, validateConfig, projectDiagnostic, createServer} from '../../scripts/bmc-observer.mjs';
const now = 1791870000000;
const c = {controllers: [{id: 'bmc01', protocol: 'ipmi', endpoint: '192.168.1.90', manages: ['qc01'], event_token_env: 'BMC01_EVENTS'}], enroll_push: false, interval_seconds: 60, stale_seconds: 180, inventory_max_age_seconds: 86400, listen_address: '127.0.0.1', port: 9105, state_directory: '/var/lib/nmc/bmc', metrics_token_file: '/etc/nmc/bmc-metrics-token'};
const inventory = () => ({generated_at_unix_ms: now, stale: false, revision: 'rev1', controllers: structuredClone(c.controllers), devices: [{id: 'qc01', controller_id: 'bmc01'}]});
const diagnostic = () => ({controller_id: 'bmc01', protocol: 'ipmi', observed_at_unix_ms: now, inventory_stale: false, chassis_status: 'System Power : on\nMain Power Fault : false\nCooling/Fan Fault : true', sensors: {available: true, output: 'CPU Temp | 52.000 | degrees C | ok\nFan | na | RPM | ns'}, system_event_log: {available: false, output: 'unavailable'}});
const memoryStore = () => ({state: {controllers: {}}, records: [], commit(r) {this.records.push(structuredClone(r));}});
const fixture = (call = async args => args[1] === 'inventory' ? inventory() : diagnostic(), config = c) => new Observer(structuredClone(config), memoryStore(), call, () => now);
const event = () => ({Context: 'continuum-bmc/bmc01', Events: [{EventId: '1', EventTimestamp: '2026-10-13T10:00:00Z', EventType: 'Alert', Severity: 'Warning', Message: 'password=do-not-store', MessageId: 'Hardware.Warning'}]});
test('BMC sensors, faults and unavailable SEL remain distinct and old sensors disappear', async () => {
  const o = fixture(); await o.tick(); const metrics = o.metrics();
  assert.match(metrics, /continuum_bmc_sensor_value\{.*sensor="CPU Temp".*\} 52/);
  assert.match(metrics, /continuum_bmc_chassis_fault\{.*fault="cooling"\} 1/);
  assert.match(metrics, /continuum_bmc_log_collection_available\{.*\} 0/);
  assert.doesNotMatch(metrics, /sensor="Fan"/);
  assert.doesNotMatch(o.metrics(now + 180001), /continuum_bmc_sensor_value/);
});
test('stale or changed controller mappings stop diagnostics and expose gaps', async () => {
  for (const change of [i => i.stale = true, i => i.controllers[0].endpoint = '192.168.1.99', i => i.devices[0].controller_id = 'other']) {
    let calls = 0; const inv = inventory(); change(inv);
    const o = fixture(async args => { if (args[1] === 'inventory') return inv; calls++; return diagnostic(); });
    await o.tick(); assert.equal(calls, 0); assert.match(o.metrics(), /continuum_bmc_available\{.*\} 0/);
  }
});
test('wrong diagnostic identity and partial polls cannot be healthy', async () => {
  const o = fixture(async args => args[1] === 'inventory' ? inventory() : {...diagnostic(), controller_id: 'other'});
  await o.tick(); assert.match(o.metrics(), /continuum_bmc_available\{.*\} 0/);
});
test('duplicate events persist across restart and raw messages never enter state journal', async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'bmc-observer-test-')); fs.chmodSync(directory, 0o700);
  try {
    const store = new Store(directory); const o = new Observer(structuredClone(c), store, async args => args[1] === 'inventory' ? inventory() : diagnostic(), () => now);
    await o.tick(); o.acceptEvents('bmc01', event()); o.acceptEvents('bmc01', event());
    assert.equal(store.state.controllers.bmc01.events_total, 1);
    assert.doesNotMatch(fs.readFileSync(path.join(directory, 'observations.jsonl'), 'utf8'), /do-not-store/);
    const restarted = new Observer(structuredClone(c), new Store(directory), o.call, () => now); await restarted.tick(); restarted.acceptEvents('bmc01', event());
    assert.equal(restarted.store.state.controllers.bmc01.events_total, 1);
  } finally { fs.rmSync(directory, {recursive: true, force: true}); }
});
test('unknown context, stale identity, malformed and oversized batches reject without increment', async () => {
  const o = fixture(); await o.tick();
  for (const e of [{...event(), Context: 'continuum-bmc/other'}, {...event(), Events: Array(33).fill(event().Events[0])}, {...event(), Events: [{EventId: 1}]}]) assert.throws(() => o.acceptEvents('bmc01', e));
  o.inventoryFresh = false; assert.throws(() => o.acceptEvents('bmc01', event()));
  assert.equal(o.store.state.controllers.bmc01.events_total, 0);
});
test('ambiguous enrollment is durably reconciled with same request and revision', async () => {
  const config = structuredClone(c); config.enroll_push = true; config.controllers[0].protocol = 'redfish'; const requests = [];
  const o = fixture(async args => {
    if (args[1] === 'inventory') {const i = inventory(); i.controllers[0].protocol = 'redfish'; i.controllers[0].event_push_enabled = true; return i;}
    if (args[1] === 'diagnostics') return {...diagnostic(), protocol: 'redfish', event_service: {push_supported: true}};
    requests.push(JSON.parse(args.at(-1))); if (requests.length === 1) throw Error('lost response'); return {configured: true};
  }, config);
  await o.tick(); assert.equal(o.store.state.controllers.bmc01.enrollment.phase, 'unknown');
  await o.tick(); assert.deepEqual(requests[0], requests[1]); assert.equal(o.store.state.controllers.bmc01.enrollment.phase, 'configured');
});
test('disabled enrollment never writes even when push is supported', async () => {
  let writes = 0; const o = fixture(async args => {if (args[1] === 'inventory') return inventory(); if (args[1] === 'diagnostics') return {...diagnostic(), event_service: {push_supported: true}}; writes++;});
  await o.tick(); assert.equal(writes, 0);
});
test('HTTP metrics and push use separate credentials, reject oversized bodies and authenticate controller', async () => {
  const o = fixture(); await o.tick(); const metricsToken = 'm'.repeat(32), pushToken = 'p'.repeat(32);
  const server = createServer(o, metricsToken, {BMC01_EVENTS: pushToken});
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve)); const base = `http://127.0.0.1:${server.address().port}`;
  try {
    assert.equal((await fetch(`${base}/metrics`)).status, 401);
    assert.equal((await fetch(`${base}/metrics`, {headers: {Authorization: `Bearer ${pushToken}`}})).status, 401);
    assert.equal((await fetch(`${base}/metrics`, {headers: {Authorization: `Bearer ${metricsToken}`}})).status, 200);
    const post = (token, body, id = 'bmc01') => fetch(`${base}/events/${id}`, {method: 'POST', headers: {Authorization: `Bearer ${token}`, 'Content-Type': 'application/json'}, body});
    assert.equal((await post(metricsToken, JSON.stringify(event()))).status, 401);
    assert.equal((await post(pushToken, JSON.stringify(event()), 'other')).status, 401);
    assert.equal((await post(pushToken, 'x'.repeat(65537))).status, 413);
    assert.equal((await post(pushToken, JSON.stringify(event()))).status, 204);
    assert.equal(o.store.state.controllers.bmc01.events_total, 1);
  } finally { await new Promise(resolve => server.close(resolve)); }
});
test('unsafe state files and non-private directories fail closed', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'bmc-state-safety-')); fs.chmodSync(directory, 0o700);
  try { const target = path.join(directory, 'target'); fs.writeFileSync(target, '{}', {mode: 0o600}); fs.symlinkSync(target, path.join(directory, 'state.json')); assert.throws(() => new Store(directory)); fs.unlinkSync(path.join(directory, 'state.json')); fs.chmodSync(directory, 0o755); assert.throws(() => new Store(directory)); }
  finally {fs.rmSync(directory, {recursive: true, force: true});}
});
test('configuration is bounded and unknown power stays explicit', () => {
  assert.throws(() => validateConfig({...c, interval_seconds: 1}));
  assert.throws(() => validateConfig({...c, controllers: [c.controllers[0], c.controllers[0]]}));
  assert.equal(projectDiagnostic({...diagnostic(), chassis_status: 'System Power : unknown'}).systems[0].available, false);
});
