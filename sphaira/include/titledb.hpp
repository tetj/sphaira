#pragma once

#include <switch.h>

namespace sphaira::titledb {

// Returns true if the titledb data has been loaded and is ready to query.
auto IsReady() -> bool;

// Returns the max numberOfPlayers for the given title ID.
// Returns 0 if the title is not found or data is not yet ready.
auto GetNumberOfPlayers(u64 app_id) -> u32;

// Starts a background download + parse if the local cache is missing or older
// than 1 month. Safe to call multiple times — only starts once per session.
void DownloadIfNeeded();

} // namespace sphaira::titledb
