#!/usr/bin/env python3
"""Generate build.ninja for the 0 A.D. engine -> WebAssembly (Emscripten) with the QuickJS shim."""
import os, sys, glob

ROOT = os.environ.get("OAD_ROOT", "/home/user/0ad-web")
SRC = f"{ROOT}/src/source"
OUT = os.environ.get("OAD_BUILD", f"{ROOT}/build_mt")
DEPS = os.environ.get("OAD_PREFIX", f"{ROOT}/deps/prefix-mt")
SHIM = os.environ.get("OAD_SHIM", "/home/user/oad/shim")
QJS = f"{ROOT}/deps/quickjs"
SHIMCPP = f"{SHIM}/jsapi.cpp"
STUBS = os.environ.get("OAD_STUBS", "/home/user/oad/stubs")

dirs = """
network
third_party/tinygettext/src
simulation2 simulation2/components simulation2/helpers simulation2/scripting simulation2/serialization simulation2/system simulation2/testcomponents
scriptinterface scriptinterface/third_party
ps ps/scripting network/scripting ps/GameSetup ps/XMB ps/XML
soundmanager soundmanager/data soundmanager/items soundmanager/scripting
maths maths/scripting i18n i18n/scripting
graphics graphics/scripting renderer renderer/backend renderer/backend/dummy renderer/backend/gl renderer/scripting
third_party/mikktspace third_party/ogre3d_preprocessor
gui gui/ObjectTypes gui/ObjectBases gui/Scripting gui/SettingTypes
lib lib/adts lib/allocators lib/external_libraries lib/file lib/file/archive lib/file/common lib/file/io lib/file/vfs
lib/posix lib/res lib/res/graphics lib/sysdep lib/tex
lib/sysdep/os/linux lib/sysdep/os/unix lib/sysdep/rtl/gcc
rlinterface third_party/mongoose
""".split()

exclude = set(os.environ.get("OAD_EXCLUDE", "lib/sysdep/os/linux/dir_watch_inotify.cpp").split())
files = []
for d in dirs:
    for f in sorted(glob.glob(f"{SRC}/{d}/*.cpp")):
        rel = os.path.relpath(f, SRC)
        if rel in exclude: continue
        files.append(rel)
files.append("main.cpp")
glad = f"{ROOT}/src/libraries/source/glad/src/gles2.cpp"
extras = [f"{STUBS}/dir_watch_stub.cpp", f"{STUBS}/curl_stub.cpp", f"{STUBS}/wasm_stubs.cpp", f"{SRC}/mocks/mocks_real.cpp"]

defs = ["-DNDEBUG", "-DCONFIG_FINAL=1", "-DCONFIG2_AUDIO=0", "-DCONFIG2_NVTT=0", "-DCONFIG2_LOBBY=0",
        "-DCONFIG2_MINIUPNPC=0", "-DCONFIG2_GLES=1"]
incs = [SRC, f"{SRC}/pch/engine", SHIM, QJS, STUBS, f"{DEPS}/include", f"{DEPS}/include/SDL2",
        f"{DEPS}/include/freetype2", f"{DEPS}/include/libxml2", f"{ROOT}/deps/boost-inc",
        f"{SRC}/third_party/tinygettext/include", f"{ROOT}/src/libraries/source/glad/include", f"{ROOT}/src/libraries/source/cxxtest-4.4"]
cflags = "-std=c++17 -pthread -fwasm-exceptions -O1 -fno-strict-aliasing -Wno-deprecated-declarations -Wno-unused-value " + " ".join(defs) + " " + " ".join(f"-I{i}" for i in incs)

os.makedirs(OUT, exist_ok=True)
with open(f"{OUT}/build.ninja", "w") as n:
    n.write(f"cflags = {cflags}\n")
    n.write("rule cxx\n  command = em++ $cflags -MMD -MF $out.d -c $in -o $out\n  depfile = $out.d\n  deps = gcc\n  description = CXX $in\n")
    n.write("rule ar\n  command = emar rcs $out $in\n  description = AR $out\n\n")
    objs = []
    for rel in files:
        o = "obj/" + rel.replace("/", "__").replace(".cpp", ".o")
        n.write(f"build {o}: cxx {SRC}/{rel}\n"); objs.append(o)
    n.write(f"build obj/glad_gl.o: cxx {glad}\n"); objs.append("obj/glad_gl.o")
    n.write(f"build obj/shim_jsapi.o: cxx {SHIMCPP}\n"); objs.append("obj/shim_jsapi.o")
    for e in extras:
        o = "obj/stub__" + os.path.basename(e).replace(".cpp", ".o")
        n.write(f"build {o}: cxx {e}\n"); objs.append(o)
    n.write("build libengine.a: ar " + " ".join(objs) + "\n")
print(len(files), "source files")

with open(f"{OUT}/link.sh", "w") as l:
    l.write(f"""#!/bin/bash
# usage: link.sh <out.js|out.html> [extra em++ flags]
source {ROOT}/emsdk/emsdk_env.sh >/dev/null 2>&1
P={DEPS}
OUT=$1; shift
cd {OUT}
em++ -O1 -pthread -fwasm-exceptions libengine.a $P/lib/libboost_fs.a $P/lib/libqjs.a -L$P/lib -L$P/lib/static \\
  -lenet -lfmt -lxml2 -lSDL2 -lpng16 -lfreetype -lvorbisfile -lvorbis -logg -lz -licui18n -licuuc -licudata -lsodium \\
  -sUSE_WEBGL2=1 -sFULL_ES3=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=4MB -sINITIAL_MEMORY=512MB -sMAXIMUM_MEMORY=2GB \\
  -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=0 -sPTHREAD_POOL_SIZE=16 -sPTHREAD_POOL_SIZE_STRICT=0 \\
  -Wl,--error-limit=0 -o $OUT "$@"
""")
os.chmod(f"{OUT}/link.sh", 0o755)
