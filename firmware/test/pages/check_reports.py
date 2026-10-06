"""The Settings page's Saved reports section against the mock board: the
list, the board's clock set from the page, saving now, downloading JSON and
CSV (with a name that would run as a formula written as text), deleting with
two taps, an older board without reports, and the phone layout.

  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python check_reports.py shots
"""
import asyncio, csv, io, json, sys, urllib.request
from playwright.async_api import async_playwright
import os as _os
sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import signin

BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"


def get(path):
    return signin.api(path)


def post(path):
    urllib.request.urlopen(urllib.request.Request(BASE + path, data=b"", method="POST")).read()


async def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    post("/__reports")
    errors, res = [], {}
    async with async_playwright() as p:
        b = await p.chromium.launch()
        ctx = await signin.context(b, viewport={"width": 1100, "height": 1000}, accept_downloads=True)
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", str(e))))
        page.on("console", lambda m: m.type == "error" and errors.append(("console", m.text)))

        # The dashboard hands the board its clock when it has none.
        res["clock_unset_at_first"] = get("/api/health")["clock"] is False
        await page.goto(BASE + "/")
        await page.wait_for_timeout(1200)
        res["clock_set_by_dashboard"] = get("/api/health")["clock"] is True

        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1200)
        rows = await page.eval_on_selector_all("#reprows tr", "r => r.map(x => x.innerText)")
        res["two_networks"] = len(rows) == 2
        res["current_first"] = bool(rows) and "HOME-2.4" in rows[0] and "On it now" in rows[0]
        res["undated_report"] = len(rows) > 1 and "before the last restart" in rows[1]
        note = await page.inner_text("#repnote")
        res["note"] = "This board is on HOME-2.4" in note and "Room for 4 networks" in note
        await page.screenshot(path=f"{OUT}/settings-reports.png", full_page=True)

        # JSON straight from the board, with a file name of its own.
        async with page.expect_download() as dl:
            await page.click("#reprows tr:first-child a.dl")
        d = await dl.value
        res["json_name"] = d.suggested_filename.startswith("netmon-HOME-2.4-") and d.suggested_filename.endswith(".json")
        j = json.loads(open(await d.path(), encoding="utf-8").read())
        res["json_whole"] = j["ssid"] == "HOME-2.4" and len(j["devices"]) == j["count"] > 20

        # CSV made in the page.
        async with page.expect_download() as dl:
            await page.click("#reprows tr:nth-child(2) button[data-csv]")
        d = await dl.value
        res["csv_name"] = d.suggested_filename.startswith("netmon-Office_Guest-report") and d.suggested_filename.endswith(".csv")
        text = open(await d.path(), encoding="utf-8-sig").read()
        table = list(csv.reader(io.StringIO(text)))
        res["csv_header"] = table[0][:5] == ["network", "subnet", "saved", "mac", "ip"]
        res["csv_rows"] = len(table) == 1 + 6
        res["csv_formula_defused"] = table[1][0] == "Office =Guest" and all(not c.startswith("=") for r in table for c in r)
        res["csv_undated"] = table[1][2] == "" and table[1][10] == ""

        # Save now: the current network's report, dated now the board has a clock.
        await page.click("#repsave")
        await page.wait_for_timeout(600)
        res["saved_msg"] = await page.inner_text("#repmsg") == "Saved."
        rows = await page.eval_on_selector_all("#reprows tr", "r => r.map(x => x.innerText)")
        res["saved_now"] = bool(rows) and "just now" in rows[0]

        # Delete takes two taps.
        await page.click("#reprows tr:nth-child(2) button[data-del]")
        await page.wait_for_timeout(200)
        res["armed"] = await page.inner_text("#reprows tr:nth-child(2) button[data-del]") == "Confirm"
        res["not_yet"] = len(get("/api/reports")["reports"]) == 2
        await page.click("#reprows tr:nth-child(2) button[data-del]")
        await page.wait_for_timeout(600)
        res["deleted"] = len(get("/api/reports")["reports"]) == 1 and \
            await page.eval_on_selector_all("#reprows tr", "r => r.length") == 1
        await ctx.close()

        # A board from before 0.12.
        ctx = await signin.context(b, viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror old", str(e))))
        await page.route("**/api/reports", lambda r: r.fulfill(status=404, body="not found", content_type="text/plain"))
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1200)
        res["old_board"] = "need firmware 0.12" in await page.inner_text("#repnote") and \
            await page.is_disabled("#repsave")
        await ctx.close()

        # Phone width.
        post("/__reports")
        ctx = await signin.context(b, viewport={"width": 360, "height": 780}, device_scale_factor=2,
                                  color_scheme="dark", is_mobile=True, has_touch=True)
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror phone", str(e))))
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1200)
        res["phone_no_overflow"] = await page.evaluate("document.documentElement.scrollWidth") <= 360
        el = await page.query_selector("#reptbl")
        await el.scroll_into_view_if_needed()
        await page.screenshot(path=f"{OUT}/settings-reports-phone.png")
        await ctx.close()
        await b.close()

    post("/__reports")
    for k, v in res.items():
        print(("ok  " if v else "FAIL"), k)
    for e in errors:
        print("ERROR", e)
    sys.exit(1 if [k for k, v in res.items() if not v] or errors else 0)


asyncio.run(main())
