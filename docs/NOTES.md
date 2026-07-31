# Device-truth log — Half-Life (Xash3D FWGS) on HP TouchPad

Running log of what was actually observed on hardware / during the port.
Newest entries at the top of each section. Keep this honest — future sessions
depend on it. The full plan lives in the session plan file; milestones M0–M6.

## Decisions

| ID | Decision | Status |
|----|----------|--------|
| D1 | Compiler: PDK gcc 4.3.3 vs Linaro 4.9.4 + PDK sysroot vs ct-ng | OPEN (resolve in M1 probe) |
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

(record per-file ICEs, demoted optimization levels, patched wscripts here)

## Device runs

- 2026-07-31 glsmoke (novacom shell, /tmp): GLES1.1 context OK at 1024×768
  ("OPENGLES, ver1, 565 d16 OK"), Adreno 220, MAX_TEXTURE 4096, 2 units,
  glGetError clean. Extensions include GL_OES_compressed_ETC1_RGB8_texture
  (note: fuller list than the earlier recorded probe — ETC1 IS present).
  ANOMALY: swap rate ~10–11 fps vs 60 recorded during sdlquake work — probably
  because the launcher owned the screen (hidden-surface swap throttle) when run
  from a shell. Re-measure from a launcher-launched app in M2 before worrying.
- Device: topaz-linux over USB novacom, uber-kernel 3.0.5-93.
