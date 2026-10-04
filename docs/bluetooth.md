# Using netmon over Bluetooth

From firmware 0.12 and app 1.2, the netmon Android app can reach the board
over Bluetooth as well as Wi-Fi. Every screen works the same either way, apart
from firmware updates, which go over Wi-Fi only.

This is useful when the phone can't reach the board over Wi-Fi:

- walking around with the board and the Finder, away from the router
- the board is in setup mode (`netmon-setup`) and the phone is on another network
- the board is on a network the phone isn't on, such as a friend's or an office's

Bluetooth reaches around 10 metres indoors.

## Pairing a phone

Each phone pairs once, with a 6-digit code the board shows. The board has no
screen, so it shows the code on its Settings page, or in the app while the
phone is on the board's Wi-Fi. Someone has to be on the board's network to
see the code.

**On the board's Wi-Fi (easiest).** In the app, open **Settings → Bluetooth →
Pair this phone**. The app shows the code. When Android asks for a PIN, type
it in. The code works for one phone within two minutes.

**Away from the board's Wi-Fi.** Open the board's **Settings** page from any
phone or computer on its network, and press **Pair a phone** in the Bluetooth
section. Then, in the app on the phone you're pairing, open **Find your
monitor** (tap the address at the top of the screen), choose **Look for
monitors over Bluetooth**, and tap the monitor. It shows as *ready to pair*
while the window is open. Type the code from the Settings page when Android
asks.

Android asks for the *Nearby devices* permission the first time. Before
Android 12, looking for Bluetooth devices also needs location.

## How the app chooses

**Settings → Bluetooth** has three modes:

- **Automatic** (the default) uses Wi-Fi when the board answers there, and
  Bluetooth when it doesn't. While on Bluetooth, the app tries Wi-Fi again
  every 30 seconds and switches back as soon as the board answers there.
- **Wi-Fi only** never uses Bluetooth.
- **Bluetooth only** always uses Bluetooth, even with the board on the phone's
  Wi-Fi.

The top bar says **Bluetooth** while the app is using it. Over Bluetooth the
app also learns the board's Wi-Fi address, so it finds the board again on
Wi-Fi after the board moves or gets a new address.

## Forgetting

- **Settings → Bluetooth → Forget** in the app drops this phone's pairing.
- **Forget every paired phone** (in the app, or on the board's Settings page)
  makes the board forget all of them. Each has to pair again with a new code.
- Switching the board's Bluetooth link **off** stops it advertising and lets
  every phone go. Paired phones stay paired for when it's switched back on.

The board keeps three paired phones. A fourth, paired with the code, replaces
the one paired longest ago.

## Security

- The board answers only phones that paired with the code, over an encrypted
  link. A phone that hasn't paired, or that paired some other way, can connect
  but can't send a request.
- The code is random for each window. A window lasts two minutes and one
  attempt. Outside a window, every pairing attempt is given a code nobody was
  shown, so it fails.
- A pairing made without the code is dropped at once. Room for a new pairing
  is only made for one made with the code, so someone in range can't push a
  paired phone out.
- A connection that doesn't encrypt within 20 seconds is dropped (90 seconds
  while a window is open, for typing the code), so strangers can't hold the
  board's connections.
- Anyone on the board's LAN can already use its API without a password, as
  before. Bluetooth adds nobody new: pairing needs someone on that network.

## Troubleshooting

- **The monitor doesn't show up when looking.** Check its Bluetooth link is on
  (board **Settings → Bluetooth**). It needs firmware 0.12 or later and Bluetooth
  on the phone.
- **Pairing didn't finish.** The code was mistyped, the two minutes ran out, or
  the window had already been used. Press *Pair a phone* again for a new code.
- **"The monitor no longer knows this phone."** The board forgot its phones.
  Pair again.
- **Android still remembers an old pairing.** Forget *netmon* in the phone's
  Bluetooth settings, then pair again.
