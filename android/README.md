# netmon for Android

Companion app for the netmon ESP32 network monitor. It talks to the board's own
HTTP API on your network: the same readings as the web pages, plus
notifications, a longer event history kept on the phone, and firmware updates
from the phone.

Version 1.0.0, for Android 8.0 and later. It was written against netmon
firmware 0.9.x and works with later firmware too, since newer versions only add
to the API; the Nearby, Finder and Map pages (0.10 and 0.11) are web-only.
Network history needs firmware 0.9.4 or later, the DHCP listener status 0.9.6.

## Install

1. Build the APK (see [Building from source](#building-from-source)), copy it
   to the phone and open it. Android asks once to allow installs from the app
   you opened it with (Files, Chrome, ...).
2. Start netmon. It looks for the monitor three ways at once: the address used
   last time, the board's mDNS adverts, and a sweep of the phone's subnet that
   asks each address for `/api/health`. Tap the monitor it finds, or type its
   address (for example `192.168.2.30`).

The phone has to be on the same Wi-Fi as the monitor. A monitor that could not
join Wi-Fi opens its own network, `netmon-setup`; join it and the app finds the
board at 192.168.4.1 (it keeps using Wi-Fi even though that network has no
internet).

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

**Tests:** `tools/test/CoreTest.kt` holds 231 checks of the parsers, formatting,
settings validation, firmware checks, event history, alert rules and the HTTP
client. `tools/mock_board.py` is a stand-in board that answers every endpoint
the way the firmware does, including uploads and the restart that follows.

## Layout

```
app/src/main/java/com/example/netmon/
  Models.kt, Parse.kt     one data class per endpoint, and JSON to model
  NetmonClient.kt         HTTP client, multipart upload, restart watch
  Board.kt                latest readings; one request at a time, in order
  AppState.kt             history, latency record, alert memory, under one lock
  EventHistory.kt         the on-phone event log
  AlertRules.kt, Alerts.kt, AlertJobService.kt   notifications
  Discovery.kt, NetRoute.kt, Subnet.kt           finding the board
  Firmware.kt, Validate.kt, Format.kt            checks and wording
  CrashLog.kt             keeps a crash report to show on the next start
  MainActivity.kt         top bar, bottom bar, screens
  ui/                     screens, view helpers, charts
```

## Notes

- Everything is plain HTTP on your network. Nothing leaves it except the
  board's own provider lookup.
- The board's web server answers one request at a time, so the app never
  sends requests in parallel, and pauses its readings during a firmware upload.
- To use it away from home, enter an address you can reach through a VPN.
  Opening the board to the internet is not recommended: only firmware updates
  need a password.
- If the app ever stops unexpectedly, the next start shows the error with a
  Copy button, so it can be sent along.
