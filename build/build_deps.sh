#!/bin/bash
# Rebuild all third-party dependencies for wasm with threads + wasm exceptions.
# Usage: build_deps.sh <prefix-dir>   (sources expected in $ROOT/deps/<name>)
set -e
ROOT=${OAD_ROOT:-/home/user/0ad-web}
source $ROOT/emsdk/emsdk_env.sh >/dev/null 2>&1
P=${1:-$ROOT/deps/prefix-mt}
FLAGS="-pthread -fwasm-exceptions -O2"
mkdir -p $P
D=$ROOT/deps
cm() { # name srcdir extra-args...
  local n=$1; shift; local s=$1; shift
  echo "== $n"; rm -rf $s/bmt
  (cd $s && emcmake cmake -B bmt -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
     -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS" -DCMAKE_PREFIX_PATH=$P "$@" > bmt.cfg.log 2>&1 \
   && cmake --build bmt -j4 > bmt.build.log 2>&1 && cmake --install bmt > bmt.inst.log 2>&1) || { echo "FAILED $n"; tail -n 8 $s/bmt.cfg.log $s/bmt.build.log; exit 1; }
}
Z="-DZLIB_LIBRARY=$P/lib/libz.a -DZLIB_INCLUDE_DIR=$P/include"
cm zlib $D/zlib
rm -f $P/lib/libz.so*
cm ogg $D/ogg
cm vorbis $D/vorbis -DOGG_INCLUDE_DIR=$P/include -DOGG_LIBRARY=$P/lib/libogg.a
cm libpng $D/libpng -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_ARM_NEON=off $Z
cm freetype $D/freetype -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_PNG=ON $Z
cm SDL $D/SDL -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_PTHREADS=ON
cm enet $D/enet
cm fmt $D/fmt -DFMT_TEST=OFF -DFMT_DOC=OFF
cm libxml2 $D/libxml2 -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_THREADS=ON
cm quickjs $D/quickjs -DBUILD_EXAMPLES=OFF -DBUILD_CLI_STATIC=OFF
cp $D/quickjs/bmt/libqjs.a $P/lib/libqjs.a

echo "== libsodium"
(cd $D/libsodium-stable && make distclean >/dev/null 2>&1 || true
 CFLAGS="$FLAGS" emconfigure ./configure --host=wasm32-unknown-emscripten --disable-shared --enable-static --disable-ssp --disable-asm --prefix=$P > cfg.mt.log 2>&1
 emmake make -j4 > make.mt.log 2>&1 && emmake make install > inst.mt.log 2>&1)

echo "== icu"
(rm -rf $D/icu/wasm_mt && mkdir -p $D/icu/wasm_mt && cd $D/icu/wasm_mt
 emconfigure ../source/configure --prefix=$P --with-cross-build=$D/icu/nat --enable-static --disable-shared --disable-tools --disable-tests \
   --disable-samples --disable-extras --disable-layoutex --disable-icuio --with-data-packaging=archive \
   CFLAGS="$FLAGS" CXXFLAGS="-std=c++17 $FLAGS" > cfg.log 2>&1
 emmake make -j4 > make.log 2>&1 && emmake make install > inst.log 2>&1)

echo "== boost filesystem"
mkdir -p $ROOT/build/boost_mt && cd $ROOT/build/boost_mt && rm -f *.o
B=$D/boost-inc; S=$D/boost/libs
for f in $S/filesystem/src/*.cpp; do n=$(basename $f .cpp)
  echo "em++ -std=c++17 $FLAGS -DBOOST_FILESYSTEM_STATIC_LINK=1 -DBOOST_SYSTEM_NO_LIB -DBOOST_ALL_NO_LIB -DBOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF -I$B -I$S/filesystem/src -c $f -o fs_$n.o"
done | xargs -P4 -I{} sh -c '{}'
emar rcs libboost_fs.a fs_*.o && cp libboost_fs.a $P/lib/libboost_fs.a
echo "deps done"
