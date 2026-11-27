#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
python "$root/scripts/test-shared-ci.py"
exec python "$root/.github/scripts/ci-driver.py"
