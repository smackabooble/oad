#!/usr/bin/env python3
"""Static server with HTTP Range support and the cross-origin-isolation headers that WebAssembly threads need.
usage: server.py [port] [dir]"""
import os, sys, re, http.server, socketserver

class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        if os.environ.get("OAD_PLAIN"):   # emulate GitHub Pages / a CORS data host: no isolation headers
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Headers", "Range")
            self.send_header("Access-Control-Expose-Headers", "Content-Range, Content-Length, Accept-Ranges")
        else:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, fmt, *a):
        if os.environ.get("OAD_VERBOSE"): super().log_message(fmt, *a)

    def do_GET(self): self._serve(True)
    def do_HEAD(self): self._serve(False)

    def _serve(self, body):
        path = self.translate_path(self.path.split("?", 1)[0])
        rng = self.headers.get("Range")
        if not rng or not os.path.isfile(path):
            return super().do_GET() if body else super().do_HEAD()
        m = re.match(r"bytes=(\d*)-(\d*)", rng)
        size = os.path.getsize(path)
        start = int(m.group(1)) if m and m.group(1) else 0
        end = int(m.group(2)) if m and m.group(2) else size - 1
        if m and not m.group(1) and m.group(2):  # suffix range
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

class T(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    root = sys.argv[2] if len(sys.argv) > 2 else "."
    os.chdir(root)
    print(f"serving {os.path.abspath(root)} on http://localhost:{port}", flush=True)
    T(("0.0.0.0", port), H).serve_forever()
