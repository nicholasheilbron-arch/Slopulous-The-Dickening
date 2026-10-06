#!/bin/sh
# Builds the engine-independent world generator against a tiny CoreMinimal shim and runs it.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../Source/Shaman"
g++ -std=c++17 -O2 -Wall -Wextra -I"$HERE/shim" -I"$SRC/Public" "$SRC/Private/World/WorldGenerator.cpp" "$HERE/main.cpp" -o "$HERE/worldgen_harness"
"$HERE/worldgen_harness" "${1:-200}"
