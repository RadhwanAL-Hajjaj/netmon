#!/usr/bin/env python3
"""A stand-in netmon 0.14.0 board for testing the pages in a browser.

Serves the pages straight out of pages.h and answers the endpoints they call
with bodies shaped like netmon.ino builds them. /api/nearby is simulated: a
house's worth of Wi-Fi networks and Bluetooth devices whose signals drift,
some of which come and go, so the radars, the trend arrows and the live log
have something to show.
The Finder is simulated too: readings at the device's own advertising rate,
getting stronger as if somebody were walking up to it, none for a few seconds
each minute while the "sweep" runs, and during a turn set up through the test
hook, strongest when facing the given direction, as a body's shadow makes it.
From 0.13 every page and API call needs signing in, as on the board: the
update password is "update-password-1" until a login password is set. The
pairing code, its window's tries and the board's MAC address are simulated.
Test hooks: POST /__nearby?ble=0|1&wifi=0|1&unavailable=0|1  GET /__log
            POST /__find?turn_in_ms=&turn_s=&dir=&walk=  GET /__find
            POST /__auth?reset=1  POST /__ble?reset=1 | ?paired=0|1&why=&status=
            POST /__guard?alerts=0|1&learning=0|1&reset=1   (the LAN watch, 0.14)
Trust, Forget, the LAN watch's alerts and Learn again change this mock's state
the way they change the board's.
"""
import json, math, random, re, secrets, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, quote

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from extract_pages import extract

PAGES_H = sys.argv[1] if len(sys.argv) > 1 else __file__.rsplit('/', 1)[0] + '/../../netmon/src/hw/pages.h'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8765
VERSION = "0.14.0-guard"
BOOT = time.time() - 5400
LOCK = threading.Lock()
LOG = []
CFG = {"wifi": True, "ble": True, "ble_ready": True, "background_s": 120}
SIM = {"unavailable": False, "requested": False}
# 0.14: devices marked as known or forgotten, the LAN watch, and the events
# those add.
OVERRIDE = {}
FORGOTTEN = set()
# The port scan (firmware 0.15): one device at a time, about four seconds a
# scan, with open ports that depend only on the address.
PORTSCAN = {"ip": "", "start": 0.0}
SCAN_PORTS = [(21, "FTP"), (22, "SSH"), (23, "Telnet"), (25, "SMTP"), (53, "DNS"), (80, "HTTP"),
              (110, "POP3"), (443, "HTTPS"), (445, "SMB"), (554, "RTSP"), (1883, "MQTT"),
              (3389, "RDP"), (8080, "HTTP-alt"), (8443, "HTTPS-alt"), (8883, "MQTT-TLS"),
              (9100, "Printer")]


def portscan(query):
    want = query.get("ip", [""])[0]
    if not PORTSCAN["ip"] or (want and want != PORTSCAN["ip"]):
        return {"state": "idle", "total": len(SCAN_PORTS)}
    last = int(PORTSCAN["ip"].rsplit(".", 1)[1])
    opened = {1: {53, 80, 443}, 11: {22, 80, 443, 445, 8080}}.get(last, {80, 8080} if last % 2 == 0 else set())
    probed = min(len(SCAN_PORTS), int((time.time() - PORTSCAN["start"]) / 0.25))
    return {"state": "done" if probed == len(SCAN_PORTS) else "scanning", "total": len(SCAN_PORTS),
            "ip": PORTSCAN["ip"], "probed": probed, "elapsed_ms": int((time.time() - PORTSCAN["start"]) * 1000),
            "open": [{"port": p, "name": n} for p, n in SCAN_PORTS[:probed] if p in opened]}
GUARD = {"learned": True, "learning": False, "alerts": []}
EXTRA_EVENTS = []
# The Bluetooth link, as GET /api/ble reports it; see ble_status().
# Saved reports (firmware 0.12): slot -> the report as the board keeps it.
CLOCK = {"boot_unix": 0}
REPORTS = {}


def report_of(ssid, subnet, seq, saved_unix, devices):
    return {"report": 1, "ssid": ssid, "subnet": subnet, "gateway": subnet.rsplit(".", 1)[0] + ".1",
            "gateway_mac": "50:91:E3:12:34:56", "board_ip": subnet.rsplit(".", 1)[0] + ".27",
            "board_mac": "D4:E9:F4:12:34:56", "version": VERSION, "seq": seq, "saved_unix": saved_unix,
            "saved_up_s": 3600, "clock": "client" if saved_unix else "none", "passes": 60, "learning": False,
            "count": len(devices), "online": sum(1 for d in devices if d["online"]), "devices": devices}


def report_rows(saved_unix):
    out = []
    for i, d in enumerate(devices()):
        ago = d["last_seen_s"]
        out.append(dict(d, seen_unix=(saved_unix - ago) if saved_unix else 0,
                        first_unix=(saved_unix - 86400) if saved_unix else 0, carried=i % 9 == 8))
    return out


def reports_list():
    now = int(time.time())
    out = []
    for slot, r in sorted(REPORTS.items()):
        out.append({"slot": slot, "ssid": r["ssid"], "subnet": r["subnet"], "gateway": r["gateway"],
                    "count": r["count"], "online": r["online"], "saved_unix": r["saved_unix"],
                    "age_s": (now - r["saved_unix"]) if r["saved_unix"] else -1,
                    "bytes": len(json.dumps(r)), "current": r["ssid"] == "HOME-2.4"})
    return {"max": 4, "every_s": 900, "clock": CLOCK["boot_unix"] != 0, "now_unix": now if CLOCK["boot_unix"] else 0,
            "free_bytes": 120000, "network": {"ssid": "HOME-2.4", "subnet": "192.168.2.0/24"}, "reports": out}


def reports_reset():
    now = int(time.time())
    REPORTS.clear()
    REPORTS[0] = report_of("HOME-2.4", "192.168.2.0/24", 5, now - 300, report_rows(now - 300))
    REPORTS[2] = report_of("Office =Guest", "10.20.0.0/16", 3, 0, report_rows(0)[:6])


LINK = {"enabled": True, "bonds": 1, "open": 0.0, "code": "", "result": "", "result_at": 0.0,
        "connected": 0, "secure": 0, "own": "", "failures": 0, "why": "",
        "last": None}
WHY = {"paired": "Paired.", "wrong_code": "The code did not match.",
       "cancelled": "The phone stopped asking for the code.",
       "timed_out": "Nobody entered the code in time.",
       "dropped": "The Bluetooth connection dropped before pairing finished.",
       "refused": "The phone and the board could not agree how to pair.",
       "no_code": "The phone paired without the code, so the board refused it.",
       "too_many": "The phone saw too many attempts. Wait a minute, then try again.",
       "failed": "Pairing failed."}

# Signing in (0.13), as src/core/auth.h does it, without the hashing.
UPDATE_PASSWORD = "update-password-1"
AUTH = {"own": None, "sessions": {}, "fails": 0, "until": 0.0}
FACTORY_MAC = "D4:E9:F4:12:34:56"
SETUP_MAC = "D4:E9:F4:12:34:57"
MAC = {"custom": "", "active": FACTORY_MAC}


def auth_reset():
    AUTH.update(own=None, sessions={}, fails=0, until=0.0)
    MAC.update(custom="", active=FACTORY_MAC)


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
            "baseline_open": GUARD["learning"], "baseline_anchored": True,
            "baseline_closes_in_s": 540 if GUARD["learning"] else 0,
            "names_known": 4, "mac": FACTORY_MAC, "wifi_mac": MAC["active"], "ble_link": LINK["enabled"],
            "clock": CLOCK["boot_unix"] != 0, "baseline_saved": GUARD["learned"],
            "known_saved": sum(1 for d in devices() if d["status"] == "known" and not d["self"]),
            "alerts": len(GUARD["alerts"])}


def sample_alerts():
    t = up()
    return [
        {"type": "router_changed", "mac": "A4:CF:12:44:55:66", "other": "50:91:E3:12:34:56",
         "ip": "192.168.2.1", "first_s": t - 900, "last_s": t - 20, "age_s": 20},
        {"type": "ip_conflict", "mac": "3C:5A:B4:01:02:03", "other": "00:11:32:AA:BB:CC",
         "ip": "192.168.2.10", "first_s": t - 300, "last_s": t - 60, "age_s": 60},
        {"type": "dhcp_server", "mac": "DA:A1:19:77:88:99", "other": "", "ip": "192.168.2.73",
         "first_s": t - 1800, "last_s": t - 400, "age_s": 400},
        {"type": "rogue_ap", "mac": "9E:2B:3C:44:55:66", "other": "", "ip": "",
         "first_s": t - 200, "last_s": t - 30, "age_s": 30},
        {"type": "weak_ap", "mac": "52:91:E3:12:34:57", "other": "", "ip": "",
         "first_s": t - 120, "last_s": t - 15, "age_s": 15},
    ]


def guard():
    return {"network": "HOME-2.4", "learned": GUARD["learned"], "learning": GUARD["learning"],
            "known": sum(1 for d in devices() if d["status"] == "known" and not d["self"]),
            "router": "" if GUARD["learning"] else "50:91:E3:12:34:56",
            "dhcp_own": "192.168.2.1", "dhcp": [] if GUARD["learning"] else ["192.168.2.1"],
            "aps": [] if GUARD["learning"] else [{"bssid": "50:91:E3:12:34:56", "auth": "WPA2/WPA3"},
                                                {"bssid": "52:91:E3:12:34:57", "auth": "WPA2/WPA3"}],
            "wifi_watch": CFG["wifi"], "alerts": GUARD["alerts"]}


def add_event(kind, mac, ip, text):
    EXTRA_EVENTS.insert(0, {"at_s": up(), "type": kind, "mac": mac, "ip": ip, "text": text})


def events():
    base = [{"at_s": up() - 30, "type": "seen", "mac": "DA:A1:19:77:88:99",
             "ip": "192.168.2.45", "text": "private device first seen"}]
    return (EXTRA_EVENTS + base)[:48]


def ble_status():
    now = time.time()
    live = LINK["open"] and now - LINK["open"] < 120
    last = LINK["last"]
    return {"link": 1, "available": True, "enabled": LINK["enabled"], "on": LINK["enabled"],
            "name": "netmon", "addr": "D4:E9:F4:12:34:58", "bonds": LINK["bonds"], "max_bonds": 3,
            "connected": LINK["connected"], "secure": LINK["secure"], "pairing": bool(live),
            "code": LINK["code"] if live else "", "left_s": int(120 - (now - LINK["open"]) + 0.999) if live else 0,
            "result": LINK["result"], "result_age_s": int(now - LINK["result_at"]) if LINK["result"] else 0,
            "why": LINK["why"], "why_text": WHY.get(LINK["why"], ""),
            "tries_left": max(0, 3 - LINK["failures"]),
            "last": dict(last, age_s=int(now - last["at"])) if last else None,
            "own_code": LINK["own"] != "", "own": LINK["own"],
            "served": 0, "via": "wifi"}


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
        if mac in FORGOTTEN:
            continue
        status = OVERRIDE.get(mac, status)
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
                       "dns": "192.168.2.1", "mac": MAC["active"], "ssid": "HOME-2.4",
                       "rssi": -38, "source": "dhcp"},
            "mac": {"active": MAC["active"], "factory": FACTORY_MAC, "custom": MAC["custom"],
                    "applied": MAC["custom"] != "" and MAC["active"] == MAC["custom"]}}


def mac_rule(text):
    h = re.sub(r"[:.\-]", "", text)
    if not re.fullmatch(r"[0-9A-Fa-f]{12}", h):
        return None, "Enter a MAC address like 02:1A:2B:3C:4D:5E."
    m = ":".join(h[i:i + 2] for i in range(0, 12, 2)).upper()
    if int(m[:2], 16) & 1:
        return None, "That is a group (multicast) address; the first pair of digits must be even."
    if m == "00:00:00:00:00:00":
        return None, "00:00:00:00:00:00 is not a usable address."
    if m == SETUP_MAC:
        return None, "That is the address the board's setup network uses; pick another."
    return m, ""


OPEN = ("/api/auth", "/api/login", "/api/logout")


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json", headers=()):
        if not isinstance(body, (bytes, str)):
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in headers:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    # The session the request carries: the nm_s cookie, or a bearer token.
    def token(self):
        a = self.headers.get("Authorization") or ""
        if a.lower().startswith("bearer "):
            return a[7:].strip()
        for part in (self.headers.get("Cookie") or "").split(";"):
            k, _, v = part.strip().partition("=")
            if k == "nm_s":
                return v
        return ""

    def session(self):
        return AUTH["sessions"].get(self.token())

    def login_required(self):
        return self.send(401, {"error": "Sign in to the monitor first.", "login": True, "netmon": True},
                         headers=(("X-Netmon-Login", "required"), ("Cache-Control", "no-store")))

    def gate(self, path):
        """True when the request may go on; otherwise it has been answered."""
        if path.startswith("/__") or path in OPEN or path == "/login":
            return True
        if self.session() is not None:
            return True
        if path.startswith("/api/"):
            self.login_required()
            return False
        here = path + ("?" + urlparse(self.path).query if urlparse(self.path).query else "")
        self.send(302, "", "text/plain", headers=(("Location", "/login?next=" + quote(here, safe="/")),))
        return False

    def auth_get(self):
        s = self.session()
        out = {"netmon": True, "name": "netmon", "version": VERSION, "id": FACTORY_MAC, "login": True,
               "signed_in": s is not None, "remember_days": 30, "via": "wifi"}
        if s is not None:
            out.update(own_password=AUTH["own"] is not None, remembered=s["remember"],
                       sessions=len(AUTH["sessions"]))
        return out

    def password_ok(self, pw):
        return pw != "" and (pw == UPDATE_PASSWORD or pw == AUTH["own"])

    def throttled(self):
        wait = int(AUTH["until"] - time.time() + 0.999)
        if wait > 0:
            self.send(429, {"error": "Too many wrong passwords. Try again in %d seconds." % wait,
                            "retry_s": wait})
            return True
        return False

    def failed(self):
        AUTH["fails"] += 1
        if AUTH["fails"] > 5:
            AUTH["until"] = time.time() + 30

    def do_GET(self):
        u = urlparse(self.path)
        with LOCK:
            LOG.append(("GET", u.path))
        if not self.gate(u.path):
            return
        pages = extract(PAGES_H)
        if u.path == "/login":
            return self.send(200, pages["LOGIN_HTML"], "text/html", headers=(("Cache-Control", "no-store"),))
        if u.path == "/api/auth":
            return self.send(200, self.auth_get(), headers=(("Cache-Control", "no-store"),))
        if u.path == "/api/portscan":
            return self.send(200, portscan(parse_qs(u.query)))
        route = {"/": "DASHBOARD_HTML", "/settings": "SETTINGS_HTML", "/isp": "ISP_HTML",
                 "/events": "EVENTS_HTML", "/nearby": "NEARBY_HTML", "/map": "MAP_HTML"}
        if u.path in route:
            return self.send(200, pages[route[u.path]], "text/html")
        api = {"/api/nearby": nearby, "/api/health": health, "/api/devices": devices,
               "/api/config": config, "/api/guard": guard,
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
               "/api/events": events,
               "/api/isp": lambda: {"version": VERSION, "valid": False, "error": "offline test",
                                    "ip": "", "isp": "", "org": "", "asn": "", "city": "",
                                    "region": "", "country": "", "timezone": "", "rtt_ms": 0,
                                    "checked_age_s": 0, "ever_checked": False,
                                    "gateway": "192.168.2.1", "gateway_mac": "", "gateway_vendor": ""},
               "/api/latency": lambda: {"valid": True, "rtt_ms": 4, "checked_s": up() - 5, "age_s": 5,
                                        "failures": 0, "method": "tcp"},
               "/api/ble": ble_status,
               "/api/reports": reports_list,
               "/__log": lambda: LOG[-200:]}
        if u.path == "/api/nearby/find":
            q = parse_qs(u.query)
            with LOCK:
                if FIND["t"] is not None and find_live(time.time()):
                    FIND["asked"] = time.time()
                return self.send(200, find_body(int(q.get("after", ["0"])[0] or 0)))
        if u.path == "/__find":
            return self.send(200, {"t": FIND["t"], "seq": FIND["seq"], "turn": FIND["turn"]})
        if u.path == "/api/reports/get":
            q = parse_qs(u.query)
            slot = q.get("slot", [""])[0]
            if not slot.isdigit() or int(slot) not in REPORTS:
                return self.send(404, {"error": "No report is saved there."})
            # As the board lays it out: one device a line.
            r = dict(REPORTS[int(slot)])
            devs = r.pop("devices")
            head = json.dumps(r, separators=(",", ":"))[:-1] + ',"devices":[\n'
            body = head + ",\n".join(json.dumps(d, separators=(",", ":")) for d in devs) + "\n]}\n"
            return self.send(200, body)
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
        if not self.gate(u.path):
            return
        if u.path in ("/api/login", "/api/auth/password", "/api/mac", "/api/config"):
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
        if u.path == "/api/login":
            if self.throttled():
                return
            if not self.password_ok(j.get("password") or ""):
                self.failed()
                return self.send(401, {"error": "That password is not right.", "wrong": True})
            AUTH["fails"] = 0
            tok = secrets.token_hex(16)
            remember = j.get("remember") is True
            AUTH["sessions"][tok] = {"remember": remember}
            c = "nm_s=" + tok + "; Path=/; HttpOnly; SameSite=Lax" + ("; Max-Age=2592000" if remember else "")
            return self.send(200, {"status": "signed in", "token": tok, "remember": remember,
                                   "days": 30 if remember else 0},
                             headers=(("Set-Cookie", c), ("Cache-Control", "no-store")))
        if u.path == "/api/logout":
            AUTH["sessions"].pop(self.token(), None)
            return self.send(200, {"status": "signed out"},
                             headers=(("Set-Cookie", "nm_s=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0"),))
        if u.path == "/api/auth/signout":
            AUTH["sessions"].clear()
            return self.send(200, {"status": "signed out everywhere"},
                             headers=(("Set-Cookie", "nm_s=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0"),))
        if u.path == "/api/auth/password":
            if self.throttled():
                return
            if not self.password_ok(j.get("current") or ""):
                self.failed()
                return self.send(403, {"error": "The current password is not right.", "wrong": True})
            new = j.get("new") or ""
            if new:
                n = len(new.encode("utf-8"))
                if n < 8:
                    return self.send(400, {"error": "The password needs at least 8 characters."})
                if n > 64:
                    return self.send(400, {"error": "The password can be at most 64 characters."})
                if new == UPDATE_PASSWORD:
                    return self.send(400, {"error": "That is the update password already; choose a different one."})
            AUTH["own"] = new or None
            mine = self.token()
            ended = len([t for t in AUTH["sessions"] if t != mine])
            AUTH["sessions"] = {t: v for t, v in AUTH["sessions"].items() if t == mine}
            return self.send(200, {"status": "changed", "own_password": AUTH["own"] is not None, "ended": ended})
        if u.path == "/api/mac":
            if not isinstance(j.get("mac"), str):
                return self.send(400, {"error": "mac must be an address like 02:1A:2B:3C:4D:5E, or empty"})
            if j["mac"] == "":
                MAC["custom"] = ""
            else:
                m, err = mac_rule(j["mac"])
                if not m:
                    return self.send(400, {"error": err})
                MAC["custom"] = "" if m == FACTORY_MAC else m
            return self.send(200, {"status": "saved", "custom": MAC["custom"], "restart": True})
        if u.path == "/api/config":
            return self.send(200, {"status": "saved"})
        if u.path == "/api/reboot":
            # A restart takes the address saved.
            MAC["active"] = MAC["custom"] or FACTORY_MAC
            return self.send(200, {"status": "restarting"})
        if u.path == "/__auth":
            auth_reset()
            return self.send(200, {"ok": True})
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
        if u.path in ("/api/ble", "/api/ble/pair", "/api/ble/forget"):
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            with LOCK:
                if u.path == "/api/ble":
                    if "enabled" not in j and "own" not in j:
                        return self.send(400, {"error": "enabled must be true or false"})
                    if "enabled" in j and not isinstance(j["enabled"], bool):
                        return self.send(400, {"error": "enabled must be true or false"})
                    if "own" in j:
                        own = j["own"]
                        if not isinstance(own, str) or not (own == "" or re.fullmatch(r"\d{6}", own)):
                            return self.send(400, {"error": "the pairing code must be six digits, or empty for a random code"})
                        if own != LINK["own"]:
                            LINK["own"] = own
                            LINK["open"] = 0.0      # a window open with the old code closes
                    if "enabled" in j:
                        LINK["enabled"] = j["enabled"]
                        if not LINK["enabled"]:
                            LINK["open"] = 0.0
                elif u.path == "/api/ble/pair":
                    if j.get("stop") is True:
                        LINK["open"] = 0.0
                    elif not LINK["enabled"]:
                        return self.send(409, {"error": "The Bluetooth link is switched off. Switch it on first."})
                    else:
                        if not (LINK["open"] and time.time() - LINK["open"] < 120):
                            LINK["code"] = "%06d" % random.randint(0, 999999)
                            LINK["failures"] = 0
                        if LINK["own"]:
                            LINK["code"] = LINK["own"]
                        LINK["open"] = time.time()
                        LINK["result"] = ""
                        LINK["why"] = ""
                else:
                    LINK["bonds"] = 0
                return self.send(200, ble_status())
        if u.path == "/api/clock":
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            t = j.get("unix")
            if not isinstance(t, int) or not (1704067200 <= t <= 4102444800):
                return self.send(400, {"error": "that time is not plausible"})
            CLOCK["boot_unix"] = t - up()
            return self.send(200, {"clock": True, "unix": t, "source": "client"})
        if u.path == "/api/reports/save":
            now = int(time.time()) if CLOCK["boot_unix"] else 0
            seq = max([r["seq"] for r in REPORTS.values()] + [0]) + 1
            REPORTS[0] = report_of("HOME-2.4", "192.168.2.0/24", seq, now, report_rows(now))
            return self.send(200, reports_list())
        if u.path == "/api/reports/delete":
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            if j.get("slot") not in REPORTS:
                return self.send(404, {"error": "No report is saved there."})
            del REPORTS[j["slot"]]
            return self.send(200, reports_list())
        if u.path == "/__reports":
            reports_reset()
            CLOCK["boot_unix"] = 0
            return self.send(200, {"ok": True})
        if u.path == "/__ble":
            # Test hooks: back to one paired phone (?reset=1), or a phone
            # finishing pairing, well or badly.
            q = parse_qs(u.query)
            if "reset" in q:
                with LOCK:
                    LINK.update(enabled=True, bonds=1, open=0.0, code="", result="", result_at=0.0,
                                own="", failures=0, why="", last=None)
                return self.send(200, {"ok": True})
            # An attempt ends, as the board's pair_done() takes it: a phone that
            # paired closes the window; a failure that says something about
            # the code costs one of three tries.
            with LOCK:
                ok = q.get("paired", ["1"])[0] == "1"
                why = "paired" if ok else q.get("why", ["wrong_code"])[0]
                now = time.time()
                in_window = bool(LINK["open"] and now - LINK["open"] < 120)
                LINK["last"] = {"why": why, "text": WHY.get(why, ""), "in_window": in_window, "at": now,
                                "status": 0 if ok else int(q.get("status", ["1284"])[0])}
                if in_window or "always" in q:
                    LINK["result"] = "paired" if ok else "failed"
                    LINK["why"] = why
                    LINK["result_at"] = now
                    if ok:
                        LINK["open"] = 0.0
                    elif why not in ("dropped", "timed_out"):
                        LINK["failures"] += 1
                        if LINK["failures"] >= 3:
                            LINK["open"] = 0.0
                if ok:
                    LINK["bonds"] = min(3, LINK["bonds"] + 1)
            return self.send(200, {"ok": True})
        if u.path == "/__find":
            q = parse_qs(u.query)
            if "walk" in q:
                FIND["walk"] = float(q["walk"][0])
            if "turn_s" in q:
                FIND["turn"] = {"start": time.time() + float(q.get("turn_in_ms", ["0"])[0]) / 1000.0,
                                "T": float(q["turn_s"][0]), "dir": float(q.get("dir", ["0"])[0])}
            return self.send(200, {"ok": True})
        if u.path == "/api/portscan":
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            ip = str(j.get("ip", ""))
            if not ip.startswith("192.168.2.") or ip.endswith((".0", ".255")):
                return self.send(400, {"error": "only devices on the board's own network can be scanned"})
            PORTSCAN.update(ip=ip, start=time.time())
            return self.send(200, {"status": "started"})
        if u.path in ("/api/devices/trust", "/api/devices/forget"):
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            mac = str(j.get("mac", "")).upper()
            row = next((d for d in devices() if d["mac"] == mac), None)
            if row is not None and row["self"]:
                return self.send(400, {"error": "this is the monitor itself"})
            if u.path.endswith("/trust"):
                if row is None:
                    return self.send(400, {"error": "mac is not a MAC address"})
                if row["randomised"]:
                    return self.send(400, {"error": "a private address changes when the device rejoins, so "
                                                    "there is nothing to remember. Private devices are never flagged."})
                if row["status"] == "unknown":
                    add_event("trusted", mac, row["ip"], "marked as known")
                OVERRIDE[mac] = "known"
                return self.send(200, {"status": "trusted"})
            if row is None:
                return self.send(404, {"error": "no such device"})
            FORGOTTEN.add(mac)
            add_event("forgotten", mac, row["ip"], "no longer recognised")
            return self.send(200, {"status": "forgotten"})
        if u.path == "/api/guard/accept":
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return self.send(400, {"error": "request body is not valid JSON"})
            kinds = ("router_changed", "ip_conflict", "dhcp_server", "rogue_ap", "weak_ap")
            if j.get("type") not in kinds:
                return self.send(400, {"error": "type is not one of the LAN watch's alerts"})
            for a in GUARD["alerts"]:
                same = a["ip"] == j.get("ip") if a["type"] == "dhcp_server" else a["mac"] == j.get("mac")
                if a["type"] == j["type"] and same:
                    GUARD["alerts"].remove(a)
                    add_event("trusted", a["mac"], a["ip"], "dismissed" if a["type"] == "ip_conflict"
                              else "accepted")
                    return self.send(200, {"status": "accepted"})
            return self.send(404, {"error": "no such alert"})
        if u.path == "/api/guard/relearn":
            GUARD.update(learned=False, learning=True, alerts=[])
            return self.send(200, {"status": "learning"})
        if u.path == "/__guard":
            q = parse_qs(u.query)
            if "reset" in q:
                OVERRIDE.clear()
                FORGOTTEN.clear()
                del EXTRA_EVENTS[:]
                GUARD.update(learned=True, learning=False, alerts=[])
            if "alerts" in q:
                GUARD["alerts"] = sample_alerts() if q["alerts"][0] == "1" else []
                if q["alerts"][0] == "1":
                    for a in reversed(GUARD["alerts"]):
                        add_event(a["type"], a["mac"], a["ip"], "sample " + a["type"])
            if "learning" in q:
                GUARD["learning"] = q["learning"][0] == "1"
                GUARD["learned"] = not GUARD["learning"]
            return self.send(200, {"ok": True})
        if u.path == "/__nearby":
            q = parse_qs(u.query)
            if "unavailable" in q:
                SIM["unavailable"] = q["unavailable"][0] == "1"
                CFG["ble_ready"] = not SIM["unavailable"]
            return self.send(200, {"ok": True})
        self.send(404, {"error": "not found"})


if __name__ == "__main__":
    reports_reset()
    print("mock board on http://127.0.0.1:%d" % PORT, flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
