import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {assess, allowedByHistory, Watchdog, Journal, unwrap, validateConfig, requestFor} from '../../scripts/availability-watchdog.mjs';
import {BootJournal} from '../../scripts/host-boot-journal.mjs';
const stamp = Date.now();
const observed = value => ({value, observed_at_unix_ms: stamp});
const evidence = (state, reference = 'observed') => ({state, source: 'test', reference, observed_at_unix_ms: stamp});
const config = () => ({enabled: true, cluster_id: 'rk1', conductor: {namespace:'conductor', deployment:'conductor'}, hosts:[{device_id:'qc01', controller_id:'qc01-bmc', node:'qc01', address:'192.168.1.61', port:22, controller_endpoint:'192.168.1.51', controller_mac:'70:e2:84:1b:a3:dc', dependencies:[{device_id:'spirit', address:'192.168.1.2', port:6443}]}], services:[], acceptance_urls:['https://octobot.neuralmimicry.ai/'], inventory_max_age_seconds:86400, interval_seconds:10, failure_samples:3, outage_seconds:120, cooldown_seconds:3600, recovery_seconds:600, daily_action_budget:2, journal:'/var/lib/nmc/test-journal', inhibit_path:'/nonexistent-watchdog-test-inhibit'});
test('boot history survives observer restarts and records boot changes once',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'boot-history-')),file=path.join(dir,'boots');
 try {
  const s=snapshot(),c=config(),h=s.hosts[0];h.node.value.ready=true;h.node.value.lease.healthy=true;h.network.state='healthy';h.node.value.boot_id='boot-before';
  new BootJournal(file).observe(c,s,stamp);assert.equal(fs.existsSync(file),false);
  h.node.value.boot_id='boot-after';new BootJournal(file).observe(c,s,stamp+1000);
  new BootJournal(file).observe(c,s,stamp+2000);
  const records=fs.readFileSync(file,'utf8').trim().split('\n').map(JSON.parse);
  assert.equal(records.length,1);assert.equal(records[0].previous_boot_id,'boot-before');assert.equal(records[0].boot_id,'boot-after');assert.equal(records[0].action,'reboot');assert.equal(records[0].previous_observed_at_unix_ms,stamp);
  assert.equal(fs.statSync(file).mode&0o077,0);
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
test('stale or mismatched evidence cannot establish a boot baseline',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'boot-evidence-'));
 try {for(const mutate of [s=>s.inventory.value.stale=true,s=>s.inventory.value.controllers[0].mac_address='wrong',s=>s.hosts[0].node.value.node='other',s=>s.hosts[0].network.observed_at_unix_ms=stamp-60001,s=>s.hosts[0].node.value.observed_at_unix_ms=stamp+1]){
  const s=snapshot(),h=s.hosts[0];h.node.value.ready=true;h.node.value.lease.healthy=true;h.network.state='healthy';h.node.value.boot_id='boot-baseline';mutate(s);
  const journal=new BootJournal(path.join(dir,'boots'));journal.observe(config(),s,stamp);assert.deepEqual(journal.state,{});
 }} finally {fs.rmSync(dir,{recursive:true,force:true});}
});
test('boot history rotation is bounded and unsafe audit files are rejected',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'boot-rotation-')),file=path.join(dir,'boots'),j=new BootJournal(file);
 try {for(let i=0;i<7;i++){fs.writeFileSync(file,' '.repeat(1024*1024+1),{mode:0o600});j.append({request_id:'boot-'+i});}
  assert(fs.existsSync(file+'.4'));assert(!fs.existsSync(file+'.5'));
  fs.chmodSync(file,0o644);assert.throws(()=>j.append({}));
  fs.unlinkSync(file);fs.symlinkSync(file+'.1',file);assert.throws(()=>j.append({}));
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
function snapshot() { return {health:observed({status:'ok'}), inventory:observed({stale:false, generated_at_unix_ms:stamp, revision:'revision', dependency_graph_revision:'graph', devices:[{id:'qc01', addresses:['192.168.1.61'], controller_id:'qc01-bmc', services:['NFS','pod:conductor'], affected_services:['NFS','pod:conductor','octobot'], depends_on:['spirit']}], controllers:[{id:'qc01-bmc', endpoint:'192.168.1.51', mac_address:'70:e2:84:1b:a3:dc', manages:['qc01'], power_actions_enabled:true}]}), conductor:observed({ready_replicas:0,desired_replicas:1,pod_diagnostics_available:true,pod_diagnostics_truncated:false,pods:[{node:'qc01'}]}), conductor_recovery:observed({hosts:[{name:'qc01',node_lease_healthy:false,healthy:false,node_lease_renew_time:new Date(stamp-300000).toISOString(),node_lease_duration_seconds:40}]}), hosts:[{id:'qc01',node:observed({node:'qc01',ready:false,observed_at_unix_ms:stamp,lease:{available:true,healthy:false,renew_time:new Date(stamp-300000).toISOString(),duration_seconds:40}}),network:evidence('unreachable'),diagnostics:observed({observed_at_unix_ms:stamp,chassis_status:'System Power : on\nMain Power Fault : false'}),dependencies:[{id:'spirit',...evidence('healthy')}]}],services:[]}; }
test('warm recovery requires independent, complete, fresh evidence', () => {
  assert.equal(assess(config(),snapshot(),stamp).action,'warm_restart');
  for (const mutate of [s=>s.hosts[0].network.state='reachable',s=>s.hosts[0].network.state='unknown',s=>s.hosts[0].node.value.ready=true,s=>s.hosts[0].node.value.lease.healthy=true,s=>s.hosts[0].dependencies[0].state='unknown',s=>s.inventory.value.stale=true,s=>s.inventory.value.controllers[0].endpoint='192.168.1.52',s=>s.inventory.value.devices[0].depends_on.push('swrack'),s=>s.hosts[0].diagnostics.value.chassis_status='System Power : on\nMain Power Fault : true',s=>s.hosts[0].node.value.lease.available=false,s=>s.hosts[0].node.value.lease.renew_time='bad-time']) {
    const s=snapshot(); mutate(s); assert.equal(assess(config(),s,stamp).action,'hold');
  }
  assert.equal(assess(config(),snapshot(),stamp+60001).action,'hold');
});
test('request preserves complete registered local impacts and dependency observations',()=>{
  const d=assess(config(),snapshot(),stamp); const r=requestFor(d,'recovery-0001',stamp);
  assert.equal(r.action,'warm_restart'); assert.deepEqual(r.preflight.affected_services,d.device.affected_services);
  assert.deepEqual(r.preflight.host_recovery.hosted_services.map(s=>s.service),d.device.services);
  assert.equal(r.preflight.host_recovery.dependencies[0].dependency_id,'spirit');
});
test('service restart has priority and does not reboot a healthy host',()=>{
  const c=config(); c.services=[{namespace:'customers',deployment:'customers'}];
  const s=snapshot(); s.hosts[0].node.value.ready=true; s.services=[{...c.services[0], status:observed({ready_replicas:0}), recovery:observed({eligible:true,recovery_enabled:true,readiness_degraded:true,hosts:[{healthy:true,node_lease_healthy:true}]})}];
  assert.equal(assess(c,s,stamp).action,'service_restart');
  s.services[0].recovery.value.recovery_enabled=false; s.hosts[0].node.value.ready=true;
  assert.equal(assess(c,s,stamp).action,'hold');
});
test('durable budgets and unresolved replies survive process restart',()=>{
  const c=config(), d=assess(c,snapshot(),stamp);
  const intent={phase:'intent',request_id:'one',key:d.key,observed_at_unix_ms:stamp-1};
  assert.equal(allowedByHistory(c,[intent],d,stamp),false);
  assert.equal(allowedByHistory(c,[intent,{phase:'failed',request_id:'one'}],d,stamp),false);
  assert.equal(allowedByHistory(c,[intent,{phase:'recovered',request_id:'one'}],d,stamp),false);
  assert.equal(allowedByHistory(c,[intent,{phase:'recovered',request_id:'one'}],d,stamp+86400001),true);
});
test('a recovered host may recover a later independent outage after cooldown within the daily budget',()=>{
 const c=config(),d=assess(c,snapshot(),stamp);
 const records=[{phase:'intent',request_id:'prior',key:d.key,action:'warm_restart',observed_at_unix_ms:stamp-3600001},{phase:'recovered',request_id:'prior',observed_at_unix_ms:stamp-3500000}];
 assert.equal(allowedByHistory(c,records,d,stamp),true);
 assert.equal(allowedByHistory(c,[...records,{phase:'intent',request_id:'second',key:'host/other',observed_at_unix_ms:stamp-3600001},{phase:'recovered',request_id:'second'}],d,stamp),false);
});
test('journal is owner-only, fsynced, and rejects symlinks/corruption',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'watchdog-'));
  try { const p=path.join(dir,'audit');const j=new Journal(p);j.append({phase:'intent',request_id:'one',observed_at_unix_ms:stamp});assert.equal(new Journal(p).records.length,1);fs.symlinkSync(p,path.join(dir,'link'));assert.throws(()=>new Journal(path.join(dir,'link')));fs.appendFileSync(p,'bad-json\n');assert.throws(()=>new Journal(p)); } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
test('journal compaction retains unresolved intents and the live cooldown window',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'watchdog-rotation-'));
 try {
  const p=path.join(dir,'audit'),j=new Journal(p),old=stamp-8*86400000;
  j.append({phase:'intent',request_id:'retired',observed_at_unix_ms:old});
  j.append({phase:'recovered',request_id:'retired',observed_at_unix_ms:old+1});
  j.append({phase:'intent',request_id:'unresolved',observed_at_unix_ms:old});
  j.append({phase:'intent',request_id:'recent',observed_at_unix_ms:stamp-1000});
  j.append({phase:'recovered',request_id:'recent',observed_at_unix_ms:stamp});
  j.compact(stamp);
  assert.deepEqual(new Journal(p).records.map(r=>r.request_id),['unresolved','recent','recent']);
  assert(fs.existsSync(p+'.1.gz'));
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
test('stale/unknown responses reset streaks without mutation',async()=>{
  const c=config(); const calls=[]; const j={records:[],append:r=>j.records.push(r)};
  const w=new Watchdog(c,j,async args=>calls.push(args),async()=>snapshot());
  assert.equal((await w.tick(stamp)).action,'observe');
  assert.equal((await w.tick(stamp+30000)).action,'observe');
  assert.equal((await w.tick(stamp+120000)).action,'hold');
  assert.equal(calls.length,0);
  assert.equal(w.streak,null);
});
test('disabled policy and incomplete config never open mutation authority',()=>{
  assert.throws(()=>validateConfig({...config(),outage_seconds:0}));
  assert.throws(()=>validateConfig({...config(),hosts:[{...config().hosts[0],dependencies:[]}]}));
  assert.throws(()=>unwrap('{"success":true,"data":{"success":false,"data":{}}}'));
});
test('qualified warm restart submits once; lost reply remains fenced after restart',async()=>{
  let now=stamp; const c=config(); const calls=[]; const j={records:[],append:r=>j.records.push(r)};
  const collectFresh=async()=>{const s=snapshot();const update=v=>{if(v&&typeof v==='object'){for(const k of Object.keys(v)) if(k==='observed_at_unix_ms'||k==='generated_at_unix_ms') v[k]=now; else update(v[k]);}};update(s);return s;};
  const api=async args=>{calls.push(args);throw Error('lost reply');};
  const w=new Watchdog(c,j,api,collectFresh,()=>now);
  assert.equal((await w.tick()).action,'observe');now+=60000;
  assert.equal((await w.tick()).action,'observe');now+=60000;
  assert.equal((await w.tick()).action,'warm_restart');assert.equal(calls.length,1);
  const restarted=new Watchdog(c,j,api,collectFresh,()=>now);now+=30000;
  assert.equal((await restarted.tick()).action,'reconcile');assert.equal(calls.length,1);
  now+=700000;await restarted.tick();assert.equal(j.records.at(-1).phase,'failed');
  for(let i=0;i<4;i++){now+=60000;await restarted.tick();}assert.equal(calls.length,1);
});
test('host returning at final preflight cancels restart',async()=>{
  let now=stamp;const c=config(),j={records:[],append:r=>j.records.push(r)};let collections=0,mutations=0;
  const collector=async()=>{const s=snapshot();const refresh=v=>{if(v&&typeof v==='object')for(const k of Object.keys(v))if(k==='observed_at_unix_ms'||k==='generated_at_unix_ms')v[k]=now;else refresh(v[k]);};refresh(s);if(++collections===4){s.hosts[0].node.value.ready=true;s.hosts[0].node.value.lease.healthy=true;}return s;};
  const w=new Watchdog(c,j,async()=>mutations++,collector,()=>now);
  await w.tick();now+=60000;await w.tick();now+=60000;
  assert.equal((await w.tick()).action,'hold');assert.equal(mutations,0);assert.equal(j.records.length,0);
});

test('host recovery survives Pod eviction, a healthy Conductor, and application-observation failure',()=>{
  for(const mutate of [s=>{s.conductor.value.pods=[];s.conductor_recovery.value.hosts=[];},s=>{s.conductor.value.ready_replicas=1;},s=>{s.conductor.value=null;s.conductor_recovery.value=null;}]){
    const s=snapshot();mutate(s);assert.equal(assess(config(),s,stamp).action,'warm_restart');
  }
});
test('absent, malformed, future, or too-recent direct Lease never authorizes a reset',()=>{
  for(const mutate of [s=>delete s.hosts[0].node.value.lease,s=>s.hosts[0].node.value.lease.renew_time=new Date(stamp+1).toISOString(),s=>s.hosts[0].node.value.lease.renew_time=new Date(stamp-30000).toISOString(),s=>s.hosts[0].node.value.lease.duration_seconds=0]){
    const s=snapshot();mutate(s);assert.equal(assess(config(),s,stamp).action,'hold');
  }
});
test('host recovery reconciles with direct Lease and TCP even while unrelated applications remain broken',async()=>{
  let now=stamp;const c=config(),j={records:[{phase:'intent',request_id:'warm-one',action:'warm_restart',key:'host/qc01',observed_at_unix_ms:stamp-60000}],append:r=>j.records.push(r)};
  const collector=async()=>{const s=snapshot();s.conductor.value=null;s.conductor_recovery.value=null;s.hosts[0].node.value.ready=true;s.hosts[0].node.value.lease.healthy=true;s.hosts[0].network.state='healthy';return s;};
  const w=new Watchdog(c,j,async()=>assert.fail('must never resubmit'),collector,()=>now);
  for(let i=0;i<3;i++)await w.tick();assert.equal(j.records.at(-1).phase,'recovered');
});
test('a timed-out action stays fenced but sustained later recovery automatically reconciles it',async()=>{
 const c=config(),j={records:[{phase:'intent',request_id:'slow',action:'warm_restart',key:'host/qc01',observed_at_unix_ms:stamp-900000},{phase:'failed',request_id:'slow',observed_at_unix_ms:stamp-1000}],append:r=>j.records.push(r)};
 const collector=async()=>{const s=snapshot();s.hosts[0].node.value.ready=true;s.hosts[0].node.value.lease.healthy=true;s.hosts[0].network.state='healthy';return s;};
 const w=new Watchdog(c,j,async()=>assert.fail('ambiguous actions must never be retried'),collector,()=>stamp);
 for(let i=0;i<3;i++)assert.equal((await w.tick()).action,'reconcile');
 assert.equal(j.records.at(-1).phase,'recovered');
});
