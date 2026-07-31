# webos-goldsrc — Half-Life (Xash3D FWGS) for the HP TouchPad

**Read `docs/NOTES.md` first** — it is the running device-truth log (decisions
D1–D6, all build gotchas, device-run findings). Milestone plan M0–M6 lives in
the session task list / plan file.

## State (2026-07-31)

M0–M3 done + touch input: **the game runs on the TouchPad** — menu and Uplink
demo campaign at native 1024×768 on ref_gles1 (NanoGL→libGLES_CM), single-tap
menus, full touch overlay + touch-look in game.

Next: **M4 remainder** — evdev gamepad + keyboard (see task list; engine-side
API is Key_Event(K_A_BUTTON…)/Joy_AxisMotionEvent; port sdlquake profile
tables from `webos/input/in_evdev.c`), then M5 perf (30fps target,
`r_dynamic 0` first), then M6 packaging.

## Layout

- `xash3d-fwgs/` — engine submodule, branch `webos` (all platform patches live
  here as commits; nested submodule `3rdparty/nanogl` also has a `webos` branch)
- `hlsdk-portable/` — game logic submodule (unpatched upstream)
- `webos/` — env.sh (toolchain), pkgconfig/sdl.pc shim, app/ (appinfo, icon,
  control-m4-todo/ = postinst/prerm to adapt in M4), input/ (sdlquake evdev
  reference), diag/ (glsmoke etc.), glibc25-math-compat.h
- `scripts/` — build-engine.sh, build-hlsdk.sh, build-diag.sh, package.sh,
  deploy.sh, check-symbols.sh (GLIBC ≤2.8 gate — run on every shipped ELF)
- `data/` (gitignored) — uplink-valve/ = extracted HL Uplink demo data

## Build & deploy quickref

```
scripts/build-engine.sh          # waf build of engine+renderers (Linaro 4.9.4 + PDK sysroot)
scripts/build-hlsdk.sh           # hl_armv7l.so + client_armv7l.so
scripts/package.sh               # .ipk (bump version in webos/app/appinfo.json first)
palm-install ipks/<latest>.ipk   # or hot-push a single .so:
novacom put file:///media/cryptofs/apps/usr/palm/applications/org.webosarchive.xashhl/libxash.so < xash3d-fwgs/build/engine/libxash.so
# then killall xash3d (via pushed script) + palm-launch org.webosarchive.xashhl
```

- Logs: `/media/internal/xash.log` (launcher redirects stdout/stderr; -dev 2 -log
  args are baked into the launcher for now — remove for release in M6)
- Game data on device: `/media/internal/xash/valve/` (pak0.pak, wads, delta.lst,
  gameinfo.txt, dlls/, cl_dlls/, config). App dir = XASH3D_RODIR (extras.pk3).
- novacom quirk: `novacom run` word-splits; push a script and `sh` it
  (see scripts/deploy.sh and the scratchpad pattern in NOTES).

## Iron rules (hardware-verified; violating any = silent black/broken screen)

- SDL_GL_SwapBuffers, never SDL_Flip, for GL frames
- SDL_GL_CONTEXT_MAJOR_VERSION=1 + SDL_SetVideoMode(0,0,0,SDL_OPENGL)
- No SDL_GL_LoadLibrary on webOS; NanoGL dlopens libGLES_CM.so
- Never call EGL directly; never ship metadata.json; `main` = real ELF binary
- Every shipped ELF passes scripts/check-symbols.sh (Linaro WITHOUT the PDK
  sysroot links GLIBC_2.15+ and will not load)

## Remote backup layout (private repo codepoet80/webos-xash3D)

- `main` — this parent repo
- `xash3d-webos` — the xash3d-fwgs submodule's `webos` branch (all engine patches)
- `nanogl-webos` — the nested nanogl submodule's `webos` branch
- .gitmodules still points at upstream FWGS URLs; after a fresh clone, fetch the
  two backup branches into the submodules (or create real GitHub forks and
  repoint .gitmodules — the cleaner long-term move).
