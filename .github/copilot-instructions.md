# Copilot Instructions

## Project Guidelines
- Always add log_write_boot() debug logging when implementing new features. The user explicitly requires this for every new feature.
- When debugging a crash or unknown issue, add as many log_write_boot() checkpoints as needed in a single pass — before and after every suspicious call, in every branch, in every thread entry point. Do not add minimal logging and iterate; instrument everything relevant at once to avoid multiple recompile/redeploy cycles.

## Threading Rules

- Every `utils::Async` object that lives beyond a single scope **must** have its lifetime
  explicitly managed. Either:
  - Store it as a `std::unique_ptr<utils::Async>` (e.g. `g_parse_async`) so it can be
    `.reset()` during teardown, or
  - Ensure it is a local variable that goes out of scope before the module exits.
- Never let an `Async` object be destroyed implicitly by static/global destruction order.
  Horizon OS does not reclaim thread handles until `threadClose()` is called — failing to
  join before process teardown leaks a kernel thread slot across NRO reloads.
- Any new background thread introduced in a subsystem **must** have a corresponding
  `Exit()` function in that subsystem's header, which joins the thread. That `Exit()`
  must be called explicitly in `App::~App()` inside the `async_exit` lambda, before
  `curl::Exit()` and `audio::Exit()`.
- When adding a new `Exit()` call in `app.cpp`, add a `log_write_boot()` checkpoint
  immediately before it, following the existing pattern.
- Never call `threadWaitForExit()` without a prior guard that checks whether the thread
  was successfully started (e.g. check `m_running` or a similar flag), to avoid waiting
  on an invalid handle.
- Never call `threadClose()` on a thread that was never created or already closed.
  On Horizon OS, a zero-initialised `Thread` struct has `handle = 0`, which is the
  main thread's handle. Calling `threadClose(handle=0)` will close the **main thread**,
  corrupting the kernel's reference count and causing crashes on the next NRO load.
- Never read or write shared state from a background `utils::Async` thread without
  protection. Use `std::atomic` for simple flags/counters, or a mutex for structs.
  Cross-thread signals must be `std::atomic<bool>`, not plain `bool`.
- The `async_exit` lambda in `App::~App()` captures `this`. Do not access any `App`
  member that may have already been destroyed by the time the lambda runs. Services
  and subsystems should be exited via their own module-level `Exit()` functions, not
  by touching `m_*` members inside `async_exit`. The one exception is POD handles
  (e.g. `audio::SongID`) that must be closed before the subsystem exits — these must
  be closed on the **main thread before `async_exit` is constructed**, not inside the
  lambda. See the `audio::CloseSong(&m_background_music)` call before `async_exit` in
  `App::~App()` as the canonical example.
- Any retry loop (e.g. service Initialize, thread creation) must have a fixed maximum
  attempt count and a `svcSleepThread` delay between attempts. Never retry indefinitely.
  Log each failed attempt with its attempt number and result code.
- Global `utils::Async` objects must be `std::unique_ptr<utils::Async>`, never
  value-type globals. Static constructors run before `userAppInit()` and static
  destructors run after `userAppExit()` — spawning threads at either point is unsafe.
- To signal a background thread to wake up and exit, use libnx `UEvent`
  (`ueventCreate` / `ueventSignal` / `waitSingleHandle`), following the pattern in
  `ThreadEntry::SignalClose()` and `ThreadQueue::SignalClose()` in `download.cpp`.
  Never use a `condvar` + plain `bool` for cross-thread wakeup — the bool is not
  atomic and the condvar requires a mutex that the sleeping thread may not hold.
- `ON_SCOPE_EXIT` guards are tied to the C++ scope they are declared in, not to any
  thread. Never rely on an `ON_SCOPE_EXIT` declared outside a thread lambda to clean
  up resources owned or used by that thread — the guard fires when the outer scope
  exits, which may be before or after the thread finishes.

## Horizon OS / NRO-Specific Rules

- **romfs is not thread-safe.** Never call `romfsInit()`, `romfsExit()`, or access
  `romfs:/` paths from any background thread (including `utils::Async` lambdas or the
  `async_init`/`async_exit` threads in `App`). All romfs access must happen on the main
  thread.

- **Always use the double-init guard when calling `romfsInit()`.** Horizon OS returns
  `0x559` if romfs is already open. The correct pattern used throughout this codebase is:
  ```cpp
  const Result _rc = romfsInit();
  if (R_SUCCEEDED(_rc) || _rc == 0x559u) {
      ON_SCOPE_EXIT( if (R_SUCCEEDED(_rc)) { romfsExit(); } );
      // use romfs here
  }
  ```
  A bare `R_SUCCEEDED(romfsInit())` check will incorrectly skip the block if romfs is
  already open, breaking theme and asset loading.

- **NVG (`NVGcontext`) is not thread-safe.** Never call any `nvg*()` function from a
  background thread. Image creation (`nvgCreateImage`, `nvgCreateImageMem`,
  `nvgCreateFontMem`) and deletion must only happen on the main thread.

- **`svcSleepThread()` takes nanoseconds, not milliseconds.** Always multiply by
  `1'000'000` for millisecond delays (e.g. `svcSleepThread(20'000'000)` = 20 ms).
  Passing a raw millisecond value will sleep for a microsecond and appear as a no-op.

- **IPC service sessions may not be immediately available after a previous NRO session
  closes them.** When reinitializing a service (e.g. `ns`, `es`, `title`) at the start
  of a new NRO load, use a retry loop with a `svcSleepThread` delay between attempts,
  following the pattern in `game_menu.cpp`. Do not assume a service `Initialize()` call
  will succeed on the first try after a relaunch.

- **`appletLockExit()` must be the first significant call in `userAppInit()`.** Any
  service initialized before `appletLockExit()` that calls `diagAbortWithResult()` on
  failure will abort before the exit lock is held, leaving the process in an
  unrecoverable state. Always acquire the exit lock first.
