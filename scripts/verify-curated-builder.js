#!/usr/bin/env node
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const base = path.resolve(__dirname, '..', 'tools/shader-builder');
const ctx = vm.createContext({window: {}, console, Map, Set, Number});
for (const file of ['engine/graph.js','nodes/catalog.js','engine/codegen.js','curated.js']) {
  vm.runInContext(fs.readFileSync(path.join(base,file), 'utf8'),ctx,{filename:file});
}
const {ShaderGraph, NodeCatalog, ShaderCodeGenerator, makeCuratedGraph} =
  vm.runInContext('({ShaderGraph,NodeCatalog,ShaderCodeGenerator,makeCuratedGraph})',ctx);
const catalog = new NodeCatalog(vm.runInContext('NODE_CATALOG',ctx));
const generator = new ShaderCodeGenerator();
const results=[];
for (const n of [0, 0.17, 0.39, 0.59, 0.76, 0.99]) {
  // Fake only the random choice; graph creation and code generation are real.
  const created=makeCuratedGraph(catalog,()=>n);
  const graph=created.graph;
  assert.ok(graph.getOutputNode(), 'curation must produce a connected output');
  const nodes=[...graph.nodes.values()];
  const color=nodes.filter(v => /^color_/.test(v.type));
  assert.ok(color.length<=1, 'one supporting color look, never competing remaps');
  const uv=nodes.filter(v => /^distort_/.test(v.type));
  assert.equal(uv.length,1,'one intentional geometric hero');
  assert.equal(graph.edges.length>=3,true);
  const out=graph.getOutputNode();
  assert.ok(graph.getInputConnection(out.id,'color'));
  const alpha=nodes.find(v=>v.type==='tex_split_alpha');
  if (color.length) {
    assert.ok(alpha,'color stage must preserve original alpha');
    const combine=nodes.find(v=>v.type==='tex_combine');
    assert.equal(graph.getInputConnection(combine.id,'a')?.from.nodeId,alpha.id);
  }
  const code=generator.generate(graph);
  assert.match(code,/technique Draw/);
  assert.match(code,/image.Sample/);
  assert.match(code,/return finalColor/);
  const preview=generator.buildPreviewShader(graph);
  assert.match(preview,/#version 300 es/);
  assert.match(preview,/out vec4 previewColor/);
  const roundtrip = graph.serialize();
  const recovered = new ShaderGraph();
  recovered.deserialize(JSON.parse(JSON.stringify(roundtrip)),catalog);
  assert.equal(recovered.edges.length,graph.edges.length);
  assert.equal(generator.generate(recovered),code,'saved graph must reproduce source');
  results.push([created.name,code]);
}
assert.equal(new Set(results.map(v=>v[0])).size>=3,true,'different seeds produce different looks');
const html=fs.readFileSync(path.join(base,'index.html'),'utf8');
const app=fs.readFileSync(path.join(base,'app.js'),'utf8');
assert.match(html,/id="btnCurate"/);
assert.match(html,/src="curated\.js"/);
assert.match(app,/makeCuratedGraph/);
if (process.env.HYPER_CURATED_EFFECT_DIR) {
  fs.mkdirSync(process.env.HYPER_CURATED_EFFECT_DIR,{recursive:true});
  results.forEach(([name,source],i)=>{
    fs.writeFileSync(path.join(process.env.HYPER_CURATED_EFFECT_DIR,`curated-${i}.effect`),source);
  });
}
console.log('CURATED_GRAPH_CONTRACT_PASS=1 variants='+results.length);
