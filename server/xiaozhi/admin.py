"""Admin HTTP server — conversation history viewer on port 7071."""

import json
import asyncio
import logging
from urllib.parse import parse_qs, urlparse

logger = logging.getLogger("xz.admin")

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Reddy Bot - 对话记录</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#0f0f1a;color:#ccc;padding:16px}
h1{color:#8888cc;margin-bottom:16px}
.controls{display:flex;gap:12px;align-items:center;margin-bottom:16px}
.controls button{padding:6px 16px;background:#3366cc;color:#fff;border:none;border-radius:4px;cursor:pointer}
.controls button:disabled{opacity:0.4;cursor:default}
.controls span{color:#888}
table{width:100%;border-collapse:collapse}
th{background:#1a1a2e;color:#8888cc;padding:8px 12px;text-align:left;position:sticky;top:0}
td{padding:8px 12px;border-bottom:1px solid #1a1a2e;vertical-align:top;max-width:500px;word-break:break-all}
tr:hover{background:#1a1a30}
.user{color:#4488cc}
.assistant{color:#44cc88}
.system{color:#aa8844}
.time{color:#666;white-space:nowrap;font-size:12px}
.tokens{color:#555;text-align:right;font-size:12px}
#loading{text-align:center;color:#888;padding:40px}
.error{color:#cc4444}
</style>
</head>
<body>
<h1>Reddy Bot 对话记录</h1>
<div class="controls">
  <button onclick="prevPage()" id="prev">上一页</button>
  <span id="pageInfo">第 1 页</span>
  <button onclick="nextPage()" id="next">下一页</button>
  <span id="total"></span>
</div>
<table id="table"><thead><tr><th style="width:60px">角色</th><th>内容</th><th style="width:70px">Tokens</th><th style="width:160px">时间</th></tr></thead><tbody id="tbody"></tbody></table>
<div id="loading">加载中...</div>
<script>
let page=1, total=0;
const perPage=100;
const colors={user:'user',assistant:'assistant',system:'system'};
async function load(p){
  document.getElementById('loading').style.display='block';
  document.getElementById('prev').disabled=true;
  document.getElementById('next').disabled=true;
  try{
    const r=await fetch('/api/messages?page='+p+'&per_page='+perPage);
    const d=await r.json();
    total=d.total; page=d.page;
    document.getElementById('pageInfo').textContent='第 '+page+' 页';
    document.getElementById('total').textContent='共 '+d.total_pages+' 页 / '+total+' 条';
    const tb=document.getElementById('tbody');
    tb.innerHTML='';
    d.messages.forEach(m=>{
      const tr=document.createElement('tr');
      tr.innerHTML='<td class="'+colors[m.role]+'">'+esc(m.role)+'</td><td>'+esc(m.content||'(空)')+'</td><td class="tokens">'+(m.tokens||0)+'</td><td class="time">'+esc(m.time||'')+'</td>';
      tb.appendChild(tr);
    });
    document.getElementById('prev').disabled=page<=1;
    document.getElementById('next').disabled=page>=d.total_pages;
  }catch(e){
    document.getElementById('tbody').innerHTML='<tr><td colspan="4" class="error">加载失败: '+esc(String(e))+'</td></tr>';
  }
  document.getElementById('loading').style.display='none';
}
function nextPage(){if(page<total)load(++page)}
function prevPage(){if(page>1)load(--page)}
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
load(1);
</script>
</body>
</html>"""


class AdminServer:
    def __init__(self, db_path: str, host: str = "0.0.0.0", port: int = 7071):
        self._db_path = db_path
        self._host = host
        self._port = port

    async def _handle_http(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        try:
            raw = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=30)
        except (asyncio.TimeoutError, ConnectionError):
            writer.close()
            return

        request = raw.decode("utf-8", errors="replace")
        lines = request.split("\r\n")
        if not lines:
            writer.close(); return
        method, path, _ = lines[0].split(" ", 2)
        parsed = urlparse(path)
        route = parsed.path

        if method == "GET" and route == "/":
            await self._serve_html(writer)
        elif method == "GET" and route == "/api/messages":
            await self._serve_api(writer, parsed.query)
        else:
            await self._send(writer, 404, '{"error":"not found"}', "application/json")

    async def _serve_html(self, writer):
        await self._send(writer, 200, HTML_TEMPLATE, "text/html; charset=utf-8")

    async def _serve_api(self, writer, query_string: str):
        params = parse_qs(query_string)
        page = int(params.get("page", ["1"])[0])
        per_page = min(int(params.get("per_page", ["100"])[0]), 500)

        import aiosqlite
        async with aiosqlite.connect(self._db_path) as db:
            # Count total
            cursor = await db.execute("SELECT COUNT(*) FROM messages")
            row = await cursor.fetchone()
            total = row[0] if row else 0

            # Fetch page
            offset = (page - 1) * per_page
            cursor = await db.execute(
                "SELECT role, content, token_count, created_at FROM messages "
                "ORDER BY id DESC LIMIT ? OFFSET ?",
                (per_page, offset),
            )
            rows = await cursor.fetchall()

        messages = [
            {
                "role": r[0],
                "content": r[1] if r[1] else "",
                "tokens": r[2] or 0,
                "time": r[3] or "",
            }
            for r in rows
        ]

        total_pages = max(1, (total + per_page - 1) // per_page)

        resp = {
            "page": page,
            "per_page": per_page,
            "total": total,
            "total_pages": total_pages,
            "messages": messages,
        }
        await self._send(writer, 200, json.dumps(resp, ensure_ascii=False), "application/json; charset=utf-8")

    @staticmethod
    async def _send(writer, status: int, body: str, content_type: str):
        writer.write(
            f"HTTP/1.1 {status} OK\r\n"
            f"Content-Type: {content_type}\r\n"
            f"Content-Length: {len(body.encode('utf-8'))}\r\n"
            f"Access-Control-Allow-Origin: *\r\n"
            f"Connection: close\r\n\r\n"
            f"{body}".encode("utf-8")
        )
        await writer.drain()
        writer.close()

    async def start(self):
        server = await asyncio.start_server(
            self._handle_http, self._host, self._port
        )
        logger.info(f"Admin HTTP on http://{self._host}:{self._port}")
        async with server:
            await server.serve_forever()
