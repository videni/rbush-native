#!/usr/bin/env bash
set -euo pipefail
rbush_repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rbush_build_dir="${RBUSH_BUILD_DIR:-$rbush_repo_root/build-native}"
"$rbush_repo_root/scripts/build-native.sh"
ctest --test-dir "$rbush_build_dir" --output-on-failure
