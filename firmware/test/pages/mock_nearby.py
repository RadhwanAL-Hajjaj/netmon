#!/usr/bin/env python3
"""A stand-in netmon 0.11.0 board for testing the pages in a browser.

Serves the pages straight out of pages.h and answers the endpoints they call
with bodies shaped like netmon.ino builds them. /api/nearby is simulated: a
house's worth of Wi-Fi networks and Bluetooth devices whose signals drift,
some of which come and go, so the radars, the trend arrows and the live log
have something to show.
The Finder is simulated too: readings at the device's own advertising rate,
getting stronger as if somebody were walking up to it, none for a few seconds
each minute while the "sweep" runs, and during a turn set up through the test
hook, strongest when facing the given direction, as a body's shadow makes it.
Test hooks: POST /__nearby?ble=0|1&wifi=0|1&unavailable=0|1  GET /__log
            POST /__find?turn_in_ms=&turn_s=&dir=&walk=  GET /__find
"""
import json, math, random, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from extract_pages import extract

PAGES_H = sys.argv[1] if len(sys.argv) > 1 else __file__.rsplit('/', 1)[0] + '/../../netmon/src/hw/pages.h'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8765
VERSION = "0.11.0-finder"
BOOT = time.time() - 5400
LOCK = threading.Lock()
LOG = []
CFG = {"wifi": True, "ble": True, "ble_ready": True, "background_s": 120}
SIM = {"unavailable": False, "requested": False}


def up():
    return int(time.time() - BOOT)


# Wi-Fi: bssid, ssid, channel, base rssi, security, comes and goes?
APS = [
    ("50:91:E3:12:34:56", "HOME-2.4", 6, -38, "WPA2/WPA3", False),
    ("52:91:E3:12:34:57", "HOME-2.4", 6, -71, "WPA2/WPA3", False),   # extender: same name
    ("AC:84:C6:AA:00:01", "Corner Cafe", 1, -62, "WPA2", False),
    ("AC:84:C6:AA:00:02", "", 1, -66, "WPA2", False),                # hidden
    ("C8:3A:35:10:20:30", "Tenda_5A2B", 11, -74, "WPA/WPA2", False),
    ("E4:6F:13:00:11:22", "D-Link guest", 11, -81, "Open", False),
    ("00:1D:7E:33:44:55", "Linksys01234", 3, -84, "WPA2", False),
    ("F0:9F:C2:66:77:88", "UBNT-Office", 9, -77, "WPA2-Enterprise", False),
    ("B0:BE:76:11:99:00", "TP-Link_99", 4, -86, "WPA2", True),
    ("38:6B:1C:22:00:11", "DIRECT-7C-HP OfficeJet", 6, -69, "WPA2", True),
    ("9C:53:22:33:44:01", "Galaxy A54 \"hotspot\"", 6, -58, "WPA2", True),
    ("10:FE:ED:00:00:09", "<script>alert(1)</script>", 13, -88, "WEP", False),
]
GONE = [("A0:63:91:01:02:03", "Neighbours-5G", 36, -90, "WPA2", 3400),
        ("74:DA:38:AB:CD:EF", "AndroidAP_4411", 11, -79, "WPA2", 900)]

# Bluetooth: addr, name, vendor, company, kind, type, sure, model, base rssi, comes and goes?
BLE = [
    ("5D:21:8A:00:11:22", "", "Apple", 76, "private", "audio", 4, "AirPods Pro", -52, False),
    ("6E:44:01:9A:BC:DE", "", "Apple", 76, "private", "tracker", 4, "Find My tracker", -77, True),
    ("4A:12:F0:33:21:10", "", "Apple", 76, "private", "unknown", 0, "", -66, False),
    ("7B:9C:22:10:44:55", "", "Apple", 76, "private", "unknown", 0, "", -83, True),
    ("D0:03:DF:4E:12:34", "Galaxy Buds2 (12AB)", "Samsung", 117, "public", "audio", 2, "", -61, False),
    ("C1:44:22:FA:01:99", "Forerunner 255", "Garmin", 135, "static", "watch", 2, "", -70, False),
    ("E2:11:09:44:21:7A", "", "", -1, "private", "unknown", 0, "", -88, False),
    ("F3:21:44:AB:01:22", "", "Microsoft", 6, "private", "computer", 4, "Windows laptop", -64, False),
    ("C4:9E:11:22:33:44", "Tile", "Tile", 1660, "static", "tracker", 3, "", -80, True),
    ("80:E1:26:12:34:56", "Flipper Uw1n1p", "", 3625, "public", "flipper", 4, "Flipper Zero", -73, True),
    ("A4:C1:38:55:66:77", "LYWSD03MMC", "", -1, "public", "sensor", 3, "", -79, False),
    ("CC:88:26:11:00:55", "[TV] Samsung Q60", "Samsung", 117, "public", "tv", 2, "", -75, False),
    ("D8:3A:DD:44:00:12", "MX Master 3", "Logitech", 474, "static", "input", 2, "", -67, False),
    ("ED:AA:01:22:33:01", "Mi Smart Band 6", "Huami (Amazfit)", 343, "static", "watch", 3, "", -82, True),
    ("5C:11:22:33:44:99", "", "", -1, "private", "unknown", 0, "", -91, True),
]


def wave(seed, period=37.0, amp=4.0):
    t = time.time()
    return amp * math.sin(t / period * 2 * math.pi + seed)


def present(i, period=70.0):
    """Comes and goes: present about two thirds of the time."""
    return math.sin(time.time() / period * 2 * math.pi + i * 1.7) > -0.4


def nearby():
    now = up()
    t = time.time()
    wifi = []
    for i, (b, s, ch, r, sec, flaky) in enumerate(APS):
        live = (not flaky) or present(i)
        wifi.append(dict(bssid=b, ssid=s, ch=ch, rssi=int(r + wave(i)), security=sec,
                         live=live, joined=(i == 0), age_s=(3 if live else 140 + i),
                         known_s=4000 - i * 100))
    for b, s, ch, r, sec, age in GONE:
        wifi.append(dict(bssid=b, ssid=s, ch=ch, rssi=r, security=sec, live=False,
                         joined=False, age_s=age, known_s=age + 600))
    ble = []
    for i, (a, n, v, c, kind, typ, sure, model, r, flaky) in enumerate(BLE):
        if flaky and not present(i + 20, 55.0):
            continue
        ble.append(dict(addr=a, name=n, vendor=v, company=c, kind=kind, type=typ, sure=sure,
                        model=model, rssi=int(r + wave(i + 5, 23.0, 5.0)),
                        age_s=int((t * 7 + i * 13) % 9), known_s=300 + i * 40, seen=10 + i))
    # A device far enough to need a long range.
    fl = find_live(time.time())
    tg = FIND["t"] if fl else None
    finding = {"type": tg["type"], "addr": tg["addr"], "name": tg["name"]} if tg else None
    return {
        "version": VERSION, "on_lan": True, "sweeping": False,
        "background_s": CFG["background_s"], "finding": finding,
        "wifi_scan": {"enabled": CFG["wifi"],
                      "state": ("finding" if tg["type"] == "wifi" else "paused") if tg else
                               "scanning" if int(t) % 15 < 2 else "idle",
                      "scans": 120 + int(t / 15) % 1000, "failures": 0,
                      "age_s": int(t) % 15, "took_ms": 1640},
        "ble_scan": {"enabled": CFG["ble"],
                     "state": "unavailable" if SIM["unavailable"] else
                              ("finding" if tg["type"] == "ble" else "paused") if tg else
                              ("listening" if int(t) % 8 < 5 else "idle"),
                     "bursts": 400 + int(t / 8) % 1000, "age_s": 0 if int(t) % 8 < 5 else int(t) % 8 - 5,
                     "dropped": 0},
        "wifi": wifi if CFG["wifi"] else [],
        "ble": ble if CFG["ble"] and not SIM["unavailable"] else [],
    }


# ---- the Finder ----
FIND = {"t": None, "prev": None, "started": 0.0, "asked": 0.0, "seq": 0, "rd": [], "last": 0.0,
        "turn": None, "walk": 0.12, "hold_from": -1e9, "hold_until": -1e9}


def find_stop():
    if FIND["t"] is not None:
        FIND["prev"] = FIND["t"]
    FIND["t"] = None
    FIND["hold_until"] = min(FIND["hold_until"], time.time())


def paused_at(t):
    if FIND["hold_from"] <= t < FIND["hold_until"]:
        return False                # a turn holds the sweep off
    return int(t) % 60 < 4          # the sweep, once a minute


def find_rssi(t):
    tg = FIND["t"]
    d0 = 10 ** ((-59 - tg["base"]) / 22.0)
    d = max(0.35, d0 - FIND["walk"] * (t - FIND["started"]))
    r = -59 - 22 * math.log10(d)
    tr = FIND["turn"]
    if tr and tr["start"] <= t <= tr["start"] + tr["T"]:
        ang = (t - tr["start"]) / tr["T"] * 360.0
        r += 7 * math.cos(math.radians(ang - tr["dir"])) - 3
    r += random.gauss(0, 2.5)
    return int(round(max(-100, min(-20, r))))


def find_generate(now):
    tg = FIND["t"]
    t = FIND["last"]
    while True:
        t += random.uniform(0.7, 1.3) / tg["rate"]
        if t > now:
            break
        FIND["last"] = t
        if paused_at(t):
            continue
        FIND["seq"] += 1
        FIND["rd"].append((FIND["seq"], t, find_rssi(t)))
    del FIND["rd"][:-64]


def find_live(now):
    return FIND["t"] is not None and now - FIND["asked"] < 15


def find_body(after):
    now = time.time()
    if not find_live(now):
        find_stop()
        return {"active": False, "seq": FIND["seq"]}
    tg = FIND["t"]
    find_generate(now)
    off = not (CFG["ble"] if tg["type"] == "ble" else CFG["wifi"])
    state, why = ("off", "") if off else (("paused", "sweep") if paused_at(now) else ("listening", ""))
    body = {"active": True, "type": tg["type"], "addr": tg["addr"], "name": tg["name"],
            "state": state, "why": why, "for_s": int(now - FIND["started"]),
            "heard_ms": int((now - FIND["rd"][-1][1]) * 1000) if FIND["rd"] else -1,
            "seq": FIND["seq"], "hold_ms": max(0, int((FIND["hold_until"] - now) * 1000)),
            "readings": [[q, int((now - t) * 1000), r] for q, t, r in FIND["rd"] if q > after]}
    if tg["type"] == "ble":
        body.update(kind=tg["kind"], dtype=tg["dtype"], model=tg["model"], vendor=tg["vendor"])
    else:
        body.update(ch=tg["ch"], security=tg["security"])
    return body


def find_start(j):
    if j.get("stop") is True:
        find_stop()
        return 200, find_body(FIND["seq"])
    typ, addr = j.get("type"), str(j.get("addr", "")).upper()
    if typ not in ("ble", "wifi"):
        return 400, {"error": "type must be wifi or ble"}
    if typ == "ble" and not CFG["ble"]:
        return 409, {"error": "Bluetooth is off, so nothing can be found by it."}
    if typ == "wifi" and not CFG["wifi"]:
        return 409, {"error": "Wi-Fi scanning is off, so nothing can be found by it."}
    now = time.time()
    cur, prev = FIND["t"], FIND["prev"]
    # As the firmware: the device being found carries on, one the Finder had
    # last starts again from what it kept, even when the lists have lost it.
    if cur and cur["addr"] == addr and cur["type"] == typ:
        FIND["asked"] = now
        hs = j.get("hold_s")
        if isinstance(hs, int) and hs >= 0:
            if hs == 0:
                FIND["hold_until"] = min(FIND["hold_until"], now)
            elif now - FIND["hold_from"] >= 120:
                FIND["hold_from"], FIND["hold_until"] = now, now + min(hs, 60)
        return 200, find_body(FIND["seq"])
    d = nearby()
    known = any(b["addr"] == addr for b in d["ble"]) if typ == "ble" else \
        any(w["bssid"] == addr for w in d["wifi"])
    if not known and prev and prev["addr"] == addr and prev["type"] == typ:
        FIND.update(t=prev, started=now, rd=[], last=now, asked=now)
        return 200, find_body(FIND["seq"])
    if typ == "ble":
        x = [b for b in d["ble"] if b["addr"] == addr]
        if not x:
            return 404, {"error": "That device has not been heard in the last five minutes."}
        x = x[0]
        rate = 0.5 if x["type"] == "tracker" else 3.0
        tg = dict(type="ble", addr=addr, name=x["name"], kind=x["kind"], dtype=x["type"],
                  model=x["model"], vendor=x["vendor"], base=x["rssi"], rate=rate)
    else:
        x = [w for w in d["wifi"] if w["bssid"] == addr]
        if not x:
            return 404, {"error": "That network has not been heard since start-up."}
        x = x[0]
        tg = dict(type="wifi", addr=addr, name=x["ssid"], ch=x["ch"], security=x["security"],
                  base=x["rssi"], rate=1.2)
    FIND.update(t=tg, started=now, rd=[], last=now, asked=now)
    return 200, find_body(FIND["seq"])


def mapdata():
    return {"version": VERSION, "wifi": "connected", "ssid": "HOME-2.4", "ip": "192.168.2.27",
            "mac": "D4:E9:F4:12:34:56", "hostname": "netmon", "gateway": "192.168.2.1",
            "subnet": "192.168.2.0/24", "rssi": -38, "channel": 6, "bssid": "50:91:E3:12:34:56",
            "uptime_s": up(), "latency_valid": True, "latency_ms": 4,
            "isp": {"checked": True, "valid": True, "isp": "Example Telecom Ltd", "org": "Example Telecom",
                    "age_s": 1300},
            "nearby_wifi": CFG["wifi"],
            "aps": [{"bssid": "50:91:E3:12:34:56", "ch": 6, "rssi": -38, "live": True,
                     "joined": True, "age_s": 0},
                    {"bssid": "52:91:E3:12:34:57", "ch": 6, "rssi": -71, "live": True,
                     "joined": False, "age_s": 9}] if CFG["wifi"] else
                   [{"bssid": "50:91:E3:12:34:56", "ch": 6, "rssi": -38, "live": True,
                     "joined": True, "age_s": 0}]}


def health():
    return {"status": "ok", "version": VERSION, "wifi": "connected", "ssid": "HOME-2.4",
            "ip": "192.168.2.27", "gateway": "192.168.2.1", "subnet": "192.168.2.0/24",
            "rssi": -38, "uptime_s": up(), "free_heap": 151234, "sweepable": True,
            "devices": len(LAN), "scan_passes": 88, "scan_remaining": 0, "last_pass_ms": 6512,
            "pass_seen": sum(1 for d in LAN if d[5]), "pass_merges": 120, "arp_cache": 10, "latency_valid": True,
            "latency_ms": 4, "latency_age_s": 12, "dhcp_packets": 3, "events": 2,
            "baseline_open": False, "baseline_anchored": True, "baseline_closes_in_s": 0,
            "names_known": 4}


LAN = [
    # mac, ip, hostname, vendor, status, online, up_s or last_seen
    ("D4:E9:F4:12:34:56", "192.168.2.27", "netmon", "Espressif Inc. (observed)", "known", True, 5400),
    ("50:91:E3:12:34:56", "192.168.2.1", "", "TP-Link Corporation Limited", "known", True, 5400),
    ("52:91:E3:12:34:57", "192.168.2.2", "Deco-M4-hall", "TP-Link Corporation Limited", "known", True, 5400),
    ("DA:A1:19:77:88:99", "192.168.2.45", "Pixel-7", "", "private", True, 900),
    ("F2:11:3C:00:12:AB", "192.168.2.46", "iPhone", "", "private", True, 3100),
    ("E49C67000001"[:2] + ":9C:67:00:00:01", "192.168.2.47", "Family-iPad", "Apple Inc.", "known", True, 2200),
    ("AE:22:10:5B:01:02", "192.168.2.48", "", "", "private", True, 400),
    ("8C:16:45:01:22:33", "192.168.2.20", "DESKTOP-4KF2QJ", "", "known", True, 5000),
    ("2C:CF:67:10:20:30", "192.168.2.21", "pihole", "Raspberry Pi (Trading) Ltd", "known", True, 5400),
    ("00:11:32:AA:BB:CC", "192.168.2.10", "DiskStation", "Synology Incorporated", "known", True, 5400),
    ("24:5E:BE:01:02:03", "192.168.2.11", "", "QNAP Systems Inc.", "known", False, 7200),
    ("A8:31:62:00:11:22", "192.168.2.60", "", "Hangzhou Huacheng Network Technology", "known", True, 5400),
    ("BC:32:5F:33:44:55", "192.168.2.61", "", "Zhejiang Dahua Technology Co. Ltd.", "known", True, 5400),
    ("24:0A:C4:12:34:56", "192.168.2.70", "shelly1pm-4A1B2C", "Espressif Inc.", "known", True, 5400),
    ("30:AE:A4:65:43:21", "192.168.2.71", "esp32-cam", "Espressif Inc.", "known", True, 5400),
    ("84:F3:EB:11:22:33", "192.168.2.72", "WLED-Kitchen", "Espressif Inc.", "known", True, 5400),
    ("A4:CF:12:44:55:66", "192.168.2.73", "", "Espressif Inc.", "unknown", True, 600),
    ("00:17:88:01:02:03", "192.168.2.74", "Philips-hue", "Philips Lighting BV", "known", True, 5400),
    ("F4:F5:D8:77:66:55", "192.168.2.80", "Chromecast", "Google Inc.", "known", True, 5400),
    ("64:E4:A5:12:34:56", "192.168.2.81", "LGwebOSTV", "", "known", True, 3000),
    ("38:6B:1C:22:00:11", "192.168.2.90", "HP-OfficeJet-Pro", "", "known", False, 86000),
    ("D8:B0:4C:01:02:03", "192.168.2.75", "", "Jinan USR IOT Technology Co. Ltd.", "known", True, 5400),
    ("7A:01:22:33:44:55", "192.168.2.112", "", "", "private", True, 120),
    ("3C:5A:B4:01:02:03", "192.168.2.114", "", "Google Inc.", "unknown", True, 240),
    ("C2:44:55:66:77:88", "192.168.2.113", "Galaxy-S23", "", "private", False, 1900),
    ("00:15:5D:01:02:03", "192.168.2.30", "build-vm", "Microsoft Corporation", "known", True, 5400),
    ("E8:4E:06:AA:BB:CC", "192.168.2.31", "MacBook-Air", "", "known", False, 600),
]


def devices():
    out = []
    for mac, ip, host, vendor, status, online, t in LAN:
        out.append(dict(mac=mac, ip=ip, hostname=host, vendor=vendor, status=status,
                        randomised=(int(mac[:2], 16) & 2) == 2, self=(ip == "192.168.2.27"),
                        online=online, last_seen_s=(20 if online else t), up_s=(t if online else 0)))
    return out


def config():
    return {"version": VERSION, "ssid": "HOME-2.4", "has_password": True, "networks": 1,
            "use_dhcp": True, "ip": "0.0.0.0", "mask": "0.0.0.0", "gw": "0.0.0.0", "dns": "0.0.0.0",
            "scan_interval_s": 60, "probe_interval_s": 30, "offline_after_s": 180,
            "learning_window_s": 600, "update_max": 1966080,
            "active": {"ip": "192.168.2.27", "mask": "255.255.255.0", "gw": "192.168.2.1",
                       "dns": "192.168.2.1", "mac": "D4:E9:F4:12:34:56", "ssid": "HOME-2.4",
                       "rssi": -38, "source": "dhcp"}}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        if not isinstance(body, (bytes, str)):
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        with LOCK:
            LOG.append(("GET", u.path))
        pages = extract(PAGES_H)
        route = {"/": "DASHBOARD_HTML", "/settings": "SETTINGS_HTML", "/isp": "ISP_HTML",
                 "/events": "EVENTS_HTML", "/nearby": "NEARBY_HTML", "/map": "MAP_HTML"}
        if u.path in route:
            return self.send(200, pages[route[u.path]], "text/html")
        api = {"/api/nearby": nearby, "/api/health": health, "/api/devices": devices,
               "/api/config": config,
               "/api/nearby/config": lambda: CFG,
               "/api/networks": lambda: [{"order": 1, "ssid": "HOME-2.4", "has_password": True,
                                          "active": True, "boot": "joined", "boot_ms": 2100,
                                          "reason": 0}],
               "/api/dhcp": lambda: {"listener": "listening", "received": 3, "capture_packets": 3,
                                     "valid": False, "client_mac": "00:00:00:00:00:00",
                                     "assigned_ip": "0.0.0.0", "server_ip": "0.0.0.0", "lease_s": 0,
                                     "message_type": "unknown", "hostname": "", "last_seen_s": 0,
                                     "local": {"ip": "192.168.2.27", "mask": "255.255.255.0",
                                               "gateway": "192.168.2.1", "dns": "192.168.2.1",
                                               "mode": "dhcp", "hostname": "netmon"}},
               "/api/events": lambda: [{"at_s": up() - 30, "type": "seen", "mac": "DA:A1:19:77:88:99",
                                        "ip": "192.168.2.45", "text": "private device first seen"}],
               "/api/isp": lambda: {"version": VERSION, "valid": False, "error": "offline test",
                                    "ip": "", "isp": "", "org": "", "asn": "", "city": "",
                                    "region": "", "country": "", "timezone": "", "rtt_ms": 0,
                                    "checked_age_s": 0, "ever_checked": False,
                                    "gateway": "192.168.2.1", "gateway_mac": "", "gateway_vendor": ""},
               "/api/latency": lambda: {"valid": True, "rtt_ms": 4, "checked_s": up() - 5, "age_s": 5,
                                        "failures": 0, "method": "tcp"},
               "/__log": lambda: LOG[-200:]}
        if u.path == "/api/nearby/find":
            q = parse_qs(u.query)
            with LOCK:
                if FIND["t"] is not None and find_live(time.time()):
                    FIND["asked"] = time.time()
                return self.send(200, find_body(int(q.get("after", ["0"])[0] or 0)))
        if u.path == "/__find":
            return self.send(200, {"t": FIND["t"], "seq": FIND["seq"], "turn": FIND["turn"]})
        if u.path == "/api/map":
            return self.send(200, mapdata())
        if u.path in api:
            return self.send(200, api[u.path]())
        self.send(404, "not found", "text/plain")

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        with LOCK:
            LOG.append(("POST", u.path, body.decode('utf-8', 'replace'), self.headers.get("Origin")))
        if u.path == "/api/nearby/scan":
            SIM["requested"] = True
            return self.send(200, {"status": "queued", "wifi": CFG["wifi"], "ble": CFG["ble"]})
        if u.path == "/api/nearby/config":
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            if "background_s" in j:
                v = j["background_s"]
                if not isinstance(v, int) or not (v == 0 or 30 <= v <= 3600):
                    return self.send(400, {"error": "the background interval must be 0, or 30 to 3600 seconds"})
                CFG["background_s"] = v
            for k in ("wifi", "ble"):
                if k in j:
                    if not isinstance(j[k], bool):
                        return self.send(400, {"error": k + " must be true or false"})
                    CFG[k] = j[k]
            return self.send(200, CFG)
        if u.path == "/api/nearby/find":
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            with LOCK:
                code, out = find_start(j)
            return self.send(code, out)
        if u.path == "/__find":
            q = parse_qs(u.query)
            if "walk" in q:
                FIND["walk"] = float(q["walk"][0])
            if "turn_s" in q:
                FIND["turn"] = {"start": time.time() + float(q.get("turn_in_ms", ["0"])[0]) / 1000.0,
                                "T": float(q["turn_s"][0]), "dir": float(q.get("dir", ["0"])[0])}
            return self.send(200, {"ok": True})
        if u.path == "/__nearby":
            q = parse_qs(u.query)
            if "unavailable" in q:
                SIM["unavailable"] = q["unavailable"][0] == "1"
                CFG["ble_ready"] = not SIM["unavailable"]
            return self.send(200, {"ok": True})
        self.send(404, {"error": "not found"})


if __name__ == "__main__":
    print("mock board on http://127.0.0.1:%d" % PORT, flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
