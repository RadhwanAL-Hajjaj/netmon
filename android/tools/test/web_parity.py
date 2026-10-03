#!/usr/bin/env python3
"""Runs the board's own Nearby and Map page scripts (from pages.h) in node and
writes what they compute for a fixed set of inputs, for CoreTest to compare
with the app's ports: distances, bearings, trend arrows, the Finder's
smoothing, trend and direction, and the network map's layout.

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


def run(js):
    with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False, encoding='utf-8') as f:
        f.write(js)
        name = f.name
    try:
        r = subprocess.run(['node', name], capture_output=True, text=True, timeout=120)
    finally:
        os.unlink(name)
    if r.returncode:
        sys.stderr.write(r.stderr[:3000])
        raise SystemExit('node failed')
    return json.loads(r.stdout.strip().splitlines()[-1])


def script(page):
    return re.search(r'<script>(.*?)</script>', page, re.S).group(1)


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
    json.dump({'nearby': nearby, 'map': netmap, 'map_input': {'map': mock_nearby.mapdata(), 'devices': mock_nearby.devices()}},
              open(out_path, 'w', encoding='utf-8'), ensure_ascii=False)
    print('wrote', out_path, '-', len(netmap['cases']), 'map layouts,', len(nearby['metres']), 'distances')


if __name__ == '__main__':
    main()
