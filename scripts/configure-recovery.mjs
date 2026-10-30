#!/usr/bin/env node
// Provision explicit local observer policy from the verified inventory.
// Run as root with the service uid/gid; never print or pass credentials in argv.
import fs from 'node:fs';
import {randomBytes} from 'node:crypto';
if(process.getuid()!==0)throw Error('Root provisioning required');
const uid=Number(process.argv[2]),gid=Number(process.argv[3]);
if(!Number.isInteger(uid)||uid<=0||!Number.isInteger(gid)||gid<=0)throw Error('Service identity required');
if(process.env.NMC_ENABLE_HOST_RECOVERY!=='true')throw Error('Explicit host recovery authorization required');
const inv=JSON.parse(fs.readFileSync('/var/lib/nmc/device-inventory.json','utf8'));
if(Date.now()-inv.generated_at_unix_ms>86400000||inv.generated_at_unix_ms>Date.now())throw Error('Refresh verified inventory first');
function directory(p){fs.mkdirSync(p,{recursive:true,mode:0o700});fs.chmodSync(p,0o700);fs.chownSync(p,uid,gid);}
function write(p,value){const temp=p+'.new';fs.writeFileSync(temp,typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{mode:0o600});fs.chownSync(temp,uid,gid);fs.chmodSync(temp,0o600);fs.renameSync(temp,p);}
directory('/var/lib/nmc/.nmc');directory('/var/lib/nmc/bmc-observer');directory('/var/lib/nmc/bmc-sel');
const tokenLine=fs.readFileSync('/etc/nmc/nmc.env','utf8').split('\n').find(l=>l.startsWith('NMC_AUTH_TOKEN='));
if(!tokenLine)throw Error('Protected local authentication token unavailable');
const token=tokenLine.slice('NMC_AUTH_TOKEN='.length).replace(/^"(.*)"$/,'$1');
if(!token)throw Error('Local control token is empty');
write('/var/lib/nmc/.nmc/config.json',{connections:[{name:'continuum-local-recovery',endpoint:'http://127.0.0.1:8080',isActive:true,token}],default_connection:'continuum-local-recovery'});
const controllers=['qc01','qc05'].map(id=>{
 const d=inv.devices.find(d=>d.id===id),b=inv.controllers.find(b=>b.id===d?.controller_id);
 if(!d||!b||b.protocol!=='ipmi'||b.power_actions_enabled===false||!b.mac_address||b.manages.length!==1||b.manages[0]!==id)throw Error('Verified IPMI target required: '+id);
 return {d,b};
});
const policy={enabled:true,cluster_id:'rk1',conductor:{namespace:'conductor',deployment:'conductor'},hosts:controllers.map(({d,b})=>({device_id:d.id,controller_id:b.id,node:d.id,address:d.addresses[0],port:22,controller_endpoint:b.endpoint,controller_mac:b.mac_address,dependencies:d.depends_on.map(id=>{const dep=inv.devices.find(x=>x.id===id);if(!dep?.addresses?.length)throw Error('Unresolved dependency '+id);return {device_id:id,address:dep.addresses[0],port:id==='spirit'?6443:80};})})),services:[],acceptance_urls:['https://conductor.neuralmimicry.ai/','https://octobot.neuralmimicry.ai/'],interval_seconds:30,failure_samples:3,outage_seconds:120,cooldown_seconds:3600,recovery_seconds:900,daily_action_budget:2,journal:'/var/lib/nmc/availability-watchdog.jsonl',inhibit_path:'/var/lib/nmc/availability-watchdog.inhibit',inventory_max_age_seconds:86400};
write('/etc/nmc/availability-watchdog.json',policy);
write('/etc/nmc/bmc-observer.json',{controllers:controllers.map(({b})=>({id:b.id,protocol:b.protocol,endpoint:b.endpoint,manages:b.manages})),enroll_push:false,interval_seconds:120,stale_seconds:600,inventory_max_age_seconds:86400,listen_address:'127.0.0.1',port:9105,state_directory:'/var/lib/nmc/bmc-observer',metrics_token_file:'/etc/nmc/bmc-metrics-token'});
if(!fs.existsSync('/etc/nmc/bmc-metrics-token'))write('/etc/nmc/bmc-metrics-token',randomBytes(32).toString('hex'));
write('/etc/nmc/bmc-log-guard.json',{controllers:controllers.map(({b})=>({id:b.id,endpoint:b.endpoint,mac_address:b.mac_address,manages:b.manages,username_env:b.username_env,password_env:b.password_env})),threshold_percent:80,archive_keep:6,state_directory:'/var/lib/nmc/bmc-sel'});
console.log(JSON.stringify({configured:true,enabled:true,targets:policy.hosts.map(h=>h.device_id),warm_restart_only:true}));
