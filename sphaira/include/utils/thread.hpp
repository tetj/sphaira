#pragma once

#include "defines.hpp"
#include "log.hpp"
#include <functional>
#include <atomic>

namespace sphaira::utils {

static inline Result CreateThread(Thread *t, ThreadFunc entry, void *arg, size_t stack_sz = 1024*128, int prio = 0x3B) {
    u64 core_mask = 0;
    Result rc;
    rc = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc)) { log_write_boot("[CreateThread] svcGetInfo FAILED rc=0x%X\n", rc); return rc; }
    rc = threadCreate(t, entry, arg, nullptr, stack_sz, prio, -2);
    if (R_FAILED(rc)) { log_write_boot("[CreateThread] threadCreate FAILED rc=0x%X stack=%zu\n", rc, stack_sz); return rc; }
    rc = svcSetThreadCoreMask(t->handle, -1, core_mask);
    if (R_FAILED(rc)) { log_write_boot("[CreateThread] svcSetThreadCoreMask FAILED rc=0x%X\n", rc); threadClose(t); return rc; }
    R_SUCCEED();
}

struct Async final {
    using Callback = std::function<void(void)>;

    // core0=main, core1=audio, core2=servers (ftp,mtp,nxlink)
    Async(Callback&& callback) : m_callback{std::forward<Callback>(callback)} {
        log_write_boot("[Async] ctor: start\n");
        m_running = true;

        log_write_boot("[Async] ctor: calling CreateThread\n");
        const auto create_rc = CreateThread(&m_thread, thread_func, &m_callback);
        if (R_FAILED(create_rc)) {
            log_write_boot("[Async] ctor: CreateThread FAILED rc=0x%X module=%u desc=%u\n",
                create_rc, R_MODULE(create_rc), R_DESCRIPTION(create_rc));
            m_running = false;
            return;
        }

        log_write_boot("[Async] ctor: calling threadStart\n");
        if (R_FAILED(threadStart(&m_thread))) {
            log_write_boot("[Async] ctor: threadStart FAILED\n");
            threadClose(&m_thread);
            m_running = false;
            return;
        }
        log_write_boot("[Async] ctor: done, thread started\n");
    }

    ~Async() {
        WaitForExit();
    }

    void WaitForExit() {
        if (m_running) {
            threadWaitForExit(&m_thread);
            threadClose(&m_thread);
            m_running = false;
        }
    }

    auto IsRunning() const -> bool {
        return m_running;
    }

private:
    static void thread_func(void* arg) {
        (*static_cast<Callback*>(arg))();
    }

private:
    Callback m_callback;
    Thread m_thread{};
    std::atomic_bool m_running{};
};

} // namespace sphaira::utils
