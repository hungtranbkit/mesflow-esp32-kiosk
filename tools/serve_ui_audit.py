#!/usr/bin/env python3
"""Serve captured real-framebuffer screenshots for Playwright review."""
import argparse, http.server, json, pathlib, urllib.parse

class AuditHTTPServer(http.server.ThreadingHTTPServer):
    allow_reuse_address = True

class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/esp-ui-test":
            state = urllib.parse.parse_qs(parsed.query).get("state", [self.states[0]])[0]
            item = next((x for x in self.report["screens"] if x["screen"] == state), self.report["screens"][0])
            body = f"<!doctype html><meta charset='utf-8'><title>ESP UI {state}</title><style>body{{margin:0;background:#111827;color:white;font:16px sans-serif}}main{{display:flex;gap:24px;padding:24px}}img{{width:240px;height:320px;box-sizing:border-box;image-rendering:pixelated;border:1px solid #475569}}code{{white-space:pre-wrap}}</style><main><img id='esp-screen' src='/{item['screenshot']}' alt='{state}'><section><h1 id='state'>{state}</h1><code id='metadata'>{json.dumps(item,ensure_ascii=False)}</code></section></main>"
            data = body.encode(); self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data); return
        return super().do_GET()
    def log_message(self, *_): pass

def main():
    p=argparse.ArgumentParser(); p.add_argument("--dir",required=True); p.add_argument("--port",type=int,default=8765); a=p.parse_args()
    root=pathlib.Path(a.dir).resolve(); Handler.report=json.loads((root/"report.json").read_text()); Handler.states=[x["screen"] for x in Handler.report["screens"]]; Handler.directory=str(root)
    import os; os.chdir(root); AuditHTTPServer(("127.0.0.1",a.port),Handler).serve_forever()
if __name__ == "__main__": main()
