"""The Settings page's Bluetooth section against the mock board: the link's
state, opening and cancelling a pairing window, a phone pairing well and
badly (three tries a window, a dropped connection not counted, the reason
shown), the owner's own pairing code, switching the link off and on,
forgetting the paired phones, an older board without the link, and the
phone layout.

  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python check_bluetooth.py shots
"""
import asyncio, json, re, sys, urllib.request
from playwright.async_api import async_playwright
import os as _os
sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import signin

BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"


hook = signin.hook


async def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    hook("/__ble?reset=1")
    errors, res = [], {}
    async with async_playwright() as p:
        b = await p.chromium.launch()
        ctx = await signin.context(b, viewport={"width": 1100, "height": 1000})
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
        again = signin.api("/api/ble/pair", {})
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

        # A wrong code costs one of three tries; the window stays open with
        # the same code, and the page says why.
        await page.click("#btpair")
        await page.wait_for_timeout(400)
        code1 = await page.inner_text("#btdigits")
        hook("/__ble?paired=0&why=wrong_code&status=1284")
        await page.wait_for_timeout(2600)
        msg = await page.inner_text("#btmsg")
        res["try_failed_msg"] = "did not pair" in msg and "The code did not match." in msg
        res["window_still_open"] = not await page.is_hidden("#btcode") and \
            await page.inner_text("#btdigits") == code1
        res["tries_left_shown"] = "2 tries left" in await page.inner_text("#btsteps")
        info = await page.inner_text("#btinfo")
        res["last_attempt_row"] = "Last attempt" in info and "0x504" in info
        # A dropped connection says nothing about the code: no try lost.
        hook("/__ble?paired=0&why=dropped&status=531")
        await page.wait_for_timeout(2600)
        res["dropped_not_counted"] = "2 tries left" in await page.inner_text("#btsteps") and \
            "connection dropped" in await page.inner_text("#btmsg")
        hook("/__ble?paired=0&why=wrong_code")
        hook("/__ble?paired=0&why=wrong_code")
        await page.wait_for_timeout(2600)
        msg = await page.inner_text("#btmsg")
        res["closed_after_three"] = await page.is_hidden("#btcode") and "Pairing failed" in msg and \
            "Three tries failed" in msg
        await page.screenshot(path=f"{OUT}/settings-bluetooth-failed.png", full_page=True)

        # The owner's own code: chosen, saved, and used by the next window.
        res["code_box_shown"] = not await page.is_hidden("#btcodebox")
        await page.select_option("#btmode", "own")
        res["own_input_shown"] = not await page.is_hidden("#btownbox")
        await page.fill("#btown", "48291")
        await page.click("#btcodebtn")
        await page.wait_for_timeout(300)
        res["short_code_refused"] = "six digits" in await page.inner_text("#btcodemsg")
        await page.fill("#btown", "482916")
        await page.click("#btcodebtn")
        await page.wait_for_timeout(500)
        res["own_saved"] = "Saved." in await page.inner_text("#btcodemsg") and \
            signin.api("/api/ble")["own"] == "482916"
        res["own_row"] = "Your own" in await page.inner_text("#btinfo")
        await page.click("#btpair")
        await page.wait_for_timeout(500)
        res["window_uses_own"] = await page.inner_text("#btdigits") == "482 916"
        await page.click("#btpair")
        await page.wait_for_timeout(300)
        # The page keeps the choice over its polls.
        await page.wait_for_timeout(2300)
        res["own_kept_after_poll"] = await page.input_value("#btown") == "482916" and \
            await page.input_value("#btmode") == "own"
        await page.fill("#btown", "123456")
        await page.click("#btcodebtn")
        await page.wait_for_timeout(500)
        res["weak_code_warned"] = "easy to guess" in await page.inner_text("#btcodemsg")
        await page.select_option("#btmode", "random")
        await page.click("#btcodebtn")
        await page.wait_for_timeout(500)
        res["back_to_random"] = signin.api("/api/ble")["own_code"] is False and \
            "random" in await page.inner_text("#btcodemsg")
        await page.screenshot(path=f"{OUT}/settings-bluetooth-own.png", full_page=True)

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
        ctx = await signin.context(b, viewport={"width": 1100, "height": 900})
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
        ctx = await signin.context(b, viewport={"width": 360, "height": 780}, device_scale_factor=2,
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
