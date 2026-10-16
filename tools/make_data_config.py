#!/usr/bin/env python3
"""make_data_config.py <data dir> <base url> [out data-config.json]
Lists the game data files (config/, mods/*/mod.json, mods/*/*.zip) with sizes.
Upload the data dir contents to <base url> (CORS + Range enabled), put data-config.json in docs/."""
import os, sys, json, time
d, base = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 else 'data-config.json'
files = []
for root, _, names in os.walk(d):
    for n in sorted(names):
        p = os.path.join(root, n); rel = os.path.relpath(p, d).replace(os.sep, '/')
        if rel.startswith('config/') or (rel.startswith('mods/') and (n == 'mod.json' or n.endswith('.zip'))):
            files.append({'path': rel, 'size': os.path.getsize(p)})
files.append({'path': 'icu/icudt75l.dat', 'size': 30729184, 'url': 'data_extra/icu/icudt75l.dat'})
files.sort(key=lambda f: f['path'])
json.dump({'version': str(int(time.time())), 'base': base, 'files': files}, open(out, 'w'), indent=1)
print(len(files), 'files,', sum(f['size'] for f in files) // 1000000, 'MB')
