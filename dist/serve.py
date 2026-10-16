#!/usr/bin/env python3
"""Local server for the browser build of 0 A.D.

  python3 serve.py --data "<path to your 0 A.D. 'data' folder>" [--port 8080]

It serves the game files from this folder, your own copy of the game data under /data/ (read straight from
disk, with HTTP Range support so only the parts the game needs are downloaded) and the cross-origin-isolation
headers that WebAssembly threads need. Nothing is uploaded anywhere.

Your data folder is the one that contains `mods/` and `config/` (for example
  Windows: C:\\Program Files\\0 A.D.\\binaries\\data
  Linux:   /usr/share/0ad/data   or   <checkout>/binaries/data
"""
import argparse, http.server, os, re, socketserver, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def build_manifest(data_dir, extra_dir):
    """Files the game needs to see, relative to the data folder (one per line)."""
    files = []
    cfg = os.path.join(data_dir, "config")
    if os.path.isdir(cfg):
        files += ["config/" + f for f in sorted(os.listdir(cfg)) if os.path.isfile(os.path.join(cfg, f))]
    mods = os.path.join(data_dir, "mods")
    for mod in sorted(os.listdir(mods)) if os.path.isdir(mods) else []:
        mdir = os.path.join(mods, mod)
        if not os.path.isdir(mdir):
            continue
        for f in sorted(os.listdir(mdir)):
            p = os.path.join(mdir, f)
            if os.path.isfile(p) and (f == "mod.json" or f.lower().endswith((".zip", ".pyromod"))):
                files.append(f"mods/{mod}/{f}")
    # extra files shipped with this build (ICU data)
    for r, _d, fs in os.walk(extra_dir):
        for f in sorted(fs):
            files.append(os.path.relpath(os.path.join(r, f), extra_dir).replace(os.sep, "/"))
    return files


def check_data(data_dir):
    problems = []
    if not os.path.isdir(os.path.join(data_dir, "mods")):
        problems.append("no 'mods' folder found")
    for need in ("mods/public/public.zip", "mods/mod/mod.zip"):
        if not os.path.isfile(os.path.join(data_dir, need)):
            problems.append(f"missing {need}")
    for z in ("mods/public/public.zip", "mods/mod/mod.zip"):
        p = os.path.join(data_dir, z)
        if os.path.isfile(p):
            try:
                zipfile.ZipFile(p).close()
            except Exception as e:  # noqa
                problems.append(f"{z} is not a readable zip ({e})")
            if os.path.getsize(p) > (1 << 32) - 1:
                problems.append(f"{z} is larger than 4 GB (not supported by the engine's zip reader)")
    return problems


class Handler(http.server.SimpleHTTPRequestHandler):
    data_dir = None
    extra_dir = None
    manifest = ""

    def translate_path(self, path):
        path = path.split("?", 1)[0].split("#", 1)[0]
        path = re.sub(r"/+", "/", path)
        if path.startswith("/data/"):
            rel = os.path.normpath(path[len("/data/"):]).lstrip("/\\")
            if rel.startswith(".."):
                return os.path.join(HERE, "__forbidden__")
            for base in (self.extra_dir, self.data_dir):
                cand = os.path.join(base, rel)
                if os.path.isfile(cand):
                    return cand
            return os.path.join(self.data_dir, rel)
        return super().translate_path(path)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, fmt, *a):
        if os.environ.get("OAD_VERBOSE"):
            super().log_message(fmt, *a)

    def do_GET(self): self._serve(True)
    def do_HEAD(self): self._serve(False)

    def _serve(self, body):
        if self.path.split("?", 1)[0] == "/data_manifest.txt":
            data = self.manifest.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            if body: self.wfile.write(data)
            return
        path = self.translate_path(self.path)
        rng = self.headers.get("Range")
        if not rng or not os.path.isfile(path):
            return super().do_GET() if body else super().do_HEAD()
        m = re.match(r"bytes=(\d*)-(\d*)", rng)
        size = os.path.getsize(path)
        start = int(m.group(1)) if m and m.group(1) else 0
        end = int(m.group(2)) if m and m.group(2) else size - 1
        if m and not m.group(1) and m.group(2):
            start, end = max(0, size - int(m.group(2))), size - 1
        end = min(end, size - 1)
        if start > end:
            self.send_response(416); self.send_header("Content-Range", f"bytes */{size}"); self.end_headers(); return
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        if body:
            with open(path, "rb") as f:
                f.seek(start); left = end - start + 1
                while left > 0:
                    chunk = f.read(min(1 << 20, left))
                    if not chunk: break
                    try: self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError): break
                    left -= len(chunk)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", required=True, help="your 0 A.D. data folder (contains mods/ and config/)")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--host", default="127.0.0.1", help="use 0.0.0.0 to allow other devices (not recommended)")
    a = ap.parse_args()

    data_dir = os.path.abspath(a.data)
    extra_dir = os.path.join(HERE, "data_extra")
    for p in check_data(data_dir):
        print("WARNING:", p, file=sys.stderr)
    Handler.data_dir, Handler.extra_dir = data_dir, extra_dir
    files = build_manifest(data_dir, extra_dir)
    Handler.manifest = "\n".join(files) + "\n"
    print(f"{len(files)} game files found in {data_dir}")
    os.chdir(HERE)
    srv = Server((a.host, a.port), Handler)
    print(f"Open  http://localhost:{a.port}/  in Chrome, Edge or another Chromium-based browser.   (Ctrl+C stops)")
    try: srv.serve_forever()
    except KeyboardInterrupt: pass


if __name__ == "__main__":
    main()
