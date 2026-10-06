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

Each phone pairs once, with a 6-digit code. The board has no screen, so it
shows the code on its Settings page, or in the app while the phone is on the
board's Wi-Fi: someone has to be on the board's network to see it. Or set a
code of your own (below). Either way, a code only works while a pairing window
is open.

**On the board's Wi-Fi (easiest).** In the app (1.3.0 or later), open
**Settings → Bluetooth → Pair this phone**. The app opens a window on the
board, gets its code, and gives it to Android itself, so there is nothing to
type. If Android does ask (a few phones insist), type the code the app shows.

**Away from the board's Wi-Fi.** Open the board's **Settings** page from any
phone or computer on its network, and press **Pair a phone** in the Bluetooth
section. Then, in the app on the phone you're pairing, open **Find your
monitor** (tap the address at the top of the screen), choose **Look for
monitors over Bluetooth**, tap the monitor and enter the code the Settings
page shows. It shows as *ready to pair* while the window is open.

A window lasts two minutes and takes three wrong tries; a connection that
drops, or a prompt left unanswered, does not count as one. The Settings page
says how each attempt went: paired, wrong code, cancelled, connection dropped,
and so on, with the Bluetooth stack's own number for it.

After pairing, the app signs in to the board over Bluetooth like any other
client (firmware 0.13): with the password it has saved, or by asking for it.

Android asks for the *Nearby devices* permission the first time. Before
Android 12, looking for Bluetooth devices also needs location.

### Your own pairing code

On the board's **Settings → Bluetooth**, choose **Pairing code: My own code**,
enter six digits and press **Save pairing code**. Every window then uses that
code instead of a new random one, which is easier on a phone that can't see
the Settings page. It still works only while a window is open. Choose one
nobody would try first: not 000000 or 123456. **A new random code for each
pairing** goes back.

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
- The code is random for each window, unless you set your own. A window lasts
  two minutes and three wrong tries. Outside a window, every pairing attempt
  is given a random code nobody was shown, so it fails, your own code or not.
- A pairing made without the code is dropped at once. Room for a new pairing
  is only made for one made with the code, so someone in range can't push a
  paired phone out.
- A connection that doesn't encrypt within 20 seconds is dropped (90 seconds
  while a window is open, for typing the code), so strangers can't hold the
  board's connections.
- From firmware 0.13 every request also needs signing in, over Bluetooth as
  over Wi-Fi, so a paired phone still needs the password.
- The board never asks a phone to pair. A phone pairs only when its app asks,
  and a phone that is already paired encrypts with its saved keys by itself.

## Troubleshooting

- **The monitor doesn't show up when looking.** Check its Bluetooth link is on
  (board **Settings → Bluetooth**). It needs firmware 0.12 or later and Bluetooth
  on the phone.
- **Pairing didn't finish.** The app and the board's Settings page say why:
  a wrong code, the two minutes ran out, the connection dropped, or three wrong
  tries closed the window. While the window is still open, tap *Pair* again
  with the same code; otherwise press *Pair a phone* again.
- **Android shows its own prompt.** A few phones won't take the code from an
  app. Type the code the app or the Settings page shows.
- **"The monitor no longer knows this phone."** The board forgot its phones.
  Pair again.
- **Android still remembers an old pairing.** Forget *netmon* in the phone's
  Bluetooth settings, then pair again.
