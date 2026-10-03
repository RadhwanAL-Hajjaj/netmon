# Taking netmon to another network

## Before you leave

Open **Settings** and check that the Address section says *Automatically, from
the router*. If you set a fixed address, the board keeps using it at the new
site and won't be reachable there. It won't fall back, because being joined to
the Wi-Fi still counts as connected. This is the one setting that can strand
it.

## At the new site

1. **Power the board.** It tries every network it remembers (up to four,
   12 seconds each), so give it up to a minute before it gives up.

2. **It opens its own network:**

   | | |
   |---|---|
   | Wi-Fi network | `netmon-setup` (open, no password) |
   | Address | `http://192.168.4.1` |

   It doesn't wait there forever. Whenever nobody has been connected to
   `netmon-setup` for three minutes, it restarts and tries its saved networks
   again, so the setup network disappears for up to a minute at a time. If
   you can't see it, give it a minute. Once your phone is connected to it, it
   stays.

3. **Join `netmon-setup` from a phone.** The phone should offer to "sign in to
   network" and open the settings page by itself. If it doesn't, open
   `http://192.168.4.1` by hand.

   If the phone complains that there's no internet and asks whether to stay
   connected, say yes. If nothing loads at all, turn mobile data off for a
   moment. Phones quietly send requests over mobile data when their Wi-Fi has
   no internet, and then the board never hears them.

4. **Tap *Scan for networks*,** pick the network you want, type its password,
   and tap *Save and restart*.

   Scanning briefly takes the radio away from the setup network. If the scan
   doesn't come back, check that the phone is still connected to
   `netmon-setup` and tap the button again.

5. **The board restarts and joins.** Find it using
   [Finding netmon](finding-netmon.md). The new router's client list and
   `netmon.local` are the best places to look.

## What to expect once it's on

- The board picks up the subnet automatically, so a `192.168.1.0/24` or a
  `10.0.0.0/24` network works without any configuration.
- Everything it finds in the first ten minutes counts as **known**. That's
  deliberate: it's learning what normal looks like there. After that, any new
  device with a manufacturer-assigned MAC is flagged as **unknown**.
- Expect more "not in registry" vendors than you might like. The board carries
  only a small built-in list of manufacturers, because the full 1.1 MB
  registry doesn't fit in flash.
- The board remembers four networks, with the newest first. Your previous
  network stays on the list, so the board rejoins it on its own when you bring
  it back.
