const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const catalog = require('../../nmc_server/src/docs/public/product-catalog.js');
const fixture = JSON.parse(fs.readFileSync(path.join(__dirname,'../../nmc_server/src/docs/public/products.json')));
test('four products retain exact targets and distinct responsibilities', () => {
 assert.equal(fixture.products.length,11);
 for (const id of ['cora','foresight','verity','relay']) {
  const product = fixture.products.find(product=>product.id===id);
  assert.equal(product.runtime_target.namespace,id);
  assert.equal(product.runtime_target.deployment,id);
  assert.equal(product.delivery_authority,'conductor');
  assert.equal(product.operations_authority,'continuum');
  assert.match(catalog.statusPath(product,{runtime_cluster_id:'k8s-local-refiner'}),new RegExp(`namespace=${id}&deployment=${id}$`));
 }
});
test('availability requires live identity, generations and ready capacity', () => {
 const state = {uid:'observed-uid',desired_replicas:1,available_replicas:1,ready_replicas:1,generation:2,observed_generation:2,eligible:false};
 assert.equal(catalog.observation({ok:true,payload:{data:state}}),'Available: 1/1 replicas');
 assert.match(catalog.observation({ok:true,payload:{data:{...state,uid:''}}}),/Not observed/);
 assert.match(catalog.observation({ok:true,payload:{data:{...state,observed_generation:1}}}),/Degraded/);
 assert.match(catalog.observation({ok:true,payload:{data:{...state,desired_replicas:0}}}),/Suspended/);
 assert.equal(catalog.observation({ok:false,status:403}),'Observation unavailable');
});
test('observation paths cannot redirect operator credentials or target another product', () => {
 const product=fixture.products.find(product=>product.id==='cora');
 for(const status_path of ['https://other.invalid/k8s/deployment/recovery-status?cluster_id=x&namespace=cora&deployment=cora','/k8s/deployment/recovery-status?cluster_id=x&namespace=postgres&deployment=postgres','/k8s/deployment/restart']) assert.equal(catalog.statusPath({...product,status_path},{}),null);
 assert.equal(catalog.statusPath(product,{runtime_cluster_id:'unsafe/id'}),null);
});
