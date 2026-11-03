#!/bin/sh
# The texture cache's source pool (GSTextureCache::Source::operator
# new/delete): slots handed out once each, 32-byte aligned, not
# overlapping, returned and reused, and the fallback past the pool's
# size. The arithmetic only - the class itself needs the whole GS.
# These test the pool itself, so they build it live even under a sanitizer
# (GSObjectPool.h makes it hand out nothing there, for the cache's sake).
set -e
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/../.." && pwd)
LC="$ROOT/libretro/libretro-common"
INC="-I $ROOT -I $ROOT/pcsx2 -I $ROOT/common -I $ROOT/common/include -I $LC/include"

${CXX:-c++} -std=c++17 -O2 -g -w -DGS_OBJECT_POOL_SANITIZED=0 $SANFLAGS $INC -o "$DIR/gspool_test" \
	"$DIR/main.cpp" "$ROOT/pcsx2/GS/Renderers/HW/GSObjectPool.c" "$LC/memmap/memalign.c"
"$DIR/gspool_test"
