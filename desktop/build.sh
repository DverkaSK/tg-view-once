#!/usr/bin/env bash
# Builds and runs the core tests, the CLI tool, and compiles the DLL scaffold.
# Requires a C++17 compiler (g++/MinGW-w64 tested). Run from desktop/.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-g++}
FLAGS="-std=c++17 -Wall -Wextra -O2"

echo "== core tests"
$CXX $FLAGS core/round_rewrite.cpp core/tests.cpp -o core/tests.exe
./core/tests.exe

echo "== cli"
$CXX $FLAGS cli/roundtool.cpp core/round_rewrite.cpp -o cli/roundtool.exe
echo "built cli/roundtool.exe"

echo "== dll scaffold"
$CXX $FLAGS -shared dll/round_dll.cpp core/round_rewrite.cpp \
	-o dll/round.dll -static -lpsapi -lkernel32 -luser32
echo "built dll/round.dll"
