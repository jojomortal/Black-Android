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
