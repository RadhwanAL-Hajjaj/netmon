#!/usr/bin/env python3
"""A stand-in netmon board for testing the Android client on the JVM.

Answers every endpoint the firmware serves, with bodies shaped exactly like
netmon.ino builds them (field names, order, types), accepts firmware uploads
the way the ESP32 WebServer does, and simulates the restart that follows.
Test hooks:  GET /__log  (requests seen)   POST /__reset   POST /__mode?x=...
             POST /__fw?v=0.9|0.11|0.12|0.13  (0.11 adds update_max, Nearby, the Finder and the map;
                                          0.12 the Bluetooth link's endpoints, saved reports and the clock;
                                          0.13 signing in: every /api/ call but /api/auth, /api/login and
                                          /api/logout wants a session, from the update key or LOGIN_PW)
             POST /__auth?expire=1  (every session ends, as after "Sign out everywhere")
             POST /__find?idle=1    (the Finder forgets its device, as after 15 s unasked)
             POST /__find?other=1   (another page takes the Finder for another device)
"""
import hashlib, json, re, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

KEY = "test-key"
STATE = {
    "boot": time.time() - 3725,       # board has been up ~1 h
    "version": "0.9.6-status-hints",
    "restart_pending": None,          # (at, new_version)
    "log": [],
    "uploads": [],
    "mode": "normal",                 # normal | slow | down | badjson
    "nets": [
        {"ssid": "Office-WiFi", "pass": "x"},
        {"ssid": "HOME-5G", "pass": "y"},
        {"ssid": "Cafe Guest", "pass": ""},
    ],
    "active_ssid": "HOME-5G",
    "fw": "0.9",
}
LOCK = threading.Lock()

V11 = "0.11.0-finder"
V12 = "0.12.0-bluetooth"
V13 = "0.13.0-login"
LOGIN_PW = "login-pass-1"
# Firmware 0.13: sessions by token, and the wrong-password count.
AUTH = {"sessions": {}, "fails": 0, "until": 0.0}
OPEN_PATHS = ("/api/auth", "/api/login", "/api/logout")
# Firmware 0.12: the Bluetooth link, as GET /api/ble reports it, saved reports and the clock.
LINK = {"enabled": True, "bonds": 1, "open": 0.0, "code": "", "result": "", "result_at": 0.0}
CLOCK = {"boot_unix": 0}
REPORTS = {}
NEARBY = {"wifi": True, "ble": True, "ble_ready": True, "background_s": 120}
FIND = {"t": None, "prev": None, "started": 0.0, "asked": 0.0, "seq": 0, "floor": 0, "rd": [],
        "last": 0.0, "hold_from": -1e9, "hold_until": -1e9}

# Nearby: bssid, ssid, channel, rssi, security, live, joined, age_s
APS = [
    ("50:91:E3:12:34:56", "HOME-5G", 6, -38, "WPA2/WPA3", True, True, 3),
    ("52:91:E3:12:34:57", "HOME-5G", 6, -71, "WPA2/WPA3", True, False, 3),
    ("AC:84:C6:AA:00:01", "Corner \"Cafe\"", 1, -62, "WPA2", True, False, 3),
    ("AC:84:C6:AA:00:02", "", 1, -66, "WPA2", True, False, 3),
    ("E4:6F:13:00:11:22", "Guest", 11, -81, "Open", True, False, 3),
    ("A0:63:91:01:02:03", "Neighbours", 11, -90, "WPA2", False, False, 3400),
]
# Bluetooth: addr, name, vendor, company, kind, type, sure, model, rssi, age_s
BLE = [
    ("5D:21:8A:00:11:22", "", "Apple", 76, "private", "audio", 4, "AirPods Pro", -52, 1),
    ("C4:9E:11:22:33:44", "Tile", "Tile", 1660, "static", "tracker", 3, "", -80, 2),
    ("D0:03:DF:4E:12:34", "Galaxy Buds2", "Samsung", 117, "public", "audio", 2, "", -61, 4),
    ("A4:C1:38:55:66:77", "LYWSD03MMC", "", -1, "public", "sensor", 3, "", -79, 40),
    ("E2:11:09:44:21:7A", "", "", -1, "private", "unknown", 0, "", -88, 7),
]


def fwv():
    return {"0.11": 11, "0.12": 12, "0.13": 13}.get(STATE["fw"], 9)


def v13():
    return fwv() >= 13


def v11():
    return fwv() >= 11


def v12():
    return fwv() >= 12


def link_status():
    now = time.time()
    live = bool(LINK["open"]) and now - LINK["open"] < 120
    return {"link": 1, "available": True, "enabled": LINK["enabled"], "on": LINK["enabled"], "name": "netmon",
            "addr": "D4:E9:F4:12:34:58", "bonds": LINK["bonds"], "max_bonds": 3, "connected": 0, "secure": 0,
            "pairing": live, "code": LINK["code"] if live else "",
            "left_s": int(120 - (now - LINK["open"]) + 0.999) if live else 0, "result": LINK["result"],
            "result_age_s": int(now - LINK["result_at"]) if LINK["result"] else 0, "served": 0, "via": "wifi",
            **({"why": "wrong_code" if LINK["result"] == "failed" else "paired" if LINK["result"] else "",
                "why_text": "The code did not match." if LINK["result"] == "failed" else "",
                "tries_left": 2 if LINK["result"] == "failed" else 3,
                "last": {"why": "wrong_code", "text": "The code did not match.", "status": 1284,
                         "in_window": True, "age_s": 5} if LINK["result"] == "failed" else None,
                "own_code": False, "own": ""} if v13() else {})}


def report_rows(saved_unix):
    out = []
    for i, d in enumerate(json.loads(devices())):
        ago = d["last_seen_s"]
        out.append(dict(d, seen_unix=(saved_unix - ago) if saved_unix else 0,
                        first_unix=(saved_unix - 86400) if saved_unix else 0, carried=(i == 5)))
    return out


def report_of(ssid, subnet, seq, saved_unix, rows):
    return {"report": 1, "ssid": ssid, "subnet": subnet, "gateway": subnet.rsplit(".", 1)[0] + ".1",
            "gateway_mac": "98:DA:C4:11:22:33", "board_ip": "192.168.2.30", "board_mac": "D4:E9:F4:12:34:56",
            "version": V12, "seq": seq, "saved_unix": saved_unix, "saved_up_s": 3600,
            "clock": "client" if saved_unix else "none", "passes": 60, "learning": False, "count": len(rows),
            "online": sum(1 for d in rows if d["online"]), "devices": rows}


def reports_reset():
    REPORTS.clear()
    REPORTS[0] = report_of("HOME-5G", "192.168.2.0/24", 4, 1791136862, report_rows(1791136862))
    REPORTS[3] = report_of("Office-WiFi", "10.20.0.0/16", 2, 0, report_rows(0)[:3])


def reports_list():
    now = int(CLOCK["boot_unix"] + uptime()) if CLOCK["boot_unix"] else 0
    out = []
    for slot, r in sorted(REPORTS.items()):
        out.append({"slot": slot, "ssid": r["ssid"], "subnet": r["subnet"], "gateway": r["gateway"],
                    "count": r["count"], "online": r["online"], "saved_unix": r["saved_unix"],
                    "age_s": (now - r["saved_unix"]) if (now and r["saved_unix"]) else -1,
                    "bytes": len(report_file(r)), "current": r["ssid"] == STATE["active_ssid"]})
    return json.dumps({"max": 4, "every_s": 900, "clock": CLOCK["boot_unix"] != 0, "now_unix": now,
                       "free_bytes": 120000, "network": {"ssid": STATE["active_ssid"], "subnet": "192.168.2.0/24"},
                       "reports": out}, separators=(",", ":"))


def report_file(r):
    """The report as the board keeps it: the header line, one device a line, then ]}."""
    head = dict(r)
    rows = head.pop("devices")
    text = json.dumps(head, separators=(",", ":"))[:-1] + ',"devices":[\n'
    return text + ",\n".join(json.dumps(d, separators=(",", ":")) for d in rows) + "\n]}\n"


def uptime():
    return int(time.time() - STATE["boot"])


def find_live(now):
    return FIND["t"] is not None and now - FIND["asked"] < 15


def find_stop():
    if FIND["t"] is not None:
        FIND["prev"] = FIND["t"]
    FIND["t"] = None
    FIND["hold_until"] = min(FIND["hold_until"], time.time())


def find_generate(now):
    """Readings as if somebody were walking up to the device: one every 0.5 s
    (Bluetooth) or 1 s (Wi-Fi), a dB stronger every two seconds."""
    tg = FIND["t"]
    step = 0.5 if tg["type"] == "ble" else 1.0
    t = FIND["last"]
    while t + step <= now:
        t += step
        FIND["seq"] += 1
        r = int(max(-40, tg["base"] + (t - FIND["started"]) / 2))
        FIND["rd"].append((FIND["seq"], t, r))
    FIND["last"] = t
    del FIND["rd"][:-64]


def find_body(after):
    now = time.time()
    if not find_live(now):
        find_stop()
        return {"active": False, "seq": FIND["seq"]}
    tg = FIND["t"]
    find_generate(now)
    rd = [[q, int((now - t) * 1000), r] for q, t, r in FIND["rd"] if q > max(after, FIND["floor"])]
    body = {"active": True, "type": tg["type"], "addr": tg["addr"], "name": tg["name"]}
    if tg["type"] == "ble":
        body.update(kind=tg["kind"], dtype=tg["dtype"], model=tg["model"], vendor=tg["vendor"])
    else:
        body.update(ch=tg["ch"], security=tg["security"])
    body.update(state="listening", why="", for_s=int(now - FIND["started"]),
                heard_ms=int((now - FIND["rd"][-1][1]) * 1000) if FIND["rd"] else -1,
                hold_ms=max(0, int((FIND["hold_until"] - now) * 1000)), seq=FIND["seq"], readings=rd)
    return body


def find_post(j):
    if j.get("stop") is True:
        find_stop()
        return 200, find_body(FIND["seq"])
    typ, addr = j.get("type", ""), str(j.get("addr", "")).upper()
    if typ not in ("ble", "wifi"):
        return 400, {"error": "type must be wifi or ble"}
    if not re.match(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$", addr):
        return 400, {"error": "addr must be an address like AA:BB:CC:DD:EE:FF"}
    if typ == "ble" and not NEARBY["ble"]:
        return 409, {"error": "Bluetooth is switched off on the Nearby page."}
    if typ == "wifi" and not NEARBY["wifi"]:
        return 409, {"error": "Wi-Fi scanning is switched off on the Nearby page."}
    now = time.time()
    cur, prev = FIND["t"], FIND["prev"]
    if not (cur and cur["type"] == typ and cur["addr"] == addr):
        if typ == "ble":
            x = [b for b in BLE if b[0] == addr]
            if not x and not (prev and prev["addr"] == addr):
                return 404, {"error": "That device has not been heard in the last five minutes."}
            tg = prev if not x else dict(type="ble", addr=addr, name=x[0][1], kind=x[0][4], dtype=x[0][5],
                                          model=x[0][7], vendor=x[0][2], base=x[0][8])
        else:
            x = [a for a in APS if a[0] == addr]
            if not x and not (prev and prev["addr"] == addr):
                return 404, {"error": "That network has not been heard since start-up."}
            tg = prev if not x else dict(type="wifi", addr=addr, name=x[0][1], ch=x[0][2], security=x[0][4],
                                          base=x[0][3])
        FIND.update(t=tg, started=now, last=now, rd=[], floor=FIND["seq"])
    FIND["asked"] = now
    hs = j.get("hold_s")
    if isinstance(hs, int) and not isinstance(hs, bool):
        if hs == 0:
            FIND["hold_until"] = min(FIND["hold_until"], now)
        elif now - FIND["hold_from"] >= 120:
            FIND["hold_from"], FIND["hold_until"] = now, now + min(hs, 60)
    return 200, find_body(FIND["seq"])


def nearby():
    t = FIND["t"] if find_live(time.time()) else None
    wifi = [dict(bssid=b, ssid=s, ch=ch, rssi=r, security=sec, live=live, joined=j, age_s=age, known_s=age + 600)
            for b, s, ch, r, sec, live, j, age in APS] if NEARBY["wifi"] else []
    ble = [dict(addr=a, name=n, vendor=v, company=c, kind=k, type=ty, sure=su, model=mo, rssi=r, age_s=age,
                known_s=age + 300, seen=12) for a, n, v, c, k, ty, su, mo, r, age in BLE] if NEARBY["ble"] else []
    return {"version": V11, "on_lan": True, "sweeping": False, "background_s": NEARBY["background_s"],
            "finding": {"type": t["type"], "addr": t["addr"], "name": t["name"]} if t else None,
            "wifi_scan": {"enabled": NEARBY["wifi"], "state": "idle", "scans": 42, "failures": 0, "age_s": 5,
                          "took_ms": 1640},
            "ble_scan": {"enabled": NEARBY["ble"], "state": "listening", "bursts": 120, "age_s": 0, "dropped": 0},
            "wifi": wifi, "ble": ble}


def mapdata():
    return {"version": V11, "wifi": "connected", "ssid": STATE["active_ssid"], "ip": "192.168.2.30",
            "mac": "D4:E9:F4:12:34:56", "hostname": "netmon", "gateway": "192.168.2.1", "subnet": "192.168.2.0/24",
            "rssi": -35, "channel": 6, "bssid": "50:91:E3:12:34:56", "uptime_s": uptime(), "latency_valid": True,
            "latency_ms": 4, "isp": {"checked": True, "valid": True, "isp": "Example Telecom Ltd",
                                     "org": "Example Telecom", "age_s": 1300},
            "nearby_wifi": NEARBY["wifi"],
            "aps": [{"bssid": "50:91:E3:12:34:56", "ch": 6, "rssi": -38, "live": True, "joined": True, "age_s": 0},
                    {"bssid": "52:91:E3:12:34:57", "ch": 6, "rssi": -71, "live": True, "joined": False, "age_s": 9}]}


def health():
    # Built by hand in the firmware; same field order here.
    return ('{"status":"ok","version":"%s","wifi":"connected","ssid":"%s","ip":"192.168.2.30",'
            '"gateway":"192.168.2.1","subnet":"192.168.2.0/24","rssi":-35,"uptime_s":%d,'
            '"free_heap":181234,"sweepable":true,"devices":6,"scan_passes":61,"scan_remaining":0,'
            '"last_pass_ms":6512,"pass_seen":5,"pass_merges":212,"arp_cache":10,"latency_valid":true,'
            '"latency_ms":4,"latency_age_s":12,"dhcp_packets":3,"events":9,"baseline_open":false,'
            '"baseline_anchored":true,"baseline_closes_in_s":0,"names_known":4%s}'
            % (version(), STATE["active_ssid"], uptime(),
               ',"mac":"D4:E9:F4:12:34:56","ble_link":%s,"clock":%s' % (
                   str(LINK["enabled"]).lower(), str(CLOCK["boot_unix"] != 0).lower()) if v12() else ""))


def version():
    if STATE["version"] != "0.9.6-status-hints":
        return STATE["version"]
    return V13 if v13() else V12 if v12() else V11 if v11() else STATE["version"]


def devices():
    rows = [
        dict(mac="D4:E9:F4:12:34:56", ip="192.168.2.30", hostname="netmon", vendor="Espressif Inc.",
             status="known", randomised=False, self=True, online=True, last_seen_s=3, up_s=3600),
        dict(mac="98:DA:C4:11:22:33", ip="192.168.2.1", hostname="", vendor="TP-LINK TECHNOLOGIES CO.,LTD.",
             status="known", randomised=False, self=False, online=True, last_seen_s=20, up_s=3600),
        dict(mac="24:5E:BE:44:55:66", ip="192.168.2.11", hostname="NAS", vendor="QNAP Systems, Inc.",
             status="known", randomised=False, self=False, online=True, last_seen_s=20, up_s=3500),
        dict(mac="DA:A1:19:77:88:99", ip="192.168.2.45", hostname="Pixel-7", vendor="",
             status="private", randomised=True, self=False, online=True, last_seen_s=40, up_s=900),
        dict(mac="7C:9E:BD:01:02:03", ip="192.168.2.77", hostname="", vendor="Espressif Inc.",
             status="unknown", randomised=False, self=False, online=True, last_seen_s=15, up_s=300),
        dict(mac="00:11:32:AA:BB:CC", ip="192.168.2.20", hostname="Desk \"PC\"", vendor="",
             status="known", randomised=False, self=False, online=False, last_seen_s=7300, up_s=0),
    ]
    out = []
    for d in rows:  # hand-built, with the firmware's escaping of quote and backslash
        hn = d["hostname"].replace("\\", "\\\\").replace('"', '\\"')
        vd = d["vendor"].replace("\\", "\\\\").replace('"', '\\"')
        out.append('{"mac":"%s","ip":"%s","hostname":"%s","vendor":"%s","status":"%s","randomised":%s,'
                   '"self":%s,"online":%s,"last_seen_s":%d,"up_s":%d}' % (
                       d["mac"], d["ip"], hn, vd, d["status"], str(d["randomised"]).lower(),
                       str(d["self"]).lower(), str(d["online"]).lower(), d["last_seen_s"], d["up_s"]))
    return "[" + ",".join(out) + "]"


def events():
    u = uptime()
    ev = [
        (u - 5, "scan_done", "D4:E9:F4:12:34:56", "192.168.2.30", "ARP sweep finished"),
        (u - 12, "scan", "D4:E9:F4:12:34:56", "192.168.2.30", "ARP sweep started"),
        (u - 300, "seen", "7C:9E:BD:01:02:03", "192.168.2.77", "device first seen"),
        (u - 900, "hostname", "DA:A1:19:77:88:99", "192.168.2.45", "Pixel-7"),
        (u - 905, "seen", "DA:A1:19:77:88:99", "192.168.2.45", "private device first seen"),
        (u - 2000, "offline", "00:11:32:AA:BB:CC", "192.168.2.20", "device went offline"),
        (60, "seen", "24:5E:BE:44:55:66", "192.168.2.11", "device first seen"),
    ]
    return "[" + ",".join('{"at_s":%d,"type":"%s","mac":"%s","ip":"%s","text":"%s"}' % e for e in ev) + "]"


def config():
    act = STATE["active_ssid"]
    shown = next((n for n in STATE["nets"] if n["ssid"] == act), STATE["nets"][0])
    return ('{"version":"%s","ssid":"%s","has_password":%s,"networks":%d,"use_dhcp":true,'
            '"ip":"0.0.0.0","mask":"0.0.0.0","gw":"0.0.0.0","dns":"0.0.0.0","scan_interval_s":60,'
            '"probe_interval_s":30,"offline_after_s":180,"learning_window_s":600,%s'
            '"active":{"ip":"192.168.2.30","mask":"255.255.255.0","gw":"192.168.2.1","dns":"192.168.2.1",'
            '"mac":"D4:E9:F4:12:34:56","ssid":"%s","rssi":-35,"source":"dhcp"}}'
            % (version(), shown["ssid"], "true" if shown["pass"] else "false",
               len(STATE["nets"]), '"update_max":1966080,' if v11() else "", act))


def networks():
    boots = {"Office-WiFi": ("failed", 3120, 205), "HOME-5G": ("joined", 2410, 0), "Cafe Guest": ("not_tried", 0, 0)}
    out = []
    for i, n in enumerate(STATE["nets"]):
        b = boots.get(n["ssid"], ("not_tried", 0, 0))
        out.append({"order": i + 1, "ssid": n["ssid"], "has_password": bool(n["pass"]),
                    "active": n["ssid"] == STATE["active_ssid"], "boot": b[0], "boot_ms": b[1], "reason": b[2]})
    return json.dumps(out, separators=(",", ":"))


def dhcp():
    return json.dumps({"listener": "listening", "received": 14, "capture_packets": 3, "valid": True,
                       "client_mac": "DA:A1:19:77:88:99", "assigned_ip": "192.168.2.45",
                       "server_ip": "192.168.2.1", "lease_s": 7200, "message_type": "REQUEST",
                       "hostname": "Pixel-7", "last_seen_s": 905,
                       "local": {"ip": "192.168.2.30", "mask": "255.255.255.0", "gateway": "192.168.2.1",
                                 "dns": "192.168.2.1", "mode": "dhcp", "hostname": "netmon"}},
                      separators=(",", ":"))


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json", headers=()):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        self.close_connection = True

    def _token(self):
        a = self.headers.get("Authorization") or ""
        return a[7:].strip() if a.lower().startswith("bearer ") else ""

    def _signed_in(self):
        return self._token() in AUTH["sessions"]

    def _needs_login(self, path):
        """0.13: answers 401 for an /api/ call without a session; True when it did."""
        if not v13() or not path.startswith("/api/") or path in OPEN_PATHS or self._signed_in():
            return False
        self._send(401, '{"error":"Sign in to the monitor first.","login":true,"netmon":true}',
                   headers=(("X-Netmon-Login", "required"),))
        return True

    def _auth_get(self):
        if not v13():
            return self._send(404, "not found", "text/plain")
        out = {"netmon": True, "name": "netmon", "version": version(), "id": "D4:E9:F4:12:34:56", "login": True,
               "signed_in": self._signed_in(), "remember_days": 30}
        if self._signed_in():
            out.update(own_password=True, remembered=AUTH["sessions"][self._token()], sessions=len(AUTH["sessions"]))
        out["via"] = "wifi"
        return self._send(200, json.dumps(out))

    def _login(self, body):
        if time.time() < AUTH["until"]:
            wait = int(AUTH["until"] - time.time() + 0.999)
            return self._send(429, '{"error":"Too many wrong passwords. Try again in %d seconds.","retry_s":%d}' % (wait, wait))
        try:
            doc = json.loads(body.decode("utf-8") or "{}")
        except Exception:
            return self._send(400, '{"error":"request body is not valid JSON"}')
        if doc.get("password") not in (KEY, LOGIN_PW):
            with LOCK:
                AUTH["fails"] += 1
                if AUTH["fails"] > 5:
                    AUTH["until"] = time.time() + 30
            return self._send(401, '{"error":"That password is not right.","wrong":true}')
        tok = "%032x" % int.from_bytes(hashlib.sha256(("%f" % time.time()).encode() + body).digest()[:16], "big")
        with LOCK:
            AUTH["fails"] = 0
            AUTH["sessions"][tok] = doc.get("remember") is True
            if isinstance(doc.get("unix"), int) and not CLOCK["boot_unix"]:
                CLOCK["boot_unix"] = doc["unix"] - uptime()
        return self._send(200, json.dumps({"status": "signed in", "token": tok, "remember": doc.get("remember") is True,
                                           "days": 30 if doc.get("remember") is True else 0}))

    def _gate(self):
        with LOCK:
            rp = STATE["restart_pending"]
            if rp and time.time() >= rp[0]:
                STATE["boot"] = rp[0] + 1.5  # comes back 1.5 s after going down
                STATE["version"] = rp[1]
                STATE["restart_pending"] = None
                # An image of 0.13 or later comes back asking to sign in.
                if re.match(r"0\.(1[3-9]|[2-9]\d)\.", rp[1]):
                    STATE["fw"] = "0.13"
            if time.time() < STATE["boot"]:
                return False  # still "restarting"
        mode = STATE["mode"]
        if mode == "down":
            return False
        if mode == "slow":
            time.sleep(3)
        return True

    def do_GET(self):
        u = urlparse(self.path)
        with LOCK:
            STATE["log"].append(("GET", self.path, dict(self.headers)))
        if u.path == "/__log":
            return self._send(200, json.dumps([[m, p] for (m, p, h) in STATE["log"]]))
        if u.path == "/__uploads":
            return self._send(200, json.dumps(STATE["uploads"]))
        if u.path == "/__config":
            return self._send(200, json.dumps(STATE.get("last_config")))
        if not self._gate():
            self.close_connection = True
            try:
                self.connection.close()
            except Exception:
                pass
            return
        if u.path == "/api/auth":
            return self._auth_get()
        if self._needs_login(u.path):
            return
        if STATE["mode"] == "badjson" and u.path.startswith("/api/"):
            return self._send(200, "<html>not json</html>", "text/html")
        if STATE["mode"] == "old" and u.path in ("/api/networks", "/api/dhcp"):
            return self._send(404, "not found", "text/plain")
        routes = {"/api/health": health, "/api/devices": devices, "/api/events": events,
                  "/api/config": config, "/api/networks": networks, "/api/dhcp": dhcp}
        if u.path in routes:
            return self._send(200, routes[u.path]())
        if v11():
            if u.path == "/api/nearby":
                # The firmware sends this one in chunks.
                return self._send_chunked(json.dumps(nearby(), separators=(",", ":")))
            if u.path == "/api/nearby/config":
                return self._send(200, json.dumps(NEARBY))
            if u.path == "/api/nearby/find":
                with LOCK:
                    if FIND["t"] is not None:
                        if find_live(time.time()):
                            FIND["asked"] = time.time()
                        else:
                            find_stop()
                    after = int(parse_qs(u.query).get("after", ["0"])[0] or 0)
                    return self._send(200, json.dumps(find_body(after)))
            if v12() and u.path == "/api/ble":
                return self._send(200, json.dumps(link_status()))
            if v12() and u.path == "/api/reports":
                return self._send(200, reports_list())
            if v12() and u.path == "/api/reports/get":
                slot = parse_qs(u.query).get("slot", [""])[0]
                if not slot.isdigit() or int(slot) not in REPORTS:
                    return self._send(404, '{"error":"No report is saved there."}')
                return self._send(200, report_file(REPORTS[int(slot)]))
            if u.path == "/api/map":
                return self._send(200, json.dumps(mapdata()))
        if u.path == "/api/latency":
            return self._send(200, '{"valid":true,"rtt_ms":4,"checked_s":%d,"age_s":12,"failures":2,"method":"tcp"}'
                              % (uptime() - 12))
        if u.path == "/api/isp":
            forced = "force" in parse_qs(u.query)
            return self._send(200, json.dumps({
                "version": STATE["version"], "valid": True, "ip": "203.0.113.10", "isp": "Example Telecom Ltd",
                "org": "Example Telecom", "asn": "AS64500 Example Telecom", "city": "Lisbon", "region": "Lisbon",
                "country": "Portugal", "timezone": "Europe/Lisbon", "error": "", "rtt_ms": 88,
                "checked_age_s": 0 if forced else 1800, "ever_checked": True, "gateway": "192.168.2.1",
                "gateway_mac": "98:DA:C4:11:22:33", "gateway_vendor": "TP-LINK TECHNOLOGIES CO.,LTD."},
                separators=(",", ":")))
        if u.path == "/api/scan":
            time.sleep(1.2)
            return self._send(200, '[{"ssid":"HOME-5G","rssi":-38},{"ssid":"Office-WiFi","rssi":-71},'
                                   '{"ssid":"Say \\"hi\\"","rssi":-80}]')
        return self._send(404, "not found", "text/plain")

    def _send_chunked(self, body):
        data = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()
        for i in range(0, len(data), 700):
            part = data[i:i + 700]
            self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
        self.wfile.write(b"0\r\n\r\n")
        self.close_connection = True

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def do_POST(self):
        u = urlparse(self.path)
        with LOCK:
            STATE["log"].append(("POST", self.path, dict(self.headers)))
        if u.path == "/__reset":
            self._body()
            with LOCK:
                STATE.update(boot=time.time() - 3725, version="0.9.6-status-hints", restart_pending=None,
                             log=[], uploads=[], mode="normal", active_ssid="HOME-5G", fw="0.9",
                             nets=[{"ssid": "Office-WiFi", "pass": "x"}, {"ssid": "HOME-5G", "pass": "y"},
                                   {"ssid": "Cafe Guest", "pass": ""}])
                NEARBY.update(wifi=True, ble=True, ble_ready=True, background_s=120)
                LINK.update(enabled=True, bonds=1, open=0.0, code="", result="", result_at=0.0)
                AUTH.update(sessions={}, fails=0, until=0.0)
                CLOCK.update(boot_unix=0)
                reports_reset()
                FIND.update(t=None, prev=None, started=0.0, asked=0.0, rd=[], last=0.0,
                            hold_from=-1e9, hold_until=-1e9)
            return self._send(200, "{}")
        if u.path == "/__fw":
            self._body()
            STATE["fw"] = parse_qs(u.query).get("v", ["0.9"])[0]
            return self._send(200, "{}")
        if u.path == "/__find":
            self._body()
            q = parse_qs(u.query)
            with LOCK:
                if "idle" in q:
                    FIND["asked"] = time.time() - 60
                if "other" in q:
                    a = BLE[3]
                    FIND.update(t=dict(type="ble", addr=a[0], name=a[1], kind=a[4], dtype=a[5], model=a[7],
                                       vendor=a[2], base=a[8]), started=time.time(), last=time.time(),
                                asked=time.time(), rd=[], floor=FIND["seq"])
            return self._send(200, "{}")
        if u.path == "/__mode":
            self._body()
            STATE["mode"] = parse_qs(u.query).get("x", ["normal"])[0]
            return self._send(200, "{}")
        if u.path == "/__auth":
            self._body()
            with LOCK:
                AUTH["sessions"].clear()
            return self._send(200, "{}")
        if not self._gate():
            self.close_connection = True
            return
        if v13() and u.path == "/api/login":
            return self._login(self._body())
        if v13() and u.path == "/api/logout":
            self._body()
            with LOCK:
                AUTH["sessions"].pop(self._token(), None)
            return self._send(200, '{"status":"signed out"}')
        if self._needs_login(u.path):
            self._body()
            return
        key = self.headers.get("X-Netmon-Key")
        if u.path == "/api/update/check":
            self._body()
            if key != KEY:
                return self._send(401, '{"error":"invalid update key"}')
            return self._send(200, '{"status":"ok"}')
        if u.path == "/api/update":
            return self._update(key)
        body = self._body()
        if u.path == "/api/reboot":
            self._send(200, '{"status":"restarting"}')
            with LOCK:
                STATE["restart_pending"] = (time.time() + 0.25, STATE["version"])
            return
        if u.path == "/api/config":
            try:
                doc = json.loads(body.decode("utf-8"))
            except Exception:
                return self._send(400, '{"error":"request body is not valid JSON"}')
            with LOCK:
                STATE["last_config"] = doc
            if not doc.get("ssid"):
                return self._send(400, '{"error":"network name is required"}')
            if len(doc["ssid"]) > 32:
                return self._send(400, '{"error":"network name is over 32 characters"}')
            return self._send(200, '{"status":"saved"}')
        if v11() and u.path == "/api/nearby/scan":
            return self._send(200, '{"status":"queued","wifi":%s,"ble":%s}' % (
                str(NEARBY["wifi"]).lower(), str(NEARBY["ble"] and NEARBY["ble_ready"]).lower()))
        if v11() and u.path == "/api/nearby/config":
            try:
                doc = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                return self._send(400, '{"error":"request body is not valid JSON"}')
            for k in ("wifi", "ble"):
                if k in doc and not isinstance(doc[k], bool):
                    return self._send(400, '{"error":"%s must be true or false"}' % k)
            if "background_s" in doc:
                v = doc["background_s"]
                if isinstance(v, bool) or not isinstance(v, int) or not (v == 0 or 30 <= v <= 3600):
                    return self._send(400, '{"error":"the background interval must be 0, or 30 to 3600 seconds"}')
            with LOCK:
                for k in ("wifi", "ble", "background_s"):
                    if k in doc:
                        NEARBY[k] = doc[k]
            return self._send(200, json.dumps(NEARBY))
        if v11() and u.path == "/api/nearby/find":
            try:
                doc = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                return self._send(400, '{"error":"request body is not valid JSON"}')
            with LOCK:
                code, out = find_post(doc)
            return self._send(code, json.dumps(out))
        if v12() and u.path in ("/api/ble", "/api/ble/pair", "/api/ble/forget", "/api/clock",
                                 "/api/reports/save", "/api/reports/delete"):
            try:
                doc = json.loads(body.decode("utf-8") or "{}")
            except Exception:
                return self._send(400, '{"error":"request body is not valid JSON"}')
            with LOCK:
                return self._post12(u.path, doc)
        if u.path == "/api/networks/forget":
            try:
                doc = json.loads(body.decode("utf-8"))
            except Exception:
                return self._send(400, '{"error":"request body is not valid JSON"}')
            ssid = doc.get("ssid", "")
            if not ssid:
                return self._send(400, '{"error":"network name is required"}')
            if ssid == STATE["active_ssid"]:
                return self._send(400, '{"error":"the board is using this network right now"}')
            with LOCK:
                before = len(STATE["nets"])
                STATE["nets"] = [n for n in STATE["nets"] if n["ssid"] != ssid]
                if len(STATE["nets"]) == before:
                    return self._send(404, '{"error":"that network is not remembered"}')
            return self._send(200, '{"status":"forgotten"}')
        return self._send(404, "not found", "text/plain")

    def _post12(self, path, doc):
        if path == "/api/ble":
            if not isinstance(doc.get("enabled"), bool):
                return self._send(400, '{"error":"enabled must be true or false"}')
            LINK["enabled"] = doc["enabled"]
            if not LINK["enabled"]:
                LINK["open"] = 0.0
            return self._send(200, json.dumps(link_status()))
        if path == "/api/ble/pair":
            if doc.get("stop") is True:
                LINK["open"] = 0.0
            elif not LINK["enabled"]:
                return self._send(409, '{"error":"The Bluetooth link is switched off. Switch it on first."}')
            else:
                if not (LINK["open"] and time.time() - LINK["open"] < 120):
                    LINK["code"] = "042517"
                LINK["open"] = time.time()
                LINK["result"] = ""
            return self._send(200, json.dumps(link_status()))
        if path == "/api/ble/forget":
            LINK["bonds"] = 0
            return self._send(200, json.dumps(link_status()))
        if path == "/api/clock":
            t = doc.get("unix")
            if isinstance(t, bool) or not isinstance(t, int):
                return self._send(400, '{"error":"unix must be the time in seconds since 1970"}')
            if not 1704067200 <= t <= 4102444800:
                return self._send(400, '{"error":"that time is not plausible"}')
            CLOCK["boot_unix"] = t - uptime()
            return self._send(200, json.dumps({"clock": True, "unix": t, "source": "client"}))
        if path == "/api/reports/save":
            now = int(CLOCK["boot_unix"] + uptime()) if CLOCK["boot_unix"] else 0
            slot = next((k for k, r in REPORTS.items() if r["ssid"] == STATE["active_ssid"]), None)
            if slot is None:
                slot = next(k for k in range(4) if k not in REPORTS)
            seq = max([r["seq"] for r in REPORTS.values()] + [0]) + 1
            REPORTS[slot] = report_of(STATE["active_ssid"], "192.168.2.0/24", seq, now, report_rows(now))
            return self._send(200, reports_list())
        if path == "/api/reports/delete":
            if doc.get("slot") not in REPORTS:
                return self._send(404, '{"error":"No report is saved there."}')
            del REPORTS[doc["slot"]]
            return self._send(200, reports_list())
        return self._send(404, "not found", "text/plain")

    def _update(self, key):
        ctype = self.headers.get("Content-Type", "")
        m = re.search(r'boundary="?([^";]+)"?', ctype)
        n = int(self.headers.get("Content-Length") or 0)
        data = self.rfile.read(n) if n else b""
        if key != KEY:
            return self._send(401, '{"error":"invalid update key"}')
        if not m:
            return self._send(400, '{"error":"no firmware file in the request"}')
        b = m.group(1).encode()
        start = data.find(b"--" + b)
        hdr_end = data.find(b"\r\n\r\n", start)
        head = data[start:hdr_end].decode("latin1")
        end = data.find(b"\r\n--" + b + b"--", hdr_end)
        if start < 0 or hdr_end < 0 or end < 0 or 'name="firmware"' not in head or "filename=" not in head:
            return self._send(400, '{"error":"no firmware file in the request"}')
        payload = data[hdr_end + 4:end]
        fn = re.search(r'filename="([^"]*)"', head).group(1)
        info = {"filename": fn, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
                "content_length": n, "part_head": head}
        with LOCK:
            STATE["uploads"].append(info)
        if not payload or payload[0] != 0xE9:
            return self._send(500, '{"error":"firmware update failed: Wrong Magic Byte"}')
        vm = re.findall(rb"\x00(\d+\.\d+\.\d+-[a-z][a-z0-9-]{1,40})\x00", payload)
        newv = vm[0].decode() if vm else STATE["version"]
        self._send(200, '{"status":"updated","restarting":true}')
        with LOCK:
            STATE["restart_pending"] = (time.time() + 0.5, newv)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
    reports_reset()
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.daemon_threads = True
    print("mock board on", port, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
