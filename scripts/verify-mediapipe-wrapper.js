#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

const wrapperPath = path.join(__dirname, '..', 'src', 'mediapipe', 'mp_wrapper.cpp');
const readmePath = path.join(__dirname, '..', 'data', 'models', 'mediapipe', 'README.md');
const text = fs.readFileSync(wrapperPath, 'utf8');
const readme = fs.readFileSync(readmePath, 'utf8');

const errors = [];

const decodedFaces = [...text.matchAll(/std::vector<FaceBox> faces = decode_face_detections/g)].length;
const storedFaces = [...text.matchAll(/runner->all_faces\s*=\s*faces\s*;/g)].length;
if (decodedFaces !== storedFaces) {
  errors.push(`Decoded face batches are not all stored for privacy mode: decoded=${decodedFaces}, stored=${storedFaces}`);
}

if (/Fall through to simulator/.test(text)) {
  errors.push('ONNX runtime errors still fall through to simulator output');
}

if (/Return neutral face landmarks for reference pose \(simulator fallback\)/.test(text)) {
  errors.push('Static image analysis still reports simulator reference landmarks as success');
}

if (/Simulated response for successful inference/.test(text)) {
  errors.push('LiteRT hand tracking still reports simulated hand results as successful inference');
}

if (/SIMULATOR FALLBACK|Simulate Face Mesh Landmarks|Simulate Hand Tracking|Simulate Depth Estimation/.test(text)) {
  errors.push('Synthetic MediaPipe simulator fallback is still compiled into the runtime');
}

const header = fs.readFileSync(path.join(__dirname, '..', 'src', 'mediapipe', 'mp_wrapper.h'), 'utf8');
if (/Simulator/.test(header)) {
  errors.push('Public MediaPipe backend contract still advertises a simulator backend');
}

for (const staleName of ['face_detection_short_range.onnx', 'face_landmark.onnx']) {
  if (text.includes(staleName) || readme.includes(staleName)) {
    errors.push(`Stale MediaPipe model name remains documented: ${staleName}`);
  }
}

if (/USE_ONNX_MEDIAPIPE=ON/.test(text) || /USE_ONNX_MEDIAPIPE=ON/.test(readme)) {
  errors.push('Stale CMake flag USE_ONNX_MEDIAPIPE remains documented; use USE_ONNX_RUNTIME');
}

if (/falling back to simulator/i.test(readme)) {
  errors.push('README still documents simulator fallback even though real backend is required');
}

if (errors.length) {
  console.error('MediaPipe wrapper verification failed:\n');
  errors.forEach((error) => console.error(`- ${error}`));
  process.exit(1);
}

console.log('MediaPipe wrapper verification passed.');
