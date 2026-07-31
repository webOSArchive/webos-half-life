# Device-truth log — Half-Life (Xash3D FWGS) on HP TouchPad

Running log of what was actually observed on hardware / during the port.
Newest entries at the top of each section. Keep this honest — future sessions
depend on it. The full plan lives in the session plan file; milestones M0–M6.

## Decisions

| ID | Decision | Status |
|----|----------|--------|
| D1 | Compiler: **RESOLVED → Linaro 4.9.4 + `--sysroot=/opt/PalmPDK/arm-gcc/sysroot`** | 2026-07-31 |
| D2 | SDL backend: patch --use-sdl1 vs new platform/webos backend | OPEN |
| D3 | Binary shape: single binary vs launcher + libxash.so | OPEN (prefer single) |
| D4 | Renderer: ref_gles1 (NanoGL) vs ref_soft fallback | OPEN (default gles1) |
| D5 | C++ runtime: device libstdc++ 6.0.9 vs bundled | OPEN (follows D1) |
| D6 | Game .so location: /media/internal (noexec?) vs app dir | OPEN (test in M3) |

## M0 — scaffold

- 2026-07-31: repo created; submodules xash3d-fwgs + hlsdk-portable (upstream URLs
  for now — fork on GitHub deferred until we need to push the webos branch).
  Copied from sdlquake: diag tools (glsmoke/evread/egldiag/eglspy/sdlspy/netprobe),
  in_evdev.[ch], control/postinst+prerm.

## Build gotchas observed

### M1 feasibility probe (2026-07-31) — D1 resolution
- **PDK gcc 4.3.3 CANNOT build Xash3D**: the codebase re-typedefs identical types
  across headers pervasively (C11-legal, error before gcc ~4.6). Not patchable
  without forking half the headers. → Linaro 4.9.4 + PDK sysroot.
- **Linaro 4.9.4 + `--sysroot=/opt/PalmPDK/arm-gcc/sysroot`** emits GLIBC_2.4-only
  binaries; hello+libm verified running on device.
- Probe results: engine C 125/137 OK (`-std=gnu11 -DENGINE_DLL=1`; all 12 fails =
  missing include paths for imagelib/soundlib/3rdparty — waf supplies those).
  mainui C++ 82/82 OK (`-std=gnu++11 -DSTDINT_H=<stdint.h>` + compat header).
  nanogl 2/2 OK.
- gcc 4.3.3 also lacks `__ARM_ARCH`; build.h's ARM detect needs `-D__ARM_ARCH=7`
  under old compilers (Linaro 4.9 defines it natively — no patch needed).
- **webos/glibc25-math-compat.h** must be force-included (`-include`) for C++11
  units: libstdc++ 4.9's <cmath>/<cstdlib> declare `using ::acoshl` etc. which
  glibc-2.5's headers never prototype (ARM: long double == double). Declarations
  alone satisfy it; nothing calls the *l variants at link time.
- C++ runtime plan (D5): link `-static-libstdc++` (Linaro libstdc++ 6.0.20 vs
  device 6.0.9).

## Device runs

- 2026-07-31 glsmoke (novacom shell, /tmp): GLES1.1 context OK at 1024×768
  ("OPENGLES, ver1, 565 d16 OK"), Adreno 220, MAX_TEXTURE 4096, 2 units,
  glGetError clean. Extensions include GL_OES_compressed_ETC1_RGB8_texture
  (note: fuller list than the earlier recorded probe — ETC1 IS present).
  ANOMALY: swap rate ~10–11 fps vs 60 recorded during sdlquake work — probably
  because the launcher owned the screen (hidden-surface swap throttle) when run
  from a shell. Re-measure from a launcher-launched app in M2 before worrying.
- Device: topaz-linux over USB novacom, uber-kernel 3.0.5-93.
