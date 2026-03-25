#pragma once

#include <switch.h>

namespace sphaira::titledb {

// Returns true if the titledb data has been loaded and is ready to query.
auto IsReady() -> bool;

// Returns the max numberOfPlayers for the given title ID. Returns 0 if not found.
auto GetNumberOfPlayers(u64 app_id) -> u32;

// Returns the raw ESRB rating value for the given title ID. Returns 0 if not found.
// Mapping: 0=unknown, 1-9=Everyone, 10-12=Everyone10+, 13-16=Teen, 17+=Mature
auto GetRating(u64 app_id) -> u32;

// Starts a background download + parse if the local cache is missing or older
// than 1 month. Safe to call multiple times — only starts once per session.
void DownloadIfNeeded();

// Waits for any in-progress background thread to finish and cleans up.
// Must be called before the NRO is unmapped (i.e. during App teardown).
void Exit();

} // namespace sphaira::titledb
