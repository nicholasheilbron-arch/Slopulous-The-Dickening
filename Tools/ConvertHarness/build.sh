#!/bin/sh
# Compiles the real Convert/tribe .cpp files against shim/UEShim.h and runs the scenarios. Logic check only.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC="$HERE/../../Source/Shaman"
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -I"$HERE/shim" -I"$SRC/Public" \
  "$SRC/Private/TribeComponent.cpp" "$SRC/Private/TribeMemberComponent.cpp" "$SRC/Private/TribeRegistrySubsystem.cpp" \
  "$SRC/Private/SpellEffect_ConvertUnits.cpp" "$HERE/main.cpp" -o "$HERE/convert_harness"
"$HERE/convert_harness"
