#!/usr/bin/env python3
"""Serve our demo plus pinned oracle files from an external cache, on loopback."""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit


ROOT = pathlib.Path(__file__).resolve().parent.parent


def reference_roots():
    node = shutil.which("node")
    if not node:
        raise RuntimeError("Node.js is required to load the external pinned references")
    helper = (ROOT / "scripts" / "upstream.mjs").as_uri()
    script = "const {ensureUpstream}=await import(process.argv[1]); process.stdout.write(JSON.stringify(await ensureUpstream()));"
    result = subprocess.run([node, "--input-type=module", "-e", script, helper],
                            cwd=ROOT, text=True, stdout=subprocess.PIPE, check=True)
    paths = json.loads(result.stdout)
    return {"/__reference/rbush": pathlib.Path(paths["rbushRoot"]).resolve(),
            "/__reference/quickselect": pathlib.Path(paths["quickselectRoot"]).resolve()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", default=8128, type=int)
    args = parser.parse_args()
    references = reference_roots()

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(ROOT), **kwargs)

        def resolved_path(self, request_path):
            path = unquote(urlsplit(request_path).path)
            base, relative = ROOT, path.lstrip("/")
            if path.startswith("/__reference/"):
                for prefix, directory in references.items():
                    if path == prefix or path.startswith(prefix + "/"):
                        base, relative = directory, path[len(prefix):].lstrip("/")
                        break
                else:
                    raise PermissionError("unknown reference route")
            resolved = (base / relative).resolve()
            try:
                resolved.relative_to(base)
            except ValueError:
                raise PermissionError("path escapes its served root") from None
            return resolved

        def send_head(self):
            requested = urlsplit(self.path)
            if requested.path in ("/wasm", "/wasm/"):
                self.send_response(302)
                self.send_header("Location", "/examples/web/" + ("?" + requested.query if requested.query else ""))
                self.send_header("Content-Length", "0")
                self.end_headers()
                return None
            try:
                self.resolved_path(self.path)
            except (PermissionError, ValueError):
                self.send_error(403, "Path outside served roots")
                return None
            return super().send_head()

        def translate_path(self, path):
            return str(self.resolved_path(path))

    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    print("Demo: http://{}:{}/examples/web/".format(args.bind, args.port), flush=True)
    for route, directory in references.items():
        print("{} -> {}".format(route, directory), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError) as error:
        print("Unable to serve demo: {}".format(error), file=sys.stderr)
        sys.exit(1)
