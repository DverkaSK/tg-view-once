#!/usr/bin/env bash
# Builds and runs everything that can be verified offline, then builds the
# Windows artifacts (DLL + injector). Requires a C++17 compiler and, for the
# hook engine, a C compiler; MinGW-w64 (g++/gcc) is tested. Run from desktop/.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-g++}
CC=${CC:-gcc}
CXXFLAGS="-std=c++17 -Wall -Wextra -O2"
MINHOOK=dll/vendor/minhook

echo "== core tests"
$CXX $CXXFLAGS core/round_rewrite.cpp core/tests.cpp -o core/tests.exe
./core/tests.exe

echo "== cli"
$CXX $CXXFLAGS cli/roundtool.cpp core/round_rewrite.cpp -o cli/roundtool.exe
echo "built cli/roundtool.exe"

echo "== minhook"
mkdir -p build
MH_OBJS=""
for c in $MINHOOK/src/buffer.c $MINHOOK/src/hook.c $MINHOOK/src/trampoline.c \
		$MINHOOK/src/hde/hde32.c $MINHOOK/src/hde/hde64.c; do
	o="build/$(basename "${c%.c}").o"
	$CC -O2 -w -I$MINHOOK/include -c "$c" -o "$o"
	MH_OBJS="$MH_OBJS $o"
done
echo "built minhook objects"

echo "== hook self-test (in-process: MinHook + core)"
$CXX $CXXFLAGS dll/hook_selftest.cpp core/round_rewrite.cpp $MH_OBJS \
	-o build/hook_selftest.exe
./build/hook_selftest.exe

echo "== dll"
$CXX $CXXFLAGS -shared dll/round_dll.cpp core/round_rewrite.cpp $MH_OBJS \
	-o dll/round.dll -static -lpsapi -lkernel32 -luser32
echo "built dll/round.dll"

echo "== injector"
$CXX $CXXFLAGS tools/inject.cpp -o tools/inject.exe -static
echo "built tools/inject.exe"
