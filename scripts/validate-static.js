#!/usr/bin/env node
/*
 * Static validation script for hyperShaderFX.
 * Run: node scripts/validate-static.js
 *
 * Checks:
 * 1. CMake source files exist
 * 2. No DEBUG_ACTIVE markers in release paths
 * 3. obs_source_info IDs are unique
 * 4. No direct obs_source_update_properties outside approved helper
 */

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
let failures = 0;

function fail(msg) {
  console.error(`FAIL: ${msg}`);
  failures++;
}

function pass(msg) {
  console.log(`PASS: ${msg}`);
}

/* 1. CMake source files exist */
function checkCmakeSources() {
  const cmake = fs.readFileSync(path.join(ROOT, 'CMakeLists.txt'), 'utf8');
  const srcDir = path.join(ROOT, 'src');

  // Extract source file paths from CMakeLists.txt
  const srcPattern = /src\/[\w./-]+\.(?:cpp|hpp|c|h|in)/g;
  const matches = cmake.match(srcPattern) || [];

  let allExist = true;
  for (const src of matches) {
    const fullPath = path.join(ROOT, src);
    // Skip .in template files
    if (src.endsWith('.in')) continue;
    if (!fs.existsSync(fullPath)) {
      fail(`CMake source file does not exist: ${src}`);
      allExist = false;
    }
  }
  if (allExist) {
    pass(`All ${matches.length} CMake source files exist`);
  }
}

/* 2. No DEBUG_ACTIVE markers outside gated blocks */
function checkDebugMarkers() {
  const files = [
    'src/plugin-main.c',
    'src/filter-hypershader.c',
    'src/hyper_ui.c',
  ];

  let clean = true;
  for (const file of files) {
    const fullPath = path.join(ROOT, file);
    if (!fs.existsSync(fullPath)) continue;
    const content = fs.readFileSync(fullPath, 'utf8');
    // Check for DEBUG_ACTIVE markers that are NOT inside #ifdef guards
    const lines = content.split('\n');
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      if (line.includes('DEBUG_ACTIVE_') && !line.trim().startsWith('#')) {
        // Check if there's an #ifdef guard within 5 lines back (skip comment lines)
        let hasGuard = false;
        for (let j = i - 1; j >= Math.max(0, i - 5); j--) {
          const t = lines[j].trim();
          if (t.startsWith('#ifdef') || t.startsWith('#if ')) {
            hasGuard = true;
            break;
          }
          // Skip blank lines and comments
          if (t === '' || t.startsWith('/*') || t.startsWith('*') || t.startsWith('//')) continue;
          break;
        }
        if (!hasGuard) {
          fail(`Ungated DEBUG_ACTIVE marker in ${file}:${i + 1}`);
          clean = false;
        }
      }
    }
  }
  if (clean) {
    pass('No ungated DEBUG_ACTIVE markers found');
  }
}

/* 3. obs_source_info IDs are unique */
function checkUniqueSourceIds() {
  const files = [
    'src/filter-hypershader.c',
  ];

  const ids = [];
  const idPattern = /\.id\s*=\s*"([^"]+)"/g;

  for (const file of files) {
    const fullPath = path.join(ROOT, file);
    if (!fs.existsSync(fullPath)) continue;
    const content = fs.readFileSync(fullPath, 'utf8');
    let match;
    while ((match = idPattern.exec(content)) !== null) {
      ids.push({ id: match[1], file });
    }
  }

  const seen = new Map();
  let unique = true;
  for (const { id, file } of ids) {
    if (seen.has(id)) {
      fail(`Duplicate obs_source_info ID: "${id}" in ${file} (also in ${seen.get(id)})`);
      unique = false;
    }
    seen.set(id, file);
  }
  if (unique) {
    pass(`All ${ids.length} obs_source_info IDs are unique: ${ids.map(x => x.id).join(', ')}`);
  }
}

/* 4. No direct obs_source_update_properties outside approved helper */
function checkUpdateProperties() {
  const files = [
    'src/filter-hypershader.c',
    'src/hyper_ui.c',
  ];

  let clean = true;
  for (const file of files) {
    const fullPath = path.join(ROOT, file);
    if (!fs.existsSync(fullPath)) continue;
    const content = fs.readFileSync(fullPath, 'utf8');
    const lines = content.split('\n');
    let inBlockComment = false;
    for (let i = 0; i < lines.length; i++) {
      const trimmed = lines[i].trim();
      // Track block comments
      if (inBlockComment) {
        if (trimmed.includes('*/')) {
          inBlockComment = false;
        }
        continue;
      }
      if (trimmed.startsWith('/*')) {
        if (!trimmed.includes('*/')) {
          inBlockComment = true;
        }
        continue;
      }
      if (trimmed.startsWith('//')) continue;
      if (trimmed.includes('obs_source_update_properties(')) {
        // Allowed in deferred_properties_refresh function
        const context = lines.slice(Math.max(0, i - 10), i + 1).join('\n');
        if (!context.includes('deferred_properties_refresh') &&
            !context.includes('hyper_ui_get_properties')) {
          fail(`Direct obs_source_update_properties in ${file}:${i + 1} (should use approved helper)`);
          clean = false;
        }
      }
    }
  }
  if (clean) {
    pass('No unauthorized obs_source_update_properties calls');
  }
}

/* Run all checks */
console.log('=== hyperShaderFX Static Validation ===\n');
checkCmakeSources();
checkDebugMarkers();
checkUniqueSourceIds();
checkUpdateProperties();

console.log(`\n=== Results: ${failures} failure(s) ===`);
process.exit(failures > 0 ? 1 : 0);
