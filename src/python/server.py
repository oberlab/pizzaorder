#!/usr/bin/env python3
import base64
import hashlib
import json
import os
import secrets
import socket
import threading
import shutil
import time
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# Paths
ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA_DIR = os.path.join(ROOT_DIR, "data")
LEGACY_WWW_DIR = os.path.join(ROOT_DIR, "httpd")
# Prefer live assets in repo (httpd/) so Python serves the same pages
# as the development sources. Allow override via WWW_DIR env.
WWW_DIR = os.environ.get("WWW_DIR") or (LEGACY_WWW_DIR if os.path.isdir(LEGACY_WWW_DIR) else os.path.join(DATA_DIR, "httpd"))
# canonical filename (per requirement): data/pizze.json
PIZZAS_JSON = os.path.join(DATA_DIR, "pizze.json")
OLD_PIZZAS_JSON = os.path.join(DATA_DIR, "pizzas.json")
SETTINGS_JSON = os.path.join(DATA_DIR, "settings.json")

os.makedirs(DATA_DIR, exist_ok=True)
os.makedirs(WWW_DIR, exist_ok=True)

# When serving from repo httpd/, no migration is needed. If serving from
# data/httpd, perform a one-time copy from httpd/ if empty.
try:
    if WWW_DIR.endswith(os.path.join("data", "httpd")):
        if not os.path.exists(os.path.join(WWW_DIR, "index.html")) and os.path.isdir(LEGACY_WWW_DIR):
            for name in os.listdir(LEGACY_WWW_DIR):
                src = os.path.join(LEGACY_WWW_DIR, name)
                dst = os.path.join(WWW_DIR, name)
                if os.path.isdir(src):
                    if not os.path.exists(dst):
                        shutil.copytree(src, dst)
                else:
                    if not os.path.exists(dst):
                        shutil.copy2(src, dst)
except Exception:
    pass


def load_pizzas():
    # prefer new filename; fall back to legacy if present
    path = PIZZAS_JSON if os.path.exists(PIZZAS_JSON) else (OLD_PIZZAS_JSON if os.path.exists(OLD_PIZZAS_JSON) else None)
    if path:
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
                if isinstance(data, list):
                    return data
                return data.get("pizzas", [])
        except Exception:
            return []
    # default sample
    return [
        {"id": "marg", "name": "Margherita", "ingredients": ["Tomate", "Mozzarella", "Basilikum"], "price": 7.5},
        {"id": "salami", "name": "Salami", "ingredients": ["Tomate", "Mozzarella", "Salami"], "price": 8.5},
        {"id": "funghi", "name": "Funghi", "ingredients": ["Tomate", "Mozzarella", "Champignons"], "price": 8.5},
        {"id": "hawaii", "name": "Hawaii", "ingredients": ["Tomate", "Mozzarella", "Schinken", "Ananas"], "price": 9.0},
        {"id": "tonno", "name": "Tonno", "ingredients": ["Tomate", "Mozzarella", "Thunfisch", "Zwiebel"], "price": 9.5},
        {"id": "quattro", "name": "Quattro Formaggi", "ingredients": ["Mozzarella", "Gorgonzola", "Parmesan", "Emmentaler"], "price": 10.5},
        {"id": "diavola", "name": "Diavola", "ingredients": ["Tomate", "Mozzarella", "Scharfe Salami", "Peperoni"], "price": 9.5},
        {"id": "veggie", "name": "Vegetariana", "ingredients": ["Tomate", "Mozzarella", "Paprika", "Oliven", "Zwiebel"], "price": 9.0},
    ]


def save_pizzas(pizzas):
    with open(PIZZAS_JSON, "w", encoding="utf-8") as f:
        json.dump({"pizzas": pizzas}, f, ensure_ascii=False, indent=2)


def load_settings():
    if os.path.exists(SETTINGS_JSON):
        try:
            with open(SETTINGS_JSON, "r", encoding="utf-8") as f:
                return json.load(f) or {}
        except Exception:
            return {}
    return {}


def save_settings(settings):
    # No-op: settings are non-persistent in Python mode to mirror device behavior
    return


class Hub:
    def __init__(self):
        self.pizzas = load_pizzas()
        # orders keyed by session id (sid)
        # value: { name:str, items:{pid:qty}, paid:bool, method:str }
        self.orders = {}
        # separate names map for sessions without orders yet
        self.names = {}
        self.ordering_open = True
        self.paypal_email = None
        self.manager_token = secrets.token_urlsafe(16)
        self.manager_claimed = False
        self.manager_sid = None
        self.clients = set()  # set of WSConnection
        self.lock = threading.RLock()
        # load initial settings (non-persistent at runtime)
        try:
            settings = load_settings()
            self.paypal_email = settings.get("paypal_email")
            if isinstance(settings.get("ordering_open"), bool):
                self.ordering_open = settings.get("ordering_open")
        except Exception:
            pass

    def state_for(self, viewer=None):
        with self.lock:
            base = {
                "pizzas": self.pizzas,
                "ordering_open": self.ordering_open,
                "paypal_email": self.paypal_email,
            }
            if not self.manager_claimed:
                base["manager_link"] = f"/manage.html?token={self.manager_token}"
            if viewer is not None and getattr(viewer, "is_manager", False):
                base["orders"] = self.orders
            elif viewer is not None:
                # provide only own order back to user
                my = self.orders.get(getattr(viewer, "sid", None))
                base["my"] = my or {"name": self.names.get(getattr(viewer, "sid", None), None), "items": {}, "paid": False, "method": None}
            return base

    def broadcast(self):
        # send viewer-specific state
        for c in list(self.clients):
            try:
                msg = json.dumps({"type": "state", "data": self.state_for(c)})
                c.send_text(msg)
            except Exception:
                pass

    def handle(self, conn, msg):
        t = msg.get("type")
        if t == "hello":
            role = msg.get("role")
            if role == "manager":
                token = msg.get("token") or conn.token
                ok = False
                # authorize either via valid token or same session as previously claimed manager
                if token and token == self.manager_token:
                    ok = True
                    if not self.manager_claimed:
                        self.manager_claimed = True
                    self.manager_sid = getattr(conn, "sid", None)
                    conn.is_manager = True
                elif self.manager_claimed and self.manager_sid and getattr(conn, "sid", None) == self.manager_sid:
                    ok = True
                    conn.is_manager = True
                else:
                    conn.is_manager = False
                conn.send_json({"type": "auth", "ok": bool(ok)})
            else:
                conn.send_json({"type": "auth", "ok": True})
            conn.send_json({"type": "state", "data": self.state_for(conn)})
            return

        if t == "set_user":
            name = (msg.get("name") or "").strip()
            conn.user = name
            with self.lock:
                # don't create order yet; just remember name
                if conn.sid:
                    self.names[conn.sid] = name
                    if conn.sid in self.orders:
                        self.orders[conn.sid]["name"] = name
            self.broadcast()
            return

        # unified handler with ACKs for pizzas/settings
        if t == "set_pizzas":
            if not getattr(conn, "is_manager", False):
                conn.send_json({"type": "error", "op": "set_pizzas", "reason": "forbidden"})
                return
            pizzas = msg.get("pizzas") or []
            cleaned = []
            for p in pizzas:
                pid = str(p.get("id") or secrets.token_hex(3))
                name = str(p.get("name") or "").strip() or pid
                ing = p.get("ingredients") or []
                if isinstance(ing, str):
                    ing = [s.strip() for s in ing.split(",") if s.strip()]
                price = float(p.get("price") or 0.0)
                cleaned.append({"id": pid, "name": name, "ingredients": ing, "price": round(price, 2)})
            with self.lock:
                self.pizzas = cleaned
                save_pizzas(self.pizzas)
            conn.send_json({"type": "ok", "op": "set_pizzas"})
            self.broadcast()
            return

        if t == "set_paypal":
            if not getattr(conn, "is_manager", False):
                conn.send_json({"type": "error", "op": "set_paypal", "reason": "forbidden"})
                return
            email = (msg.get("email") or None)
            with self.lock:
                self.paypal_email = email
            conn.send_json({"type": "ok", "op": "set_paypal", "paypal_email": self.paypal_email})
            self.broadcast()
            return

        if t == "add_item":
            if not self.ordering_open:
                return
            pid = msg.get("pizza_id")
            delta = int(msg.get("delta") or 0)
            if not pid or delta == 0:
                return
            with self.lock:
                sid = getattr(conn, "sid", None)
                if not sid:
                    return
                if sid not in self.orders:
                    self.orders[sid] = {"name": self.names.get(sid, conn.user), "items": {}, "paid": False, "method": None}
                o = self.orders[sid]
                qty = max(0, int(o["items"].get(pid, 0)) + delta)
                if qty == 0:
                    o["items"].pop(pid, None)
                else:
                    o["items"][pid] = qty
                o["paid"] = False
                o["method"] = None
                # remove empty orders to avoid clutter in manager view
                if not o["items"]:
                    self.orders.pop(sid, None)
            self.broadcast()
            return

        if t == "set_pizzas" and getattr(conn, "is_manager", False):
            pizzas = msg.get("pizzas") or []
            # sanitize
            cleaned = []
            for p in pizzas:
                pid = str(p.get("id") or secrets.token_hex(3))
                name = str(p.get("name") or "").strip() or pid
                ing = p.get("ingredients") or []
                if isinstance(ing, str):
                    ing = [s.strip() for s in ing.split(",") if s.strip()]
                price = float(p.get("price") or 0.0)
                cleaned.append({"id": pid, "name": name, "ingredients": ing, "price": round(price, 2)})
            with self.lock:
                self.pizzas = cleaned
                save_pizzas(self.pizzas)
            self.broadcast()
            return

        if t == "set_paypal" and getattr(conn, "is_manager", False):
            email = (msg.get("email") or None)
            with self.lock:
                self.paypal_email = email
                s = load_settings()
                if email:
                    s["paypal_email"] = email
                else:
                    s.pop("paypal_email", None)
                save_settings(s)
            self.broadcast()
            return

        if t == "mark_paid":
            method = msg.get("method") or "cash"
            # manager may pass explicit sid; user uses own sid
            sid = msg.get("sid") or getattr(conn, "sid", None)
            if not sid:
                return
            with self.lock:
                o = self.orders.get(sid)
                if o and o["items"]:
                    o["paid"] = True
                    o["method"] = method
            self.broadcast()
            return

        if t == "mark_unpaid":
            # manager only
            if not getattr(conn, "is_manager", False):
                conn.send_json({"type": "error", "op": "mark_unpaid", "reason": "forbidden"})
                return
            sid = msg.get("sid")
            if not sid:
                return
            with self.lock:
                o = self.orders.get(sid)
                if o:
                    o["paid"] = False
                    o["method"] = None
            self.broadcast()
            return

        if t == "payment_started":
            # user indicates PayPal popup started; mark method so manager sees
            method = msg.get("method") or "paypal_started"
            with self.lock:
                sid = getattr(conn, "sid", None)
                if sid:
                    o = self.orders.get(sid)
                    if o:
                        o["paid"] = False
                        o["method"] = "paypal_started"
            self.broadcast()
            return

        if t == "close_orders" and getattr(conn, "is_manager", False):
            with self.lock:
                self.ordering_open = False
            self.broadcast()
            return

        if t == "open_orders" and getattr(conn, "is_manager", False):
            with self.lock:
                self.ordering_open = True
            self.broadcast()
            return


GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WSConnection:
    def __init__(self, handler, sock, addr, path, query, sid):
        self.handler = handler
        self.sock = sock
        self.addr = addr
        self.path = path
        self.query = query
        self.is_manager = False
        self.user = None
        self.token = (parse_qs(query).get("token") or [None])[0]
        self.sid = sid
        self.sock.settimeout(60)
        self.alive = True

    def send_raw(self, data: bytes):
        try:
            self.sock.sendall(data)
        except Exception:
            self.alive = False
            raise

    def send_text(self, text: str):
        payload = text.encode("utf-8")
        b1 = 0x80 | 0x1  # FIN + text
        length = len(payload)
        if length < 126:
            header = bytes([b1, length])
        elif length < (1 << 16):
            header = bytes([b1, 126]) + length.to_bytes(2, "big")
        else:
            header = bytes([b1, 127]) + length.to_bytes(8, "big")
        self.send_raw(header + payload)

    def send_json(self, obj):
        self.send_text(json.dumps(obj))

    def recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    def read_message(self):
        b1, b2 = self.recv_exact(2)
        fin = (b1 & 0x80) != 0
        opcode = b1 & 0x0F
        masked = (b2 & 0x80) != 0
        length = (b2 & 0x7F)
        if length == 126:
            length = int.from_bytes(self.recv_exact(2), "big")
        elif length == 127:
            length = int.from_bytes(self.recv_exact(8), "big")
        mask = self.recv_exact(4) if masked else b"\x00\x00\x00\x00"
        payload = self.recv_exact(length) if length else b""
        if masked:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        if opcode == 0x1:  # text
            return payload.decode("utf-8")
        if opcode == 0x9:  # ping
            try:
                # respond with pong
                b1 = 0x80 | 0xA
                length = len(payload)
                if length < 126:
                    header = bytes([b1, length])
                elif length < (1 << 16):
                    header = bytes([b1, 126]) + length.to_bytes(2, "big")
                else:
                    header = bytes([b1, 127]) + length.to_bytes(8, "big")
                self.send_raw(header + payload)
            except Exception:
                self.alive = False
                return None
            return None
        if opcode == 0x8:  # close
            self.alive = False
            return None
        # ignore other opcodes
        return None

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass
        self.alive = False


hub = Hub()


class Handler(SimpleHTTPRequestHandler):
    # Ensure HTTP/1.1 for proper WebSocket upgrade
    protocol_version = "HTTP/1.1"
    def _parse_cookies(self):
        cookies = {}
        raw = self.headers.get("Cookie") or ""
        for part in raw.split(";"):
            if "=" in part:
                k, v = part.split("=", 1)
                cookies[k.strip()] = v.strip()
        return cookies

    def _ensure_sid(self):
        cookies = self._parse_cookies()
        sid = cookies.get("sid")
        if not sid:
            sid = secrets.token_urlsafe(16)
            # flag to set cookie on this response
            self._set_cookie_sid = sid
        else:
            self._set_cookie_sid = None
        self.sid = sid

    def _maybe_set_cookie(self):
        if getattr(self, "_set_cookie_sid", None):
            # HttpOnly to keep it server-side; Lax for cross-site safety
            self.send_header("Set-Cookie", f"sid={self._set_cookie_sid}; Path=/; HttpOnly; SameSite=Lax")
            self._set_cookie_sid = None

    def translate_path(self, path):
        # serve from repo httpd/ (or overridden WWW_DIR)
        self.directory = WWW_DIR
        return super().translate_path(path)

    def log_message(self, fmt, *args):
        # quiet default http logs
        print("[http]", self.address_string(), "-", fmt % args)

    def do_GET(self):
        # set or keep session id cookie
        self._ensure_sid()
        u = urlparse(self.path)
        # Claim manager by token in URL (helps when WS param is missing)
        qs = parse_qs(u.query)
        tok = (qs.get('token') or [None])[0]
        if tok and tok == hub.manager_token:
            with hub.lock:
                hub.manager_claimed = True
                hub.manager_sid = getattr(self, 'sid', None)
        if u.path == "/api/pizzas/export":
            data = {"pizzas": hub.pizzas}
            body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Disposition", "attachment; filename=\"pizze.json\"")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if u.path == "/api/state":
            # lightweight JSON state for initial render
            class Viewer:
                def __init__(self, sid):
                    self.sid = sid
                    self.is_manager = False
            data = hub.state_for(Viewer(getattr(self, "sid", None)))
            body = json.dumps({"type": "state", "data": data}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if u.path == "/clear_cookies":
            # expire cookies and redirect
            next_url = parse_qs(u.query).get('next', ['/'])[0] or '/'
            self.send_response(302)
            # expire known cookies
            self.send_header("Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax")
            self.send_header("Set-Cookie", "cookie_ok=; Path=/; Max-Age=0; SameSite=Lax")
            self.send_header("Set-Cookie", "name=; Path=/; Max-Age=0; SameSite=Lax")
            self.send_header("Location", next_url)
            self.end_headers()
            return
        if u.path == "/ws":
            self.handle_ws(u)
            return
        # fall back to static files; default to index
        if u.path == "/" or u.path == "":
            self.path = "/index.html"
        return super().do_GET()

    def end_headers(self):
        # inject Set-Cookie if needed
        self._maybe_set_cookie()
        super().end_headers()

    def handle_ws(self, u):
        if self.headers.get("Upgrade", "").lower() != "websocket":
            self.send_error(HTTPStatus.BAD_REQUEST, "Expected WebSocket upgrade")
            return
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(HTTPStatus.BAD_REQUEST, "Missing Sec-WebSocket-Key")
            return
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode("utf-8")).digest()).decode("ascii")
        # ensure we have a sid for this socket too
        self._ensure_sid()
        # Switch protocol
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        # set cookie if new
        self._maybe_set_cookie()
        self.end_headers()

        try:
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except Exception:
            pass

        conn = WSConnection(self, self.connection, self.client_address, u.path, u.query, getattr(self, "sid", None))
        with hub.lock:
            hub.clients.add(conn)

        try:
            # no blocking on HTTP handler rfile/wfile; use raw socket.
            while conn.alive:
                try:
                    text = conn.read_message()
                except socket.timeout:
                    continue
                except ConnectionError:
                    break
                except Exception as e:
                    print("[ws] unexpected read error:", e)
                    break
                if text is None:
                    break
                try:
                    msg = json.loads(text)
                except Exception:
                    continue
                try:
                    hub.handle(conn, msg)
                except Exception as e:
                    print("[ws] error handling message:", e)
                    continue
        finally:
            with hub.lock:
                hub.clients.discard(conn)
            conn.close()


def run(host="0.0.0.0", port=8000):
    print("Serving from:", WWW_DIR)
    print("Data dir:", DATA_DIR)
    print("Manager-Link (solange unbeansprucht): http://%s:%d/manage.html?token=%s" % (host if host != "0.0.0.0" else "localhost", port, hub.manager_token))
    httpd = ThreadingHTTPServer((host, port), Handler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    run()
