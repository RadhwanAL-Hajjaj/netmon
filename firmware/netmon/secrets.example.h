#pragma once
// Copy this file to secrets.h, in this same folder, and change the value.
// secrets.h is listed in .gitignore, so your password never ends up in git.
//
// The update password protects firmware updates: the Firmware update section
// of the Settings page, POST /api/update (used by the Android app) and
// ArduinoOTA. Anyone who knows it, on the same network as the board, can
// install new firmware on it. At least 8 characters; the build refuses the
// placeholder below.
#define NETMON_UPDATE_PASSWORD "change-me"
