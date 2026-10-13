#!/usr/bin/env node
// Continuum's independent, bounded observer. Mutations use its typed API only.
import fs from 'node:fs';
import net from 'node:net';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {randomUUID} from 'node:crypto';
import {pathToFileURL} from 'node:url';
const execute = promisify(execFile);
const fresh = (at, now) => Number.isSafeInteger(at) && now - at >= 0 && now - at <= 60000;
const sameSet = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && new Set(a).size === a.length && a.every(x => b.includes(x));
const ready = s => s?.rollout_complete === true && s.desired_replicas > 0 && s.ready_replicas === s.desired_replicas && s.pod_diagnostics_available === true && s.pod_diagnostics_truncated === false && s.pods?.length > 0 && s.pods.every(p => p.conditions?.some(c => c.type === 'Ready' && c.status === 'True'));
export function unwrap(stdout) {
  const start = stdout.indexOf('{');
  if (start < 0) throw Error('Missing typed response');
  let value = JSON.parse(stdout.slice(start));
  for (let depth = 0; depth < 8; depth++) {
    if (!Object.hasOwn(value ?? {}, 'success')) {
      if (!value || typeof value !== 'object' || (Object.hasOwn(value, 'upstream_status') && (value.upstream_status < 200 || value.upstream_status >= 300))) throw Error('Invalid typed payload');
      return value;
    }
    if (value.success !== true || !Object.hasOwn(value, 'data')) throw Error('Typed API rejected operation');
    value = value.data;
  }
  throw Error('Typed response exceeds nesting bound');
}
export function validateConfig(c) {
  const safe = s => typeof s === 'string' && /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,126}$/.test(s);
  if (![false, true].includes(c.enabled) || !safe(c.cluster_id) || !safe(c.conductor?.namespace) || !safe(c.conductor?.deployment)) throw Error('Explicit policy and cluster/Conductor identity required');
  if (!Array.isArray(c.hosts) || c.hosts.length > 16 || !Array.isArray(c.services) || c.services.length > 32) throw Error('Bounded explicit targets required');
  if (new Set(c.hosts.map(h => h.device_id)).size !== c.hosts.length || new Set(c.services.map(s => `${s.namespace}/${s.deployment}`)).size !== c.services.length) throw Error('Duplicate targets');
  for (const h of c.hosts) {
    if (![h.device_id, h.controller_id, h.node].every(safe) || !net.isIP(h.address) || !Number.isInteger(h.port) || h.port < 1 || h.port > 65535 || !net.isIP(h.controller_endpoint) || !/^[0-9a-f:]{17}$/.test(h.controller_mac) || !Array.isArray(h.dependencies) || h.dependencies.length < 1) throw Error('Pinned host/controller/dependency identities required');
    for (const d of h.dependencies) if (!safe(d.device_id) || !net.isIP(d.address) || !Number.isInteger(d.port) || d.port < 1 || d.port > 65535) throw Error('Invalid dependency probe');
  }
  for (const s of c.services) if (![s.namespace, s.deployment].every(safe)) throw Error('Invalid service target');
  for (const [key, min, max] of [['inventory_max_age_seconds', 300, 86400], ['interval_seconds', 10, 60], ['failure_samples', 3, 20], ['outage_seconds', 120, 3600], ['cooldown_seconds', 3600, 86400], ['recovery_seconds', 300, 3600], ['daily_action_budget', 1, 4]]) if (!Number.isInteger(c[key]) || c[key] < min || c[key] > max) throw Error(`Invalid ${key}`);
  if (!Array.isArray(c.acceptance_urls) || !c.acceptance_urls.length || c.acceptance_urls.length > 16 || c.acceptance_urls.some(u => !/^https:\/\/[a-z0-9.-]+\.neuralmimicry\.ai\//.test(u))) throw Error('Explicit bounded HTTPS estate acceptance URLs required');
  if (typeof c.journal !== 'string' || !c.journal.startsWith('/') || typeof c.inhibit_path !== 'string' || !c.inhibit_path.startsWith('/')) throw Error('Absolute local journal/inhibit paths required');
  return c;
}
export async function tcp(address, port, now = Date.now) {
  return new Promise(resolve => {
    const socket = net.createConnection({host: address, port});
    let settled = false;
    const finish = state => { if (!settled) { settled = true; socket.destroy(); resolve({state, observed_at_unix_ms: now(), source: 'continuum_tcp', reference: `${address}:${port} ${state}`}); } };
    socket.setTimeout(2500, () => finish('unreachable'));
    socket.once('connect', () => finish('healthy'));
    socket.once('error', error => finish(error.code === 'ECONNREFUSED' ? 'reachable' : error.code === 'ETIMEDOUT' ? 'unreachable' : 'unknown'));
  });
}
export async function nmc(args) {
  return unwrap((await execute('/usr/bin/nmc', [...args, '--output', 'json'], {timeout: 15000, maxBuffer: 1024 * 1024})).stdout);
}
const targetArgs = (c, t) => ['--cluster-id', c.cluster_id, '--namespace', t.namespace, '--deployment', t.deployment];
async function observed(call) { try { return {value: await call(), observed_at_unix_ms: Date.now()}; } catch { return {value: null, observed_at_unix_ms: Date.now()}; } }
export async function collect(c, api = nmc, probe = tcp) {
  const s = {observed_at_unix_ms: Date.now(), hosts: [], services: []};
  s.health = await observed(() => api(['server', 'health']));
  s.inventory = await observed(() => api(['device', 'inventory']));
  s.conductor = await observed(() => api(['k8s', 'deployment-status', ...targetArgs(c, c.conductor)]));
  s.conductor_recovery = await observed(() => api(['k8s', 'recovery-status', ...targetArgs(c, c.conductor)]));
  for (const h of c.hosts) {
    s.hosts.push({id: h.device_id,
      node: await observed(() => api(['k8s', 'node-resources', h.node])),
      network: await probe(h.address, h.port),
      diagnostics: await observed(() => api(['device', 'diagnostics', '--controller-id', h.controller_id])),
      dependencies: await Promise.all(h.dependencies.map(async d => ({id: d.device_id, ...(await probe(d.address, d.port))}))) });
  }
  for (const t of c.services) s.services.push({namespace: t.namespace, deployment: t.deployment, status: await observed(() => api(['k8s', 'deployment-status', ...targetArgs(c, t)])), recovery: await observed(() => api(['k8s', 'recovery-status', ...targetArgs(c, t)]))});
  s.acceptance = await Promise.all(c.acceptance_urls.map(async url => { try { const response = await fetch(url, {redirect: 'manual', signal: AbortSignal.timeout(5000)}); await response.body?.cancel(); return {url, status: response.status, observed_at_unix_ms: Date.now()}; } catch { return {url, status: null, observed_at_unix_ms: Date.now()}; } }));
  return s;
}
export function assess(c, s, now = Date.now()) {
  const blocked = reason => ({action: 'hold', reason});
  const observedFresh = o => o?.value && fresh(o.observed_at_unix_ms, now);
  if (!observedFresh(s.health) || s.health.value.status !== 'ok' || !observedFresh(s.inventory) || s.inventory.value.stale !== false || (!Number.isSafeInteger(s.inventory.value.generated_at_unix_ms) || now - s.inventory.value.generated_at_unix_ms < 0 || now - s.inventory.value.generated_at_unix_ms > c.inventory_max_age_seconds * 1000)) return blocked('Continuum or inventory evidence unavailable/stale');
  if (!observedFresh(s.conductor) || !observedFresh(s.conductor_recovery)) return blocked('Conductor outage cannot be independently observed');
  if (ready(s.conductor.value)) return blocked('Conductor available; defer to normal governance');
  if (s.conductor.value.pod_diagnostics_available !== true || s.conductor.value.pod_diagnostics_truncated !== false || s.conductor.value.ready_replicas !== 0 || s.conductor.value.desired_replicas < 1) return blocked('Conductor outage is incomplete or ambiguous');
  const inv = s.inventory.value;
  // Prioritise service recovery on healthy hosts. Server preflight rechecks
  // policy, placement, NodeLease, strategy and resource version before patch.
  for (const t of c.services) {
    const o = s.services.find(x => x.namespace === t.namespace && x.deployment === t.deployment);
    if (observedFresh(o?.status) && observedFresh(o?.recovery) && !ready(o.status.value) && o.recovery.value.eligible === true && o.recovery.value.recovery_enabled === true && o.recovery.value.readiness_degraded === true && o.recovery.value.hosts?.length && o.recovery.value.hosts.every(h => h.healthy === true && h.node_lease_healthy === true)) return {action: 'service_restart', target: t, key: `service/${t.namespace}/${t.deployment}`, preflight: o.recovery.value};
  }
  for (const h of c.hosts) {
    const o = s.hosts.find(x => x.id === h.device_id);
    const dev = inv.devices?.find(x => x.id === h.device_id);
    const controller = inv.controllers?.find(x => x.id === h.controller_id);
    if (!dev || dev.controller_id !== h.controller_id || !dev.addresses?.includes(h.address) || !controller?.manages?.includes(h.device_id) || controller.endpoint !== h.controller_endpoint || controller.mac_address !== h.controller_mac || controller.power_actions_enabled !== true || !dev.services?.length || !sameSet(dev.depends_on, h.dependencies.map(d => d.device_id))) continue;
    // Require two independent signals: a queried stale NodeLease on the same
    // host as Conductor, and timed-out host connectivity. Refused TCP is alive.
    const host = s.conductor_recovery.value.hosts?.find(x => x.name === h.node);
    const leaseAt = typeof host?.node_lease_renew_time === 'string' && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$/.test(host.node_lease_renew_time) ? Date.parse(host.node_lease_renew_time) : NaN;
    if (!Number.isFinite(leaseAt) || !Number.isInteger(host.node_lease_duration_seconds) || host.node_lease_duration_seconds < 1 || host.node_lease_duration_seconds > 3600 || now - leaseAt < Math.max(c.outage_seconds * 1000, host.node_lease_duration_seconds * 1000 + 30000)) continue;
    if (!host || host.node_lease_healthy !== false || host.healthy !== false || !s.conductor.value.pods?.some(p => p.node === h.node) || !observedFresh(o?.node) || o.node.value.node !== h.node || o.node.value.ready !== false || !fresh(o.node.value.observed_at_unix_ms, now) || o.network.state !== 'unreachable' || !fresh(o.network.observed_at_unix_ms, now)) continue;
    if (!observedFresh(o.diagnostics) || !fresh(o.diagnostics.value.observed_at_unix_ms, now) || !/System Power\s*:\s*on/.test(o.diagnostics.value.chassis_status ?? '') || /Power Overload\s*:\s*true|Main Power Fault\s*:\s*true|Cooling\/Fan Fault\s*:\s*true/.test(o.diagnostics.value.chassis_status ?? '')) continue;
    if (!sameSet(o.dependencies.map(d => d.id), dev.depends_on) || !o.dependencies.every(d => d.state === 'healthy' && fresh(d.observed_at_unix_ms, now))) continue;
    return {action: 'warm_restart', target: h, key: `host/${h.device_id}`, device: dev, controller, observation: o, inventory: inv};
  }
  return blocked('No independently proven eligible recovery target');
}
export function allowedByHistory(c, records, decision, now) {
  const intents = records.filter(r => r.phase === 'intent');
  if (intents.some(r => !records.some(x => x.phase === 'recovered' && x.request_id === r.request_id))) return false;
  if (intents.some(r => !Number.isSafeInteger(r.observed_at_unix_ms) || r.observed_at_unix_ms > now)) return false;
  const recent = intents.filter(r => now - r.observed_at_unix_ms < 86400000);
  return recent.length < c.daily_action_budget && !recent.some(r => r.key === decision.key && (decision.action === 'warm_restart' || now - r.observed_at_unix_ms < c.cooldown_seconds * 1000));
}
export class Journal {
  constructor(path) {
    const flags = fs.constants.O_RDWR | fs.constants.O_CREAT | fs.constants.O_APPEND | fs.constants.O_NOFOLLOW;
    this.fd = fs.openSync(path, flags, 0o600);
    const st = fs.fstatSync(this.fd);
    if (!st.isFile() || (st.mode & 0o077) || st.uid !== process.getuid() || st.size > 16 * 1024 * 1024) throw Error('Unsafe or oversized availability journal');
    this.records = fs.readFileSync(this.fd, 'utf8').split('\n').filter(Boolean).map(line => JSON.parse(line));
    if (this.records.some(r => !['intent', 'submitted', 'recovered', 'failed'].includes(r.phase) || typeof r.request_id !== 'string' || !Number.isSafeInteger(r.observed_at_unix_ms))) throw Error('Invalid journal; recovery disabled');
  }
  append(record) { const bytes = Buffer.from(`${JSON.stringify(record)}\n`); let offset = 0; while (offset < bytes.length) offset += fs.writeSync(this.fd, bytes, offset); fs.fsyncSync(this.fd); this.records.push(record); }
}
export function requestFor(decision, requestId, now) {
  if (decision.action === 'service_restart') return {cluster_id: decision.cluster_id, namespace: decision.target.namespace, deployment: decision.target.deployment, request_id: requestId};
  const {device: d, controller: b, observation: o, inventory: inv} = decision;
  const evidence = (state, reference, at = now) => ({state, observed_at_unix_ms: at, source: 'continuum_availability_watchdog', reference});
  const reference = `Fresh Kubernetes NodeLease and NotReady observation for ${decision.target.node}; independent TCP timeout ${o.network.reference}`;
  return {controller_id: b.id, device_id: d.id, action: 'warm_restart', request_id: requestId, change_id: requestId, reason: 'Independent Continuum recovery: repeated Conductor outage, stale host NodeLease and unreachable host; fresh controller/dependency proof.', expected_inventory_revision: inv.revision,
    preflight: {observed_at_unix_ms: now, inventory_revision: inv.revision, dependency_graph_revision: inv.dependency_graph_revision, expected_power_state: 'On', target_identity_verified: true, dependencies_healthy: true, dependents_known: true, affected_services_healthy: false, monitoring_healthy: false, affected_services: d.affected_services, dependency_ids: d.depends_on,
      host_recovery: {outage_id: requestId, target: {device_id: d.id, ...evidence('unreachable', reference)}, hosted_services: d.services.map(service => ({service, ...evidence('unavailable', `Registered local service on independently unreachable host ${d.id}; ${reference}`)})), dependencies: o.dependencies.map(dep => ({dependency_id: dep.id, ...evidence('healthy', dep.reference, dep.observed_at_unix_ms)})), monitoring: evidence('unavailable', 'Conductor Deployment has no Ready replicas; independent Continuum and Kubernetes/BMC observations remain available')}}};
}
export class Watchdog {
  constructor(c, journal, api = nmc, collector = collect, clock = Date.now) { this.config = validateConfig(c); this.journal = journal; this.api = api; this.collector = collector; this.streak = null; this.recoveryStreak = 0; this.clock = clock; }
  async tick(now = this.clock()) {
    const c = this.config;
    const snapshot = await this.collector(c, this.api);
    now = Math.max(now, this.clock());
    const unresolved = this.journal.records.find(r => r.phase === 'intent' && !this.journal.records.some(x => x.request_id === r.request_id && ['recovered', 'failed'].includes(x.phase)));
    if (unresolved) {
      // Reconcile actual state after process restart or a lost response. Never
      // resubmit an acknowledged or ambiguous mutation, even with a new ID.
      const nowFresh = o => o?.value && fresh(o.observed_at_unix_ms, now);
      const healthy = nowFresh(snapshot.conductor) && ready(snapshot.conductor.value) && nowFresh(snapshot.conductor_recovery) && snapshot.conductor_recovery.value.hosts?.length > 0 && snapshot.conductor_recovery.value.hosts.every(h => h.healthy === true && h.node_lease_healthy === true) && snapshot.services.every(s => nowFresh(s.status) && ready(s.status.value)) && snapshot.acceptance?.length === c.acceptance_urls.length && snapshot.acceptance.every(p => fresh(p.observed_at_unix_ms, now) && [200, 302, 401].includes(p.status)) && (unresolved.action === 'service_restart'
        ? ready(snapshot.services.find(s => `service/${s.namespace}/${s.deployment}` === unresolved.key)?.status.value)
        : snapshot.hosts.some(h => `host/${h.id}` === unresolved.key && nowFresh(h.node) && h.node.value?.ready === true && fresh(h.node.value.observed_at_unix_ms, now) && h.node.value.metrics?.freshness === 'fresh' && h.network.state === 'healthy' && fresh(h.network.observed_at_unix_ms, now) && h.dependencies.every(d => d.state === 'healthy' && fresh(d.observed_at_unix_ms, now))));
      this.recoveryStreak = healthy ? this.recoveryStreak + 1 : 0;
      if (this.recoveryStreak >= c.failure_samples) this.journal.append({phase: 'recovered', request_id: unresolved.request_id, observed_at_unix_ms: now, key: unresolved.key});
      else if (now - unresolved.observed_at_unix_ms > c.recovery_seconds * 1000) this.journal.append({phase: 'failed', request_id: unresolved.request_id, observed_at_unix_ms: now, key: unresolved.key, reason: 'Recovery deadline exceeded; circuit remains open until operator reconciliation'});
      return {action: 'reconcile', request_id: unresolved.request_id};
    }
    const decision = assess(c, snapshot, now);
    if (decision.action === 'hold' || fs.existsSync(c.inhibit_path)) { this.streak = null; return {action: 'hold', reason: decision.reason ?? 'Maintenance inhibition active'}; }
    if (this.streak?.key === decision.key) this.streak.samples++; else this.streak = {key: decision.key, samples: 1, since: now};
    if (this.streak.samples < c.failure_samples || now - this.streak.since < c.outage_seconds * 1000) return {action: 'observe', target: decision.key};
    if (!c.enabled || !allowedByHistory(c, this.journal.records, decision, now)) return {action: 'hold', reason: 'Mutation disabled or action budget/circuit breaker open', target: decision.key};
    // Recollect every signal immediately before intent. Conductor returning,
    // inventory drift, dependency loss or a healthy host cancels the decision.
    const finalSnapshot = await this.collector(c, this.api);
    const final = assess(c, finalSnapshot, this.clock());
    if (final.action !== decision.action || final.key !== decision.key || fs.existsSync(c.inhibit_path)) { this.streak = null; return {action: 'hold', reason: 'Preflight changed'}; }
    const id = `continuum-${randomUUID()}`;
    final.cluster_id = c.cluster_id;
    const request = requestFor(final, id, this.clock());
    this.journal.append({phase: 'intent', observed_at_unix_ms: this.clock(), request_id: id, key: final.key, action: final.action, request, snapshot: finalSnapshot});
    try {
      const result = await this.api(final.action === 'warm_restart' ? ['device', 'action', '--request-json', JSON.stringify(request)] : ['k8s', 'restart-deployment', ...targetArgs(c, final.target), '--request-id', id]);
      this.journal.append({phase: 'submitted', observed_at_unix_ms: this.clock(), request_id: id, result});
    } catch { this.journal.append({phase: 'submitted', observed_at_unix_ms: this.clock(), request_id: id, outcome: 'ambiguous; inspect actual state; no resubmission'}); }
    this.streak = null;
    return {action: final.action, request_id: id, target: final.key};
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const config = validateConfig(JSON.parse(fs.readFileSync(process.argv[2], 'utf8')));
  const watchdog = new Watchdog(config, new Journal(config.journal));
  for (;;) { try { process.stdout.write(`${JSON.stringify(await watchdog.tick())}\n`); } catch (error) { watchdog.streak = null; process.stderr.write(`Availability observation blocked: ${error.message}\n`); } await new Promise(resolve => setTimeout(resolve, config.interval_seconds * 1000)); }
}
