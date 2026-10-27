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

### Findings that change `memory.cpp` (shared runtime, **needs your OK before I touch it**)

1. Host page is 16 KB (macOS arm64 and iPhone). `Commit` does `mprotect` on 4 KB guest pages: a range not aligned to
   16 KB fails with `EINVAL` (measured). Commit/Decommit must round to host pages. Consequence: guest guard pages
   smaller than 16 KB cannot be exact (stack guard in `AllocStack`), and `IsCommitted` bookkeeping stays 4 KB.
2. `Decommit` uses `madvise(MADV_DONTNEED)`. On Darwin that does **not** zero (measured: page still `0xAB`). Needs
   `mmap(MAP_FIXED|MAP_ANON|PROT_NONE)` over the range instead, which also zeroes.
3. `memfd_create` does not exist: replace with one of the aliasing methods above.

### NOT verified (needs the phone)

- Whether iOS lets the app reserve 4 GB at all, with and without `extended-virtual-addressing` / `increased-memory-limit`.
- Whether `shm_open` is allowed by the iOS sandbox (expect the mach methods to be the safe choice).
- `os_proc_available_memory()` / jetsam headroom on the iPhone 15 (6 GB RAM).
- The device showed as **offline** in `xctrace` (locked / not trusted / not paired?), and I did not try to sign anything.

### Entitlements and sideloading (from general knowledge, not tested here)

- Both entitlements are provisioning-profile capabilities. A paid Apple Developer team can enable them on an
  App ID; a free Personal Team cannot (Xcode strips/rejects them). Sideload tools using a free account behave the
  same way. Which of the two your setup can grant depends on the account, so the answer is pending (see open questions).

## Open questions

- Signing decided: the Apple Distribution identity of the developer account, manual, Ad Hoc profiles. iPhone UDID `<device UDID>` (devicectl: paired, available).
- Dev portal: one App ID `com.muhammad.memtest` with both capabilities + one Ad Hoc profile; variants sign a subset of what the profile grants.
