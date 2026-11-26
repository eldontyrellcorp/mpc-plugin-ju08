#!/bin/sh
# Tests of the native build (run the SDK's tools/build.py first; SDK is the MPC Plugin SDK checkout).
#   compare_engine: every program through the plugin against the engine driven directly
#   bridge_test:    programs, performances, lower preset, key modes, MFX types, state, MIDI, odd block sizes
#   perf_levels:    the 100 performances: each written once, both parts sound, loudness like the patches'
#                   (perf_levels --table > src/performance_levels.cpp after changing a performance)
set -e
cd "$(dirname "$0")/.."
SDK=${SDK:-../mpc-plugin-builder-sdk}
mkdir -p build/tests
D="src/engine.cpp src/fx.cpp src/params.cpp src/presets.cpp src/fx_params.cpp src/performances.cpp src/performance_levels.cpp"
F="-std=c++17 -O2 -w -I$SDK/framework -Isrc"
g++ $F tests/compare_engine.cpp $D -ldl -o build/tests/compare_engine
g++ $F tests/bridge_test.cpp $D -ldl -o build/tests/bridge_test
g++ $F tests/perf_levels.cpp $D -o build/tests/perf_levels
./build/tests/compare_engine build/native/ju-08.so
./build/tests/bridge_test build/native/ju-08.so
./build/tests/perf_levels
"$SDK/.sdk-tools/host_sim" build/native/ju-08.so test --seconds 3 --notes 48,55,60,64 --wav build/test.wav | grep -v "empty name" | tail -4
