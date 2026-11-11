#!/usr/bin/env bash
set -euo pipefail
rbush_repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rbush_build_dir="${RBUSH_BUILD_DIR:-$rbush_repo_root/build-native}"
cmake -S "$rbush_repo_root" -B "$rbush_build_dir" -DCMAKE_BUILD_TYPE="${RBUSH_BUILD_TYPE:-Release}" -DBUILD_TESTING=ON
cmake --build "$rbush_build_dir" --parallel "${RBUSH_BUILD_JOBS:-3}"
