#!/usr/bin/env bash
# Convenience build script for CS 3.
# Uses g++ explicitly: on some distros the default clang toolchain cannot find
# libstdc++ (error: "cannot find -lstdc++"), while g++ ships it.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build
CXX_COMPILER=${CXX:-g++}

echo ">> Configuring (CXX=${CXX_COMPILER})..."
CC=${CC:-gcc} CXX="${CXX_COMPILER}" cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release

echo ">> Building..."
cmake --build "${BUILD_DIR}" -j

echo ">> Done. Run the game with: ./${BUILD_DIR}/cs3"
