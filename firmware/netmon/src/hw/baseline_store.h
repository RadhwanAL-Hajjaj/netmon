#pragma once
// Each network's record (baseline.h) on LittleFS, one file per network.
#include "../core/baseline.h"
#include "config_store.h"

// Loads the record kept for `b.ssid`. True when this network had one; false
// leaves `b` as it was, empty for a network the board has not learned.
bool baseline_load(Baseline& b);

// Writes the record, beside the old file and renamed over it, so a power cut
// mid-write leaves the previous record rather than half of a new one.
bool baseline_save(const Baseline& b);

// Deletes a network's record, for a network the board is told to forget.
bool baseline_remove(const char* ssid);

// Deletes the records of networks no longer in the saved list: one that fell
// off the end when a fifth was saved leaves its file behind otherwise.
// Returns how many went.
size_t baseline_prune(const Settings& s);
