#!/bin/bash
# Fetch every source needed to rebuild the browser port from scratch (versions as used here).
# Usage: setup_sources.sh [ROOT]     (default ROOT=$HOME/0ad-web; this repo is expected at $HOME/oad)
set -e
ROOT=${1:-$HOME/0ad-web}; REPO=${OAD_REPO:-$HOME/oad}
mkdir -p "$ROOT/deps" && cd "$ROOT"

[ -d emsdk ] || git clone --depth 1 https://github.com/emscripten-core/emsdk.git
(cd emsdk && ./emsdk install latest && ./emsdk activate latest)

# Engine source: the 0 A.D. Release 27.1 tarball (the game data must be the same release), plus our port patch.
if [ ! -d src27 ]; then
  curl -sSLO http://archive.ubuntu.com/ubuntu/pool/universe/0/0ad/0ad_0.27.1.orig.tar.xz
  mkdir src27 && tar xf 0ad_0.27.1.orig.tar.xz -C src27 --strip-components=1
  (cd src27 && patch -p1 < "$REPO"/patches/0001-0ad-0.27.1-browser-port.patch)
  # FCollada: extract the bundled sources and apply the wasm32 size_t fix
  (cd src27/libraries/source/fcollada && tar xf fcollada-28209.tar.xz && cp -r fcollada-28209/src fcollada-28209/include . && \
   patch -p1 < "$REPO"/patches/0002-fcollada-wasm32.patch)
  (cd src27/libraries/source/cxxtest-4.4 && tar xf cxxtest-4.4.tar.gz --strip-components=1)
fi

cd deps
clone() { [ -d "$3" ] || git clone --depth 1 -b "$2" "https://github.com/$1.git" "$3"; }
clone quickjs-ng/quickjs v0.10.1 quickjs
clone madler/zlib v1.3.1 zlib
clone pnggroup/libpng v1.6.43 libpng
clone freetype/freetype VER-2-13-2 freetype
clone xiph/ogg v1.3.5 ogg
clone xiph/vorbis v1.3.7 vorbis
clone libsdl-org/SDL release-2.30.8 SDL
clone lsalzman/enet v1.3.18 enet
clone fmtlib/fmt 11.0.2 fmt
clone GNOME/libxml2 v2.12.9 libxml2
# SDL: WebGL-direct video driver (the EGL layer cannot run in a worker)
cp "$REPO"/sdl/SDL_emscriptenopengles.[ch] "$REPO"/sdl/SDL_emscriptenvideo.c SDL/src/video/emscripten/

[ -d boost ] || { mkdir boost && curl -sSL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.gz | tar xz -C boost --strip-components=1; }
[ -d libsodium-stable ] || curl -sSL https://github.com/jedisct1/libsodium/releases/download/1.0.19-RELEASE/libsodium-1.0.19.tar.gz | tar xz
[ -d icu ] || { mkdir icu && curl -sSL https://github.com/unicode-org/icu/releases/download/release-75-1/icu4c-75_1-src.tgz | tar xz -C icu --strip-components=1; }

# boost headers: flatten libs/*/include/boost into one include dir
mkdir -p boost-inc
for d in boost/libs/*/include boost/libs/numeric/*/include; do [ -d "$d/boost" ] && cp -rsf "$(pwd)/$d/boost" boost-inc/ 2>/dev/null || true; done

# native ICU build (needed to cross-compile ICU)
if [ ! -f icu/nat/DONE ]; then
  mkdir -p icu/nat && (cd icu/nat && ../source/runConfigureICU Linux && make -j"$(nproc)" && touch DONE)
fi

echo "sources ready. Next: $REPO/build/build_deps.sh, then python3 $REPO/build/gen_ninja.py && ninja -C $ROOT/build_mt"
