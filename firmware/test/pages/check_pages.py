"""Loads every page against the mock board in headless Chromium: script
errors, the Nearby page's behaviour, and screenshots to look at."""
import asyncio, json, sys
from playwright.async_api import async_playwright

BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"

async def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    errors = []
    async with async_playwright() as p:
        b = await p.chromium.launch()
        for scheme in ("light", "dark"):
            ctx = await b.new_context(viewport={"width": 1100, "height": 900}, color_scheme=scheme)
            page = await ctx.new_page()
            page.on("pageerror", lambda e, s=scheme: errors.append(("pageerror", s, str(e))))
            page.on("console", lambda m, s=scheme: m.type == "error" and errors.append(("console", s, m.text)))
            for path in ["/", "/settings", "/events", "/isp", "/nearby", "/map"]:
                await page.goto(BASE + path)
                await page.wait_for_timeout(1500)
                nav = await page.inner_text("nav")
                if "Nearby" not in nav or "Map" not in nav:
                    errors.append(("nav", path, nav))
            # Nearby: let it poll, then look.
            await page.goto(BASE + "/nearby")
            await page.wait_for_timeout(4000)
            await page.screenshot(path=f"{OUT}/nearby-{scheme}.png", full_page=True)
            await page.screenshot(path=f"{OUT}/nearby-{scheme}-top.png")
            await ctx.close()

        # Phone width, every page and every tab.
        ctx = await b.new_context(viewport={"width": 360, "height": 780}, device_scale_factor=2,
                                  color_scheme="light", is_mobile=True, has_touch=True)
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", "phone", str(e))))
        for path in ["/", "/settings", "/events", "/isp", "/map", "/nearby#wifi",
                     "/nearby#bluetooth", "/nearby#finder"]:
            await page.goto(BASE + path)
            await page.wait_for_timeout(2500)
            sw = await page.evaluate("document.documentElement.scrollWidth")
            if sw > 360:
                errors.append(("overflow", "phone", path, sw))
        await page.screenshot(path=f"{OUT}/nearby-phone.png", full_page=True)
        await ctx.close()

        # Behaviour.
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", "behaviour", str(e))))
        await page.goto(BASE + "/nearby")
        await page.wait_for_timeout(3500)
        res = {}
        res["count"] = await page.inner_text("#count")
        res["state"] = await page.inner_text("#state")
        res["wifi_rows"] = await page.eval_on_selector_all("#tw tr", "r => r.length")
        res["ble_rows"] = await page.eval_on_selector_all("#tb tr", "r => r.length")
        res["hist_rows"] = await page.eval_on_selector_all("#th tr", "r => r.length")
        res["xss_safe"] = await page.evaluate("!document.querySelector('#tw script')")
        res["twin"] = await page.evaluate("document.querySelector('#tw').innerText.indexOf('same name as yours')>-1")
        res["legend"] = await page.inner_text("#lg")
        await page.click("#t-bluetooth")
        await page.wait_for_timeout(300)
        # Click a table row: the radar caption names it.
        await page.click("#tb tr:first-child")
        await page.wait_for_timeout(300)
        res["pick_after_row"] = await page.inner_text("#pb")
        # Signal scale.
        await page.select_option("#rb", "0")
        await page.wait_for_timeout(300)
        res["scale_saved"] = await page.evaluate("localStorage.getItem('nm.nb.rb')")
        # Hide a group, hide private addresses.
        before = await page.eval_on_selector_all("#tb tr", "r => r.length")
        await page.click("#lg button[data-g=u]")
        await page.check("#hidep")
        await page.wait_for_timeout(300)
        after = await page.eval_on_selector_all("#tb tr", "r => r.length")
        res["rows_before_after_hiding"] = [before, after]
        res["counts_after_hiding"] = await page.inner_text("#cnb")
        await page.click("#lg button[data-g=u]")
        await page.uncheck("#hidep")
        # Search, and the '/' shortcut.
        await page.keyboard.press("/")
        res["slash_focus"] = await page.evaluate("document.activeElement.id")
        await page.keyboard.type("tracker")
        await page.wait_for_timeout(300)
        res["search_tracker_rows"] = await page.eval_on_selector_all("#tb tr", "r => r.length")
        await page.fill("#q", "")
        # Masking.
        await page.click("details:not(.how) summary")
        await page.check("#mask")
        await page.click("#t-wifi")
        await page.wait_for_timeout(300)
        res["masked_bssid"] = await page.inner_text("#tw tr:first-child td:nth-child(6)")
        await page.uncheck("#mask")
        # Switches post to the board.
        await page.select_option("#bg", "300")
        await page.wait_for_timeout(500)
        res["bg_after"] = await page.evaluate("fetch('/api/nearby/config').then(r=>r.json())")
        # Live log: skip the 20 s learning period, then wait for things to come and go.
        await page.evaluate("opened = Date.now() - 30000")
        await page.click("#t-bluetooth")
        await page.wait_for_timeout(40000)
        res["feed"] = (await page.inner_text("#feed"))[:600]
        await page.evaluate("document.querySelector('#logbox').scrollIntoView()")
        await page.evaluate("window.scrollBy(0,-560)")
        await page.wait_for_timeout(300)
        await page.screenshot(path=f"{OUT}/nearby-after-log.png", full_page=False)
        # Radar draws: the canvas has non-background pixels.
        res["canvas_painted"] = await page.evaluate("""(() => {const c=document.getElementById('cb');
          const d=c.getContext('2d').getImageData(0,0,c.width,c.height).data;let n=0;
          for(let i=3;i<d.length;i+=4){if(d[i])n++}return n})()""")
        await ctx.close()

        # Reduced motion: no animation loop, still drawn.
        ctx = await b.new_context(viewport={"width": 1100, "height": 900}, reduced_motion="reduce")
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", "reduced", str(e))))
        await page.goto(BASE + "/nearby")
        await page.wait_for_timeout(3500)
        await page.screenshot(path=f"{OUT}/nearby-reduced.png")
        await ctx.close()
        await b.close()
    print(json.dumps(res, indent=1, ensure_ascii=False))
    print("ERRORS:", json.dumps(errors, indent=1))

asyncio.run(main())
