# Saved reports

From firmware 0.12 the board keeps a saved report of each network it has been
on: the full device list as it last stood, in its own flash. You can look at
a network's report after the board has moved on, and compare networks without
being on them.

## What's in a report

One row per device the board knew on that network:

- MAC address, IP address, hostname and maker
- whether it was recognised, unrecognised or using a private address
- whether it was online at the save, and how long it had been up
- when it was last seen and first seen
- whether it was **carried over** from an earlier report (see below)

The report also records the network's name, subnet, router address and MAC,
the board's own address, and the firmware that saved it.

## When reports are saved

- two sweeps after the board joins a network (about two minutes)
- every 15 minutes after that
- just before every restart the board makes on purpose: *Save and restart*,
  *Restart*, a firmware update
- when you press **Save this network now**

The board keeps the **latest** report of up to **four** networks, the most it
remembers. A network is its name and subnet. When a fifth network is saved,
it replaces the one saved longest ago.

### Carried-over devices

After a power cut or restart, the board only knows the devices it has seen
since. So a report saved then keeps the devices from the previous report of
the same network that haven't come back yet. They're marked **carried over**,
with their last-seen time aged by the time in between. A report lists up to 80
devices; the ones seen longest ago drop off first.

### Dates

The board has no clock. It learns the time from the provider lookup it
already makes (the Internet page), or from the app or a page, which send their
own clock when the board has none. Until then, a report says how long before
the save each device was seen, but not when.

## Looking at reports

**On the board's Settings page**, under *Saved reports*: one line per network,
with **JSON** and **CSV** downloads and **Delete**.

**In the app**, under **Settings → Saved reports**. Tap a network to open its
report, filter it (online, not recognised, carried over), and **Export CSV**
or **Export JSON** to a file on the phone or in the cloud. This works over
Bluetooth too, so you can read a report from the board while it sits on
another network.

## The files

The **JSON** file is the report exactly as the board keeps it:

```json
{"report":1,"ssid":"HOME-2.4","subnet":"192.168.2.0/24","gateway":"192.168.2.1",
 "saved_unix":1791136862,"clock":"internet","count":27,"online":23,"devices":[
{"mac":"D4:E9:F4:12:34:56","ip":"192.168.2.27","hostname":"netmon","vendor":"Espressif Inc.",
 "status":"known","randomised":false,"self":true,"online":true,"up_s":5400,"last_seen_s":3,
 "seen_unix":1791136859,"first_unix":1791131462,"carried":false},
...
]}
```

Device rows have the same fields as `GET /api/devices`, with `last_seen_s`
counted back from the save, plus `seen_unix`, `first_unix` (0 when the board
didn't know the time) and `carried`.

The **CSV** file has one line per device and these columns, the same from the
Settings page and from the app: `network, subnet, saved, mac, ip, hostname,
vendor, status, private_mac, online, last_seen, last_seen_s_before_save,
first_seen, uptime_s, carried_over`. Times are local, as `2026-10-04 21:01:02`.
A cell that a spreadsheet would run as a formula, such as a hostname starting
with `=`, is written as text: hostnames are whatever devices say about
themselves.

## Storage

Reports live in the board's 190 KB file system, beside its settings and
learned names, and survive restarts and firmware updates. A report of 30
devices takes about 8 KB. The board never writes a report that would leave
less than 24 KB free for its settings and names.
