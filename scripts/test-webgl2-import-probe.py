#!/usr/bin/env python3
"""Pure conversion-contract tests; no GPU involved."""
import importlib.util
import sys
sys.dont_write_bytecode = True
import unittest
from pathlib import Path

module_path = Path(__file__).with_name('probe-webgl2-import.py')
spec = importlib.util.spec_from_file_location('webgl_probe', module_path)
assert spec and spec.loader

class ConversionTests(unittest.TestCase):
    def get_module(self):
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_shader_stage_removes_browser_entry_and_binds_native_contract(self):
        source = '''#version 300 es
precision highp float;
uniform vec2 u_resolution;
uniform float u_time;
out vec4 out_color;
void main() {
 vec2 uv = gl_FragCoord.xy / u_resolution;
 out_color = vec4(uv, sin(u_time), 1.0);
}
'''
        output = self.get_module().wrap_shader(source)
        self.assertNotIn('#version 300 es', output)
        self.assertNotIn('precision highp', output)
        self.assertNotIn('uniform vec2 u_resolution', output)
        self.assertIn('void hs_shade_pixel(out vec4 fragColor, in vec2 fragCoord)', output)
        self.assertIn('fragCoord / hs_resolution.xy', output)
        self.assertIn('sin(hs_time)', output)
        self.assertIn('HYPERSHADER_PIXEL_WRAPPER', output)

    def test_glsl_two_argument_atan_not_rewritten_as_one_argument(self):
        source = '''void main() { float angle = atan(uv.y, uv.x); float other = atan(uv.x); out_color = vec4(angle + other); }'''
        output = self.get_module().wrap_shader(source)
        self.assertIn('atan2(uv.y, uv.x)', output)
        self.assertIn('atan(uv.x)', output)

    def test_rejects_shader_without_entrypoint(self):
        with self.assertRaises(ValueError):
            self.get_module().wrap_shader('float mystery() { return 1.0; }')

if __name__ == '__main__': unittest.main()
