# webos-goldsrc — Half-Life (Xash3D FWGS) for the HP TouchPad

**Read `docs/NOTES.md` first** — it is the running device-truth log (decisions
D1–D6, all build gotchas, device-run findings). Milestone plan M0–M6 lives in
the session task list / plan file.

## State (2026-08-15, evening)

M0–M4 + packaging DONE, hardware-verified end to end: full **retail
Half-Life and the bundled Uplink demo run on the TouchPad** — 1024×768
ref_gles1, touch + evdev gamepads/keyboards with per-pad bind profiles,
WOSQI-verified postinst/prerm, data-not-found + App-Museum-update menu
notices, ipk 0.1.11 (playable out of the box; user data overrides per-file).

- **M5 perf: deferred by choice** ("perf is fine" — user, 2026-08-15).
  30fps target + `r_dynamic 0` notes stand for whenever it's picked up.
- **Next: App Museum release** — the in-app updater is wired and verified
  (listing name **"Half-Life"**); remaining work is user-side: submit the
  listing (assets like sdlquake's `_meta/`, downloadURI to the ipk on
  appstorage). The WON-strings cosmetic (GameUI_* keys) is the only known
  release nit.

## Layout

- `xash3d-fwgs/` — engine submodule, branch `webos` (all platform patches live
  here as commits; nested submodules `3rdparty/nanogl` AND `3rdparty/mainui`
  also carry `webos` branches)
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
- `mainui-webos` — the nested mainui submodule's `webos` branch (menu patches)
- .gitmodules still points at upstream FWGS URLs; after a fresh clone, fetch the
  backup branches into the submodules (or create real GitHub forks and
  repoint .gitmodules — the cleaner long-term move).

## Public release repo (github.com/webOSArchive/webos-half-life)

Single flattened tree, no submodules: parent history + a `flatten:` commit
vendoring the fork branches (SHAs recorded in that commit's message).
Releases re-vendor from the dev fork branches; verified to build from a
fresh clone.
