import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {maintain,parseInfo} from '../../scripts/bmc-log-guard.mjs';
const b={id:'qc01-bmc',protocol:'ipmi',endpoint:'192.168.1.51',mac_address:'70:e2:84:1b:a3:dc',manages:['qc01'],username_env:'BMC_USER',password_env:'BMC_PASS'};
const inventory=()=>({stale:false,generated_at_unix_ms:Date.now(),revision:'verified',controllers:[b]});
function fixture(fn){return async()=>{const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sel-guard-'));try{await fn({state_directory:dir,threshold_percent:80,archive_keep:3});}finally{fs.rmSync(dir,{recursive:true,force:true});}};}
test('capacity evidence must be complete and bounded',()=>{assert.throws(()=>parseInfo('garbage'));assert.throws(()=>parseInfo('Entries: 5\nPercent Used: 101%'));assert.deepEqual(parseInfo('Entries: 3\nPercent Used: 100%'),{entries:3,percent:100});});
test('full log is durably archived before a reservation-fenced clear',fixture(async c=>{
 let cleared=false;const calls=[];
 const run=async(exe,args,opts)=>{calls.push(args);assert.equal(exe,'/usr/bin/ipmitool');assert.equal(opts.env.IPMI_PASSWORD,'test-secret');assert(!args.includes('test-secret'));const cmd=args.slice(12);if(args.includes('writeraw')){fs.writeFileSync(args.at(-1),Buffer.alloc(48,7));return {stdout:''};}if(args.includes('0x42'))return {stdout:'2a 01\n'};if(args.includes('0x47')){assert(args.includes('0x2a')&&args.includes('0x01'));assert(fs.readdirSync(c.state_directory).some(f=>f.endsWith('.sel.gz')));cleared=true;return {stdout:'00'};}return {stdout:`Entries: ${cleared?0:3}\nPercent Used: ${cleared?0:100}%`};};
 const result=await maintain(c,b,inventory(),run,{BMC_USER:'admin',BMC_PASS:'test-secret'});assert.equal(result.action,'archived_and_cleared');assert.equal(gunzipSync(fs.readFileSync(result.archive)).length,48);assert(cleared);
}));
test('partial archive and changing SEL never clear hardware history',fixture(async c=>{
 let clears=0;let short=true;let info=0;
 const run=async(exe,args)=>{if(args.includes('writeraw')){fs.writeFileSync(args.at(-1),Buffer.alloc(short?16:48));return {stdout:''};}if(args.includes('0x42'))return {stdout:'01 00'};if(args.includes('0x47')){clears++;return {stdout:'00'};}return {stdout:`Entries: ${++info>1?4:3}\nPercent Used: 100%`};};
 await assert.rejects(maintain(c,b,inventory(),run,{BMC_USER:'admin',BMC_PASS:'secret'}),/Incomplete/);short=false;info=0;await assert.rejects(maintain(c,b,inventory(),run,{BMC_USER:'admin',BMC_PASS:'secret'}),/changed/);assert.equal(clears,0);
}));
test('stale inventory or changed controller identity blocks before IPMI',fixture(async c=>{
 let calls=0;const run=async()=>{calls++;throw Error('must not execute');};const env={BMC_USER:'admin',BMC_PASS:'secret'};
 await assert.rejects(maintain(c,b,{...inventory(),stale:true},run,env));await assert.rejects(maintain(c,{...b,endpoint:'192.168.1.52'},inventory(),run,env));assert.equal(calls,0);
}));
