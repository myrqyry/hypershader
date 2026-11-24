#!/usr/bin/env bash
# Compatibility alias for the stronger runtime ABI verifier.
#
# The runtime verifier builds one binary against OBS <=32, then executes that
# same binary under both the host OBS runtime and the installed OBS 33 Flatpak.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$repo/scripts/verify-image-channel-runtime.sh"
