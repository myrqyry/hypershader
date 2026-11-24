#!/usr/bin/env node
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const base = path.join(__dirname, '..', 'tools', 'shader-builder');
const ctx = vm.createContext({ console, Map, Set, Number, window: {} });
for (const file of ['engine/graph.js', 'nodes/catalog.js', 'engine/codegen.js']) {
  vm.runInContext(fs.readFileSync(path.join(base, file), 'utf8'), ctx, { filename: file });
}
const { ShaderGraph, NodeCatalog, ShaderCodeGenerator } = vm.runInContext(
  '({ ShaderGraph, NodeCatalog, ShaderCodeGenerator })', ctx);
const catalog = new NodeCatalog(vm.runInContext('NODE_CATALOG', ctx));
const build = (type='input_color') => {
  const graph = new ShaderGraph();
  const input = graph.addNode(type, {x:0,y:0}, catalog);
  const output = graph.addNode('output_color',{x:200,y:0}, catalog);
  assert.ok(graph.connect(input.id, 'output', output.id, 'color'));
  return {graph, input, output};
};
const codegen = new ShaderCodeGenerator();
vm.runInContext(fs.readFileSync(path.join(base, 'preview.js'), 'utf8'), ctx, { filename: 'preview.js' });
const { ShaderPreviewRenderer } = vm.runInContext('({ ShaderPreviewRenderer })', ctx);
{
  const {graph} = build();
  const preview = codegen.buildPreviewShader(graph);
  const faultyLine = preview.split('\n').findIndex(line => line.includes('vec4 v0 =')) + 1;
  assert.ok(faultyLine > 0);
  const error = ShaderPreviewRenderer.describeCompileError(`ERROR: 0:${faultyLine}: undeclared identifier`, preview);
  assert.match(error, /node_1.*input_color/);
  assert.match(error, /undeclared identifier/);
  assert.equal(ShaderPreviewRenderer.describeCompileError('ERROR: 0:2: invalid version', preview).includes('node_1'), false,
    'Errors before generated node blocks must not implicate a node');
}
{
  const empty = codegen.generate(new ShaderGraph());
  assert.match(empty, /vertex_shader = VSDefault\(v_in\)/);
  assert.match(empty, /#include "\.\.\/stdlib\/hypershader_standard\.glsl"/);
  assert.doesNotMatch(empty, /sampler_state textureSampler/);
}
{
  const {graph} = build();
  const effect = codegen.generate(graph);
  assert.match(effect, /#include "\.\.\/stdlib\/hypershader_standard\.glsl"/);
  assert.match(effect, /vertex_shader = VSDefault\(v_in\)/);
  assert.doesNotMatch(effect, /mainTransform|sampler_state textureSampler/);
  assert.match(effect, /float4 hs_shade_pixel\(VertData v_in\) : TARGET/);
  assert.match(effect, /\/\/ @node node_1 input_color\n\s+float4 v0/,
    'Native shader sections must identify their originating node');
  assert.match(effect, /\/\/ @node node_2 output_color\n\s+finalColor/,
    'Output assignment must also identify its originating node');
}
{
  const {graph,input} = build();
  graph.setNodeValue(input.id,'r',0.8);
  const source = codegen.buildPreviewShader(graph);
  assert.match(source, /#version 300 es/);
  assert.match(source, /vec4 v0 = vec4\(0\.8,/);
  assert.match(source, /out vec4/);
  assert.doesNotMatch(source, /\bfloat4\b|\bVertData\b/);
  assert.match(source, /\/\/ @node node_1 input_color\n\s+vec4 v0/,
    'WebGL preview code must retain source-node attribution');
}
{
  const {graph} = build('input_texture');
  const source = codegen.buildPreviewShader(graph);
  assert.match(source, /texture\(uSource,/);
}
{
  const {graph,output} = build('input_color');
  const node = graph.addNode('noise_value', {x:100,y:0}, catalog);
  assert.match(codegen.buildPreviewShader(graph), /#version 300 es/,
    'Unconnected native-only node must not disable the output preview');
  assert.ok(graph.connect(node.id,'result',output.id,'color'));
  assert.throws(() => codegen.buildPreviewShader(graph), /native-only|unsupported/i);
}
if (process.env.HYPER_BUILDER_EFFECT_PATH) {
  const dir = path.dirname(process.env.HYPER_BUILDER_EFFECT_PATH);
  fs.writeFileSync(path.join(dir, 'generated-empty.effect'), codegen.generate(new ShaderGraph()));
  fs.writeFileSync(process.env.HYPER_BUILDER_EFFECT_PATH, codegen.generate(build().graph));
  const graph = new ShaderGraph();
  const uv = graph.addNode('input_uv', {x:0,y:0}, catalog);
  const swirl = graph.addNode('distort_swirl', {x:100,y:0}, catalog);
  const sample = graph.addNode('tex_sample', {x:200,y:0}, catalog);
  const output = graph.addNode('output_color', {x:300,y:0}, catalog);
  assert.ok(graph.connect(uv.id, 'output', swirl.id, 'uv'));
  assert.ok(graph.connect(swirl.id, 'result', sample.id, 'uv'));
  assert.ok(graph.connect(sample.id, 'result', output.id, 'color'));
  const effect = codegen.generate(graph);
  assert.ok(effect.includes('#include "../stdlib/distortion.glsl"'));
  fs.writeFileSync(path.join(dir, 'generated-swirl.effect'), effect);
}
console.log('PASS: native effect codegen + browser preview GLSL contract');
