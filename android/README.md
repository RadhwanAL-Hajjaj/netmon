# netmon for Android

Companion app for the netmon ESP32 network monitor. It talks to the board's own
API, over your Wi-Fi or over Bluetooth: the same readings as the web pages,
including the Wi-Fi and Bluetooth radars, the Finder and the network map, plus
notifications, a longer event history kept on the phone, the board's saved
reports, and firmware updates from the phone.

Version 1.3.0, for Android 8.0 and later. It works with netmon firmware 0.9.x
and later, and shows what each board has: network history needs firmware 0.9.4
or later, the DHCP listener status 0.9.6, Nearby 0.10, the Finder and the
access points on the map 0.11, Bluetooth and saved reports 0.12, and signing
in 0.13. On older firmware those screens say what they need instead. Firmware
0.13 needs this version: earlier apps cannot sign in.

## Install

1. Build the APK (see [Building from source](#building-from-source)), copy it
   to the phone and open it. Android asks once to allow installs from the app
   you opened it with (Files, Chrome, ...). An APK signed with the same key as
   the one installed updates it in place and keeps the event history and
   settings; one signed with another key has to replace it, which means
   uninstalling the old one first.
2. Start netmon. It looks for the monitor three ways at once: the address used
   last time, the board's mDNS adverts, and a sweep of the phone's subnet that
   asks each address what it is (`/api/auth`, or `/api/health` before
   firmware 0.13). Tap the monitor it finds, or type its address (for example
   `192.168.2.30`).
3. With firmware 0.13 the app asks for the monitor's password: your login
   password, or the update password from `secrets.h`. Leave *Save password*
   on and it signs in again by itself whenever it needs to.

The phone has to be on the same Wi-Fi as the monitor, or paired with it over
Bluetooth (below). A monitor that could not join Wi-Fi opens its own network,
`netmon-setup`; join it and the app finds the board at 192.168.4.1 (it keeps
using Wi-Fi even though that network has no internet).

## Signing in

From firmware 0.13 the monitor wants a session with every request, over
Wi-Fi and over Bluetooth. The app signs in once, for 30 days. With *Save
password* on, the password is kept on the phone, encrypted with a key held by
Android's keystore and readable only by this app, and when a session ends (its
30 days are up, someone pressed *Sign out everywhere*, the login password
changed) the app gets a new one by itself; only when it can't, does it ask.
The saved password is only ever sent to the monitor it belongs to: a new
address first has to say it is that monitor, by its chip's MAC.

*Settings → Signing in* shows whether the password is saved and how many
browsers and phones are signed in, and has *Sign out* and *Forget password*.
The login password itself is set on the monitor's own Settings page.

## Bluetooth

With firmware 0.12 or later, a phone paired with the monitor reaches it over
Bluetooth whenever Wi-Fi can't: walking with the Finder, in setup mode, on
another network. Every screen works the same; firmware updates stay on Wi-Fi.

- **Pairing.** *Settings → Bluetooth → Pair this phone*, on the monitor's
  Wi-Fi: the app asks the monitor for a pairing window, gets its 6-digit code,
  and gives it to Android itself (should Android ask anyway, the code is on
  screen to type). Away from that Wi-Fi, press *Pair a phone* on the monitor's
  own Settings page from any device on its network, then use *Find your
  monitor → Look for monitors over Bluetooth* here, tap it and enter the code,
  or your own pairing code if you set one. When pairing fails, the app says
  why, in the monitor's words where it can ask it over Wi-Fi, and whether the
  window is still open for another try.
- **Choosing.** *Automatic* uses Wi-Fi when the monitor answers there and
  Bluetooth when it doesn't, checking Wi-Fi again every 30 seconds while on
  Bluetooth; *Wi-Fi only* and *Bluetooth only* do what they say. The top bar
  says *Bluetooth* while it's in use. Over Bluetooth the app also learns the
  monitor's Wi-Fi address.
- **Forgetting.** *Forget* drops this phone's pairing; *Forget every paired
  phone* makes the monitor forget them all. The monitor's link can be switched
  off here too.
- The background check for notifications uses Bluetooth the same way, when the
  phone is paired and in range.

See [Using netmon over Bluetooth](../docs/bluetooth.md) for the security model.

## What is where

**Overview.** How many devices are online, with one mark per device in the
colour of its status (green known, blue private address, red unrecognised,
grey offline), unrecognised ones first. Below: the verdict the web dashboard
gives (still learning, everything recognised, N not recognised), the monitor's
own state (firmware, uptime, Wi-Fi and signal, address, last sweep, names
learned, free memory), gateway latency, and recent activity.

**Devices.** Search by name, address or maker; filter by status; sort by
address, name, status, maker or time online. Tap a device for its details, to
copy its MAC or address, or to open its web page.

*Map*, at the top of Devices, draws the network as the board's Map page does:
the router in the middle, the internet above it, and around it a bubble per
group of devices, by what they are (network gear, servers and storage,
computers, phones and tablets, TV and media, printers, cameras, smart home,
private addresses, not identified) or by whether they are recognised. Your
Wi-Fi gets a bubble of its own with this board and the access points that
carry your network's name. Tap a group for its devices, a dot for what the
board knows about it, the router or the internet for theirs; an access point
offers *Find it with the board*. Offline devices can be shown or left out.
The layout is the web page's own, worked out the same way, so the two look
alike. On firmware before 0.11 the map is drawn from the health report,
without the access points and the provider.

**Nearby.** What the board hears around it, read every three seconds while
the screen is open, which is also what keeps the board scanning quickly:

- *Wi-Fi*: a radar of the networks in range, scaled in metres (estimated from
  signal strength, up to 5 to 100 m) or in signal strength itself. Open
  networks are rings, the network the board is on has a second ring, a dot
  pulses when a network arrives and leaves a fading ring when it goes. Tap a
  dot for its details and a *Find it* button. Below: the networks in range,
  with signal, a trend arrow, distance, channel and security, then the ones
  heard earlier and gone.
- *Bluetooth*: the same for Bluetooth devices, coloured by kind (personal,
  trackers, home and things, not identified), with a switch per group and one
  to hide private addresses. The kind and product come from the board's own
  reading of each advertisement ("AirPods Pro", "Find My tracker", ...).
- A live log of arrivals and departures while the screen is open, and a search
  box on each.
- *Scanning on the board*: Wi-Fi and Bluetooth scanning on or off, how often the
  board scans while nobody is watching, distance calibration, and masking of
  names and addresses for screenshots.

**Finder.** Pick a tracker, any Bluetooth device or an access point (or press
*Find* beside one anywhere) and the board listens for that device alone. Take
the board with you on a power bank and walk: the app shows the distance
smoothed over about three seconds, warmer or colder from the trend over the
last eight, a cold-to-hot bar, the last minute of signal, and optional beeps
that quicken as you close in. The phone buzzes when you are within arm's
reach, and the screen stays on while you look.

For a direction, hold the board flat against your chest and turn on the spot:
your body blocks the signal from behind you, so it is strongest when you face
the device. On a phone with a rotation sensor the app follows your turn
itself, so you can turn at your own pace either way round, and afterwards an
arrow on the Finder's radar keeps pointing the way as you turn. Without one,
the turn is timed, as on the web page: turn to your right in step with the
hand. The arithmetic (smoothing, trend, direction) is the web page's own, so
both give the same answer from the same readings. Leaving the Finder lets the
board go back to scanning for everything.

**Events.** The board keeps only its last 48 events, in RAM, and two ARP sweeps
a minute take two of those slots, so a device event scrolls off within half an
hour and everything is lost when the board restarts. The app copies the list
each time it reads the board, turns "seconds since start" into clock time,
leaves out the sweep entries, and keeps up to 600 events on the phone. A board
restart (power cut, update) is noticed from its uptime and logged as an event.

**Internet.** Public address and provider (the board's own lookup; Check again
asks it now), router address, maker and MAC, and a gateway latency chart built
from the readings the phone has collected. Unanswered checks show as red dots.

**Settings.**
- Wi-Fi: scan, network name, password (blank keeps the saved one), automatic
  or fixed address, and the advanced intervals. The values are checked with
  the firmware's own rules before anything is sent; saving asks for
  confirmation, then restarts the board, as the web page does.
- Network history: the remembered networks in start-up order, how each fared
  at the last start-up, and Forget (not for the network in use).
- DHCP: the listener's state and the last DHCP request heard.
- Firmware update: choose the `.ino.bin` from Arduino IDE's Export Compiled
  Binary. The app refuses the bootloader, partitions and merged images, files
  that are too large or too small, and files without the ESP32 magic byte; it
  shows the netmon version inside the file. It checks the password before the
  upload, shows progress, then waits for the board to come back and says
  whether the new version is running. The password is the update password
  set in `firmware/netmon/secrets.h` when the firmware was built; the app can
  remember it.
- Signing in: whether the password is saved, sign out, forget the password
  (see above).
- Bluetooth: pairing, how the app reaches the monitor, and the monitor's own
  link, its kind of pairing code and how the last attempt went (see above).
- Saved reports (firmware 0.12): the device list of each network the monitor
  has been on, up to four, as it last stood. Tap one to look through it,
  filtered by online, not recognised or carried over from before a restart,
  and export it as CSV (the same columns as the Settings page's) or as the
  JSON the monitor keeps. *Save this network now* saves the current one. See
  [Saved reports](../docs/saved-reports.md).
- Notifications: off, unrecognised devices, or every new device, checked every
  15, 30 or 60 minutes.
- Restart the monitor, or switch to another one.

## Notifications

"Unrecognised devices" follows the board's own verdict: a device with a
manufacturer address first seen after the learning window. "Every new device"
also covers devices the phone has never seen before, including phones with
private addresses, and remembers them even after the board restarts and starts
learning again.

The first check after switching notifications on only records what is already
there, so you do not get a burst about devices already on the screen. Checks
run in the background with Android's job scheduler while the phone can reach
the monitor; Android may run them later than asked to save battery. While the
app is open it reads the board every 10 seconds.

## Building from source

**Android Studio:** open the `android` folder. The project uses the
Android Gradle plugin 8.7.3, Kotlin 2.1.21 and Gradle 8.11.1, compiles against
API 35 (Studio offers to install it) and targets API 34. It has no dependencies
beyond the Kotlin standard library: plain Android views, no AndroidX or
Compose.

**Signing:** copy `keystore.properties.example` to `keystore.properties` and
point it at your own keystore; both that file and any `*.jks` are git-ignored.
Debug builds are then signed with the same key as release builds, so a Studio
build installs over an APK you made before. Without it, debug builds use
Android's debug key and release builds are left unsigned.

**Without Gradle:** `tools/build_apk.sh` builds the APK with aapt2, kotlinc
1.9.24, dx, zipalign and apksigner, for machines that cannot reach Google's
Maven repository. Set `KS_PASS` (and `KEYSTORE`, if your keystore is not
`netmon-release.jks` in this folder) first; a missing keystore is created.
It refuses to package if any code could reach `invokedynamic`, which dx
cannot convert for Android.

Where the toolchain folder is not `../tc`, set `TOOLCHAIN`. kotlinc needs
about 2 GB of heap for the API 35 jar; the script asks for that unless
`JAVA_OPTS` says otherwise.

**Tests:** `tools/test/run.sh` runs `tools/test/CoreTest.kt`: 2293 checks of the
parsers, formatting, settings validation, firmware checks, event history,
alert rules, the Nearby, Finder and map logic, the Bluetooth link's frames
(against the same bytes the firmware's own tests use), the choice between
Wi-Fi and Bluetooth, saved reports, and the client against
`tools/mock_board.py`, a stand-in board that answers every endpoint the way
the firmware does, including uploads and the restart that follows (and, after
`POST /__fw?v=0.11`, `0.12` or `0.13`, Nearby, the Finder, the map, the
Bluetooth link's endpoints, saved reports, the clock and signing in, with
sessions that end and wrong passwords that wait). The client runs a second
time over a simulated Bluetooth link: a stand-in for the board's end that
takes the request frames, asks the mock board, and answers in frames cut as
the firmware cuts them. Where node is installed, `tools/test/web_parity.py`
first runs the board's own Nearby, Map and Settings page scripts from
`pages.h` and the checks compare the app's distances, bearings, trend arrows,
smoothing, warmer and colder, directions, map layouts and CSV exports with
them, number for number and byte for byte. It needs Android's org.json for the JVM
(`libandroid-json-java` on Debian and Ubuntu). Then `tools/test/beeper` checks
the Finder's beeps against a stand-in for Android's audio track: one sound at
a time however quickly they are stopped and started.

`tools/test/shots.sh` draws the radars, the Finder, the signal trace and the
map with the app's own drawing code and writes PNG files, for looking at a
change without a phone (into `build-manual/shots` unless told otherwise).

## Layout

```
app/src/main/java/com/example/netmon/
  Models.kt, Parse.kt     one data class per endpoint, and JSON to model
  NetmonClient.kt         HTTP client, sessions, multipart upload, restart watch
  Auth.kt                 signing in: the session, the saved password and its keystore key
  Board.kt                latest readings; one request at a time, in order
  AppState.kt             history, latency record, alert memory, under one lock
  EventHistory.kt         the on-phone event log
  AlertRules.kt, Alerts.kt, AlertJobService.kt   notifications
  Discovery.kt, NetRoute.kt, Subnet.kt           finding the board
  Firmware.kt, Validate.kt, Format.kt            checks and wording
  Nearby.kt               radar wording, distances, trend arrows, the live log
  Finder.kt               the Finder's smoothing, trend and direction
  NetMap.kt               the map's groups and layout
  TurnSensor.kt, Beeper.kt   the phone's rotation sensor, the Finder's beeps
  LinkCodec.kt            the Bluetooth link's frames, as the firmware defines them
  BleLink.kt              the phone's end of the link: GATT, pairing, looking for boards
  RoutePlan.kt            Wi-Fi or Bluetooth, and when to change
  Reports.kt              saved reports as CSV and file names
  CrashLog.kt             keeps a crash report to show on the next start
  MainActivity.kt         top bar, bottom bar, screens
  ui/                     screens, view helpers, charts
  ui/Painters.kt          the drawing of the radars, the Finder and the map
  ui/NearbyScreen.kt, ui/FinderPane.kt, ui/MapPane.kt   Nearby, the Finder, the map
  ui/LinkCard.kt, ui/ReportsCard.kt, ui/SignInCard.kt   Settings: Bluetooth, saved reports, signing in
```

## Notes

- Everything is plain HTTP on your network, or the monitor's own encrypted
  Bluetooth link. Nothing leaves your network except the board's own provider
  lookup.
- Permissions: *Nearby devices* (Android 12 and later) for Bluetooth, asked
  the first time it's used; before Android 12, location, only to look for the
  monitor over Bluetooth.
- The board's web server answers one request at a time, so the app never
  sends requests in parallel, and pauses its readings during a firmware upload.
- To use it away from home, enter an address you can reach through a VPN.
  Opening the board to the internet is not recommended: its pages and API
  are plain HTTP, so the password would cross the internet unencrypted.
- If the app ever stops unexpectedly, the next start shows the error with a
  Copy button, so it can be sent along.
- The Finder asks for no permission beyond vibration: the rotation sensor needs
  none, and neither do the beeps. These play at media volume, as the web
  page's do, so they sound with the phone on silent once you turn them on.

## Changes

**1.3.0**
- Signing in (firmware 0.13): the app asks for the monitor's password once,
  keeps a 30-day session, and with *Save password* signs in again by itself.
  Every request carries the session, over Wi-Fi and Bluetooth; the background
  check and firmware updates too. *Settings → Signing in*.
- Pairing works on the phones where it failed: the app starts the pairing over
  its own connection and gives Android the code itself, and says why an
  attempt failed. Away from the monitor's Wi-Fi, the code (or your own) is
  entered in the app.
- Find your monitor identifies boards by `/api/auth`, which answers without
  signing in, and says which ones ask for a password.

**1.2.0**
- Bluetooth: pairing with the monitor's code, Automatic, Wi-Fi only or
  Bluetooth only, and every screen over Bluetooth (firmware 0.12).
- Saved reports: browse each network's report, export CSV or JSON.
- The monitor is told the time when it has none, to date its reports.
- Find your monitor looks for monitors over Bluetooth too.
- Choosing another address for the same monitor keeps the event history and
  notification memory: firmware 0.12 says its MAC, which tells it apart.

**1.1.0**
- Nearby: the Wi-Fi and Bluetooth radars, lists, live log and scanning
  settings of the board's Nearby page.
- The Finder, with a turn the phone follows with its own rotation sensor, an
  arrow that keeps pointing the way afterwards, beeps and a buzz on arrival.
- The network map, under Devices.
- Firmware updates: the file check now uses the largest image the board says
  it takes (`update_max`, firmware 0.10 and later). Before this the app refused
  every 0.10 and 0.11 image as larger than the old 1.3 MB partition.
- Six tabs: Overview, Devices, Nearby, Events, Internet, Settings.

**1.0.0**
- First release: overview, devices, events, internet, settings, notifications
  and firmware updates.
