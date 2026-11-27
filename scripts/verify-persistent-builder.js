#!/usr/bin/env node
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const base = path.resolve(__dirname, '..', 'tools/shader-builder');
const ctx = vm.createContext({window: {}, console, Map, Set, Number});
for (const file of ['engine/graph.js','nodes/catalog.js','engine/codegen.js']) {
  vm.runInContext(fs.readFileSync(path.join(base,file),'utf8'),ctx,{filename:file});
}
const {ShaderGraph, NodeCatalog, ShaderCodeGenerator} =
  vm.runInContext('({ShaderGraph,NodeCatalog,ShaderCodeGenerator})',ctx);
const catalog = new NodeCatalog(vm.runInContext('NODE_CATALOG',ctx));
const generator = new ShaderCodeGenerator();
function makeGraph(type, passId, increment) {
  const graph = new ShaderGraph();
  const source = graph.addNode(type,{x:0,y:0},catalog);
  assert.ok(source, 'persistent texture nodes must be available');
  if (passId) graph.setNodeValue(source.id,'passId',passId);
  const out = graph.addNode('output_color',{x:300,y:0},catalog);
  if (increment !== undefined) {
    const add = graph.addNode('math_add',{x:150,y:0},catalog);
    graph.setNodeValue(add.id,'b',increment);
    assert.ok(graph.connect(source.id,'output',add.id,'a'));
    assert.ok(graph.connect(add.id,'result',out.id,'color'));
  } else assert.ok(graph.connect(source.id,'output',out.id,'color'));
  return graph;
}
const a = makeGraph('input_pass_previous','accumulate',0.01);
const b = makeGraph('input_pass_current','accumulate',0.02);
const final = makeGraph('input_pass_current','resolve');
const passes = [
  {id:'accumulate',divisor:2,format:1,graph:a},
  {id:'resolve',divisor:4,format:1,graph:b}
];
const code = generator.generatePersistent(passes, final);
assert.match(code,/uniform int hs_pass_accumulate_order = 0;/);
assert.match(code,/uniform int hs_pass_resolve_order = 1;/);
assert.match(code,/uniform int hs_pass_accumulate_divisor = 2;/);
assert.match(code,/uniform int hs_pass_resolve_format = 1;/);
assert.match(code,/hs_pass_accumulate_previous.Sample\(textureSampler, v_in.uv\)/);
assert.match(code,/hs_pass_accumulate.Sample\(textureSampler, v_in.uv\)/);
assert.match(code,/hs_pass_resolve.Sample\(textureSampler, v_in.uv\)/);
assert.equal((code.match(/#include "..\/stdlib\/hypershader_standard.glsl"/g)||[]).length,1);
assert.match(code,/technique Pass_accumulate/);
assert.match(code,/technique Pass_resolve/);
assert.match(code,/technique Draw/);
assert.throws(()=>generator.generatePersistent([{...passes[0],id:'bad-id'}],final),/pass/i);
assert.throws(()=>generator.generatePersistent([passes[0],passes[0]],final),/pass/i);
assert.throws(()=>generator.generatePersistent([{...passes[0],divisor:3}],final),/divisor/i);
assert.throws(()=>generator.generatePersistent(passes.slice(0,1),final),/resolve/i);
assert.throws(()=>generator.generatePersistent([{...passes[0],graph:new ShaderGraph()}],final),/output/i);
assert.throws(()=>generator.generate(final),/persistent/i);
assert.equal(generator.generatePersistent(passes,final),code);
assert.match(generator.generate(makeGraph('input_color')),/technique Draw/);
if(process.env.HS_PERSISTENT_EFFECT) fs.writeFileSync(process.env.HS_PERSISTENT_EFFECT,code);
console.log('PERSISTENT_BUILDER_CODEGEN_PASS=1');
