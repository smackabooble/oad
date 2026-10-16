#!/usr/bin/env python3
"""Write data_manifest.txt: every file under <data_dir>, relative to it (one per line).
usage: gen_manifest.py <data_dir> <out.txt>"""
import os, sys
root, out = sys.argv[1], sys.argv[2]
with open(out, "w") as f:
    for r, d, files in os.walk(root):
        for n in sorted(files):
            f.write(os.path.relpath(os.path.join(r, n), root) + "\n")
