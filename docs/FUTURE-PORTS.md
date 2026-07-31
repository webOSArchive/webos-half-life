# Future port candidates for the TouchPad

Scratch notes from end-of-session brainstorming (2026-07-31). Nothing started.

## Command & Conquer / Red Alert — **most promising, scoped a little**

EA open-sourced the full game (https://github.com/electronicarts/CnC_Red_Alert),
but the better base is the community fork that already did the portability work:
**https://github.com/TheAssemblyArmada/Vanilla-Conquer** (default branch:
`vanilla`, NOT `develop`).

Why it looks lower-risk than GoldSrc was:
- **Native C/C++** — our whole toolchain applies directly (Linaro 4.9 + PDK
  sysroot + check-symbols.sh). No managed runtime.
  (This is why OpenRA is the wrong target: it's C#/Mono, which would mean
  porting a JIT to ARMv7 softfp against glibc 2.5 — a nizovn-Qt5-scale project.)
- **It ships a real SDL1 backend**: `common/video_sdl1.cpp` does
  `SDL_SetVideoMode(w, h, 8, SDL_HWSURFACE|SDL_HWPALETTE)` + `SDL_Flip()` —
  8-bit palettized software rendering, no GL/EGL at all. That is exactly the
  Commander Keen technique already proven on this device, and it sidesteps the
  entire GLES1-vs-GLES2 minefield that shaped the GoldSrc port.
  (An SDL2 backend also exists — `SDL_CreateRenderer`/`SDL_CreateTexture` in
  `video_sdl2.cpp` — but we would deliberately take the SDL1 path.)
- **Software-drawn mouse cursor**: `SDL_BlitSurface(hwcursor.Surface, ...)` into
  the same 8-bit surface each frame. No hardware cursor API to shim; we just
  feed position from normal SDL mouse events. The TouchPad detects USB/BT mice
  but does nothing with them (per webos-hardware-tests), and our multitouch
  translation from the GoldSrc port maps onto this directly.
- Game data licensing is a non-issue — EA released the whole thing.

Still unknown (do this first if we pick it up):
1. Build system (CMake?) and how it selects the SDL1 backend.
2. Size of the Win32 surface left to `#ifdef` out for a single-player build
   (WinSock multiplayer, any DirectX-adjacent leftovers).
3. Internal render resolution vs. the 1024x768 panel — scaling strategy.
4. RTS touch controls are the real *design* work: drag-select, minimap taps,
   right-click equivalent, edge scrolling. Fun problem, not a technical risk.

## Other candidates considered

| Game | Base | Notes |
|---|---|---|
| **Quake II** | Yamagi Quake II / Qudos | Closest sibling to the GoldSrc port; fixed-function GL 1.x, shareware data redistributable. Highest-confidence fastest win; best test of whether our "port library" (env.sh, packaging, touch overlay, evdev) is genuinely reusable. |
| **Duke Nukem 3D / Blood** | eduke32 / NBlood | Build engine (2.5D sectors), fixed-function GL, shareware data free. Diversifies beyond id-derived engines. |
| **Quake III Arena** | ioquake3 | Bigger lift: QVM bytecode VM, curved surfaces, heavier shader system. A "how far can this go" showcase. |
| **Descent / Descent II** | DXX-Rebirth | Similar engine weight; interesting because 6-DOF flight would stress-test the input design in a genuinely new way. |
