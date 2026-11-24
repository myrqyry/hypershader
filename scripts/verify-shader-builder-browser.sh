#!/usr/bin/env bash
# Actual browser rendering + graph integration tests; isolated localhost server.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
port="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
log="$(mktemp /tmp/hyper-browser-smoke-XXXXXX)"
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$root/tools/shader-builder" >"$log" 2>&1 &
server=$!
trap 'kill "$server" 2>/dev/null || true; rm -f "$log"' EXIT
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if curl -sf "http://127.0.0.1:$port/index.html" >/dev/null; then break; fi
    sleep 0.15
done
for route in preview-smoke.html editor-smoke.html curated-smoke.html; do
    echo "BROWSER TEST: $route"
    html="$(timeout 20s google-chrome --headless=new --no-sandbox --disable-dev-shm-usage         --disable-background-networking --enable-unsafe-swiftshader         --use-gl=angle --use-angle=swiftshader --virtual-time-budget=4000         --dump-dom "http://127.0.0.1:$port/tests/$route" 2>/dev/null)"
    result="$(printf '%s\n' "$html" | grep -o '<output id="result">[^<]*</output>' | head -1)"
    echo "$result"
    if [[ "$result" != *'>PASS:'* ]]; then
        echo "FAIL: $route did not produce confirmed WebGL2 output" >&2
        exit 1
    fi
done
