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

### M4 hardware-test feedback round (2026-08-15, TouchPadE, ipk 0.1.6)
- USER-VERIFIED: DS4 + ShanWan work (rare stuck button), Logitech works,
  Apple USB keyboard works, **HP BT keyboard works** (after re-pair, see below).
- Stuck buttons (DS4/ShanWan BT): missed release events -- 2.6.35 predates
  SYN_DROPPED so evdev overflows drop silently. FIX: 1 Hz EVIOCGKEY resync of
  btn_raw/dpad_btn against kernel state; stuck button self-heals <=1 s.
- **HP TouchPad Wireless Keyboard**: when paired it shows up as an evdev node
  (name exactly that) emitting STANDARD KEY_* codes -- our module grabs it
  and it just works. When the node is absent/ungrabbed, keys arrive via the
  Luna->Palm-SDL path as private low keysyms; arrows VERIFIED by ordered-
  press capture 2026-08-15: **18=Left 19=Up 20=Right 21=Down** (sym 24 =
  dismiss-keyboard gesture, ignored). Mapped in host_sdl1.c; dev-mode
  raw-code logging kept in both paths for future oddball keyboards.
- TRAP fixed: a BT keyboard SLEEPING kills its event node; kbd_poll used to
  ignore read errors, so the module held the dead fd and the scan skipped
  that index forever ("held") -- keyboard silently fell back to the SDL path
  (with then-wrong arrows) after every sleep cycle. kbd_poll now detects the
  dead node and frees the slot like pad_poll; re-grab <=1 s after wake.
- Diag trick: `chmod 0640 /dev/input/eventN` (root) hides a device from the
  jailed game (uid 5003) without unpairing -- forces the Luna/SDL delivery
  path for controlled capture. Restore with chmod 0666.
- Keyboard defaults: engine table now binds e=+use f=impulse 100 r=+reload
  (HL letters the demo's config never bound; device config.cfg patched too).
- **Per-pad bind profiles**: valve/pad_<profile>.cfg exec'd on profile change
  (deferred until host.config_executed so startup config.cfg can't stomp it).
  Shipped: ds4/shanwan/generic = stock scheme, logitech/dragonrise =
  quake-style stickless scheme (face buttons look, L1/R1 jump/fire, L2 use).
  MODE (PS/Guide) button = "toggle touch_enable" (overlay on/off from pad).
  VERIFIED end-to-end on device via fakepad. One unexplained silent skip of
  the exec in the first 0.1.6 session -- diagnostic else-branch now logs
  gamedironly/anypath flags if FS_FileExists ever fails there again.
- **fakepad diag tool** (webos/diag/fakepad.c): creates a uinput gamepad
  (default name ShanWan) from a novacom root shell -- exercises hotplug,
  profile match, and cfg exec with NO physical pad. uinput node on this
  kernel is /dev/input/uinput (not /dev/uinput).
- **LunaSysMgr LANDMINE**: if a pad/dongle's event node disappears while
  LunaSysMgr holds it open (webOS opens new HID nodes as candidate
  keyboards), Luna busy-loops spamming "Could not read from input device"
  and SILENTLY STOPS LAUNCHING APPS (palm-launch says "launching", nothing
  happens, no log). Recovery: `killall LunaSysMgr` (UI restarts ~30 s).
  Hit after unplugging the ShanWan dongle post-test. Not caused by our code.
- palm-launch also no-ops while the screen is off/locked; wake first:
  luna-send -n 1 palm://com.palm.display/control/setState '{"state":"on"}'
- Launcher: -dev 2 now conditional on marker file /media/internal/xash/dev
  (present on TouchPadE); without it only -log. Kills on-screen dev spam for
  normal play.
- Gamepad look sensitivity: adjustable in Options -> Gamepad (mainui
  Gamepad.cpp, joy_pitch/joy_yaw) -- no code needed.

### M4 input: evdev gamepad + keyboard ported (2026-08-15)
- New engine module `engine/platform/linux/in_evdev_webos.c` (webos branch),
  ported from sdlquake's in_evdev.c. **Stage 1 kept verbatim** (profiles
  DS4/ShanWan/Logitech/DragonRise/default, stick_ok axis heuristics, 1 Hz scan
  with node_boring cache, EVIOCGRAB, padstatus/padtest/padbtn/padaxis).
  **Stage 2 replaced with engine-native output**: positions → Key_Event
  (K_A_BUTTON..K_DPAD_*, default HL binds in in_keys.c + mainui menu nav),
  sticks/triggers → Joy_AxisMotionEvent(0..5) in the default "sfpyrl"
  hardware-axis order; keyboards → Key_Event + CL_CharEvent when host.textmode
  (shift tracked module-side). No action scheme of our own anymore.
- Hooks: Platform_RunEvents (host_sdl1.c, lazy init on first poll),
  Linux_Shutdown (sys_linux.c), decls in platform.h under __webos__.
- Trigger semantics: analog axes feed engine 0..32767 (K_JOY1/2 pressed at
  joy_*_threshold 16384 = 50% pull; sdlquake used 30% — tune via cvars if it
  feels dull). Digital trigger buttons emit K_L2/K_R2 ONLY when the pad has no
  analog axis for that trigger (same default binds, no double-source races).
- Stickless pads (Logitech/DragonRise): profile maps the d-pad axes as
  move_x/move_y → analog movement; there is NO face-button-look fallback like
  sdlquake had (engine scheme: face buttons = jump/use/reload/flashlight).
  Touch-look covers aiming on those pads; rebindable if it ever matters.
- Packaging: postinst/prerm adapted from sdlquake into webos/app/control/
  (udev rule 99-xashhl-pad.rules; prerm coexists with sdlquake/sdlquakeHD/
  cmdrkeen/btgamepad rules). **palm-install does NOT run postinst** — dev loop
  pushes it and runs via novacom (root); end users need Preware/WOSQI (M6).
- ipk 0.1.5 built (also carries the touch.cfg empty-file guard).
- DEPLOYED to TouchPadE (c37f7a34…, stock 2.6.35 kernel) — this is the July-31
  dev tablet where the sdlquake pad truth tables were captured; jail was
  already patched by sdlquakeHD/btgamepad. Data tree from Jul 31 verified
  current (dll md5s match local builds). Engine runs; evdev scan live, quiets
  after 3 empty scans, no SCAN/POLL TOOK hitches, gpio-keys EACCES correctly
  silent. Pad/keyboard hardware test pending (nothing attached yet).

### Vacation regression: touch overlay vanished (diagnosed 2026-08-15)
- SYMPTOM (user, on FreshPad): second launch onward, touch controls gone,
  taps just moved a cursor. config.cfg was FINE (touch_enable/m_ignore both 1).
- ROOT CAUSE: `touch.cfg` AND `touch.cfg.bak` both **0 bytes** (mtime Aug 13
  15:20) — classic FAT zero-length-file damage: Touch_WriteConfig's rename
  dance completed but the device died (battery/hard reset) before the vfat
  page cache flushed. Engine then **execs the empty touch.cfg instead of
  loading default buttons** (Touch_InitConfig only falls back when the file is
  *missing*), and Touch_WriteConfig refuses to rewrite a config with no
  buttons (`!touch.list_user.first → return`) — so the broken state is
  **self-perpetuating**.
- FIX (engine webos branch, in_touch.c): guard at end of Touch_InitConfig —
  if the config produced zero buttons, Touch_LoadDefaults_f(). LoadDefaults
  sets configchanged=true, so the next clean exit rewrites a valid touch.cfg
  (self-healing). Deleted the two empty files on device; hot-pushed rebuilt
  libxash.so (GLIBC_2.4 clean); relaunch shows no "execing touch.cfg" →
  defaults loaded.
- TRAP generalized: any engine-written cfg on /media/internal (vfat, async)
  can be zero-length after power loss. config.cfg self-heals (empty exec →
  defaults → rewritten on exit); only touch.cfg had the sticky trap.

### Fresh-device flight deploy (2026-08-08) — first clean end-to-end install
- Target: hostname `FreshPad`, stock webOS 3.0.5, kernel **2.6.35-palm-tenderloin**
  (NOT the uber-kernel unit). Nothing preinstalled; 10 GB free on /media/internal.
- **ipk 0.1.3 was stale and would have shipped a broken game**: it predates the
  SDL_GL_SwapBuffers fix and touch input (verified by symbol diff — 0.1.3 imports
  `SDL_GL_LoadLibrary` and lacks `SDL_GL_SwapBuffers`). Rebuilt as **0.1.4**;
  its libxash.so is byte-identical (sha256 636f0957…) to the stripped build tree.
- Installed via palm-install; on-device md5 of every pushed file matches local.
  Data pushed to /media/internal/xash/valve: pak0.pak (79 MB, ~19 s over novacom),
  gfx/halflife/cached.wad, delta.lst, gameinfo.txt, dlls/hl_armv7l.so,
  cl_dlls/client_armv7l.so. Total data tree ≈ 99 MB.
- **D6 re-confirmed on this unit**: /media/internal is vfat rw, no `noexec`;
  dlopen of the game dlls from there works.
- Time to first frame: 4.98 s cold (first pak0 read), **1.6–1.8 s warm**.
- `map hldemo1` verified: `Spawn Server: hldemo1` → `loading maps/hldemo1.bsp`,
  process stable. Cosmetic-only warnings: `alpha_sky`/`solid_sky` missing (demo
  has no sky textures), no resource/*_english.txt, no gamestartup music, vgui.
- TRAP for future sessions: busybox **`ps` with no args does not list the app**;
  use `ps ax`. A bare `ps | grep xash` reads as "crashed" when it is running.

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

### Config exec-order trap (2026-07-31, CORRECTED 2026-08-08)
- The DEMO's valve.rc order is: autoexec.cfg -> skill.cfg -> **config.cfg**.
  So archived cvars in config.cfg override autoexec — don't put overrides there.
- **CORRECTION (2026-08-08, fresh-device deploy):** `userconfig.cfg` is NOT in
  valve.rc. The engine writes the line `exec userconfig.cfg` into **config.cfg
  when it saves it** (`engine/common/con_utils.c:1424`). So userconfig.cfg only
  ever runs on a device that has already saved a config.cfg — on a **fresh
  install it is silently never executed**. The earlier entry only looked right
  because that device had a saved config.cfg.
- **Use `valve/userconfig.d/*.cfg` instead** — `host.c:1281` issues the
  `userconfigd` command unconditionally every launch, which execs every
  `userconfig.d/*.cfg` (`cmd.c:1371`), in filename order, regardless of whether
  config.cfg exists. Verified on device: `execing userconfig.d/10-flighttest.cfg`.
- MEASURED (cl_showfps, user-reported): **60 fps still, 29–44 fps heavy
  movement** at native 1024×768, zero tuning applied. 30fps target essentially
  met stock. M5 scope shrinks to: verify flashlight + big firefight don't
  cliff (r_dynamic 0 standing by), then lock in.
