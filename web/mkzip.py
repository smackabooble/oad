#!/usr/bin/env python3
"""Pack loose mod files (git layout) into mod zips like a release install has.
Zips are split so each stays well below 4 GB (the engine's zip reader has no Zip64 support).
usage: mkzip.py <mod_dir> <out_prefix> [max_uncompressed_bytes]"""
import os, sys, zipfile
src, prefix = sys.argv[1], sys.argv[2]
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 3_000_000_000
files = []
for root, dirs, fs in os.walk(src):
    dirs.sort()
    for f in sorted(fs):
        p = os.path.join(root, f); rel = os.path.relpath(p, src)
        if rel == "mod.json" or f.startswith("."): continue
        files.append((p, rel, os.path.getsize(p)))
idx, cur, z = 0, 0, None
def name(i): return f"{prefix}.zip" if i == 0 else f"{prefix}_{i}.zip"
for p, rel, size in files:
    if z is None or cur + size > limit:
        if z: z.close(); print(name(idx), os.path.getsize(name(idx)), flush=True); idx += 1
        z = zipfile.ZipFile(name(idx), "w", zipfile.ZIP_DEFLATED, compresslevel=1); cur = 0
    z.write(p, rel); cur += size
z.close(); print(name(idx), os.path.getsize(name(idx)), "files:", len(files), flush=True)
