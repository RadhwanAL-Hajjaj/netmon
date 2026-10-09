"""0.12's pages against the mock board: Trust and Forget on the Devices page,
the LAN watch's alerts on the Devices and Events pages, and Learn again on
Settings. Prints what it found and lists any script errors; screenshots go in
the folder given.

  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python check_guard.py shots
"""
import asyncio, json, sys, urllib.request
from playwright.async_api import async_playwright

import signin

BASE = "http://127.0.0.1:8765"
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"
UNKNOWN = "A4:CF:12:44:55:66"
KNOWN = "00:11:32:AA:BB:CC"        # DiskStation
SELF = "D4:E9:F4:12:34:56"
PRIVATE = "DA:A1:19:77:88:99"


def hook(q):
    urllib.request.urlopen(urllib.request.Request(BASE + "/__guard?" + q, data=b"", method="POST")).read()


def posts():
    log = json.loads(urllib.request.urlopen(BASE + "/__log").read())
    return [e for e in log if e[0] == "POST" and not e[1].startswith("/__")]


async def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    errors, res = [], {}
    hook("reset=1")
    async with async_playwright() as p:
        b = await p.chromium.launch()
        ctx = await signin.context(b, viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        page.on("pageerror", lambda e: errors.append(("pageerror", str(e))))
        page.on("console", lambda m: m.type == "error" and errors.append(("console", m.text)))

        # ---- Devices: no alerts, two unknown devices.
        await page.goto(BASE + "/")
        await page.wait_for_timeout(1200)
        res["verdict_start"] = await page.inner_text("#verdict")
        if "2 devices are not recognised" not in res["verdict_start"]:
            errors.append(("verdict", res["verdict_start"]))
        res["panel_hidden_at_start"] = await page.is_hidden("#act")

        # Pick the unknown device: Trust and Forget both offered.
        await page.click(f'tr[data-m="{UNKNOWN}"]')
        res["unknown_panel"] = await page.inner_text("#act")
        res["unknown_buttons"] = [await page.is_visible("#atrust"), await page.is_visible("#aforget")]
        if res["unknown_buttons"] != [True, True]:
            errors.append(("unknown buttons", res["unknown_buttons"]))
        res["row_selected"] = await page.eval_on_selector(f'tr[data-m="{UNKNOWN}"]', "r => r.classList.contains('sel')")
        await page.screenshot(path=f"{OUT}/devices-selected.png")
        await page.click("#atrust")
        await page.wait_for_timeout(900)
        res["after_trust_status"] = await page.inner_text(f'tr[data-m="{UNKNOWN}"] td.st')
        res["after_trust_msg"] = await page.inner_text("#amsg")
        res["after_trust_verdict"] = await page.inner_text("#verdict")
        res["trust_hidden_after"] = await page.is_hidden("#atrust")
        if res["after_trust_status"] != "known" or "1 device is not recognised" not in res["after_trust_verdict"]:
            errors.append(("trust", res["after_trust_status"], res["after_trust_verdict"]))

        # A known device: Forget only, and only on the second tap.
        await page.click(f'tr[data-m="{KNOWN}"]')
        res["known_buttons"] = [await page.is_visible("#atrust"), await page.is_visible("#aforget")]
        if res["known_buttons"] != [False, True]:
            errors.append(("known buttons", res["known_buttons"]))
        await page.click("#aforget")
        res["forget_armed"] = await page.inner_text("#aforget")
        before = len(posts())
        await page.wait_for_timeout(200)
        res["no_post_on_first_tap"] = len(posts()) == before
        await page.click("#aforget")
        await page.wait_for_timeout(900)
        res["forgotten_row_gone"] = await page.query_selector(f'tr[data-m="{KNOWN}"]') is None
        res["panel_hidden_after_forget"] = await page.is_hidden("#act")
        if not (res["forgotten_row_gone"] and res["panel_hidden_after_forget"] and res["no_post_on_first_tap"]):
            errors.append(("forget", res["forgotten_row_gone"], res["panel_hidden_after_forget"]))

        # The monitor itself: nothing to do. A private address: Forget only.
        await page.click(f'tr[data-m="{SELF}"]')
        res["self_buttons"] = [await page.is_visible("#atrust"), await page.is_visible("#aforget")]
        res["self_msg"] = await page.inner_text("#amsg")
        if res["self_buttons"] != [False, False]:
            errors.append(("self buttons", res["self_buttons"]))
        await page.click(f'tr[data-m="{PRIVATE}"]')
        res["private_buttons"] = [await page.is_visible("#atrust"), await page.is_visible("#aforget")]
        # Picking the same row again closes the panel; so does the close button.
        await page.click(f'tr[data-m="{PRIVATE}"]')
        res["closed_by_second_pick"] = await page.is_hidden("#act")
        # The keyboard: Enter on a focused row picks it.
        await page.focus(f'tr[data-m="{SELF}"]')
        await page.keyboard.press("Enter")
        res["enter_picks"] = await page.is_visible("#act")
        await page.click("#aclose")
        res["closed_by_button"] = await page.is_hidden("#act")

        # ---- the LAN watch noticed things.
        hook("alerts=1")
        await page.goto(BASE + "/")
        await page.wait_for_timeout(1200)
        res["verdict_alerts"] = await page.inner_text("#verdict")
        res["verdict_link"] = await page.get_attribute("#verdict a", "href")
        if "weaker security" not in res["verdict_alerts"] or "4 more" not in res["verdict_alerts"]:
            errors.append(("alert verdict", res["verdict_alerts"]))
        await page.screenshot(path=f"{OUT}/devices-alerts.png")

        # ---- Events: the alerts, their buttons, and the new event types.
        await page.goto(BASE + "/events")
        await page.wait_for_timeout(1500)
        res["alert_count"] = await page.eval_on_selector_all("#alerts .al", "a => a.length")
        res["alert_buttons"] = await page.eval_on_selector_all("#alerts .al button", "a => a.map(b => b.textContent)")
        res["watch"] = await page.inner_text("#watch")
        res["event_types"] = await page.eval_on_selector_all(".event .type", "a => a.map(t => t.textContent)")
        res["alert_type_colour_differs"] = await page.evaluate(
            "getComputedStyle(document.querySelector('.event.router_changed .type')).color !="
            " getComputedStyle(document.querySelector('.event.seen .type')).color")
        await page.screenshot(path=f"{OUT}/events-alerts.png", full_page=True)
        if res["alert_count"] != 5 or "Dismiss" not in res["alert_buttons"]:
            errors.append(("alerts", res["alert_count"], res["alert_buttons"]))
        if "router" not in res["event_types"] or "rogue ap" not in res["event_types"]:
            errors.append(("event labels", res["event_types"]))
        # Dismiss the clash, accept the router.
        idx = res["alert_buttons"].index("Dismiss")
        await page.click(f"#alerts button[data-i='{idx}']")
        await page.wait_for_timeout(900)
        texts = await page.eval_on_selector_all("#alerts .al b", "a => a.map(b => b.textContent)")
        res["after_dismiss"] = texts
        i = [t.startswith("The router") for t in texts].index(True)
        await page.click(f"#alerts button[data-i='{i}']")
        await page.wait_for_timeout(900)
        res["after_accept"] = await page.eval_on_selector_all("#alerts .al b", "a => a.map(b => b.textContent)")
        sent = [json.loads(e[2]) for e in posts() if e[1] == "/api/guard/accept"]
        res["accept_bodies"] = sent
        if len(res["after_accept"]) != 3 or not any(s["type"] == "router_changed" for s in sent):
            errors.append(("accept", res["after_accept"]))
        res["xss_safe"] = await page.evaluate("!document.querySelector('#alerts script')")

        # ---- Settings: what is kept, and Learn again on the second tap.
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1500)
        res["basenote"] = await page.inner_text("#basenote")
        if "recognised on HOME-2.4" not in res["basenote"]:
            errors.append(("basenote", res["basenote"]))
        await page.click("#relearn")
        res["relearn_armed"] = await page.inner_text("#relearn")
        await page.click("#relearn")
        await page.wait_for_timeout(1200)
        res["relearn_msg"] = await page.inner_text("#basemsg")
        res["basenote_learning"] = await page.inner_text("#basenote")
        if "Learning HOME-2.4" not in res["basenote_learning"]:
            errors.append(("relearn", res["basenote_learning"]))
        res["relearn_posts"] = sum(1 for e in posts() if e[1] == "/api/guard/relearn")
        await ctx.close()

        # ---- Phone width and dark: the panel and the alerts fit.
        hook("alerts=1&learning=0")
        for scheme in ("light", "dark"):
            ctx = await signin.context(b, viewport={"width": 360, "height": 780}, device_scale_factor=2,
                                       color_scheme=scheme, is_mobile=True, has_touch=True)
            page = await ctx.new_page()
            page.on("pageerror", lambda e: errors.append(("pageerror", "phone", str(e))))
            await page.goto(BASE + "/")
            await page.wait_for_timeout(1500)
            await page.click(f'tr[data-m="{PRIVATE}"]')
            await page.wait_for_timeout(300)
            sw = await page.evaluate("document.documentElement.scrollWidth")
            if sw > 360:
                errors.append(("overflow", scheme, "/", sw))
            await page.screenshot(path=f"{OUT}/devices-phone-{scheme}.png")
            await page.goto(BASE + "/events")
            await page.wait_for_timeout(1500)
            sw = await page.evaluate("document.documentElement.scrollWidth")
            if sw > 360:
                errors.append(("overflow", scheme, "/events", sw))
            await page.screenshot(path=f"{OUT}/events-phone-{scheme}.png", full_page=True)
            await ctx.close()
        await b.close()
    hook("reset=1")
    print(json.dumps(res, indent=1))
    print("ERRORS:", errors)
    sys.exit(1 if errors else 0)

asyncio.run(main())
