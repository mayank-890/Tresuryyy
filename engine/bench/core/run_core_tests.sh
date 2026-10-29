#!/usr/bin/env bash
# T-0005: compile and run core unit tests (CTest wiring blocked — see PROGRESS Q-10).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PRESET="${1:-debug-asan}"
BUILD="${ROOT}/build/${PRESET}"
TRIPLET="arm64-osx"
VCPKG_INST="${BUILD}/vcpkg_installed/${TRIPLET}"
VCPKG_GTEST="${VCPKG_INST}/debug/lib/libgtest.a"
VCPKG_GTEST_MAIN="${VCPKG_INST}/debug/lib/manual-link/libgtest_main.a"
GTEST_INCLUDE="${VCPKG_INST}/include"

just build "${PRESET}" >/dev/null

c++ -std=c++20 -I "${ROOT}/engine/include" -isystem "${GTEST_INCLUDE}" -fsanitize=address,undefined -g \
  "${ROOT}/engine/tests/unit/core/core_tests.cpp" \
  "${BUILD}/engine/libtrading_core.a" \
  "${VCPKG_GTEST_MAIN}" "${VCPKG_GTEST}" \
  -pthread \
  -o "${BUILD}/engine/core_tests_t0005"

"${BUILD}/engine/core_tests_t0005"
