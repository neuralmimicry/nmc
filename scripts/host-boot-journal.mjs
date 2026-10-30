import fs from 'node:fs';
import path from 'node:path';
import {randomUUID} from 'node:crypto';

// Independent of the recovered host and Conductor's database. The event is
// fsynced before its boot-ID checkpoint, so interruption can only replay it.
export class BootJournal {
  constructor(file) {
    this.file=file;this.stateFile=file+'.state.json';this.state={};this.lastCheckpoint=0;
    if(fs.existsSync(this.stateFile)){
      const st=fs.lstatSync(this.stateFile);
      if(!st.isFile()||st.isSymbolicLink()||st.uid!==process.getuid()||(st.mode&0o077)||st.size>65536)throw Error('Unsafe boot checkpoint');
      this.state=JSON.parse(fs.readFileSync(this.stateFile,'utf8'));
    }
  }
  checkpoint(now) {
    const temporary=this.stateFile+'.'+randomUUID();
    const fd=fs.openSync(temporary,fs.constants.O_WRONLY|fs.constants.O_CREAT|fs.constants.O_EXCL|fs.constants.O_NOFOLLOW,0o600);
    try{fs.writeFileSync(fd,JSON.stringify(this.state));fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
    fs.renameSync(temporary,this.stateFile);this.syncDirectory();this.lastCheckpoint=now;
  }
  syncDirectory(){const fd=fs.openSync(path.dirname(this.file),'r');try{fs.fsyncSync(fd);}finally{fs.closeSync(fd);}}
  append(record) {
    if(fs.existsSync(this.file)){
      const st=fs.lstatSync(this.file);
      if(!st.isFile()||st.isSymbolicLink()||st.nlink!==1||st.uid!==process.getuid()||(st.mode&0o077))throw Error('Unsafe boot audit');
      if(st.size>1024*1024){for(let i=3;i>=1;i--)if(fs.existsSync(this.file+'.'+i))fs.renameSync(this.file+'.'+i,this.file+'.'+(i+1));fs.renameSync(this.file,this.file+'.1');}
    }
    const bytes=Buffer.from(JSON.stringify(record)+'\n');if(bytes.length>16384)throw Error('Boot record exceeds bound');
    const fd=fs.openSync(this.file,fs.constants.O_WRONLY|fs.constants.O_CREAT|fs.constants.O_APPEND|fs.constants.O_NOFOLLOW,0o600);
    try{let offset=0;while(offset<bytes.length)offset+=fs.writeSync(fd,bytes,offset);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
    this.syncDirectory();
  }
  observe(config,snapshot,now=Date.now()) {
    const fresh=at=>Number.isSafeInteger(at)&&now>=at&&now-at<=60000;
    const inventory=snapshot.inventory?.value;
    if(!fresh(snapshot.inventory?.observed_at_unix_ms)||inventory?.stale!==false||!Number.isSafeInteger(inventory.generated_at_unix_ms)||now<inventory.generated_at_unix_ms||now-inventory.generated_at_unix_ms>config.inventory_max_age_seconds*1000)return;
    let changed=false;
    for(const host of config.hosts){
      const observation=snapshot.hosts.find(h=>h.id===host.device_id),node=observation?.node?.value;
      const device=inventory.devices?.find(d=>d.id===host.device_id),controller=inventory.controllers?.find(c=>c.id===host.controller_id);
      if(device?.controller_id!==host.controller_id||!device.addresses?.includes(host.address)||!controller?.manages?.includes(host.device_id)||controller.endpoint!==host.controller_endpoint||controller.mac_address!==host.controller_mac)continue;
      if(node?.node!==host.node||!fresh(observation?.node?.observed_at_unix_ms)||!fresh(node.observed_at_unix_ms)||node.ready!==true||node.lease?.available!==true||node.lease.healthy!==true||observation.network?.state!=='healthy'||!fresh(observation.network.observed_at_unix_ms)||typeof node.boot_id!=='string'||!/^[A-Za-z0-9-]{8,64}$/.test(node.boot_id))continue;
      const previous=this.state[host.device_id];
      if(previous&&previous.boot_id!==node.boot_id){
        const id=`boot-${host.device_id}-${node.boot_id}`;
        this.append({request_id:id,device_id:host.device_id,controller_id:host.controller_id,action:'reboot',phase:'observed',verified:true,
          started_at_unix_ms:now,observed_at_unix_ms:now,previous_observed_at_unix_ms:previous.seen_at_unix_ms,
          previous_boot_id:previous.boot_id,boot_id:node.boot_id,source:'continuum_direct_nodelease_and_ssh',
          reason:'Boot ID changed; exact physical reset cause remains to be investigated'});
        changed=true;
      }
      if(!previous)changed=true;
      this.state[host.device_id]={boot_id:node.boot_id,seen_at_unix_ms:now};
    }
    if(changed||now-this.lastCheckpoint>=300000)this.checkpoint(now);
  }
}
