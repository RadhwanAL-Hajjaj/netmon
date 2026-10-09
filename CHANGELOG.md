# Changelog

Firmware releases, newest first. The Android app's notes are in
[android/README.md](android/README.md).

## 0.14.0-guard

Recognised devices are kept on the board, and a LAN watch notices the changes
an attacker on the network would have to make.

- **Recognised devices survive restarts.** When a network's learning window
  closes, the board saves the devices it learned, one list per network, in
  LittleFS (`/base-<hash>.txt`, `src/core/baseline.h`). After a restart, a power
  cut or an update there is no second window: a device with a manufacturer MAC
  that is not on the list is unknown from the first sweep. Before, the first
  ten minutes after every restart counted whatever was on the network as known,
  an intruder included. Forgetting a network in Settings deletes its list, and
  lists for networks no longer saved are removed at start-up.
- **Trust and Forget.** Pick a device on the Devices page. Trust makes an
  unknown device known and keeps it that way; Forget takes a device off the
  list and out of the table, so it is flagged if it turns up again. Forget
  takes two taps. Both are logged as events (`trusted`, `forgotten`).
- **Learn this network again**, in Settings, forgets the list and everything
  the LAN watch keeps, and opens a new learning window.
- **LAN watch** (`src/core/lan_guard.h`). The board notices and logs:
  - `router_changed`: the router's address answers from a different MAC, as
    ARP spoofing makes it. The first MAC seen there becomes the record.
  - `ip_conflict`: two MACs take turns answering for one address, A, B, A, B,
    each change within ten minutes of the last. A DHCP server handing an
    address on is not one, nor is a sleep proxy answering for a sleeping Mac.
  - `dhcp_server`: a device took a lease from a DHCP server the network does
    not use, read from option 54 of its REQUEST. Expected are the server that
    leased the board its own address, those seen while learning, and, with
    none on record, the router.
  - `rogue_ap` and `weak_ap`: after a Nearby Wi-Fi scan, an access point with
    this network's name that is not one of its own, or one of its own offering
    weaker security than before. The access points heard while learning are
    the network's own; with none on record, the first scan that hears the name
    records them.

  Each is logged once, then again hourly while it carries on, and forgotten a
  day after it stops. The Events page lists them with **Accept** (or
  **Dismiss** for a clash), and the Devices page names the newest at the top.
- **API.** `POST /api/devices/trust` and `/api/devices/forget` take
  `{"mac"}`. `GET /api/guard` gives `network`, `learned`, `learning`, `known`,
  `router`, `dhcp_own`, `dhcp`, `aps` (`bssid`, `auth`), `wifi_watch` and
  `alerts` (`type`, `mac`, `other`, `ip`, `first_s`, `last_s`, `age_s`).
  `POST /api/guard/accept` takes an alert's `type`, `mac` and `ip`;
  `POST /api/guard/relearn` takes nothing. `/api/health` gains
  `baseline_saved`, `known_saved` and `alerts`, and `baseline_open` is false
  on a network learned before. Like every other endpoint they need a session,
  the POSTs get the Origin check, and all of them answer over the Bluetooth
  link too.
- **Upgrade.** From 0.13 over the air; the partition scheme is unchanged and
  settings, sessions, reports and paired phones are kept. Each network learns
  once more after the update (a 10-minute window), then its list is saved.
- 2187 host checks, clean under AddressSanitizer and UBSan, and new page checks
  in `firmware/test/pages/check_guard.py`.
- Image: 1,621,171 bytes, 82% of the 1.9 MB app partition.

## 0.13.0-login

Every page and API call now needs signing in, Bluetooth pairing works on the
phones where it failed, the pairing code can be one of your own, and the
board's Wi-Fi MAC address can be set. Use it with app 1.3.0: earlier apps
cannot sign in.

- **Signing in.** Every page and every `/api/` call needs a session, over
  Wi-Fi and over Bluetooth alike (`src/core/auth.h`). Only the sign-in page,
  signing in and out, and `GET /api/auth`, which says what the board is,
  answer without one. The password is the update password from `secrets.h`
  until you set a login password of your own on the Settings page; the update
  password keeps working after that, as the way back in. A page opened
  without a session goes to `/login` and comes back once you have signed in. A
  session that ends while a page is open does the same at the page's next
  request.
- **Remember me.** Ticked, the session lasts 30 days and its cookie outlives
  the browser. Unticked, it ends with the browser, or after 12 hours unused,
  and a week at most. Sessions survive restarts and firmware updates. The
  sign-in page names its fields so the browser's password manager can save the
  password. In the app, *Save password* keeps it on the phone, encrypted, and
  the app signs in again by itself when a session ends.
- **How sessions are kept.** A session is a random 128-bit token: an
  `HttpOnly`, `SameSite=Lax` cookie (`nm_s`) in a browser, an
  `Authorization: Bearer` header from the app. The board keeps only each
  token's SHA-256, eight sessions at most, in `/auth.json`. A login password
  is kept as PBKDF2-HMAC-SHA256 with a random salt and 4096 rounds. Five wrong
  passwords from one address (or one Bluetooth connection) are free; after
  that each costs a wait, 30 seconds doubling up to 15 minutes. More than 30
  wrong passwords in ten minutes, from anywhere, stop every sign-in for a
  minute.
- **Settings, Signing in.** Who is signed in, *Sign out*, *Sign out
  everywhere*, and setting, changing or removing the login password. A new
  password signs out every other browser and phone. The dashboard has a *Sign
  out* link too.
- **Bluetooth pairing fixed.** Pairing often failed after the code had been
  typed. The board asked for pairing the moment a phone connected, while the
  app asked Android to pair at the same time. On many phones the two requests
  crossed and the link dropped part-way through. The board no longer asks
  first: the phone pairs over the connection it opens, and a phone that is
  already paired encrypts by itself with its first request. App 1.3.0 also
  gives Android the code itself, so no prompt has to be found and typed into.
- **Three tries per window, and why.** A pairing window now takes three
  failed attempts before it closes, keeping its code; a dropped connection or
  a timeout does not count as one. The board records how each attempt ended
  (paired, wrong code, cancelled, timed out, connection dropped, refused,
  paired without the code, too many attempts) with the Bluetooth stack's own
  status number. The Settings page and `GET /api/ble` show it, and the serial
  log says it. Nearby scans keep off the radio while a phone is pairing.
- **Your own pairing code.** Settings → Bluetooth → *Pairing code* sets a
  six-digit code of your own in place of a new random one for each window. It
  still works only while a window is open, and changing it closes any window
  open with the old code.
- **MAC address.** Settings → Address → *MAC address on Wi-Fi* sets the
  address the board uses on your network, or makes a random locally
  administered one. It is taken at the restart after saving; *The chip's own*
  goes back. The router sees a new device and may hand out a new IP address.
  The Bluetooth address and the board's identity for the app (`mac` in
  `/api/health`, `id` in `/api/auth`, both the chip's own) stay the same, so
  paired phones stay paired.
- **API.** New: `GET /api/auth`, `POST /api/login`, `POST /api/logout`,
  `POST /api/auth/password`, `POST /api/auth/signout` and `POST /api/mac`.
  `POST /api/ble` takes `{"own":"123456"}` (`""` for random codes) as well as
  `enabled`. `GET /api/ble` gains `why`, `why_text`, `tries_left`, `last`,
  `own_code` and `own`. `/api/health` gains `wifi_mac`, and `/api/config` a
  `mac` object (`active`, `factory`, `custom`, `applied`). A request without a
  session is answered `401` with `"login":true` in the body and, over Wi-Fi,
  an `X-Netmon-Login: required` header. The firmware upload needs a session as
  well as the update password.
- **Upgrade.** From 0.10, 0.11 or 0.12 over the air; the partition scheme is
  unchanged, and settings, networks, names, reports and paired phones are
  kept. After the update, sign in with the update password, and update the
  app to 1.3.0.
- 1940 host checks, clean under AddressSanitizer and UBSan. Image: 1,596,439
  bytes, 81% of the 1.9 MB app partition.

## 0.12.0-bluetooth

The board's API now also answers over Bluetooth, to phones paired with a
6-digit code, and the board keeps a saved report of each network it has been
on.

- **Bluetooth link.** The board advertises as `netmon` and carries every
  `/api/` endpoint over Bluetooth LE, so the Android app (1.2.0) works away
  from the board's Wi-Fi: walking with the Finder, in setup mode, or from the
  next room. One GATT service: the app writes requests to one characteristic
  and the board answers with notifications on the other, both as numbered
  frames that carry the same request and the same status and body as over
  Wi-Fi (`src/core/ble_link.h`). Firmware updates stay Wi-Fi only.
- **Pairing with a code.** The request characteristic takes writes only over a
  link encrypted with keys from a passkey pairing (bonded, with protection
  against a man in the middle, LE Secure Connections where the phone has
  them). The board has no screen, so the code is shown on the Settings page,
  or in the app over Wi-Fi: only someone on the board's network can see it. A
  window lasts two minutes and one attempt; outside one, every attempt is
  given a code nobody was shown. Up to three phones stay paired. A pairing
  made without the code is dropped at once, and room for a new pairing (by
  forgetting the phone paired longest ago) is made only for one with the code,
  so a stranger in range cannot push a paired phone out. Connections that do
  not encrypt within 20 seconds are let go.
- **Saved reports.** For each network the board has been on, up to four, the
  device list as it last stood is kept in flash (`src/core/report.h`): saved
  two sweeps after joining, then every 15 minutes, before every restart the
  board makes on purpose, and on request. A network is its name and subnet; a
  fifth replaces the one saved longest ago. Devices in the last report of the
  same network that have not been seen since the board started are carried
  over, so a power cut does not shrink the report. A report is never written
  when it would leave the file system short for settings and names.
- **A clock.** The board learns the time from the Date header of the provider
  lookup it already makes, or from the app or a page, which send their own
  clock when the board has none. Reports saved before then say how long before
  the save each device was seen, not when.
- **Settings page.** A Bluetooth section (state, *Pair a phone* with the code
  and a countdown, on and off, *Forget paired phones*) and a Saved reports
  section (download as JSON, or as CSV made in the page, with cells a
  spreadsheet would run as formulas written as text; delete; *Save this
  network now*).
- **API.** `GET`/`POST /api/ble`, `POST /api/ble/pair`, `POST /api/ble/forget`,
  `GET /api/reports`, `GET /api/reports/get?slot=N`, `POST /api/reports/save`,
  `POST /api/reports/delete` and `POST /api/clock` are new. `/api/health` gains
  `mac`, `ble_link` and `clock`. The API handlers no longer talk to the web
  server directly, so the same code answers both ways in.
- The update password now lives in `firmware/netmon/secrets.h`, which git
  ignores: copy `secrets.example.h` and set your own. The build stops with a
  clear message if the file is missing, still holds the placeholder, or the
  password is shorter than 8 characters.
- **Upgrade.** From 0.10.x or 0.11.x over the air; the partition scheme is
  unchanged. Settings, saved networks and learned names are kept.
- 1574 host checks, clean under AddressSanitizer and UBSan. Image: 1,551,427
  bytes, 78% of the 1.9 MB app partition.

## 0.11.0-finder

The Nearby page is now split into tabs, and there's a Finder for walking up to
one device. A new Map page draws the local network.

- **Tabs.** Nearby now has Wi-Fi, Bluetooth and Finder tabs, each with its own
  address (`/nearby#wifi`, `#bluetooth` and `#finder`). Arrow keys move
  between tabs, and the page remembers the last one you opened. Every row in
  the Wi-Fi and Bluetooth lists has a *Find* button, and so does any dot you
  pick on a radar.
- **Finder.** Pick one Bluetooth device or access point and the board listens
  for that device alone. For Bluetooth the controller filters by address,
  listening in back-to-back 2 s windows, 80% of the time. For Wi-Fi it probes
  the access point's own channel about once a second. The page shows:
  - a distance smoothed over about 3 s, drawn as a ring with a band for its
    uncertainty
  - warmer or colder, from a straight line fitted through the last 8 s of
    readings
  - a direction, if you turn on the spot holding the board against your chest
    (your body blocks 2.4 GHz, so the signal is strongest when you face the
    device)
  - the last minute of signal, and optional beeps that quicken as you close in

  Finding stops by itself 15 seconds after the page stops asking.
- **Map** (`/map`). Draws the router in the middle with the internet above it.
  Around it are groups of devices by kind (network gear, servers and storage,
  computers, phones and tablets, TV and media, printers, cameras, smart home,
  private addresses, not identified) or by state. A separate bubble shows the
  access points that carry your network's name. Tapping a device offers *Show
  in Devices*, which opens `/?q=<address>`. Tapping an access point offers
  *Find it with the board*.
- **API.** `GET`/`POST /api/nearby/find` (readings are numbered so a page can
  ask only for new ones) and `GET /api/map` are new. `/api/nearby` gains
  `finding`.
- **Fixed.** Starting a Bluetooth burst straight after another could free
  results the Bluetooth task was still handing over, which corrupted memory. A
  burst now counts as running until the stack reports it done. A burst that
  never ends is now stopped after 4 s, so it can't block the sweep forever.
  Returning to the Finder for a device the Bluetooth list had already
  forgotten no longer drops it.
- **Upgrade.** From 0.10.x you can update over the air. From 0.9.x, flash over
  USB first (see 0.10.0).
- 1325 host checks, clean under AddressSanitizer and UBSan. Image: 1,503,443
  bytes, 76% of the 1.9 MB app partition.

## 0.10.0-nearby

The standalone ESP32 Wi-Fi + BLE scanner sketch is merged into netmon and
runs on the same board.

- **Nearby page.** It has a Wi-Fi radar and a Bluetooth radar, scaled in
  metres (estimated) or in signal strength. Dots glow as the beam passes,
  pulse when they arrive and leave a fading ring when they go. Bluetooth
  devices are coloured by kind (personal, trackers, home and things, not
  identified), and the legend can hide each group. A switch hides rotating
  private addresses. The page also has:
  - a live log of arrivals and departures
  - sortable lists (Wi-Fi in range, Bluetooth devices, Wi-Fi history) with
    trend arrows and distance estimates
  - one search box (press `/` to jump to it) and masking for screenshots
- **Device kinds** come from the advertisement: Apple's and Microsoft's own
  formats, the services offered, the declared appearance, the name and the
  maker, strongest evidence first (`src/core/ble_type.h`). The page names
  products such as "AirPods Pro", "Find My tracker", "Windows laptop" and
  "Flipper Zero".
- **Sharing one radio** (`src/core/air_plan.h`):
  - The ARP sweep and the latency probe come first, and a Nearby scan never
    overlaps them.
  - Scans run quickly only while the page is open (Wi-Fi every 15 s,
    Bluetooth for 5 s in every 8). Otherwise they run at the background
    interval, every 2 minutes by default, or not at all if you turn it off.
  - Bluetooth listens 50% of the time during a burst.
  - Nothing scans in setup mode unless the page is open.
  - A firmware upload holds all scans.
- *Scan for networks* in Settings reuses the Nearby page's Wi-Fi scans. The
  DHCP listener is no longer stopped for scans. The updater checks files
  against the size this board can take (`update_max` in `/api/config`).
- **API.** `GET /api/nearby` (sent in chunks, up to about 30 KB),
  `POST /api/nearby/scan`, and `GET`/`POST /api/nearby/config` are new.
- **Breaking: new partition scheme.** The build needs *Minimal SPIFFS (1.9MB
  APP with OTA)* and NimBLE-Arduino 2. A partition scheme can only be changed
  over USB, so 0.10.0 has to be flashed over USB once. Settings stored in the
  old file-system partition are lost, so you have to run Wi-Fi setup again,
  and learned hostnames start again from nothing. On a board that ran the old
  scanner sketch, netmon finds and imports the Wi-Fi network that sketch was
  set up on.
- Ideas from [BlueWatch](https://github.com/PolarPatch/BlueWatch) (MIT),
  reimplemented in C++.
- 1228 host checks. Image: 1,424,459 bytes, 72% of the app partition.

## 0.9.7

Fixes from a code review. No API response changes shape.

- **Leaves setup mode by itself.** If the board has saved networks and nobody
  has been connected to `netmon-setup` for three minutes, it restarts and
  tries them again. Before this, a router that booted more slowly than the
  board after a power cut left the board in setup mode for good.
- **No more 49.7-day wrap.** Timestamps now come from the 64-bit timer
  instead of `millis()`. When `millis()` wrapped, it reopened the learning
  window, stopped devices ageing out and reset uptimes.
- A device returning through DHCP is now logged the same way as one found by
  the sweep. Sweep start and finish are no longer logged as events, which had
  filled the 48-entry history.
- The learning window starts only when a sweep finds something other than the
  board itself and the gateway.
- Each `/api/dhcp` response now describes a single packet, instead of mixing
  fields from several.
- Learned names are no longer lost. Failed writes are retried, and pending
  names are written before a restart or update.
- When the device table is full, offline devices give way first, then
  randomised addresses, then unknown devices. Known devices go last.
- **Cross-site requests are refused.** `POST /api/config`,
  `/api/networks/forget` and `/api/reboot` refuse requests whose `Origin`
  header names another site (`src/core/origin.h`). The Android app sends no
  Origin and is unaffected. This is not authentication.
- The DHCP listener restarts itself if it is ever found stopped. Network names
  are now escaped in `/api/health` and `/api/scan`.
- 841 host checks, clean under ASan and UBSan.

## 0.9.6

- *Upload and install* is always clickable and says what's missing (the file,
  the password, or why a file was refused) instead of staying greyed out.
- `/api/dhcp` reports the listener's state (`listening`, `paused`, `failed`
  or `off`) and how many packets have reached port 67. The DHCP section of
  Settings shows both, so you can tell a listener that never started from a
  router that never relays the broadcasts. The endpoint is now built with
  ArduinoJson.

## 0.9.5

- **Hostnames that actually arrive.** Promiscuous capture can't decrypt other
  stations' traffic on WPA2, so it never decoded a single DHCP packet. The
  board now listens on UDP port 67 for the DISCOVER and REQUEST broadcasts the
  router relays to every station. Phones and laptops get names within hours.
  Always-on devices get theirs when they next reconnect.
- Names are kept by MAC in `/names.txt` in flash, up to 64 entries with the
  least recently seen dropped first. They survive restarts and updates. Writes
  are batched and atomic (write to a temporary file, then rename).
- The board joins the strongest access point with a given name. Before this,
  the core's fast scan joined the first one it found, which could be the far
  extender.
- Network history shows the Wi-Fi stack's disconnect reason code and its
  meaning.
- The Settings scan list shows each name once, at its strongest, and leaves
  hidden networks out. It lists up to 24 networks.
- Only a DHCP REQUEST or INFORM moves a device's address. A DISCOVER's
  requested address is only a wish.
- **API.** `/api/health` gains `names_known`, and `/api/networks` gains
  `reason`.

## 0.9.4

- **Network history** in Settings lists every remembered network in the order
  the board tries them, with how each fared at the last start-up (joined, not
  in range, refused, could not join, no answer, not tried) and a two-tap
  *Forget*.
- **Firmware upload from the browser.** Before anything is sent, the page
  checks the `.ino.bin`: its extension, its size, the `0xE9` magic byte, and
  that it isn't a bootloader, partition or merged image. The password is
  tested first, a progress bar shows the upload, and afterwards the page waits
  for the board and confirms which version is running.
- Saving a network moves it to the front of the start-up order. The network
  name field shows the network actually joined.
- `/api/update` now pauses DHCP capture during the transfer, reports the real
  failure reason and rejects a POST without a file.
- **API.** `GET /api/networks`, `POST /api/networks/forget` and
  `POST /api/update/check` are new.
- Pages now declare UTF-8, which fixes garbled "—" and "…". The header fits
  360 px screens.

## 0.9.3

- Fixed `/api/dhcp`. It had an extra closing brace, so the response was never
  valid JSON. Its `local.ip` also reported the last DHCP client's address
  instead of the board's own.

## 0.9.2

- Fixed a JavaScript syntax error that stopped the Settings page script from
  running, so *Scan for networks* did nothing.

## 0.9.1

- The Wi-Fi scan no longer hangs while DHCP capture is on. It is now a
  bounded passive scan and returns HTTP 503 when busy.
- `/api/dhcp` is new. Devices learned through DHCP join the device table. The
  dashboard gets a Hostname column and Settings gets a DHCP section.

## 0.9.0-complete

- Event history: 48 entries in RAM at `/events` and `/api/events`, covering
  devices appearing, going offline and returning.
- Gateway latency is measured as a TCP connect to port 80 or 443 (not ICMP),
  at `/api/latency` and on the dashboard.
- Passive DHCP hostname capture (best effort; replaced in 0.9.5).
- Monitoring intervals are back in Settings, under *Advanced*.

## 0.7.1 to 0.8.0-portal

No detailed notes were kept for these releases. In them the board started
giving its name to the router (`netmon` in the DHCP client list) and
advertising its web server over mDNS. Setup mode became a captive portal, so
phones offer "sign in to network" and open the settings page by themselves.

## 0.7.0-table

- The device table has State, IP, MAC, Vendor and Uptime columns. You can sort
  by any column and drag to resize them, and the browser remembers both. IPs
  sort numerically, and State sorts unknown devices first.
- Uptime means continuous presence as the board has seen it. Offline devices
  show a dash, with the time they were last seen in a tooltip.
- The status line shows the board's own Wi-Fi signal. Every page shows a logo
  and the running version.

## 0.6.0-isp

- New Internet page. It shows the public address, provider, AS, location and
  time zone from [ip-api.com](https://ip-api.com), plus the router's address,
  maker and MAC from the board's own sweep.
- Lookups run at most every six hours, with at least two minutes between any
  two attempts. *Check again* skips the six-hour wait but not the two
  minutes. The lookup uses plain HTTP, because TLS would cost too much flash.

## 0.5.0-gui

- New dashboard. It opens with an answer ("1 device is not recognised") and
  marks each device's state with a coloured bar. Search covers address, MAC,
  vendor, hostname and state.
- Designed dark mode. Nothing is loaded from the internet: no fonts, no
  frameworks.

## 0.4.3

- The device list fits a phone. Narrow screens get two-line rows, and vendor
  names lose their corporate suffixes on screen.

## 0.4.2

- The board lists itself. A host never ARPs its own address, so it has to be
  added explicitly.

## 0.4.1

- Settings shows the address actually in use: DHCP or fixed, gateway, DNS,
  network, signal and MAC.
- Intervals were removed from the form. A field missing from a save now means
  "unchanged" rather than "reset to default".

## 0.4.0-classify

- Randomised (locally administered) MACs are classed as **private**, never
  **unknown**. Before this, phones that rotate their address would have been
  flagged as intruders.
- `GET /api/config` returns the live settings, and the form fills itself in
  from them. Stored passwords are never sent to the browser. A blank password
  field keeps the stored password for that same network only.

## 0.3.3

- `baseline_open` now reports whether the learning window is still open. Two
  fields are new: `baseline_anchored` and `baseline_closes_in_s`.
- `pass_hits`, which counted cache re-reads, is replaced by `pass_seen` and
  `pass_merges`.
- Randomised MACs are labelled as such instead of showing a blank vendor.
  Vendor names run to 40 characters and break on a word boundary.

## 0.3.2

- Scan passes now complete. The loop used to drop the batch in flight once per
  pass, so the pass counter, offline marking and learning window never moved.
  `last_pass_ms` is new in `/api/health`.

## 0.3.1-discovery

- Takes the lwIP TCP/IP core lock around ARP calls. In 0.3.0 the first scan
  hit an lwIP assertion and the board rebooted.
- ARP batches are now 6 addresses instead of 8, so replies aren't evicted
  from lwIP's 10-entry ARP table before they are read.
