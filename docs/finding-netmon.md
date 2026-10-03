# Finding netmon on your network

These are listed roughly in order of how likely they are to work.

1. **By name: `http://netmon.local`.** The board announces itself over mDNS.
   macOS, iOS, Windows 10 and later, and Linux with Avahi resolve `.local`
   names. Chrome on Android generally doesn't, so on a phone this is the one
   most likely to fail.

2. **Your router's client list.** The board gives the router the name
   `netmon` when it asks for an address, so it shows up by name rather than
   as an anonymous Espressif device.

3. **A network scanner app** such as Fing, or any mDNS service browser. The
   board advertises its web server, so it shows up there.

4. **The serial monitor** at 115200 baud. The board prints its address at
   boot:

   ```
   [wifi] connected, ip 192.168.1.50
   ```

5. **The Android app.** It looks for the board three ways at once: the
   address it used last time, mDNS, and a sweep of the phone's subnet that
   asks each address for `/api/health`.

## Stop it moving in the first place

In **Settings → Address → Fixed address**, pick an address outside your
router's DHCP pool. The board then keeps that address across restarts.

## If it isn't on the network at all

If the board couldn't join any of its saved networks, it has started its own:

| | |
|---|---|
| Wi-Fi network | `netmon-setup` (open, no password) |
| Address | `http://192.168.4.1` |

Join it from a phone and the settings page is there.

The board also keeps trying by itself. Whenever nobody has been connected to
`netmon-setup` for three minutes, it restarts and tries its saved networks
again. So after a power cut it finds its way back once the router is up.
