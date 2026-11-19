#pragma once
// The login's state in flash: the owner's login password, as a salted hash,
// and the sessions, as their tokens' hashes. See src/core/auth.h for the
// rules. LittleFS is mounted by settings_load(), which runs first at boot.
#include <cstddef>
#include <cstdint>

#include "../core/auth.h"

static const size_t kAuthSessions = kSessionSlots;

struct AuthState {
    PasswordHash login;                     // .set false: the update password only
    SessionTable<kAuthSessions> sessions;
};

// Reads /auth.json. Sessions come back marked as made before this boot.
// False, with an empty state, when there is nothing saved or it cannot be read.
bool auth_load(AuthState& a);

// Writes it beside the old file and renames it over, so a power cut leaves
// one or the other whole.
bool auth_save(const AuthState& a);

// Random bytes from the hardware generator, which the radio keeps fed.
void auth_random(uint8_t* out, size_t n);

// A new session token, 32 hex digits.
void auth_new_token(char out[kTokenHex + 1]);
