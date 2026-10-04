"""The Settings page's Bluetooth section against the mock board: the link's
state, opening and cancelling a pairing window, a phone pairing well and
badly, switching the link off and on, forgetting the paired phones, an older
board without the link, and the phone layout.

  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python check_bluetooth.py shots
"""
import asyncio, json, re, sys, urllib.request
from playwright.async_api import async_playwright

BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"


def hook(path):
    urllib.request.urlopen(urllib.request.Request(BASE + path, data=b"", method="POST")).read()


async def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    hook("/__ble?reset=1")
    errors, res = [], {}
    async with async_playwright() as p:
        b = await p.chromium.launch()
        ctx = await b.new_context(viewport={"width": 1100, "height": 1000})
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", str(e))))
        page.on("console", lambda m: m.type == "error" and errors.append(("console", m.text)))
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1200)
        info = await page.inner_text("#btinfo")
        res["state"] = "On, advertising as netmon" in info and "1 of 3" in info
        res["code_hidden_at_first"] = await page.is_hidden("#btcode")

        # Open a window: a six-digit code, split for reading, and the time left.
        await page.click("#btpair")
        await page.wait_for_timeout(500)
        digits = await page.inner_text("#btdigits")
        res["code_shown"] = bool(re.fullmatch(r"\d{3} \d{3}", digits))
        res["steps"] = "for the next 2:00" in await page.inner_text("#btsteps") or \
            "for the next 1:59" in await page.inner_text("#btsteps")
        res["cancel_label"] = await page.inner_text("#btpair") == "Cancel pairing"
        await page.screenshot(path=f"{OUT}/settings-bluetooth-code.png", full_page=True)
        # Asking again keeps the same code.
        again = json.loads(urllib.request.urlopen(urllib.request.Request(
            BASE + "/api/ble/pair", data=b"{}", method="POST",
            headers={"Content-Type": "application/json"})).read())
        res["same_code"] = again["code"] == digits.replace(" ", "")

        # Cancel.
        await page.click("#btpair")
        await page.wait_for_timeout(500)
        res["cancelled"] = await page.is_hidden("#btcode") and await page.inner_text("#btpair") == "Pair a phone"

        # A phone pairs: the page notices within its two-second poll.
        await page.click("#btpair")
        await page.wait_for_timeout(400)
        hook("/__ble?paired=1")
        await page.wait_for_timeout(2600)
        res["paired_msg"] = "Paired." in await page.inner_text("#btmsg")
        res["paired_count"] = "2 of 3" in await page.inner_text("#btinfo")
        res["code_gone_after_pairing"] = await page.is_hidden("#btcode")

        # And one that gets the code wrong.
        await page.click("#btpair")
        await page.wait_for_timeout(400)
        hook("/__ble?paired=0")
        await page.wait_for_timeout(2600)
        res["failed_msg"] = "Pairing failed" in await page.inner_text("#btmsg")

        # Off and on again.
        await page.click("#btonoff")
        await page.wait_for_timeout(500)
        res["off"] = "Off" in await page.inner_text("#btinfo") and await page.is_disabled("#btpair") \
            and await page.inner_text("#btonoff") == "Switch on"
        await page.click("#btonoff")
        await page.wait_for_timeout(500)
        res["on_again"] = "On," in await page.inner_text("#btinfo") and not await page.is_disabled("#btpair")

        # Forgetting takes two taps; the first only arms the button.
        forgets = []
        page.on("request", lambda r: r.method == "POST" and r.url.endswith("/api/ble/forget") and forgets.append(r.url))
        await page.click("#btforget")
        await page.wait_for_timeout(200)
        res["armed"] = (await page.inner_text("#btforget")).startswith("Confirm")
        res["no_forget_yet"] = not forgets
        await page.click("#btforget")
        await page.wait_for_timeout(500)
        res["forgot"] = "0 of 3" in await page.inner_text("#btinfo") and \
            "Forgot every paired phone" in await page.inner_text("#btmsg") and len(forgets) == 1
        await page.screenshot(path=f"{OUT}/settings-bluetooth.png", full_page=True)
        await ctx.close()

        # A board from before 0.12 has no /api/ble.
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror old", str(e))))
        await page.route("**/api/ble", lambda r: r.fulfill(status=404, body="not found",
                                                           content_type="text/plain"))
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1200)
        res["old_board"] = "Needs firmware 0.12" in await page.inner_text("#btinfo") and \
            await page.is_disabled("#btpair")
        await ctx.close()

        # Phone width, dark, with the code showing.
        ctx = await b.new_context(viewport={"width": 360, "height": 780}, device_scale_factor=2,
                                  color_scheme="dark", is_mobile=True, has_touch=True)
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror phone", str(e))))
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1000)
        await page.click("#btpair")
        await page.wait_for_timeout(500)
        sw = await page.evaluate("document.documentElement.scrollWidth")
        res["phone_no_overflow"] = sw <= 360
        el = await page.query_selector("#btcode")
        await el.scroll_into_view_if_needed()
        await page.screenshot(path=f"{OUT}/settings-bluetooth-phone.png")
        await page.click("#btpair")       # leave no window open behind us
        await ctx.close()
        await b.close()

    hook("/__ble?reset=1")
    for k, v in res.items():
        print(("ok  " if v else "FAIL"), k)
    for e in errors:
        print("ERROR", e)
    bad = [k for k, v in res.items() if not v]
    sys.exit(1 if bad or errors else 0)


asyncio.run(main())
