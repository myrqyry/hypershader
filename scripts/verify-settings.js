#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

const sourceFiles = [
  path.join(__dirname, '..', 'src', 'hyper_ui.c'),
  path.join(__dirname, '..', 'src', 'filter-hypershader.c'),
];
const uiSourceFile = sourceFiles[0];
const sourceByFile = new Map(sourceFiles.map((f) => [f, fs.readFileSync(f, 'utf8')]));

const INTERNAL_KEYS_WITHOUT_UI = new Set([
  // Saved-scene migration marker; deliberately no OBS control.
  'preset_schema',
  'input_mode',
  'audio_mix',
  'sound_mix',
  'audio_smoothing',
  'example_list',
]);

const DOCK_ONLY_KEYS = new Set([
  'ui_page',
  'import_mode',
  'shadertoy_import_url',
  'add_shader_file',
  'shadertoy_import_file',
  'shadertoy_import_json',
  'save_preset_name',
  'export_format',
  'raw_shader_code',
  'raw_edit_mode',
  'pg_insert_func',
  'audio_update_rate',
  'audio_fft_size',
  'audio_window_type',
  'audio_test_tone',
  'audio_debug',
]);

const REQUIRED_FILTER_UI_KEYS = new Set([
  'preset',
  'shader_file',
  'speed',
  'mp_controlnet_model',
  'enable_sound',
]);

const typeAliases = {
  int: 'number',
  double: 'number',
  bool: 'boolean',
  string: 'string',
  color: 'number',
  list: 'unknown',
  path: 'string',
  text: 'string',
  float: 'number',
};

function collect(text, regex, project) {
  const out = [];
  let match;
  while ((match = regex.exec(text)) !== null) out.push(project(match));
  return out;
}

const defaults = collect(
  sourceByFile.get(uiSourceFile),
  /obs_data_set_default_(string|int|bool|double)\s*\(\s*settings\s*,\s*"([^"]+)"/g,
  (m) => ({ key: m[2], type: typeAliases[m[1]], origin: `default:${m[1]}` })
);

const runtimeAccesses = [];
for (const [file, text] of sourceByFile.entries()) {
  runtimeAccesses.push(
    ...collect(
      text,
      /obs_data_(get|set)_(string|int|bool|double)\s*\(\s*settings\s*,\s*"([^"]+)"/g,
      (m) => ({ key: m[3], type: typeAliases[m[2]], origin: `${path.basename(file)}:${m[1]}:${m[2]}` })
    )
  );
}

const uiBindings = collect(
  sourceByFile.get(uiSourceFile),
  /obs_properties_add_(list|path|text|float(?:_slider)?|int(?:_slider)?|bool|color)\s*\(([^;]*?)\);/gs,
  (m) => {
    const fn = m[1];
    const args = m[2];
    const keyMatch = args.match(/^[^,]+,\s*"([^"]+)"/s);
    if (!keyMatch) return null;

    if (fn === 'text' && /OBS_TEXT_INFO/.test(args)) return null;

    const key = keyMatch[1];
    if (key.startsWith('param_')) return null;

    const normalizedFn = fn.startsWith('float') ? 'float' : fn.startsWith('int') ? 'int' : fn;
    return { key, type: typeAliases[normalizedFn], origin: `ui:${fn}` };
  }
).filter(Boolean);

const sourceTruth = new Map();
for (const entry of [...defaults, ...runtimeAccesses]) {
  const arr = sourceTruth.get(entry.key) || [];
  arr.push(entry);
  sourceTruth.set(entry.key, arr);
}

const uiMap = new Map();
for (const entry of uiBindings) {
  const arr = uiMap.get(entry.key) || [];
  arr.push(entry);
  uiMap.set(entry.key, arr);
}

const errors = [];

// These host-read controls are created from effect uniforms at runtime,
// not literal obs_properties_add_* keys in hyper_ui.c. Verify both effects
// and their exact scalar types instead of exempting the param_ namespace.
const spriteEffects = ['source.reactive_sprite.effect', 'filter.reactive_sprite.effect'];
const spriteSettings = {
  param_response_driver: 'int',
  param_temporal_response: 'bool',
  param_animation_playback: 'int',
};
for (const [key, expected] of Object.entries(spriteSettings)) {
  const name = key.slice('param_'.length);
  const entries = [];
  for (const effect of spriteEffects) {
    const file = path.join(__dirname, '..', 'data', 'shaders', 'root', effect);
    const shader = fs.readFileSync(file, 'utf8');
    const declarations = collect(shader,
      /^\s*uniform\s+(\w+)\s+(\w+)\s*;[^\S\r\n]*(?:\/\/([^\r\n]*))?/gm,
      (m) => ({ type: m[1], name: m[2], metadata: m[3] || '' }))
      .filter((entry) => entry.name === name);
    if (declarations.length !== 1 || declarations[0].type !== expected ||
        !/@label\s+"/.test(declarations[0].metadata)) {
      errors.push(`Dynamic setting '${key}' must have one labeled ${expected} uniform in ${effect}`);
      continue;
    }
    entries.push({ key, type: typeAliases[expected], origin: `shader:${effect}` });
  }
  const defs = sourceTruth.get(key) || [];
  if (!defs.length || defs.some((entry) => !entry.origin.endsWith(':' + expected))) {
    errors.push(`Dynamic setting '${key}' must use the runtime ${expected} getter`);
  }
  if (entries.length === spriteEffects.length) uiMap.set(key, entries);
}
// The Performance dock writes these persistent settings; they must
// have exact typed native getter and Qt setter contracts.
const performance = fs.readFileSync(path.join(__dirname, '..', 'src', 'ui', 'shader_performance_panel.hpp'), 'utf8');
for (const [key, kind] of Object.entries({
  perf_mod_shader: 'string', perf_mod_param: 'string',
  perf_mod_source: 'int', perf_mod_depth: 'double', perf_mod_hz: 'double',
  perf_mod_data_uuid: 'string', perf_mod_data_age_ms: 'int',
})) {
  const defs = sourceTruth.get(key) || [];
  const setter = new RegExp('obs_data_set_' + kind + '\\s*\\(\\s*cfg\\s*,\\s*"' + key + '"');
  if (!defs.length || defs.some(d => !d.origin.endsWith(':' + kind)) || !setter.test(performance)) {
    errors.push('Performance setting missing typed native/dock contract: ' + key);
  }
  uiMap.set(key, [{key, type: typeAliases[kind], origin: 'dock:' + kind}]);
}
for (const [key, defs] of sourceTruth.entries()) {
  if (!uiMap.has(key) && !INTERNAL_KEYS_WITHOUT_UI.has(key) && !DOCK_ONLY_KEYS.has(key)) {
    errors.push(`Missing UI binding for key '${key}' (${defs.map((d) => d.origin).join(', ')})`);
  }
}

// Migration markers are not user settings; exposing them as a control would
// allow saved-scene compatibility state to be edited accidentally.
if (uiMap.has('preset_schema')) {
  errors.push("Migration marker 'preset_schema' must not be exposed as a UI control");
}

for (const key of DOCK_ONLY_KEYS) {
  if (uiMap.has(key)) {
    errors.push(`Dock-only key '${key}' is still exposed in filter properties (${uiMap.get(key).map((d) => d.origin).join(', ')})`);
  }
}

for (const key of REQUIRED_FILTER_UI_KEYS) {
  if (!uiMap.has(key)) {
    errors.push(`Required filter UI key '${key}' is missing`);
  }
}

for (const [key, uiDefs] of uiMap.entries()) {
  if (!sourceTruth.has(key)) {
    errors.push(`UI references unknown key '${key}' (${uiDefs.map((d) => d.origin).join(', ')})`);
  }
}

for (const [key, defs] of sourceTruth.entries()) {
  const declaredTypes = new Set(defs.map((d) => d.type));
  if (declaredTypes.size > 1) {
    errors.push(`Source-of-truth type mismatch for '${key}': ${[...declaredTypes].join(' vs ')}`);
    continue;
  }
  const expectedType = [...declaredTypes][0];
  for (const uiDef of uiMap.get(key) || []) {
    if (uiDef.type !== 'unknown' && uiDef.type !== expectedType) {
      errors.push(`Type mismatch for '${key}': source=${expectedType}, ui=${uiDef.type} (${uiDef.origin})`);
    }
  }
}

if (errors.length) {
  console.error('Settings verification failed:\n');
  errors.forEach((e) => console.error(`- ${e}`));
  process.exit(1);
}

console.log(`Settings verification passed. sourceKeys=${sourceTruth.size}, uiKeys=${uiMap.size}`);
