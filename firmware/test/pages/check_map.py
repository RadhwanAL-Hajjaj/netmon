"""The Map page against the mock board: groups, choosing a group, a device, the
router, the internet and an access point; offline devices; grouping by
status; the Devices page search it links to; keyboard; phone width."""
import asyncio, json, sys
from playwright.async_api import async_playwright
import os as _os
sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import signin
BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"

async def click_svg(page, x, y):
    """Clicks a point given in the map's own coordinates."""
    pt = await page.evaluate("""([x,y])=>{const s=document.getElementById('map'),r=s.getBoundingClientRect(),
      b=L.box,k=r.width/b[2];return [r.left+(x-b[0])*k, r.top+(y-b[1])*k]}""", [x, y])
    await page.mouse.click(pt[0], pt[1])

async def main():
    res, errors = {}, []
    async with async_playwright() as p:
        b = await p.chromium.launch()
        for scheme in ("light", "dark"):
            ctx = await signin.context(b, viewport={"width": 1100, "height": 1000}, color_scheme=scheme)
            page = await ctx.new_page()
            page.on("pageerror", lambda e, s=scheme: errors.append(("pageerror", s, str(e))))
            page.on("console", lambda m, s=scheme: m.type == "error" and errors.append(("console", s, m.text)))
            await page.goto(BASE + "/map")
            await page.wait_for_timeout(2000)
            await page.screenshot(path=f"{OUT}/map-{scheme}.png", full_page=True)
            if scheme == "dark":
                await ctx.close()
                continue
            res["count"] = await page.inner_text("#count")
            res["state"] = await page.inner_text("#state")
            res["groups"] = await page.evaluate("L.cs.map(c=>c.title+' '+c.items.length)")
            # Overlaps: no two bubbles or labels on top of each other.
            res["overlaps"] = await page.evaluate("""(()=>{let n=0;const cs=L.cs;
              for(let i=0;i<cs.length;i++)for(let j=i+1;j<cs.length;j++){const a=cs[i],c=cs[j];
               if(Math.hypot(a.x-c.x,a.y-c.y)<a.r+c.r)n++;
               const ba=boxes(a),bc=boxes(c);if(hitRect(ba,bc))n++;if(hitCircle(c,ba)||hitCircle(a,bc))n++}
              return n})()""")
            # A group: its devices listed.
            g = await page.evaluate("(()=>{const c=L.cs.find(c=>c.key=='iot');return [c.x,c.y+c.r-6]})()")
            await click_svg(page, g[0], g[1])
            await page.wait_for_timeout(300)
            res["group_info"] = (await page.inner_text("#info"))[:300]
            res["group_rows"] = await page.eval_on_selector_all("#info li", "l => l.length")
            # A device from the list: details, and the line goes to the Devices page.
            await page.click("#info li:first-child button")
            await page.wait_for_timeout(300)
            res["device_info"] = await page.inner_text("#info")
            res["device_link"] = await page.get_attribute("#info .acts a", "href")
            res["selected_ring"] = await page.evaluate("!!document.querySelector('#map .sel')")
            # Tap near a dot rather than on it.
            d = await page.evaluate("(()=>{const c=L.cs.find(c=>c.key=='cam');const it=c.items[0];return [it.x+7,it.y+5]})()")
            await click_svg(page, d[0], d[1])
            await page.wait_for_timeout(300)
            res["near_dot_info"] = (await page.inner_text("#info"))[:120]
            await click_svg(page, 0, 0)
            await page.wait_for_timeout(300)
            res["router_info"] = await page.inner_text("#info")
            await click_svg(page, 0, await page.evaluate("L.iy"))
            await page.wait_for_timeout(300)
            res["internet_info"] = await page.inner_text("#info")
            ap = await page.evaluate("(()=>{const c=L.cs.find(c=>c.wifi);const it=c.items.find(i=>i.ap&&!i.ap.joined);return [it.x,it.y]})()")
            await click_svg(page, ap[0], ap[1])
            await page.wait_for_timeout(300)
            res["ap_info"] = await page.inner_text("#info")
            # "Find it with the board" hands the access point to the Finder.
            await page.click("#info a[data-find]")
            await page.wait_for_timeout(5000)
            res["finder_from_map"] = [await page.evaluate("location.pathname+location.hash"),
                                      await page.evaluate("F.t&&F.t.addr"), await page.is_visible("#fgo")]
            await page.click("#fstop")
            await page.goto(BASE + "/map")
            await page.wait_for_timeout(1500)
            # Offline devices, then grouping by status.
            before = await page.evaluate("L.cs.reduce((s,c)=>s+(c.wifi?0:c.items.length),0)")
            await page.check("#off")
            await page.wait_for_timeout(300)
            after = await page.evaluate("L.cs.reduce((s,c)=>s+(c.wifi?0:c.items.length),0)")
            res["devices_shown_before_after_offline"] = [before, after]
            await page.select_option("#by", "status")
            await page.wait_for_timeout(300)
            res["status_groups"] = await page.evaluate("L.cs.map(c=>c.title+' '+c.items.length)")
            await page.screenshot(path=f"{OUT}/map-status.png", full_page=True)
            res["kept"] = await page.evaluate("[localStorage.getItem('nm.map.by'),localStorage.getItem('nm.map.off')]")
            # Keyboard: Tab to a group and press Enter.
            await page.select_option("#by", "kind")
            await page.uncheck("#off")
            await page.focus("#map [data-k=pc]")
            await page.keyboard.press("Enter")
            await page.wait_for_timeout(300)
            res["keyboard_group"] = (await page.inner_text("#info"))[:60]
            # The Devices page opened with a search.
            await page.goto(BASE + "/?q=192.168.2.70")
            await page.wait_for_timeout(2500)
            res["devices_search"] = [await page.input_value("#q"),
                                     await page.eval_on_selector_all("#rows tr", "r => r.length")]
            await ctx.close()
        # Phone.
        ctx = await signin.context(b, viewport={"width": 360, "height": 780}, device_scale_factor=2,
                                  is_mobile=True, has_touch=True)
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", "phone", str(e))))
        await page.goto(BASE + "/map")
        await page.wait_for_timeout(2000)
        res["phone_overflow"] = await page.evaluate("document.documentElement.scrollWidth")
        res["phone_scale"] = await page.evaluate("Math.round(document.getElementById('map').getBoundingClientRect().width/L.box[2]*100)/100")
        await page.screenshot(path=f"{OUT}/map-phone360.png", full_page=True)
        await ctx.close()
        await b.close()
    print(json.dumps(res, indent=1, ensure_ascii=False))
    print("ERRORS:", json.dumps(errors, indent=1))

asyncio.run(main())
