# Fix DkRenderer::Create to keep romfs mounted after loading shaders.
#
# Root cause: romfsMountSelf("romfs") is a one-shot operation on Nintendo Switch
# NRO homebrew. Once romfsExit() is called the device is gone and cannot be
# remounted. Any subsequent caller that needs romfs:/ (theme loader, i18n, etc.)
# silently fails, leaving the app with zero/transparent theme colours.
#
# Fix: do NOT call romfsExit() inside Create(). Also handle the 0x559
# "already-mounted" return code gracefully so the function still works when
# another caller mounted romfs first.
#
# Idempotent: no-op if the patch has already been applied.

set(file "${SOURCE_DIR}/source/dk_renderer.cpp")
file(READ "${file}" content)

string(FIND "${content}" "romfs_mounted_by_us" already_patched)
if(NOT already_patched EQUAL -1)
    message(STATUS "nanovg patch_nanovg_romfs: already applied, skipping")
    return()
endif()

# ---- Patch 1: replace the simple R_FAILED(romfsInit()) guard ----
string(REPLACE
    "        if (R_FAILED(romfsInit())) {
            return 0;
        }"
    "        const Result romfs_rc = romfsInit();
        const bool romfs_mounted_by_us = R_SUCCEEDED(romfs_rc);
        if (!romfs_mounted_by_us) {
            // romfsMountSelf(\"romfs\") returns 0x559 when the device is already
            // mounted by a previous caller. romfs is still accessible in that case
            // -- verify by probing the shader file before proceeding.
            FILE* probe = fopen(\"romfs:/shaders/fill_vsh.dksh\", \"rb\");
            if (!probe) { return 0; }
            fclose(probe);
        }"
    content "${content}")

# ---- Patch 2: remove the romfsExit() call ----
string(REPLACE
    "        romfsExit();

        /* Set the size of fragment uniforms. */"
    "        // romfsExit() intentionally omitted: the romfs device must stay mounted
        // so that theme loading, i18n, and other post-init romfs consumers work.
        // The OS unmounts it automatically when the process exits.

        /* Set the size of fragment uniforms. */"
    content "${content}")

string(FIND "${content}" "romfs_mounted_by_us" verify)
if(verify EQUAL -1)
    message(FATAL_ERROR "patch_nanovg_romfs: string replacement failed for ${file}")
endif()

file(WRITE "${file}" "${content}")
message(STATUS "nanovg patch_nanovg_romfs: applied to ${file}")
