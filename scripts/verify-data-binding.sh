#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
module="$(realpath "$1")"
data="$(realpath "$2")"
provider="$(realpath "$3")"
providerData="$(realpath "$4")"
work="$(mktemp -d /tmp/hs-data-binding.XXXXXX)"
echo "Evidence: $work"
inc="$(pkg-config --variable=includedir libobs)/obs"
g++ -std=c++17 -pthread -DHS_DATA_DRIVER_TEST -I"$repo/src" -I"$inc" "$repo/scripts/verify-data-driver.cpp" "$repo/src/interop/data_driver.cpp" -o "$work/driver" -lobs
"$work/driver"
g++ -std=c++17 -fPIC -O2 -I"$repo/src" -I"$inc" $(pkg-config --cflags Qt6Widgets) "$repo/scripts/verify-data-binding-obs.cpp" -o "$work/probe" $(pkg-config --libs Qt6Widgets libobs)
mkdir "$work/home"
cp -a "$data" "$work/data"
mkdir -p "$work/data/shaders/tests"
cat > "$work/data/shaders/tests/data_binding_probe.effect" <<'SHADER'
#include "../../stdlib/hypershader_standard.glsl"
uniform float amount; // @min 0 @max 1 @step 0.01 @default 0.25
void hs_shade_pixel(out vec4 fragColor,in vec2 fragCoord){fragColor=float4(amount,0,0,1);}
HYPERSHADER_PIXEL_WRAPPER
SHADER
python3 - "$work" <<'PY' &
import http.server,sys,pathlib,json
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  values={"/high":.75,"/over":2,"/string":"0.75","/bool":True}
  body=json.dumps({"value":values.get(self.path)}).encode()
  self.send_response(200);self.send_header("Content-Type","application/json");self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
pathlib.Path(sys.argv[1],"port").write_text(str(server.server_port))
server.serve_forever()
PY
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
for i in $(seq 1 30); do [[ -s "$work/port" ]] && break; sleep .1; done
if ! (cd "$work/home" && HOME="$PWD" XDG_CONFIG_HOME="$PWD" LIBGL_ALWAYS_SOFTWARE=1 NO_PROXY=127.0.0.1 xvfb-run -a timeout 35 "$work/probe" "$module" "$work/data" "$provider" "$providerData" "http://127.0.0.1:$(cat "$work/port")") >"$work/result.log" 2>&1; then
 tail -65 "$work/result.log";exit 1
fi
grep '^DATA_BINDING ' "$work/result.log"
