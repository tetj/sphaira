# Fix stb_truetype CFF buffer size for Nintendo Switch shared fonts.
#
# Root cause: stb_truetype sets the CFF buffer to a fake 512 MB size, which
# bypasses the stbtt__buf_get8 EOF check. On Nintendo Switch the shared font
# memory is mapped to exactly the font size with no trailing guard page, so
# the CFF parser walks off the end and triggers a data abort.
#
# Fix: read the actual CFF table length from the OpenType table directory and
# use that as the buffer size instead of the hardcoded 512 MB.
#
# Idempotent: no-op if the patch has already been applied.

# stb_truetype.h is fetched by nanovg's own CMakeLists as "stb_nanovg".
# After FetchContent_MakeAvailable(nanovg) the variable stb_nanovg_SOURCE_DIR
# is populated in the global CMake state.
if(NOT DEFINED stb_nanovg_SOURCE_DIR)
    FetchContent_GetProperties(stb_nanovg SOURCE_DIR stb_nanovg_SOURCE_DIR)
endif()

if(NOT stb_nanovg_SOURCE_DIR OR NOT EXISTS "${stb_nanovg_SOURCE_DIR}/stb_truetype.h")
    message(WARNING "patch_stbtt: stb_truetype.h not found at '${stb_nanovg_SOURCE_DIR}' -- skipping")
    return()
endif()

set(file "${stb_nanovg_SOURCE_DIR}/stb_truetype.h")
file(READ "${file}" content)

string(FIND "${content}" "stbtt__find_table_len" already_patched)
if(NOT already_patched EQUAL -1)
    message(STATUS "nanovg patch_stbtt: already applied, skipping")
    return()
endif()

# ---- Patch 1: add stbtt__find_table_len right before GetFontOffsetForIndex ----
string(REPLACE
    "static int stbtt_GetFontOffsetForIndex_internal"
    "// Returns the byte length of a named font table (bytes 12-15 of each
// 16-byte table directory entry: tag(4) checksum(4) offset(4) length(4)).
static stbtt_uint32 stbtt__find_table_len(stbtt_uint8 *data, stbtt_uint32 fontstart, const char *tag)
{
   stbtt_int32 num_tables = ttUSHORT(data+fontstart+4);
   stbtt_uint32 tabledir = fontstart + 12;
   stbtt_int32 i;
   for (i=0; i < num_tables; ++i) {
      stbtt_uint32 loc = tabledir + 16*i;
      if (stbtt_tag(data+loc+0, tag))
         return ttULONG(data+loc+12);
   }
   return 0;
}

static int stbtt_GetFontOffsetForIndex_internal"
    content "${content}")

# ---- Patch 2: replace the hardcoded 512 MB CFF size ----
string(REPLACE
    "      // @TODO this should use size from table (not 512MB)
       info->cff = stbtt__new_buf(data+cff, 512*1024*1024);"
    "      // Use the real CFF table size so stbtt__buf_get8's EOF guard fires
      // correctly on platforms without a trailing memory guard page (e.g. NX).
      {
         stbtt_uint32 cff_len = stbtt__find_table_len(data, fontstart, \"CFF \");
         info->cff = stbtt__new_buf(data+cff, cff_len ? cff_len : 512*1024*1024);
      }"
    content "${content}")

string(FIND "${content}" "stbtt__find_table_len" verify)
if(verify EQUAL -1)
    message(FATAL_ERROR "patch_stbtt: string replacement failed for ${file}")
endif()

file(WRITE "${file}" "${content}")
message(STATUS "nanovg patch_stbtt: applied to ${file}")
