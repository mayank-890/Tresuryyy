# Task runner (TECHNICAL_IMPLEMENTATION.md §10.1). T-0003: build and test presets only.

set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default_preset := "debug-asan"
vcpkg_root := env_var_or_default("VCPKG_ROOT", justfile_directory() / ".vcpkg")

export VCPKG_ROOT := vcpkg_root
export PATH := "/opt/homebrew/bin:" + env_var_or_default("PATH", "")

setup-vcpkg:
    if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"; fi
    if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics; fi

build preset=default_preset: setup-vcpkg
    cmake --preset "{{preset}}"
    cmake --build --preset "{{preset}}"

test preset=default_preset:
    just build "{{preset}}"
    ctest --preset "{{preset}}"

check-watchdog preset=default_preset:
    tools/check_watchdog_links.sh "build/{{preset}}"

codegen:
    python3 schemas/codegen/generate.py --write

codegen-check:
    python3 schemas/codegen/generate.py --check

test-journal-schema:
    mkdir -p build
    g++ -std=c++20 -I engine/include -fsanitize=address,undefined schemas/tests/journal_round_trip_test.cpp -o build/journal_round_trip_test
    ./build/journal_round_trip_test
    python3 schemas/tests/test_journal_round_trip.py

check preset=default_preset:
    just codegen-check
    just test-journal-schema
    just test "{{preset}}"
    just check-watchdog "{{preset}}"

lint:
    @echo "TODO(T-0002+): wire clang-format, clang-tidy, ruff (not required for T-0003)"

check-all-presets:
    just check debug-asan
    just check debug-tsan
    just check debug-ubsan
    just check release
    just check bench
    just check coverage
