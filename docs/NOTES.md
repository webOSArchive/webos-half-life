# Device-truth log — Half-Life (Xash3D FWGS) on HP TouchPad

Running log of what was actually observed on hardware / during the port.
Newest entries at the top of each section. Keep this honest — future sessions
depend on it. The full plan lives in the session plan file; milestones M0–M6.

## Decisions

| ID | Decision | Status |
|----|----------|--------|
| D1 | Compiler: **RESOLVED → Linaro 4.9.4 + `--sysroot=/opt/PalmPDK/arm-gcc/sysroot`** | 2026-07-31 |
| D2 | SDL backend: **RESOLVED → patched --use-sdl1 path** (webOS ifdefs in sdl1 backend) | 2026-07-31 |
| D3 | Binary shape: **RESOLVED → launcher + libxash.so** (launcher is a real ELF, LS2-safe) | 2026-07-31 |
| D4 | Renderer: **RESOLVED → ref_gles1 (NanoGL→libGLES_CM)** — working on device | 2026-07-31 |
| D5 | C++ runtime: **RESOLVED → -static-libstdc++ -static-libgcc -lgcc_eh** | 2026-07-31 |
| D6 | Game .so location: **RESOLVED → /media/internal/xash works** (dlopen OK, no noexec) | 2026-07-31 |

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

### M1 waf build (2026-07-31) — SUCCESS
- `./waf configure -T release --use-sdl1 --enable-gles1 --disable-gl
  --enable-stbtt --disable-mbedtls` with env CC/CXX = Linaro (no sysroot in CC;
  sysroot via CFLAGS/LINKFLAGS). SDL 1.2 found via our `webos/pkgconfig/sdl.pc`
  (PKG_CONFIG_LIBDIR).
- Fixes needed (all in scripts/build-engine.sh env or fork commit 411ccc0f):
  `-std=gnu11` (4.9 defaults gnu90); `-fgnu89-inline` (glibc-2.5 stat64 inline
  dupes); `-Wno... → wscript: -Werror=strict-aliasing demoted`; link
  `-static-libstdc++ -static-libgcc -lgcc_eh -lrt` (Linaro libstdc++.so wants
  GLIBC_2.17 clock_gettime; libbacktrace wants _Unwind_Backtrace).
- SDL1 backend bit-rot fixed: window_mode_t refactor, SDLash_Init(void),
  + 3 stubs (VID_Info_f, Platform_GetDisplayOrientation→LANDSCAPE, gyro=false).
- Artifacts (ALL GLIBC_2.4 clean): game_launch/xash3d (launcher exe),
  engine/libxash.so (12MB unstripped), ref/gl/libref_gles1.so,
  ref/soft/libref_soft.so, 3rdparty/mainui/libmenu.so,
  filesystem/filesystem_stdio.so. D3: launcher+libxash shape (fine for LS2 —
  launcher is a real ELF `main`).
- M2 TODO noted: NanoGL dlopens Android name `libGLESv1_CM.so`; TouchPad has
  `/usr/lib/libGLES_CM.so` → patch nanogl lib list. vgui skipped (arm). VOICE
  uses opus — kept. mbedtls off (no TLS on device anyway).

## Device runs

### M2/M3 first light (2026-07-31, new dev tablet, stock 3.0.5 kernel)
- **Engine RUNS: menu at native 1024×768 on ref_gles1 (NanoGL→libGLES_CM), first
  frame 1.9 s; `map hldemo1` loads, player spawns, scripted intro sequence runs.**
- Fixes en route: Palm SDL has no SDL_GL_LoadLibrary ("No dynamic GL support in
  video driver") → skip on webOS; NanoGL lib name is the `GLES_LIB` **macro**
  (line ~85), not the `lib1` locals in the dead second nanoGL_Init — patch both.
  The GL_GetProcAddress error flood at ref init is harmless (egl* intentionally
  unresolved; SDL owns the context).
- Engine name-suffixes dlls: wants `dlls/hl_armv7l.so` / `cl_dlls/client_armv7l.so`
  — hlsdk-portable's waf produces exactly those names. Client dll is REQUIRED
  even for the menu.
- **D6 resolved: dlopen from /media/internal/xash WORKS** (no noexec issue on
  this vfat mount). Data contract: XASH3D_RODIR=app dir (extras.pk3),
  XASH3D_BASEDIR=/media/internal/xash (valve/ data, dlls, saves, config).
- Uplink demo data: Wise setup.exe carved by walking raw-deflate streams from
  the PE overlay (0x3800; first stream 0x38b1; stream+CRC32 back-to-back).
  wise_030=pak0.pak(1952 files), wise_076=gfx.wad(CONCHARS), wise_073=
  halflife.wad(222), wise_070=cached.wad. Demo lacks delta.lst → fetched from
  ValveSoftware/halflife network/delta.lst. gameinfo startmap = hldemo1
  (Uplink campaign; t0a0* = hazard course).
- Engine also needs gfx/conchars at boot (extras.pk3 alone is NOT enough to
  reach the menu — contrary to earlier research assumption).
- Cosmetic gaps: missing satchel/squeak/hgun models (not in demo), missing
  resource/*_english.txt localization, no gamestartup music. vgui_support
  missing is fine (disabled on ARM).
- Iteration loop: hot-push .so via `novacom put` into installed app dir +
  killall + palm-launch — no reinstall needed.

### Session wrap 2026-07-31 (M3 done, touch done)
- Black-screen-with-audio bug: GL frames were presented with SDL_Flip →
  **SDL_GL_SwapBuffers is mandatory** on Palm SDL for GL surfaces.
- Touch: Palm per-finger mouse events (`which`=0..4) → IN_TouchEvent
  (host_sdl1.c SDLash_WebOSTouch). Fixes single-tap menu activation
  (UI_MouseMove precedes the click inside IN_TouchEvent) and drives the
  engine's built-in overlay. defaults.h: touch_enable=1, m_ignore=1 on webOS.
- TRAP: cvars are ARCHIVED — a config.cfg written by an earlier build with old
  defaults overrides new defaults (had to sed touch_enable/m_ignore to 1 in
  /media/internal/xash/valve/config.cfg). Remember when changing any default.
- USER-VERIFIED on device: single-tap menu, new game starts, touch overlay +
  touch-look all work. cl_showfps 1 left in autoexec.cfg for a baseline
  reading next session.
- Uplink demo maps: campaign = hldemo1..3, hazard course = t0a0*.

### Device runs (older)
- 2026-07-31 glsmoke (novacom shell, /tmp): GLES1.1 context OK at 1024×768
  ("OPENGLES, ver1, 565 d16 OK"), Adreno 220, MAX_TEXTURE 4096, 2 units,
  glGetError clean. Extensions include GL_OES_compressed_ETC1_RGB8_texture
  (note: fuller list than the earlier recorded probe — ETC1 IS present).
  ANOMALY: swap rate ~10–11 fps vs 60 recorded during sdlquake work — probably
  because the launcher owned the screen (hidden-surface swap throttle) when run
  from a shell. Re-measure from a launcher-launched app in M2 before worrying.
- Device: topaz-linux over USB novacom, uber-kernel 3.0.5-93.

### Perf first impressions (2026-07-31, user-reported, pre-tuning)
- "Buttery smooth in a bunch of scenarios" — early Uplink areas, NPCs
  interacting, no visible framerate drop. No numeric reading yet (running
  instance predated the cl_showfps autoexec; counter appears after relaunch).
- Still untested: room-full-of-enemies combat, flashlight (the known
  dynamic-lightmap cliff), water/glass-heavy areas. Get numbers in M5.
