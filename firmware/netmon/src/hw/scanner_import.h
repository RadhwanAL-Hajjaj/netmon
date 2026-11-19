#pragma once
#include "config_store.h"

// A board that ran the Wi-Fi + BLE scanner sketch keeps that sketch's network
// in NVS, under "scanner". When this firmware starts with no network of its
// own, it takes that one, so flashing it onto the scanner board does not mean
// a trip through netmon-setup. The old entry is then erased: left behind, it
// would come back the next time somebody forgot every network on purpose.
//
// Returns true when a network was imported and saved into `s`.
bool scanner_import(Settings& s);
