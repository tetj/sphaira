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