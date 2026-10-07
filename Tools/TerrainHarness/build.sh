#!/bin/sh
# Off-engine verification build of the SHAMAN terrain core. Requires g++ (C++17).
set -e
cd "$(dirname "$0")"
SRC=../../Source/Shaman
g++ -std=c++17 -O2 -Wall -Wno-unused-function -pthread -include atomic -Ishim -I$SRC/Public \
  TerrainHarness.cpp $SRC/Private/Terrain/PlanetHeightField.cpp $SRC/Private/Terrain/PlanetTerrainQueries.cpp -o terrain_harness
./terrain_harness
