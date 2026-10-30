#!/usr/bin/env node
// Preserve the complete IPMI SEL before a reservation-fenced clear. This has
// no chassis/power commands and never clears Continuum's recovery audit.
import fs from 'node:fs';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {gzipSync} from 'node:zlib';
import {randomUUID} from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {nmc} from './availability-watchdog.mjs';
const execute = promisify(execFile);
export function parseInfo(text) {
  const entries = Number(text.match(/^Entries\s*:\s*(\d+)\s*$/m)?.[1]);
  const percent = Number(text.match(/^Percent Used\s*:\s*(\d+)%/m)?.[1]);
  if (!Number.isSafeInteger(entries) || entries < 0 || entries > 65535 || !Number.isInteger(percent) || percent < 0 || percent > 100) throw Error('Malformed SEL capacity evidence');
  return {entries, percent};
}
function persist(file, bytes) {
  const fd = fs.openSync(file, fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL | fs.constants.O_NOFOLLOW, 0o600);
  try { fs.writeFileSync(fd, bytes); fs.fsyncSync(fd); } finally { fs.closeSync(fd); }
  const directory = fs.openSync(path.dirname(file), 'r'); try { fs.fsyncSync(directory); } finally { fs.closeSync(directory); }
}
export async function maintain(config, pinned, inventory, run = execute, env = process.env) {
  const now = Date.now();
  if (inventory.stale !== false || !Number.isSafeInteger(inventory.generated_at_unix_ms) || now < inventory.generated_at_unix_ms || now - inventory.generated_at_unix_ms > 86400000) throw Error('Fresh inventory required for SEL maintenance');
  const b = inventory.controllers?.find(b => b.id === pinned.id);
  if (!b || b.protocol !== 'ipmi' || b.endpoint !== pinned.endpoint || b.mac_address !== pinned.mac_address || JSON.stringify(b.manages) !== JSON.stringify(pinned.manages)) throw Error('Pinned controller identity changed');
  if (!/^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,126}$/.test(b.id) || !/^192\.168\.1\.\d{1,3}$/.test(b.endpoint)) throw Error('Invalid controller identity');
  // The public inventory projection intentionally omits credential references.
  // Use only the protected policy's references commissioned from private state.
  const usernameRef=pinned.username_env, passwordRef=pinned.password_env;
  if (![usernameRef,passwordRef].every(s=>typeof s==='string' && /^[A-Za-z_][A-Za-z0-9_]{0,127}$/.test(s)) || !env[usernameRef] || !env[passwordRef]) throw Error('Controller credentials unavailable');
  if (!path.isAbsolute(config.state_directory) || !Number.isInteger(config.threshold_percent) || config.threshold_percent < 50 || config.threshold_percent > 90 || !Number.isInteger(config.archive_keep) || config.archive_keep < 2 || config.archive_keep > 16) throw Error('Bounded archive policy required');
  fs.mkdirSync(config.state_directory, {recursive:true, mode:0o700});
  const dir = fs.lstatSync(config.state_directory);
  if (!dir.isDirectory() || dir.isSymbolicLink() || dir.uid !== process.getuid() || (dir.mode & 0o077)) throw Error('Private local SEL archive required');
  const ipmi = async args => (await run('/usr/bin/ipmitool', ['-I','lanplus','-H',b.endpoint,'-U',env[usernameRef],'-E','-N','2','-R','1',...args], {timeout:180000, maxBuffer:1024*1024, env:{PATH:'/usr/bin:/bin',LANG:'C',IPMI_PASSWORD:env[passwordRef]}})).stdout;
  const beforeText = await ipmi(['sel','info']);
  const before = parseInfo(beforeText);
  if (before.percent < config.threshold_percent) return {controller_id:b.id, action:'observe', ...before};
  const free = fs.statfsSync(config.state_directory);
  if (free.bavail * free.bsize < 128*1024*1024) throw Error('Insufficient space to preserve SEL; clear refused');
  const reservation = (await ipmi(['raw','0x0a','0x42'])).trim().split(/\s+/);
  if (reservation.length !== 2 || !reservation.every(x=>/^[0-9a-fA-F]{2}$/.test(x))) throw Error('SEL reservation unavailable; clear refused');
  const prefix = `${b.id}-${now}-${randomUUID()}`;
  const temporary = path.join(config.state_directory, prefix+'.raw');
  persist(temporary, Buffer.alloc(0));
  try {
    await ipmi(['sel','writeraw',temporary]);
    const st = fs.lstatSync(temporary);
    if (!st.isFile() || st.isSymbolicLink() || st.uid !== process.getuid() || (st.mode & 0o077) || st.size !== before.entries*16 || st.size === 0) throw Error('Incomplete SEL archive; clear refused');
    const bytes = fs.readFileSync(temporary);
    const archive = path.join(config.state_directory, prefix+'.sel.gz');
    persist(archive,gzipSync(bytes));
    // Persist intent and archive before the write. A BMC that changes its SEL
    // after reservation rejects the clear with invalid-reservation status.
    persist(path.join(config.state_directory,prefix+'.json'),JSON.stringify({controller_id:b.id,inventory_revision:inventory.revision,observed_at_unix_ms:now,before,archive:path.basename(archive),phase:'archived'})+'\n');
    // Apply the bounded history policy to failed maintenance attempts too.
    // These are SEL snapshots, never the host-reset intent/cooldown audit.
    const archives=fs.readdirSync(config.state_directory).filter(f=>f.startsWith(b.id+'-') && f.endsWith('.sel.gz')).sort().reverse();
    for(const old of archives.slice(config.archive_keep)) {
      const base=old.slice(0,-'.sel.gz'.length);
      for(const suffix of ['.sel.gz','.json','.cleared.json']) {
        const file=path.join(config.state_directory,base+suffix);
        if(fs.existsSync(file))fs.unlinkSync(file);
      }
    }
    const after = parseInfo(await ipmi(['sel','info']));
    if (after.entries !== before.entries) throw Error('SEL changed during archive; clear postponed');
    await ipmi(['raw','0x0a','0x47',...reservation.map(x=>'0x'+x),'0x43','0x4c','0x52','0xaa']);
    const verified = parseInfo(await ipmi(['sel','info']));
    if (verified.percent >= config.threshold_percent) throw Error('SEL clear not yet verified; archive retained');
    persist(path.join(config.state_directory,prefix+'.cleared.json'),JSON.stringify({controller_id:b.id,observed_at_unix_ms:Date.now(),after:verified,phase:'cleared'})+'\n');
    return {controller_id:b.id,action:'archived_and_cleared',before,after:verified,archive};
  } finally { fs.unlinkSync(temporary); }
}
if(process.argv[1] && import.meta.url===pathToFileURL(process.argv[1]).href){
  const config=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
  const inventory=await nmc(['device','inventory']);
  let failed=false;
  for(const b of config.controllers){try{console.log(JSON.stringify(await maintain(config,b,inventory)));}catch(error){failed=true;console.error(JSON.stringify({controller_id:b.id,action:'blocked',reason:error.message.slice(0,600)}));}}
  if(failed)process.exitCode=1;
}
