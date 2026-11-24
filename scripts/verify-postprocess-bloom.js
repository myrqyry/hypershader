const fs = require("fs");
const path = require("path");

const file = path.join(__dirname, "..", "data", "stdlib", "postprocess_3d.glsl");
const src = fs.readFileSync(file, "utf8");

const required = [
  "float bloomKarisWeight",
  "float4 bloomWeightedBrightTap",
  "float3 bloomGather13",
  "float3 applyBloom",
  "Single-pass multi-scale bloom approximation",
];

for (const token of required) {
  if (!src.includes(token)) {
    console.error(`FAIL: missing bloom contract token: ${token}`);
    process.exit(1);
  }
}

if (src.includes("// Multi-pass bloom")) {
  console.error("FAIL: stdlib still overclaims single-shader bloom as multi-pass");
  process.exit(1);
}

const gatherStart = src.indexOf("float3 bloomGather13");
const applyStart = src.indexOf("float3 applyBloom", gatherStart);
const gather = src.slice(gatherStart, applyStart);
const tapCalls = (gather.match(/bloomWeightedBrightTap\(/g) || []).length;

if (tapCalls !== 13) {
  console.error(`FAIL: bloomGather13 uses ${tapCalls} weighted taps; expected 13`);
  process.exit(1);
}

for (const radius of ["1.0", "2.0", "4.0"]) {
  if (!src.includes(`bloomGather13(uv, resolution, ${radius}`)) {
    console.error(`FAIL: applyBloom missing ${radius}x scale`);
    process.exit(1);
  }
}

console.log("PASS: Karis-weighted multi-scale bloom stdlib contract is intact");
