#!/usr/bin/env node
// Exercise the actual settings checker on isolated source/shader fixtures.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { test } = require('node:test');

const repo = path.resolve(__dirname, '..');
const inputs = [
  'scripts/verify-settings.js',
  'src/hyper_ui.c',
  'src/filter-hypershader.c',
  'data/shaders/root/source.reactive_sprite.effect',
  'data/shaders/root/filter.reactive_sprite.effect',
];
function check(t, mutate) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hs-settings-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  for (const input of inputs) {
    const dest = path.join(root, input);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.copyFileSync(path.join(repo, input), dest);
  }
  if (mutate) mutate(root);
  return spawnSync(process.execPath, [path.join(root, inputs[0])], { encoding: 'utf8' });
}
function change(root, relative, from, to) {
  const dest = path.join(root, relative);
  const source = fs.readFileSync(dest, 'utf8');
  assert.ok(source.includes(from));
  fs.writeFileSync(dest, source.replace(from, to));
}
test('real sprite metadata supplies dynamic settings in both effects', (t) => {
  const result = check(t);
  assert.equal(result.status, 0, result.stderr);
});
test('missing dynamic setting in one effect fails with its filename', (t) => {
  const result = check(t, (root) => change(root, inputs[4],
    'uniform int response_driver;', 'uniform int unrelated_driver;'));
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /response_driver.*filter\.reactive_sprite\.effect/);
});
test('wrong dynamic type fails even for two numeric shader types', (t) => {
  const result = check(t, (root) => change(root, inputs[3],
    'uniform int animation_playback;', 'uniform float animation_playback;'));
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /animation_playback.*source\.reactive_sprite\.effect/);
});
test('unknown param key is not silently exempted', (t) => {
  const result = check(t, (root) => change(root, inputs[2],
    '"param_response_driver"', '"param_unknown_control"'));
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /Missing UI binding for key 'param_unknown_control'/);
});
