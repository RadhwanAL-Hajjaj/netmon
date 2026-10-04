Page checks
===========

The web pages, loaded in headless Chromium against a simulated board.

  pip install playwright && python -m playwright install chromium
  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python jscheck.py ../../netmon/src/hw/pages.h     (needs node)
  python check_pages.py shots
  python check_states.py
  python check_finder.py shots
  python check_map.py shots
  python check_bluetooth.py shots
  python check_reports.py shots

mock_nearby.py serves the pages straight out of pages.h and fakes the
endpoints they call: a house's worth of Wi-Fi networks and Bluetooth devices
that drift and come and go, a LAN of 27 devices for the map, and the Finder,
with readings at each device's own advertising rate that grow stronger as if
somebody were walking up to it, a few seconds' pause each minute for the
network sweep, and, during a turn set up through its test hook, a signal
strongest in the given direction.

jscheck.py      every page's script parses
check_pages.py  every page light, dark and at phone width; the Wi-Fi and
                Bluetooth tabs' radars, lists, search, masking and live log
check_states.py Bluetooth unable to start, Wi-Fi switched off
check_finder.py the tabs and their addresses; finding from a list, from the
                picker and from a table; the turn giving the right direction
                across a sweep; arriving; leaving and coming back; a reload;
                two pages at once; the phone layout
check_map.py    the map's groups, choosing each kind of thing on it, offline
                devices, grouping by status, the keyboard, phone width
check_bluetooth.py  Settings, Bluetooth: the link's state, a pairing window
                with its code, pairing well and badly, off and on, forgetting
                every phone, a board before 0.12, phone width
check_reports.py    Settings, Saved reports: the list, the clock sent from the
                dashboard, JSON and CSV downloads (formulas written as text),
                saving now, deleting, a board before 0.12, phone width

Each prints what it found and lists any script errors; screenshots go in the
folder given.
