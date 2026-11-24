# Third-party notices

This directory accompanies the runtime data so notices survive binary packaging.
The source distribution also retains original source headers and the root LICENSE.
This is a partial notice inventory, not a blanket licensing clearance.

| Component | Source and evidence | Notice |
| --- | --- | --- |
| HyperShaderFX / OBS plugin template | [OBS plugin template](https://github.com/obsproject/obs-plugintemplate); GPL notice in template source headers | [GPL version 2 text](GPL-2.0.txt) |
| EnTT | [v3.16.0](https://github.com/skypjack/entt/tree/v3.16.0); vendored header SHA-256 exactly matches upstream (3272191cf3d46cec92368f918695502dc8d174c92e0b8b4a5d53fed28e240c9f) | [Upstream MIT notice](EnTT.txt) |
| JSMN | Local header credits Serge A. Zaitsev, v1.0.0; local parser is modified; [upstream v1.0.0](https://github.com/zserge/jsmn/tree/v1.0.0) | [Upstream MIT notice](jsmn.txt) |
| miniaudio | [miniaudio](https://github.com/mackron/miniaudio), vendored header in src/audio; the header contains alternative Unlicense / MIT No Attribution terms | [Verbatim header notice](miniaudio.txt) |
| Colormap tables | kbinani; existing MIT notice in the standard library | [Colormaps](../stdlib/COLORMAPS-LICENSE.txt) |
| Face triangulation | [Shadertastic](https://github.com/xurei/shadertastic); local src/mediapipe/face_surface_triangles.h credits xurei and GPL-2.0-or-later | [GPL text](GPL-2.0.txt); retain source credit; all 898 index triples match upstream commit 10edbee3ec9d2b1812307b3b02e11f366c30d483; [upstream LICENSE](Shadertastic.txt) |

The optional MediaPipe ONNX/TFLite binaries are not approved by this inventory.
The candidate source export excludes them. Qualcomm's repository-level license
metadata is not sufficient: its LICENSE explicitly refers deployable model
files to separate deployment terms. The exact local binaries and applicable
terms still need review before redistribution.

The shader library's retained source comments and attribution review remain
separate from these runtime notices. Do not restore quarantined effects or
distribute the private repository history solely on the basis of this list.
