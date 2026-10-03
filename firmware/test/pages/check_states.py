import asyncio, json
from playwright.async_api import async_playwright
BASE="http://127.0.0.1:8765"
async def main():
    out={};errs=[]
    async with async_playwright() as p:
        b=await p.chromium.launch();page=await b.new_page(viewport={"width":1100,"height":900})
        page.on("pageerror",lambda e:errs.append(str(e)))
        await page.request.post(BASE+"/__nearby?unavailable=1")
        await page.goto(BASE+"/nearby");await page.wait_for_timeout(3500)
        out["unavailable_state"]=await page.inner_text("#state")
        out["unavailable_ble_empty"]=await page.inner_text("#eb")
        await page.request.post(BASE+"/__nearby?unavailable=0")
        await page.uncheck("#onw");await page.wait_for_timeout(3600)
        out["wifi_off_state"]=await page.inner_text("#state")
        out["wifi_off_empty"]=await page.inner_text("#ew")
        out["wifi_off_count"]=await page.inner_text("#count")
        await page.check("#onw");await page.wait_for_timeout(500)
        # A refused change is reported and the switch put back.
        r=await page.evaluate("fetch('/api/nearby/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({background_s:5})}).then(r=>r.status)")
        out["bad_bg_status"]=r
        import os; os.makedirs("shots", exist_ok=True)
        await page.screenshot(path="shots/states.png")
        await b.close()
    print(json.dumps(out,indent=1));print("ERRORS",errs)
asyncio.run(main())
