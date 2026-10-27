# iOS port notes

Target: iPhone 15 (A16), sideloaded. Branch `ios`. Android build must keep working.
SDK checkout: `../XboxXBE-Recomp` at `99078b1` (picked up by CMake before FetchContent).

## Phase 0: address space spike (`spike/memtest`)

`spike/memtest/build-ios.sh <none|eva|iml|both> [--sign]` builds a UIKit app that runs what
`kernel/memory.cpp` needs and prints/saves the report (`Documents/memtest-<variant>.txt`, also on screen).
`gen-ent.sh` writes the entitlements of a variant (Ad Hoc: app id, team id, get-task-allow=false + the capability keys); `run-device.sh` installs with devicectl and copies the report back; os_log subsystem `com.muhammad.memtest`. macOS runs the same code (`cli.c`).

### Verified (macOS 26 arm64, 16 KB pages, no sandbox)

- 4 GB `PROT_NONE|MAP_NORESERVE` mmap works. The `0x100000000` hint is **ignored** (got `0x300000000`); nothing in
  Darwin code should depend on the fixed base (the Windows path only uses it for comparable dumps).
- All three aliasing methods give coherent views of one 64 MB region at `+0x80000000` and `+0xF0000000`:
  `vm_remap(copy=FALSE)` onto the reservation, `mach_make_memory_entry_64` + `vm_map(FIXED|OVERWRITE)`, `shm_open` + `mmap`.
- iOS SDK has no `mach_vm.h` (`#error`), so use the `vm_*` calls (`vm_allocate`, `vm_remap`, `vm_map`); `vm_address_t` is 64-bit on arm64.

### Verified on the iPhone 15 (iPhone15,4, 5687 MB, iOS 27.0.1), Ad Hoc build, all four variants

| variant | 4 GB PROT_NONE reserve | vm_remap | memory entry + vm_map | shm_open | available memory at start |
|---|---|---|---|---|---|
| none | ok | coherent | coherent | EPERM (sandbox) | 3067 MB |
| eva  | ok | coherent | coherent | EPERM | 3067 MB |
| iml  | ok | coherent | coherent | EPERM | 4091 MB |
| both | ok | coherent | coherent | EPERM | 4091 MB |

- The profile for `TEAMID.com.example.memtest` grants **both** capabilities and the signed entitlements of each
  variant matched (printed by `build-ios.sh --sign`). So the team can grant both.
- Reserving 4 GB of `PROT_NONE` works with **no** entitlement (reserved address space is not counted by jetsam). Extended
  Virtual Addressing changes nothing measurable here, so the real app does not need it; it only raises the limit on
  *accessible* address space, which the guest does not come near (64 MB RAM + commits).
- Increased Memory Limit raises the jetsam headroom from ~3.0 GB to ~4.0 GB (`os_proc_available_memory`). Use it.
- `shm_open` is denied by the sandbox: it can't be the aliasing method. `vm_remap` and a memory entry both work; the
  patch uses the memory entry (no source mapping to keep, port released after mapping).
- Base is ignored hint: `0x7000000000` on the phone.
- 16 KB host pages: `mprotect` of a 4 KB range not 16 KB aligned fails (EINVAL); `MADV_DONTNEED` does not zero.
- Launch trap, not memory related: iOS 27 kills an app without the UIScene lifecycle
  (`UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption`, SIGTRAP). The real app must adopt it
  (check what SDL 3.4.16 does on iOS in Phase 3).
- `devicectl device copy from --domain-type appDataContainer` works on the Ad Hoc build (Documents/ readback).

### SDK patch `patches/sdk/0001-memory-darwin.patch` (on 99078b1, `kernel/memory.cpp`, additions under `__APPLE__` only)

- RAM: one `mach_make_memory_entry_64` mapped at both windows with `vm_map(FIXED|OVERWRITE)`; the port is released after.
- `Commit`: `mprotect` rounded out to host pages (`sysconf(_SC_PAGESIZE)`).
- `Decommit`: clears the 4 KB bits, then releases only host pages with no committed guest page, by remapping them
  `MAP_FIXED|MAP_ANON|MAP_NORESERVE` `PROT_NONE` (zeroes). Neighbours stay.
- A reserve failure logs errno, the kern_return of the step that failed and the largest reservation that works.
- Windows and the generic Unix/Android path are unchanged. Tested on macOS with the real memory.cpp (aliasing, odd
  4 KB commits, neighbour survives, release faults, recommit reads zero). Not yet run on the phone inside the game.
- Side effect: the guest cannot have a guard smaller than 16 KB; `AllocStack`'s 64 KB guard is fine.
- Apply to a sibling checkout with `git -C ../XboxXBE-Recomp apply ../Black-Android/patches/sdk/0001-memory-darwin.patch`;
  CMake wiring for the FetchContent case comes in Phase 2.

## Phase 1: recompiler on macOS (done)

`scripts/build-apple.sh` (tools, xbe from `assets/` or `iso/`, recompiler with Apple clang, symbols, recompile) runs in
~5 s and writes the same 64 partitions; two runs hash identically. `tools/CMakeLists.txt` needed no change. Not compared
byte for byte with a Windows run.

## Phase 2: native macOS arm64 build (done, with caveats below)

Build: `scripts/fetch-deps.sh`, `scripts/apply-sdk-patches.sh`, then
`cmake -S . -B out/build/mac -G Ninja && ninja -C out/build/mac` (first build ~12 min, 64 partitions at -O2), run with
`scripts/run-mac.sh` (game from `../Black (USA)`, data in `out/run-data`; `BLACK_GAME_DIR`/`BLACK_DATA_DIR` override).
`-DBLACK_VERBOSE_LOG=ON` writes info lines (the SDK's logx follows NDEBUG); host sources only, so it rebuilds in seconds.
Needs `brew install molten-vk vulkan-headers` (Homebrew's MoltenVK is a macOS static lib).

- **Platform split**: `native/platform.h` (log redirect, hints, data dir, game folders, device lines, surface ready,
  asset path, loose controller, update check) with `platform_android.cpp` (the old code moved verbatim) and
  `platform_apple.cpp`. `main.cpp`, `game_files.cpp`, `input.cpp`, `touch.cpp` keep only shared logic. `update.cpp` is gone
  (moved into platform_android.cpp). The Android sources were syntax-checked with NDK 26 clang; the full Android build
  (NDK 28.2) was **not** run here: do a CI/Gradle build before merging.
- `europe.patch` is embedded by a CMake-generated byte array (was ELF `.incbin`), for Android too.
- SDL3 is static on Apple and finds MoltenVK through `dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")` (both Cocoa and UIKit
  do), so MoltenVK is linked in with `-Wl,-u,_vkGetInstanceProcAddr`; no loader and no dylib.
- `vk_portability.{h,cpp}`: portability enumeration and subset extensions, features chained, missing ones logged. No-ops
  off Apple. On the M1 Pro: **missing samplerMipLodBias and pointPolygons**; constant-alpha blend, comparison samplers,
  triangle fans, stencil ref, events etc. are all there. LOD bias is clamped to 0 without the feature (`vulkan.cpp`);
  constant-alpha for colour falls back to constant colour if a device lacks it (not exercised on the Mac).
  Not yet checked: point sprites need a written PointSize on Metal; the iPhone's subset may differ from the M1's.
- MoltenVK here reports Vulkan 1.3, depth **D32S8** (no D24S8 on Apple GPUs), no vsync / 3 swapchain images.
  The portability-enumeration extension is not listed when MoltenVK is linked directly; harmless.
- "Check for Updates" is hidden on Apple (`host::UpdatesSupported()`), Android keeps it.
- `BLACK_FORCE_NO_BC=1` forces the CPU DXT path (the iPhone has no BC textures). The decode time is logged every 5 s
  while it runs (`vulkan: DXT decoded on the CPU: N levels, X MB, T ms in all, U ms in the last S s`). At the title screen
  it was 0.2 ms over 10 s: **not representative**, measure in a mission before judging A16.

### Verified on the Mac (M1 Pro, macOS 26.7.1)

- Boots from the ISO, SHA1 matches, no unresolved kernel imports, guest space reserved through the patched `memory.cpp`,
  reaches the title screen (`menu cursor: page PressStart` at 11 s), CoreAudio stream opens, attract-mode videos start, the
  run lasted the full 60 s with no guest trap or error line. Shader cache fills.
- The forced no-BC path runs and logs.

### NOT verified

- **What is on screen.** A screenshot came back black (missing Screen Recording permission for the terminal, or a black
  window: I cannot tell). Please look at the window.
- Sound heard, a controller, touch/keyboard/mouse input, the options menu with the update row hidden, anything past the
  title screen, saves, dynamic resolution, the Europe disc path (`europe.patch` embed was only compiled).
- No `x86_trap` fired in 60 s of the title screen; deeper play is untested.

### Phase 3 needs

- **MoltenVK for iOS**: Homebrew's `molten-vk` is macOS-only. Phase 3 needs the iOS build as an xcframework (from the
  Vulkan SDK or MoltenVK's own `make ios` / release package), linked statically, with the same `-u _vkGetInstanceProcAddr`.
- The UIScene lifecycle, `UIFileSharingEnabled`, safe area, and an `.app` bundle target (the Mac target is a bare executable).
- `RedirectLog` has an os_log branch for iOS that is written but has never run.

## Decisions

- Full 4 GB guest layout (approved after Phase 0). No smaller layout needed.
- Real app bundle ID `com.muhammad.black`. Its App ID gets Increased Memory Limit **and** Extended Virtual Addressing
  in the portal, with an Ad Hoc profile; the build signs **only** `increased-memory-limit`. EVA is the fallback to
  sign in if the reservation ever fails inside the full app (large binary + MoltenVK use address space the spike did not).
- iOS 27 requires the UIScene lifecycle (see above); the real app's Info.plist needs `UIApplicationSceneManifest`.
- Aliasing: memory entry + `vm_map`; `shm_open` is not available on iOS.
- Signing: the Apple Distribution identity of the developer account, manual, Ad Hoc. iPhone UDID `<device UDID>`.
- Game files for Mac testing: `iso/` symlink to `<the USA disc image>`; the USA
  `assets/default.xbe` has SHA1 `c5bfed25449c8212d7a976f15246e87ead57713f`. Nothing under iso/, assets/, generated/ is committed.

## Open questions

- Signing decided: the Apple Distribution identity of the developer account, manual, Ad Hoc profiles. iPhone UDID `<device UDID>` (devicectl: paired, available).
- Dev portal (done): one App ID `com.muhammad.memtest` with both capabilities + one Ad Hoc profile; variants sign a subset of what the profile grants.
