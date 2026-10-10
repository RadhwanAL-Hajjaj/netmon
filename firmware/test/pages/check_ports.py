"""0.15's port scan on the Devices page, against the mock board: Scan ports
on a picked device, progress while it runs, the open ports when done, the
results cleared when another device is picked, and no button for the monitor
itself. Prints what it found and lists any script errors; screenshots go in
the folder given.

  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python check_ports.py shots
"""
import asyncio, json, sys, urllib.request
from playwright.async_api import async_playwright

import signin

BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"
KNOWN = "00:11:32:AA:BB:CC"        # DiskStation
UNKNOWN = "A4:CF:12:44:55:66"
SELF = "D4:E9:F4:12:34:56"


def posts():
    log = json.loads(urllib.request.urlopen(BASE + "/__log").read())
    return [e for e in log if e[0] == "POST" and e[1] == "/api/portscan"]


async def run(b, label, errors, res, **opts):
    ctx = await signin.context(b, **opts)
    page = await ctx.new_page()
    page.on("pageerror", lambda e: errors.append((label, "pageerror", str(e))))
    page.on("console", lambda m: m.type == "error" and errors.append((label, "console", m.text)))
    await page.goto(BASE + "/")
    await page.wait_for_timeout(1200)

    # The monitor itself: nothing to scan.
    await page.click(f'tr[data-m="{SELF}"]')
    if await page.is_visible("#ascan"):
        errors.append((label, "scan offered for the monitor itself"))

    await page.click(f'tr[data-m="{KNOWN}"]')
    if not await page.is_visible("#ascan"):
        errors.append((label, "no Scan ports button"))
    before = len(posts())
    await page.click("#ascan")
    await page.wait_for_timeout(1700)
    res[label + " running"] = await page.inner_text("#aports")
    res[label + " button while running"] = [await page.inner_text("#ascan"), await page.is_disabled("#ascan")]
    if "Scanning" not in res[label + " running"]:
        errors.append((label, "no progress shown", res[label + " running"]))
    await page.screenshot(path=f"{OUT}/ports-{label}-running.png")
    await page.wait_for_timeout(4200)
    res[label + " done"] = await page.inner_text("#aports")
    if await page.is_disabled("#ascan"):
        errors.append((label, "button still disabled after the scan"))
    await page.screenshot(path=f"{OUT}/ports-{label}-done.png")
    if len(posts()) != before + 1:
        errors.append((label, "expected one POST", posts()[before:]))

    # Another device: the first one's ports are gone.
    await page.click(f'tr[data-m="{UNKNOWN}"]')
    if await page.is_visible("#aports"):
        errors.append((label, "results left showing for another device"))
    await ctx.close()


async def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    errors, res = [], {}
    async with async_playwright() as p:
        b = await p.chromium.launch()
        await run(b, "light", errors, res, viewport={"width": 1100, "height": 800})
        await run(b, "dark", errors, res, viewport={"width": 1100, "height": 800}, color_scheme="dark")
        await run(b, "phone", errors, res, viewport={"width": 390, "height": 844}, device_scale_factor=2)
        await b.close()
    for k, v in res.items():
        print(k, "->", v)
    print("errors:", errors or "none")
    sys.exit(1 if errors else 0)


asyncio.run(main())
