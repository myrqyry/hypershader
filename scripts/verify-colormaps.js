#!/usr/bin/env node
// Verifies named-colormap integration; intentionally exercises both outputs.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const base = path.join(root, 'tools/shader-builder');
const ctx = vm.createContext({ console, window: {} });
for (const file of ['engine/graph.js','nodes/catalog.js','nodes/colormap-kernels.js','engine/codegen.js']) {
  vm.runInContext(fs.readFileSync(path.join(base,file),'utf8'),ctx,{filename:file});
}
const { ShaderGraph, NodeCatalog, ShaderCodeGenerator } =
  vm.runInContext('({ShaderGraph,NodeCatalog,ShaderCodeGenerator})',ctx);
const catalog = new NodeCatalog(vm.runInContext('NODE_CATALOG', ctx));
const expected = {
  'colormap_jet': 'colormapJet',
  'colormap_spectral': 'colormapSpectral',
  'colormap_lava_waves': 'colormapLavaWaves',
  'colormap_space': 'colormapSpace',
};
for(const [name, fn] of Object.entries(expected)) {
  const graph = new ShaderGraph();
  const scalar = graph.addNode('input_float', {x:0,y:0}, catalog);
  const mapping = graph.addNode(name, {x:120,y:0}, catalog);
  const output = graph.addNode('output_color', {x:240,y:0}, catalog);
  assert.ok(graph.connect(scalar.id, 'output', mapping.id, 't'), name+' scalar connection');
  assert.ok(graph.connect(mapping.id, 'result', output.id, 'color'), name+' color connection');
  graph.setNodeValue(scalar.id, 'value', 0.5);
  const generator = new ShaderCodeGenerator();
  const native = generator.generate(graph);
  assert.match(native, /#include "..\/stdlib\/colormaps.glsl"/);
  assert.ok(native.includes(fn+'('), name+' native symbol');
  const preview = generator.buildPreviewShader(graph);
  assert.match(preview, /#version 300 es/);
  assert.ok(preview.includes('vec4 '+fn+'(float x)'), name+' browser definition');
  assert.ok(preview.includes('vec4 v1 = '+fn+'('), name+' browser call');
  assert.ok(!preview.includes('float4'), name+' browser must not contain HLSL');
  const out = process.env.HYPER_BUILDER_EFFECT_PATH;
  if(out) fs.writeFileSync(path.join(path.dirname(out), 'generated-'+name+'.effect'),native);
}
const kernels = ctx.window.COLORMAP_KERNELS;
assert.deepEqual(Object.keys(kernels).sort(), Object.values(expected).sort());
const glsl = fs.readFileSync(path.join(root,'data/stdlib/colormaps.glsl'),'utf8');
for(const fn of Object.values(expected)) assert.ok(glsl.includes('vec4 '+fn+'(float x)'));
assert.match(glsl, /Copyright \(c\) 2015 kbinani/);
execFileSync(process.execPath, [path.join(root, 'scripts/generate-colormaps.js'), '--check']);
console.log('PASS: 4 named-colormap nodes with native includes and WebGL previews');
