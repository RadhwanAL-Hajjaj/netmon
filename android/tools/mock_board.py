#!/usr/bin/env python3
"""A stand-in netmon board for testing the Android client on the JVM.

Answers every endpoint the firmware serves, with bodies shaped exactly like
netmon.ino builds them (field names, order, types), accepts firmware uploads
the way the ESP32 WebServer does, and simulates the restart that follows.
Test hooks:  GET /__log  (requests seen)   POST /__reset   POST /__mode?x=...
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
}
LOCK = threading.Lock()


def uptime():
    return int(time.time() - STATE["boot"])


def health():
    # Built by hand in the firmware; same field order here.
    return ('{"status":"ok","version":"%s","wifi":"connected","ssid":"%s","ip":"192.168.2.30",'
            '"gateway":"192.168.2.1","subnet":"192.168.2.0/24","rssi":-35,"uptime_s":%d,'
            '"free_heap":181234,"sweepable":true,"devices":6,"scan_passes":61,"scan_remaining":0,'
            '"last_pass_ms":6512,"pass_seen":5,"pass_merges":212,"arp_cache":10,"latency_valid":true,'
            '"latency_ms":4,"latency_age_s":12,"dhcp_packets":3,"events":9,"baseline_open":false,'
            '"baseline_anchored":true,"baseline_closes_in_s":0,"names_known":4}'
            % (STATE["version"], STATE["active_ssid"], uptime()))


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
            '"probe_interval_s":30,"offline_after_s":180,"learning_window_s":600,'
            '"active":{"ip":"192.168.2.30","mask":"255.255.255.0","gw":"192.168.2.1","dns":"192.168.2.1",'
            '"mac":"D4:E9:F4:12:34:56","ssid":"%s","rssi":-35,"source":"dhcp"}}'
            % (STATE["version"], shown["ssid"], "true" if shown["pass"] else "false",
               len(STATE["nets"]), act))


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

    def _send(self, code, body, ctype="application/json"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        self.close_connection = True

    def _gate(self):
        with LOCK:
            rp = STATE["restart_pending"]
            if rp and time.time() >= rp[0]:
                STATE["boot"] = rp[0] + 1.5  # comes back 1.5 s after going down
                STATE["version"] = rp[1]
                STATE["restart_pending"] = None
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
        if STATE["mode"] == "badjson" and u.path.startswith("/api/"):
            return self._send(200, "<html>not json</html>", "text/html")
        if STATE["mode"] == "old" and u.path in ("/api/networks", "/api/dhcp"):
            return self._send(404, "not found", "text/plain")
        routes = {"/api/health": health, "/api/devices": devices, "/api/events": events,
                  "/api/config": config, "/api/networks": networks, "/api/dhcp": dhcp}
        if u.path in routes:
            return self._send(200, routes[u.path]())
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
                             log=[], uploads=[], mode="normal", active_ssid="HOME-5G",
                             nets=[{"ssid": "Office-WiFi", "pass": "x"}, {"ssid": "HOME-5G", "pass": "y"},
                                   {"ssid": "Cafe Guest", "pass": ""}])
            return self._send(200, "{}")
        if u.path == "/__mode":
            self._body()
            STATE["mode"] = parse_qs(u.query).get("x", ["normal"])[0]
            return self._send(200, "{}")
        if not self._gate():
            self.close_connection = True
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
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.daemon_threads = True
    print("mock board on", port, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
