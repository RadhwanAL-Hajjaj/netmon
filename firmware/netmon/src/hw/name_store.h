#pragma once
#include <cstddef>

#include "../core/name_cache.h"

// Hostnames learned from DHCP, kept in flash so they outlive a restart.
static const size_t kNamesKept = 64;
using Names = NameCache<kNamesKept>;

// Loads /names.txt into `names`. False when there is no file yet.
bool names_load(Names& names);

// Rewrites /names.txt. False when the flash could not be written.
bool names_save(const Names& names);
