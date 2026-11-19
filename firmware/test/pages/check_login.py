"""Signing in (firmware 0.13) against the mock board: every page sends you to
the sign-in page and back, wrong passwords and the wait after too many,
"Remember me" as a 30-day cookie or one that ends with the browser, a
session ending while a page is open, where the page may send you afterwards,
the Settings page's Signing in section (own login password, signing out,
signing out everywhere) and its MAC address setting, and the phone layout.

  python mock_nearby.py ../../netmon/src/hw/pages.h 8765 &
  python check_login.py shots
"""
import asyncio, os, sys, time
from urllib.parse import quote, unquote
from playwright.async_api import async_playwright

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import signin

BASE = signin.BASE
OUT = sys.argv[1] if len(sys.argv) > 1 else "shots"
PW = signin.PASSWORD


async def sign_in(page, password=PW, remember=False):
    await page.fill("#pw", password)
    if remember:
        await page.check("#keep")
    else:
        await page.uncheck("#keep")
    await page.click("#go")


async def main():
    os.makedirs(OUT, exist_ok=True)
    signin.hook("/__auth?reset=1")
    signin.hook("/__ble?reset=1")
    errors, res = [], {}

    def watch(page, tag):
        page.on("pageerror", lambda e: errors.append(("pageerror", tag, str(e))))
        page.on("console", lambda m: m.type == "error" and not any(c in m.text for c in ("400", "401", "403", "429")) and errors.append(("console", tag, m.text)))

    async with async_playwright() as p:
        b = await p.chromium.launch()

        # --- A browser that has never signed in ----------------------------------
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        watch(page, "fresh")
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(600)
        res["page_sends_to_login"] = page.url.startswith(BASE + "/login?next=") and \
            unquote(page.url.split("next=")[1]) == "/settings"
        res["login_page_text"] = (await page.inner_text("h1")) == "Sign in" and \
            "0.14.0" in await page.inner_text("#ver") and "netmon at 127.0.0.1" in await page.inner_text("#who")
        res["autocomplete_names"] = await page.get_attribute("#pw", "autocomplete") == "current-password" and \
            await page.get_attribute("#user", "autocomplete") == "username"
        r = await page.request.get(BASE + "/api/health")
        res["api_needs_session"] = r.status == 401 and r.headers.get("x-netmon-login") == "required"
        await page.screenshot(path=f"{OUT}/login.png")

        # Show / hide the password.
        await page.fill("#pw", "abc")
        await page.click("#peek")
        res["peek_shows"] = await page.get_attribute("#pw", "type") == "text" and \
            await page.inner_text("#peek") == "Hide"
        await page.click("#peek")
        res["peek_hides"] = await page.get_attribute("#pw", "type") == "password"

        # Empty, then wrong.
        await page.fill("#pw", "")
        await page.click("#go")
        await page.wait_for_timeout(200)
        res["empty_refused"] = "Enter the password" in await page.inner_text("#err")
        await sign_in(page, "not-the-password")
        await page.wait_for_timeout(500)
        res["wrong_password"] = "not right" in await page.inner_text("#err") and "/login" in page.url
        await page.screenshot(path=f"{OUT}/login-wrong.png")

        # Right, not remembered: back to Settings, with a cookie that ends
        # with the browser.
        await sign_in(page, PW, remember=False)
        await page.wait_for_url(BASE + "/settings", timeout=3000)
        await page.wait_for_timeout(800)
        res["back_to_settings"] = page.url == BASE + "/settings"
        ck = [c for c in await ctx.cookies() if c["name"] == "nm_s"]
        res["session_cookie"] = len(ck) == 1 and ck[0]["expires"] == -1 and ck[0]["httpOnly"]
        info = await page.inner_text("#authinfo")
        res["authinfo_session"] = "until the browser closes" in info and "update password" in info

        # Signed in, the sign-in page goes straight on.
        await page.goto(BASE + "/login?next=%2Fmap")
        await page.wait_for_url(BASE + "/map", timeout=3000)
        res["login_when_signed_in"] = page.url == BASE + "/map"

        # Sign out from the dashboard's footer.
        await page.goto(BASE + "/")
        await page.wait_for_timeout(500)
        await page.click("text=Sign out")
        await page.wait_for_timeout(700)
        res["signed_out"] = page.url.startswith(BASE + "/login") and \
            not [c for c in await ctx.cookies() if c["name"] == "nm_s" and c["value"]]
        await page.goto(BASE + "/")
        await page.wait_for_timeout(400)
        res["out_means_out"] = page.url.startswith(BASE + "/login")
        await ctx.close()

        # --- Remember me ---------------------------------------------------------
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        watch(page, "remember")
        await page.goto(BASE + "/nearby#finder")
        await page.wait_for_timeout(500)
        res["hash_carried"] = page.url.endswith("#finder")
        await sign_in(page, PW, remember=True)
        await page.wait_for_url(BASE + "/nearby#finder", timeout=3000)
        res["back_with_hash"] = page.url == BASE + "/nearby#finder"
        if not res["back_with_hash"]:
            print("at", page.url)
        await page.wait_for_timeout(1000)
        ck = [c for c in await ctx.cookies() if c["name"] == "nm_s"]
        left = ck[0]["expires"] - time.time() if ck else 0
        res["remember_cookie_30_days"] = 29.9 * 86400 < left < 30.1 * 86400
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(800)
        res["authinfo_remembered"] = "remembered for 30 days" in await page.inner_text("#authinfo")
        # A session that ends while a page is open: the next request goes to
        # the sign-in page, which comes back here.
        signin.hook("/__auth?reset=1")
        await page.evaluate("fetch('/api/config');0")
        await page.wait_for_timeout(800)
        res["ended_session_to_login"] = page.url.startswith(BASE + "/login?next=") and \
            unquote(page.url.split("next=")[1]) == "/settings"
        res["tick_kept"] = await page.is_checked("#keep")
        await ctx.close()

        # --- Where "next" may send you -------------------------------------------
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        watch(page, "next")
        for bad in ["//example.com/x", "/\\example.com", "https://example.com/", "/\t/example.com",
                    "/\n/example.com", "javascript:alert(1)"]:
            await page.goto(BASE + "/login?next=" + quote(bad, safe=""))
            await page.wait_for_timeout(300)
            await sign_in(page, PW)
            await page.wait_for_timeout(700)
            res["next_refused " + repr(bad)] = page.url == BASE + "/"
            await page.evaluate("fetch('/api/logout',{method:'POST'}).then(()=>0)")
            await page.wait_for_timeout(200)
        await ctx.close()

        # --- Settings: own login password, signing out everywhere -----------------
        signin.hook("/__auth?reset=1")
        other = await signin.context(b, viewport={"width": 900, "height": 700})
        ctx = await signin.context(b, viewport={"width": 1100, "height": 1100})
        page = await ctx.new_page()
        watch(page, "settings")
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1000)
        res["sessions_counted"] = "2 browsers and phones" in await page.inner_text("#authinfo")
        res["set_label"] = await page.inner_text("#pwbtn") == "Set login password" and \
            await page.is_hidden("#pwdel")
        await page.fill("#pwcur", PW)
        await page.fill("#pwnew", "kitchen-radio-42")
        await page.fill("#pwnew2", "kitchen-radio-43")
        await page.click("#pwbtn")
        await page.wait_for_timeout(300)
        res["mismatch_refused"] = "not the same" in await page.inner_text("#authmsg")
        await page.fill("#pwnew2", "kitchen-radio-42")
        await page.fill("#pwcur", "wrong-current")
        await page.click("#pwbtn")
        await page.wait_for_timeout(400)
        res["wrong_current_refused"] = "current password is not right" in await page.inner_text("#authmsg")
        await page.fill("#pwcur", PW)
        await page.fill("#pwnew", "short")
        await page.fill("#pwnew2", "short")
        await page.click("#pwbtn")
        await page.wait_for_timeout(400)
        res["short_refused"] = "at least 8" in await page.inner_text("#authmsg")
        await page.fill("#pwcur", PW)
        await page.fill("#pwnew", "kitchen-radio-42")
        await page.fill("#pwnew2", "kitchen-radio-42")
        await page.click("#pwbtn")
        await page.wait_for_timeout(600)
        msg = await page.inner_text("#authmsg")
        res["password_set"] = "Login password set." in msg and "1 other browser or phone was signed out" in msg
        res["fields_cleared"] = await page.input_value("#pwnew") == "" and await page.input_value("#pwcur") == ""
        res["change_label"] = await page.inner_text("#pwbtn") == "Change login password" and \
            not await page.is_hidden("#pwdel")
        info = await page.inner_text("#authinfo")
        res["authinfo_own"] = "Your login password" in info and "1 browser or phone" in info
        await page.screenshot(path=f"{OUT}/settings-signin.png", full_page=True)
        # The other browser was signed out.
        op = await other.new_page()
        await op.goto(BASE + "/settings")
        await op.wait_for_timeout(400)
        res["other_signed_out"] = op.url.startswith(BASE + "/login")
        # Both passwords sign in now.
        await sign_in(op, "kitchen-radio-42")
        await op.wait_for_url(BASE + "/settings", timeout=3000)
        res["own_password_signs_in"] = True
        await op.evaluate("fetch('/api/logout',{method:'POST'}).then(()=>0)")
        await op.goto(BASE + "/login")
        await sign_in(op, PW)
        await op.wait_for_url(BASE + "/", timeout=3000)
        res["update_password_still_signs_in"] = True
        await other.close()
        # Removing it takes the current password and two taps.
        await page.fill("#pwcur", "kitchen-radio-42")
        await page.click("#pwdel")
        await page.wait_for_timeout(200)
        res["remove_armed"] = await page.inner_text("#pwdel") == "Confirm"
        await page.click("#pwdel")
        await page.wait_for_timeout(600)
        res["password_removed"] = "Login password removed" in await page.inner_text("#authmsg") and \
            signin.api("/api/auth")["own_password"] is False
        # Sign out everywhere: two taps, this browser too.
        await page.click("#soall")
        await page.wait_for_timeout(200)
        res["everywhere_armed"] = (await page.inner_text("#soall")).startswith("Confirm")
        await page.click("#soall")
        await page.wait_for_timeout(700)
        res["everywhere_out"] = page.url.startswith(BASE + "/login")
        await ctx.close()

        # --- Settings: the board's MAC address -------------------------------------
        signin.hook("/__auth?reset=1")
        ctx = await signin.context(b, viewport={"width": 1100, "height": 1100})
        page = await ctx.new_page()
        watch(page, "mac")
        posts = []
        page.on("request", lambda r: r.method == "POST" and posts.append((r.url.split(BASE)[1], r.post_data)))
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1000)
        res["mac_shown"] = await page.input_value("#macin") == "D4:E9:F4:12:34:56" and \
            "chip's own address" in await page.inner_text("#machelp")
        await page.click("text=Random address")
        v = await page.input_value("#macin")
        res["random_is_local_unicast"] = len(v) == 17 and (int(v[:2], 16) & 3) == 2
        res["random_not_saved_yet"] = "Not saved yet" in await page.inner_text("#machelp")
        await page.fill("#macin", "03:11:22:33:44:55")
        await page.click("text=Save and restart")
        await page.wait_for_timeout(300)
        res["group_refused"] = "group" in await page.inner_text("#msg") and \
            not [x for x in posts if x[0] in ("/api/config", "/api/mac")]
        await page.fill("#macin", "zz")
        await page.wait_for_timeout(100)
        res["unreadable_hint"] = "Six pairs" in await page.inner_text("#machelp")
        await page.fill("#macin", "02-1a-2b-3c-4d-5e")
        await page.click("text=Save and restart")
        await page.wait_for_timeout(800)
        sent = [x for x in posts if x[0] == "/api/mac"]
        res["mac_posted"] = len(sent) == 1 and '"02:1A:2B:3C:4D:5E"' in sent[0][1]
        res["config_before_mac_then_reboot"] = [x[0] for x in posts][-3:] == ["/api/config", "/api/mac", "/api/reboot"]
        res["restart_message"] = "02:1A:2B:3C:4D:5E" in await page.inner_text("#msg") and \
            "new IP address" in await page.inner_text("#msg")
        await page.reload()
        await page.wait_for_timeout(1000)
        res["custom_in_use"] = await page.input_value("#macin") == "02:1A:2B:3C:4D:5E" and \
            "Your own address, in use now" in await page.inner_text("#machelp") and \
            "02:1A:2B:3C:4D:5E" in await page.inner_text("#act")
        await page.screenshot(path=f"{OUT}/settings-mac.png", full_page=True)
        # Unchanged, nothing is sent; the chip's own goes back as "".
        posts.clear()
        await page.click("text=Save and restart")
        await page.wait_for_timeout(500)
        res["unchanged_not_sent"] = "/api/mac" not in [x[0] for x in posts]
        await page.click("button:has-text(\"The chip's own\")")
        await page.click("text=Save and restart")
        await page.wait_for_timeout(600)
        sent = [x for x in posts if x[0] == "/api/mac"]
        res["back_to_chip"] = len(sent) == 1 and '"mac":""' in sent[0][1].replace(" ", "") and \
            signin.api("/api/config")["mac"]["custom"] == ""
        await ctx.close()

        # --- Phone width, dark --------------------------------------------------------
        ctx = await b.new_context(viewport={"width": 360, "height": 740}, device_scale_factor=2,
                                  color_scheme="dark", is_mobile=True, has_touch=True)
        page = await ctx.new_page()
        watch(page, "phone")
        await page.goto(BASE + "/")
        await page.wait_for_timeout(500)
        res["phone_login_fits"] = await page.evaluate("document.documentElement.scrollWidth") <= 360
        await page.screenshot(path=f"{OUT}/login-phone-dark.png")
        await sign_in(page, PW)
        await page.wait_for_url(BASE + "/", timeout=3000)
        await page.goto(BASE + "/settings")
        await page.wait_for_timeout(1000)
        res["phone_settings_fits"] = await page.evaluate("document.documentElement.scrollWidth") <= 360
        el = await page.query_selector("#authinfo")
        await el.scroll_into_view_if_needed()
        await page.screenshot(path=f"{OUT}/settings-signin-phone.png")
        await ctx.close()

        # --- Too many wrong passwords ------------------------------------------------
        ctx = await b.new_context(viewport={"width": 1100, "height": 900})
        page = await ctx.new_page()
        watch(page, "throttle")
        await page.goto(BASE + "/login")
        for _ in range(6):
            await sign_in(page, "guess-guess")
            await page.wait_for_timeout(250)
        await sign_in(page, "guess-guess")
        await page.wait_for_timeout(400)
        t = await page.inner_text("#err")
        res["throttled"] = "Too many wrong passwords" in t and await page.is_disabled("#go")
        await page.wait_for_timeout(1200)
        t2 = await page.inner_text("#err")
        res["countdown_runs"] = t2 != t and "Try again in" in t2
        await page.screenshot(path=f"{OUT}/login-throttled.png")
        await ctx.close()
        await b.close()

    signin.hook("/__auth?reset=1")
    for k, v in res.items():
        print(("ok  " if v else "FAIL"), k)
    for e in errors:
        print("ERROR", e)
    bad = [k for k, v in res.items() if not v]
    sys.exit(1 if bad or errors else 0)


asyncio.run(main())
