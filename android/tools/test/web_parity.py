#!/usr/bin/env python3
"""Runs the board's own Nearby, Map and Settings page scripts (from pages.h) in
node and writes what they compute for a fixed set of inputs, for CoreTest to
compare with the app's ports: distances, bearings, trend arrows, the Finder's
smoothing, trend and direction, the network map's layout, and the saved
reports' CSV export and file names.

  python3 web_parity.py ../../../firmware/netmon/src/hw/pages.h parity.json
"""
import json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'firmware', 'test', 'pages'))
from extract_pages import extract  # noqa: E402

PRELUDE = r"""
var NOW = 1800000000000;
Date.now = function () { return NOW; };
function mk(id) {
  var store = {value: '', checked: false, textContent: '', innerHTML: '', hidden: false, id: id,
    clientWidth: 360, clientHeight: 300, width: 0, height: 0, options: [], children: [], style: {},
    tabIndex: 0, className: ''};
  var handler = {
    get: function (t, p) {
      if (p in store) return store[p];
      if (p === 'getContext') return function () { return ctx2d; };
      if (p === 'getBoundingClientRect') return function () { return {left: 0, top: 0, width: 360, height: 360}; };
      if (p === 'classList') return {toggle: function () {}, add: function () {}, remove: function () {}};
      if (p === 'add') return function (o) { store.options.push(o); };
      if (p === 'firstChild' || p === 'firstElementChild' || p === 'parentNode' || p === 'nextElementSibling') return null;
      if (p === Symbol.toPrimitive) return function () { return ''; };
      return function () { return mk('x'); };
    },
    set: function (t, p, v) { store[p] = v; return true; }
  };
  return new Proxy({}, handler);
}
var ctx2d = new Proxy({}, {get: function (t, p) {
  if (p === 'measureText') return function (s) { return {width: String(s).length * 6}; };
  return function () {};
}, set: function () { return true; }});
var IDS = __IDS__;
IDS.forEach(function (i) { globalThis[i] = mk(i); });
globalThis.document = {getElementById: function (i) { return globalThis[i] || (globalThis[i] = mk(i)); },
  createElement: function () { return mk('el'); }, createElementNS: function () { return mk('el'); },
  querySelector: function () { return null; }, addEventListener: function () {}, hidden: false,
  documentElement: mk('root'), activeElement: null};
globalThis.window = globalThis;
window.addEventListener = function () {};
window.matchMedia = function () { return {matches: false, addListener: function () {}}; };
globalThis.matchMedia = window.matchMedia;
globalThis.getComputedStyle = function () { return {getPropertyValue: function () { return '#336699'; }}; };
globalThis.localStorage = {getItem: function () { return null; }, setItem: function () {}};
globalThis.location = {hash: ''};
globalThis.history = {replaceState: function () {}};
globalThis.fetch = function () { return new Promise(function () {}); };
globalThis.requestAnimationFrame = function () { return 0; };
globalThis.setTimeout = function () { return 0; };
globalThis.clearTimeout = function () {};
globalThis.navigator = {};
globalThis.devicePixelRatio = 1;
"""

NEARBY_VECTORS = r"""
var out = {};
out.metres = [];
[-30, -45, -59, -60, -70, -77, -88, -100].forEach(function (r) {
  out.metres.push([r, metres(r, 'w'), metres(r, 'b')]);
});
out.bearing = [];
['50:91:E3:12:34:56', 'AC:84:C6:AA:00:01', '5D:21:8A:00:11:22', 'C4:9E:11:22:33:44', 'X'].forEach(function (k) {
  out.bearing.push([k, bearing(k)]);
});
out.fdist = [0.05, 0.4, 3.25, 9.96, 10, 12.5, 200].map(function (m) { return [m, fdist(m)]; });
// Trend arrows: one device read eight times.
var series = [-80, -79, -78, -76, -74, -73, -72, -70];
var tr = [];
hist = {};
series.forEach(function (r) { remember([{addr: 'T', rssi: r}]); tr.push(trend({addr: 'T'})); });
out.trend = {series: series, trend: tr};
// The Finder's smoothing: readings at irregular times, then the trend and rate.
F.t = {type: 'ble', addr: 'AA'};
F.rd = []; F.ema = null; F.emaT = 0; F.since = NOW - 30000;
var rd = [[-28000, -70], [-27000, -69], [-26200, -90], [-25000, -68], [-23000, -66], [-21500, -67],
  [-20000, -64], [-18000, -63], [-9000, -61], [-8000, -60], [-6500, -95], [-5000, -58], [-3000, -57], [-1000, -55]];
var pts = [];
rd.forEach(function (x) { addReading(NOW + x[0], x[1]); var p = F.rd[F.rd.length - 1]; pts.push([x[0], x[1], p.m, p.e]); });
out.finder = {readings: pts, trend: trendOf(), rate: rate(), now: fnow()};
out.hot = [0.2, 0.3, 0.7, 1, 2, 5, 15, 30, 60].map(function (d) { return [d, hot(d), prox(d), fbig(d)]; });
// Direction from a turn: a peak at 100 degrees, with a deterministic wobble.
var turn = [];
for (var i = 0; i < 40; i++) {
  var a = i / 40 * 2 * Math.PI;
  var r = Math.round(-70 + 9 * Math.cos(a - 100 * Math.PI / 180) + 2 * Math.sin(i * 1.7));
  turn.push({a: a, r: r});
}
var d = direction(turn);
out.direction = {pts: turn.map(function (p) { return [p.a, p.r]; }), a: d.a, contrast: d.contrast, ok: d.ok,
  gap: d.gap, curve: d.curve, text: dirText(d)};
var flat = [];
for (i = 0; i < 12; i++) flat.push({a: i / 12 * 2 * Math.PI, r: -70 + (i % 2)});
var fd = direction(flat);
out.flat = {pts: flat.map(function (p) { return [p.a, p.r]; }), ok: fd.ok, contrast: fd.contrast, text: dirText(fd)};
var few = direction([{a: 0, r: -60}, {a: 1, r: -61}]);
out.few = {text: dirText(few)};
var part = [];
for (i = 0; i < 20; i++) part.push({a: i / 20 * 4, r: -75 + (i > 8 && i < 13 ? 12 : 0)});
var pd = direction(part);
out.partial = {pts: part.map(function (p) { return [p.a, p.r]; }), a: pd.a, gap: pd.gap, ok: pd.ok, text: dirText(pd)};
console.log(JSON.stringify(out));
"""

MAP_VECTORS = r"""
var out = {cases: []};
M = __MAP__;
DV = __DEVICES__;
function snap(label) {
  var cs = L.cs.map(function (c) {
    return {key: c.key, title: c.title, lines: c.lines, lw: c.lw, lh: c.lh, r: c.r, x: c.x, y: c.y,
      items: c.items.map(function (it, k) { return [c.x + c.at[k][0], c.y + c.at[k][1],
        it.ap ? 'ap:' + it.ap.bssid : it.me ? 'me' : 'd:' + it.mac]; })};
  });
  out.cases.push({label: label, W: W, by: document.getElementById('by').value, off: off.checked,
    R: L.R, iy: L.iy, box: L.box, scale: W / L.box[2], groups: cs});
}
[[360, 'kind', false], [360, 'status', true], [412, 'kind', true], [800, 'kind', false], [800, 'status', false],
 [1100, 'kind', true]].forEach(function (c) {
  W = c[0]; document.getElementById('by').value = c[1]; off.checked = c[2];
  L = plan(); snap(c.join(','));
});
out.kinds = DV.map(function (d) { return [d.mac, kindOf(d)]; });
out.wrap = [['Wi-Fi “HOME-2.4”', 12], ['TV, media and games', 12], ['Servers and storage', 34],
  ['A very long group title that goes on', 12]].map(function (x) { return [x[0], x[1], wrap(x[0], x[1])]; });
console.log(JSON.stringify(out));
"""


# Saved reports: one dated, one from a board that had no clock. Names a
# spreadsheet would run as formulas, commas, quotes and a line break.
REPORT_DEVICES = [
    {"mac": "D4:E9:F4:12:34:56", "ip": "10.20.0.27", "hostname": "netmon", "vendor": "Espressif Inc.",
     "status": "known", "randomised": False, "self": True, "online": True, "up_s": 5400, "last_seen_s": 3,
     "seen_unix": 1791136859, "first_unix": 1791131462, "carried": False},
    {"mac": "DA:A1:19:77:88:99", "ip": "10.20.0.45", "hostname": "=HYPERLINK(\"x\")", "vendor": "",
     "status": "private", "randomised": True, "self": False, "online": True, "up_s": 900, "last_seen_s": 40,
     "seen_unix": 0, "first_unix": 0, "carried": False},
    {"mac": "7C:9E:BD:01:02:03", "ip": "10.20.0.77", "hostname": "-cmd", "vendor": "Acme, \"Widgets\" Ltd",
     "status": "unknown", "randomised": False, "self": False, "online": False, "up_s": 0, "last_seen_s": 7300,
     "seen_unix": 1791129562, "first_unix": 1791000000, "carried": True},
    {"mac": "00:11:32:AA:BB:CC", "ip": "10.20.0.20", "hostname": "Desk\nPC", "vendor": "Synology Incorporated",
     "status": "known", "randomised": False, "self": False, "online": False, "up_s": 0, "last_seen_s": 120,
     "seen_unix": 0, "first_unix": 0, "carried": False},
]


def report_fixtures():
    head = {"report": 1, "ssid": "Office =Guest, \"2\"", "subnet": "10.20.0.0/16", "gateway": "10.20.0.1",
            "gateway_mac": "98:DA:C4:11:22:33", "board_ip": "10.20.0.27", "board_mac": "D4:E9:F4:12:34:56",
            "version": "0.12.0-bluetooth", "seq": 9, "saved_unix": 1791136862, "saved_up_s": 5400,
            "clock": "internet", "passes": 88, "learning": False, "count": 4, "online": 2}
    dated = dict(head, devices=REPORT_DEVICES)
    undated = dict(head, saved_unix=0, clock="none",
                   devices=[dict(d, seen_unix=0, first_unix=0) for d in REPORT_DEVICES])
    return [dated, undated]


CSV_VECTORS = r"""
var out = {csv: [], cells: [], names: []};
__REPORTS__.forEach(function (r) { out.csv.push(tocsv(r)); });
['', 'plain', '=1+1', '+x', '-x', '@x', '\tx', '\rx', 'a,b', 'say "hi"', 'two\nlines', "'quoted", 'x=1'].forEach(
  function (v) { out.cells.push([v, csvcell(v)]); });
[['HOME-2.4', 1791136862], ['Office =Guest, "2"', 0], ['Café ☕', 1791136862], ['', 5], ['☕', 0]].forEach(
  function (x) { out.names.push([x[0], x[1], fname({ssid: x[0], saved_unix: x[1]}, 'csv')]); });
console.log(JSON.stringify(out));
"""


def run(js, env=None):
    with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False, encoding='utf-8') as f:
        f.write(js)
        name = f.name
    try:
        r = subprocess.run(['node', name], capture_output=True, text=True, timeout=120,
                           env=dict(os.environ, **(env or {})))
    finally:
        os.unlink(name)
    if r.returncode:
        sys.stderr.write(r.stderr[:3000])
        raise SystemExit('node failed')
    return json.loads(r.stdout.strip().splitlines()[-1])


def script(page):
    # Every script of the page in order: from firmware 0.13 the shared head
    # carries one of its own (the sign-in redirect) before the page's.
    return '\n'.join(re.findall(r'<script>(.*?)</script>', page, re.S))


def ids(page):
    return sorted(set(re.findall(r'\bid=(\w+)', page)) | set(re.findall(r'\bid="(\w+)"', page)))


def main():
    pages_h = sys.argv[1]
    out_path = sys.argv[2]
    pages = extract(pages_h)
    # The firmware page tests' simulated board: 27 devices, two access points.
    # It reads its own arguments when imported, so it gets none.
    argv = sys.argv
    sys.argv = [argv[0]]
    try:
        import mock_nearby
    finally:
        sys.argv = argv
    nb = pages['NEARBY_HTML']
    nearby = run(PRELUDE.replace('__IDS__', json.dumps(ids(nb))) + script(nb) + NEARBY_VECTORS)
    mp = pages['MAP_HTML']
    mapjs = (PRELUDE.replace('__IDS__', json.dumps(ids(mp))) + script(mp) +
             MAP_VECTORS.replace('__MAP__', json.dumps(mock_nearby.mapdata()))
             .replace('__DEVICES__', json.dumps(mock_nearby.devices())))
    netmap = run(mapjs)
    # The Settings page, with its timers stubbed so node can finish, in UTC so
    # the times it writes do not depend on where the tests run.
    st = pages['SETTINGS_HTML']
    fixtures = report_fixtures()
    csvjs = (PRELUDE.replace('__IDS__', json.dumps(ids(st))) + 'globalThis.setInterval = function () { return 0; };\n'
             + script(st) + CSV_VECTORS.replace('__REPORTS__', json.dumps(fixtures)))
    csv = run(csvjs, env={'TZ': 'UTC'})
    csv['input'] = [json.dumps(f, ensure_ascii=False) for f in fixtures]
    json.dump({'nearby': nearby, 'map': netmap, 'map_input': {'map': mock_nearby.mapdata(), 'devices': mock_nearby.devices()},
               'csv': csv},
              open(out_path, 'w', encoding='utf-8'), ensure_ascii=False)
    print('wrote', out_path, '-', len(netmap['cases']), 'map layouts,', len(nearby['metres']), 'distances,',
          len(csv['csv']), 'CSV exports')


if __name__ == '__main__':
    main()
