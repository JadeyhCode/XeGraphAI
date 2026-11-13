#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/fake_openai.py — 最小的 OpenAI 兼容假端点（给 `::tool` 的套件用，不联网）
   · POST /v1/chat/completions  ⇒ 固定答复（内容由 --reply 给）
   · 把收到的请求体原样写到 --dump（排查"发出去的到底是不是 JSON"）
   · 端口由 --port 指定；`--port 0` 时把实际端口打到 stdout 第一行
"""
import argparse
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--reply", default="假端点的答复")
    ap.add_argument("--dump", default="")
    a = ap.parse_args()

    class H(BaseHTTPRequestHandler):
        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n) if n else b""
            if a.dump:
                open(a.dump, "wb").write(body)
            out = json.dumps({"choices": [{"message": {"role": "assistant",
                                                       "content": a.reply}}]},
                             ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)

        def log_message(self, *args):
            pass

    srv = HTTPServer(("127.0.0.1", a.port), H)
    print(srv.server_port, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    sys.exit(main())
