"""Signing the page checks in to the mock board: from firmware 0.13 every page
and API call needs a session, as a cookie in the browser or a bearer token.

    ctx = await signin.context(browser, viewport=...)   # a signed-in context
    signin.api("/api/health")                            # a signed-in request
"""
import json
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:8765"
PASSWORD = "update-password-1"      # the mock board's update password


def token(remember=True, password=PASSWORD):
    req = urllib.request.Request(BASE + "/api/login", method="POST",
                                 data=json.dumps({"password": password, "remember": remember}).encode(),
                                 headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req).read())["token"]


async def context(browser, **kw):
    ctx = await browser.new_context(**kw)
    await ctx.add_cookies([{"name": "nm_s", "value": token(), "url": BASE}])
    return ctx


_TOKEN = None


def api(path, body=None):
    """GET, or POST with a JSON body, as a signed-in client; the parsed answer.
    Signs in again when a test has ended the session meanwhile."""
    global _TOKEN
    for attempt in (0, 1):
        if _TOKEN is None:
            _TOKEN = token()
        headers = {"Authorization": "Bearer " + _TOKEN}
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(BASE + path, data=data, method="POST" if body is not None else "GET",
                                     headers=headers)
        try:
            return json.loads(urllib.request.urlopen(req).read())
        except urllib.error.HTTPError as e:
            if e.code != 401 or attempt:
                raise
            _TOKEN = None


def hook(path):
    """The mock's own test hooks (/__...), which need no session."""
    urllib.request.urlopen(urllib.request.Request(BASE + path, data=b"", method="POST")).read()
