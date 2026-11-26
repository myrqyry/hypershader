# HyperShader development

Native OBS shader framework, not a Shadertoy host. Use the canonical hypershader module name and the existing native source IDs; no old-brand aliases. Explicit CMake source lists; C17/C++17. Never block the render thread or synchronously refresh properties inside a property callback. Test in isolated libobs/OpenGL sessions; close OBS before any user installation. Never install into system plugin directories. Keep imported code, models, and reference assets out until provenance and distribution rights are established. Preserve upstream notices and pin changes to runtime data in scripts/release-assets.txt.

See README.md and DEVELOPMENT.md for build instructions.
