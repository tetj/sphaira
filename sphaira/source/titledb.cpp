#include "titledb.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "utils/thread.hpp"

#include <yyjson.h>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <cstdlib>
#include <sys/stat.h>
#include <time.h>

namespace sphaira::titledb {
namespace {

constexpr const char TITLEDB_CACHE_DIR[] = "/switch/sphaira/cache/titledb";
constexpr const char TITLEDB_PATH[] = "/switch/sphaira/cache/titledb/US.en.json";
constexpr const char TITLEDB_URL[] = "https://raw.githubusercontent.com/blawar/titledb/refs/heads/master/US.en.json";
constexpr time_t MAX_AGE_SECONDS = 30LL * 24 * 60 * 60; // 30 days

std::atomic_bool g_ready{};
std::atomic_bool g_started{};
Mutex g_mutex{};
std::unique_ptr<utils::Async> g_parse_async{};

struct TitleInfo {
    u32 players{};
    u32 rating{};
};
std::unordered_map<u64, TitleInfo> g_title_map{};

auto IsStale() -> bool {
    struct stat st;
    if (::stat(TITLEDB_PATH, &st) != 0) {
        log_write_boot("[titledb] file not found at %s, will download\n", TITLEDB_PATH);
        return true;
    }

    const auto now = ::time(nullptr);
    const auto age = now - st.st_mtime;
    log_write_boot("[titledb] file found: size=%lld bytes, mtime=%lld, now=%lld, age=%lld days\n",
        (long long)st.st_size, (long long)st.st_mtime, (long long)now,
        (long long)(age / 86400));

    if (st.st_size == 0) {
        log_write_boot("[titledb] file is empty, will re-download\n");
        return true;
    }

    return age > MAX_AGE_SECONDS;
}

void DoParse() {
    log_write_boot("[titledb] DoParse() started\n");

    struct stat st;
    if (::stat(TITLEDB_PATH, &st) == 0) {
        log_write_boot("[titledb] file size before parse: %lld bytes\n", (long long)st.st_size);
    } else {
        log_write_boot("[titledb] stat() failed before parse — file may not exist\n");
        return;
    }

    yyjson_read_err err{};
    auto doc = yyjson_read_file(TITLEDB_PATH, YYJSON_READ_NOFLAG, nullptr, &err);
    if (!doc) {
        log_write_boot("[titledb] yyjson_read_file failed: code=%u msg=%s pos=%zu\n",
            err.code, err.msg ? err.msg : "(null)", err.pos);
        return;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    log_write_boot("[titledb] JSON parsed into DOM successfully\n");

    const auto root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        log_write_boot("[titledb] root is not an object (type=%u)\n", yyjson_get_type(root));
        return;
    }

    const auto total_entries = yyjson_obj_size(root);
    log_write_boot("[titledb] root object has %zu entries\n", total_entries);

    std::unordered_map<u64, TitleInfo> map;
    map.reserve(total_entries);

    u32 skipped_bad_id = 0;
    u32 sample_count = 0;

    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        const char* key_str = yyjson_get_str(key);
        if (!key_str) {
            continue;
        }

        const auto val = yyjson_obj_iter_get_val(key);

        // log the first 3 entries to verify the id field and fields present
        if (sample_count < 3) {
            log_write_boot("[titledb] sample[%u]: nsuId_key=\"%s\" is_obj=%d\n",
                sample_count, key_str, yyjson_is_obj(val));

            if (yyjson_is_obj(val)) {
                const auto id_val = yyjson_obj_get(val, "id");
                log_write_boot("[titledb] sample[%u] id field: found=%d val=\"%s\"\n",
                    sample_count,
                    id_val != nullptr,
                    (id_val && yyjson_is_str(id_val)) ? yyjson_get_str(id_val) : "(not a string)");

                const auto pval = yyjson_obj_get(val, "numberOfPlayers");
                const auto rval = yyjson_obj_get(val, "rating");
                log_write_boot("[titledb] sample[%u] numberOfPlayers: found=%d type=%u  rating: found=%d type=%u\n",
                    sample_count,
                    pval != nullptr, pval ? yyjson_get_type(pval) : 0,
                    rval != nullptr, rval ? yyjson_get_type(rval) : 0);
            }
            sample_count++;
        }

        if (!yyjson_is_obj(val)) {
            continue;
        }

        // the outer JSON key is the nsuId (decimal) — the actual title ID is in the "id" field
        const auto id_val = yyjson_obj_get(val, "id");
        if (!id_val || !yyjson_is_str(id_val)) {
            skipped_bad_id++;
            continue;
        }
        const auto title_id = static_cast<u64>(std::strtoull(yyjson_get_str(id_val), nullptr, 16));
        if (title_id == 0) {
            skipped_bad_id++;
            continue;
        }

        TitleInfo info{};

        // parse numberOfPlayers — can be uint, sint, or real (e.g. 1.0)
        const auto players_val = yyjson_obj_get(val, "numberOfPlayers");
        if (players_val) {
            if (yyjson_is_uint(players_val)) {
                info.players = static_cast<u32>(yyjson_get_uint(players_val));
            } else if (yyjson_is_sint(players_val)) {
                const auto v = yyjson_get_sint(players_val);
                info.players = v > 0 ? static_cast<u32>(v) : 0;
            } else if (yyjson_is_real(players_val)) {
                const auto v = yyjson_get_real(players_val);
                info.players = v > 0.0 ? static_cast<u32>(v) : 0;
            }
            // null or other type → players stays 0
        }

        // parse rating — same type flexibility
        const auto rating_val = yyjson_obj_get(val, "rating");
        if (rating_val) {
            if (yyjson_is_uint(rating_val)) {
                info.rating = static_cast<u32>(yyjson_get_uint(rating_val));
            } else if (yyjson_is_sint(rating_val)) {
                const auto v = yyjson_get_sint(rating_val);
                info.rating = v > 0 ? static_cast<u32>(v) : 0;
            } else if (yyjson_is_real(rating_val)) {
                const auto v = yyjson_get_real(rating_val);
                info.rating = v > 0.0 ? static_cast<u32>(v) : 0;
            }
            // null or other type → rating stays 0
        }

        map[title_id] = info;
    }

    log_write_boot("[titledb] loop done: map_size=%zu skipped_bad_id=%u\n",
        map.size(), skipped_bad_id);

    {
        SCOPED_MUTEX(&g_mutex);
        g_title_map = std::move(map);
    }

    g_ready = true;
    log_write_boot("[titledb] ready — %zu title IDs loaded\n", g_title_map.size());
}

} // namespace

auto IsReady() -> bool {
    return g_ready;
}

auto GetNumberOfPlayers(u64 app_id) -> u32 {
    if (!g_ready) {
        return 0;
    }
    SCOPED_MUTEX(&g_mutex);
    const auto it = g_title_map.find(app_id);
    return it != g_title_map.end() ? it->second.players : 0;
}

auto GetRating(u64 app_id) -> u32 {
    if (!g_ready) {
        return 0;
    }
    SCOPED_MUTEX(&g_mutex);
    const auto it = g_title_map.find(app_id);
    return it != g_title_map.end() ? it->second.rating : 0;
}

void DownloadIfNeeded() {
    if (g_ready || g_started.exchange(true)) {
        log_write_boot("[titledb] DownloadIfNeeded: already ready or started, skipping\n");
        return;
    }

    log_write_boot("[titledb] DownloadIfNeeded called\n");
    fs::FsNativeSd().CreateDirectoryRecursively(TITLEDB_CACHE_DIR);

    if (!IsStale()) {
        log_write_boot("[titledb] cache is fresh, launching background parse\n");
        g_parse_async = std::make_unique<utils::Async>(DoParse);
        return;
    }

    log_write_boot("[titledb] cache is stale or missing, starting background download from %s\n", TITLEDB_URL);

    curl::Api{}.ToFileAsync(
        curl::Url{TITLEDB_URL},
        curl::Path{fs::FsPath{TITLEDB_PATH}},
        curl::Priority::Normal,
        std::stop_token{},
        curl::OnComplete{[](curl::ApiResult& result) {
            if (!result.success) {
                log_write_boot("[titledb] download failed: http_code=%ld\n", result.code);
                g_started = false;
                return;
            }
            log_write_boot("[titledb] download succeeded, launching background parse\n");
            if (!g_parse_async) {
                g_parse_async = std::make_unique<utils::Async>(DoParse);
            }
        }}
    );
}

} // namespace sphaira::titledb

