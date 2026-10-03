"""The Finder and the tabs, driven in headless Chromium against the mock board:
picking from the list and from a table, readings arriving, the turn giving a
direction, stopping, the tab and hash routing, and keyboard tab switching."""
import asyncio, json, sys, re
from playwright.async_api import async_playwright
BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"

async def main():
    res, errors = {}, []
    async with async_playwright() as p:
        b = await p.chromium.launch()
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", str(e))))
        page.on("console", lambda m: m.type == "error" and errors.append(("console", m.text)))
        await page.request.post(BASE + "/api/nearby/find", data='{"stop":true}',
                                headers={"Content-Type": "application/json"})
        await page.request.post(BASE + "/__find?walk=0.12")
        await page.goto(BASE + "/nearby#bluetooth")
        await page.wait_for_timeout(3500)
        res["tab_from_hash"] = await page.evaluate("tab")
        res["bt_panel_visible"] = await page.is_visible("#p-bluetooth")
        res["wifi_panel_hidden"] = await page.is_hidden("#p-wifi")
        res["search_in_bt_panel"] = await page.evaluate("!!document.querySelector('#p-bluetooth #q')")
        res["log_in_bt_panel"] = await page.evaluate("!!document.querySelector('#p-bluetooth #logbox')")
        # Keyboard: arrows move between tabs and the hash follows.
        await page.focus("#t-bluetooth")
        await page.keyboard.press("ArrowLeft")
        await page.wait_for_timeout(300)
        res["arrow_left_tab"] = [await page.evaluate("tab"), await page.evaluate("location.hash"),
                                 await page.evaluate("document.activeElement.id")]
        await page.keyboard.press("End")
        await page.wait_for_timeout(500)
        res["end_tab"] = await page.evaluate("tab")
        await page.click("#t-bluetooth")
        await page.wait_for_timeout(500)
        # Find from the Bluetooth table: the sticky button for Galaxy Buds2 (3 readings a second).
        btn = page.locator("#tb tr[data-k='D0:03:DF:4E:12:34'] button.fb")
        res["find_button_visible"] = await btn.is_visible()
        await btn.click()
        await page.wait_for_timeout(500)
        res["after_find_tab"] = [await page.evaluate("tab"), await page.evaluate("location.hash")]
        res["top_hidden_while_finding"] = await page.is_hidden("#topbox")
        await page.wait_for_timeout(6000)
        res["fname"] = await page.inner_text("#fname")
        res["fwhat"] = await page.inner_text("#fwhat")
        res["fstate"] = await page.inner_text("#fstate")
        res["fbd"] = await page.inner_text("#fbd")
        res["fprox"] = await page.inner_text("#fprox")
        res["ftrend"] = await page.inner_text("#ftrend")
        res["fmeta"] = await page.inner_text("#fmeta")
        res["readings_held"] = await page.evaluate("F.rd.length")
        res["board_finding"] = await page.evaluate("fetch('/api/nearby').then(r=>r.json()).then(d=>d.finding)")
        # The turn: the mock puts the device at 3 o'clock, i.e. 90 degrees.
        await page.request.post(BASE + "/__find?walk=0")
        # Start the turn so the board's once-a-minute sweep would fall inside it.
        import time as _t
        while not (44 <= _t.time() % 60 <= 47):
            await page.wait_for_timeout(250)
        await page.click("#fturn")
        await page.request.post(BASE + "/__find?turn_in_ms=3000&turn_s=20&dir=90")
        await page.wait_for_timeout(1000)
        res["turn_countdown"] = await page.inner_text("#fturnmsg")
        await page.wait_for_timeout(9000)
        res["turn_midway"] = await page.inner_text("#fturnmsg")
        await page.screenshot(path=f"{OUT}/finder-turning.png")
        await page.wait_for_timeout(15000)
        res["turn_result"] = await page.inner_text("#fturnmsg")
        res["turn_dir_deg"] = await page.evaluate("F.dir&&Math.round(F.dir.a*180/Math.PI)")
        res["turn_points"] = await page.evaluate("F.dir&&F.dir.n")
        res["turn_gap_deg"] = await page.evaluate("F.dir&&Math.round(F.dir.gap*180/Math.PI)")
        res["hold_after_turn"] = await page.evaluate("fetch('/api/nearby/find?after=0').then(r=>r.json()).then(d=>d.hold_ms)")
        await page.screenshot(path=f"{OUT}/finder-direction.png")
        # Walking up to it: found.
        await page.request.post(BASE + "/__find?walk=2.5")
        await page.wait_for_timeout(9000)
        res["found_prox"] = await page.inner_text("#fprox")
        res["found_class"] = await page.get_attribute("#fmain", "class")
        await page.screenshot(path=f"{OUT}/finder-found.png")
        # Leaving the tab stops it on the board; coming back starts it again.
        await page.click("#t-wifi")
        await page.wait_for_timeout(1000)
        res["board_after_leaving"] = await page.evaluate("fetch('/api/nearby').then(r=>r.json()).then(d=>d.finding)")
        await page.click("#t-finder")
        await page.wait_for_timeout(1500)
        res["board_after_return"] = await page.evaluate("fetch('/api/nearby').then(r=>r.json()).then(d=>d.finding&&d.finding.addr)")
        # Reload keeps the target.
        await page.reload()
        await page.wait_for_timeout(2500)
        res["after_reload"] = [await page.evaluate("tab"), await page.evaluate("F.t&&F.t.addr"),
                               await page.is_visible("#fgo")]
        # Stop: back to the picker, board not finding.
        await page.click("#fstop")
        await page.wait_for_timeout(1000)
        res["after_stop"] = [await page.is_visible("#fpick"), await page.is_visible("#topbox"),
                             await page.evaluate("localStorage.getItem('nm.nb.ft')")]
        res["board_after_stop"] = await page.evaluate("fetch('/api/nearby').then(r=>r.json()).then(d=>d.finding)")
        # A device the board does not know: the error shows on the picker.
        await page.evaluate("pickFind('ble','01:02:03:04:05:06')")
        await page.wait_for_timeout(1000)
        res["unknown_device_msg"] = await page.inner_text("#fmsg")
        res["unknown_device_view"] = await page.is_visible("#fpick")
        # The picker's chips.
        await page.click("#chips button[data-c=w]")
        await page.wait_for_timeout(300)
        res["wifi_chip_rows"] = await page.eval_on_selector_all("#flist li", "l => l.length")
        # Find a Wi-Fi network from the picker.
        await page.click("#flist li:first-child button")
        await page.wait_for_timeout(5000)
        res["wifi_target"] = [await page.inner_text("#fname"), await page.inner_text("#fstate"),
                              await page.evaluate("F.rd.length")]
        await page.click("#fstop")
        await page.wait_for_timeout(500)
        # The radar's "Find it" in the pick panel.
        await page.click("#t-bluetooth")
        await page.wait_for_timeout(500)
        await page.click("#tb tr:first-child td:nth-child(2)")
        await page.wait_for_timeout(300)
        res["pick_has_find"] = await page.is_visible("#pb button.fb")
        await ctx.close()

        # Two pages finding different things: the first gives way, no tug of war.
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        p1 = await ctx.new_page()
        p2 = await ctx.new_page()
        for pg, tag in ((p1, "p1"), (p2, "p2")):
            pg.on("pageerror", lambda e, t=tag: errors.append(("pageerror", t, str(e))))
        await p1.goto(BASE + "/nearby#bluetooth")
        await p1.wait_for_timeout(3000)
        await p1.locator("#tb tr[data-k='D0:03:DF:4E:12:34'] button.fb").click()
        await p1.wait_for_timeout(2500)
        await p2.goto(BASE + "/nearby#bluetooth")
        await p2.wait_for_timeout(3000)
        await p2.locator("#tb tr[data-k='C1:44:22:FA:01:99'] button.fb").click()
        await p2.wait_for_timeout(4000)
        res["two_pages_p1_msg"] = await p1.inner_text("#fmsg")
        res["two_pages_p1_target"] = await p1.evaluate("F.t")
        res["two_pages_board"] = await p2.evaluate("fetch('/api/nearby').then(r=>r.json()).then(d=>d.finding&&d.finding.addr)")
        posts = await p2.evaluate("fetch('/__log').then(r=>r.json())")
        res["two_pages_posts_last_4s"] = len([x for x in posts[-40:] if x[0] == "POST" and x[1] == "/api/nearby/find"])
        await p2.click("#fstop")
        await ctx.close()

        # Phone: the finder fits, distance above the fold.
        ctx = await b.new_context(viewport={"width": 390, "height": 844}, device_scale_factor=2,
                                  is_mobile=True, has_touch=True)
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", "phone", str(e))))
        await page.goto(BASE + "/nearby#finder")
        await page.wait_for_timeout(3500)
        await page.tap("#chips button[data-c=b]")
        await page.wait_for_timeout(300)
        await page.tap("#flist li:first-child button")
        await page.wait_for_timeout(5000)
        box = await page.evaluate("(()=>{const r=document.getElementById('ftrend').getBoundingClientRect();return [r.top,r.bottom]})()")
        res["phone_trend_y"] = box
        res["phone_overflow"] = await page.evaluate("document.documentElement.scrollWidth")
        await page.screenshot(path=f"{OUT}/finder-phone-fold.png")
        await page.tap("#fstop")
        await ctx.close()
        await b.close()
    print(json.dumps(res, indent=1, ensure_ascii=False))
    print("ERRORS:", json.dumps(errors, indent=1))

asyncio.run(main())
