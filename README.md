# Half-Life for the HP TouchPad

A native port of **Half-Life** (Xash3D FWGS engine + hlsdk-portable game logic)
to the HP TouchPad running webOS 3.0.5 — 1024×768 on the GPU via GLES1
(NanoGL→libGLES_CM), touch controls, and full gamepad/keyboard support.

- Engine: [xash3d-fwgs](https://github.com/FWGS/xash3d-fwgs), `webos` branch
  (all platform patches live there as commits)
- Game data: works with the Half-Life *Uplink* demo data or retail `valve/`
  content — game data is **not** included in this repo or the .ipk
- Install: `.ipk` via Preware / WebOS Quick Install (the postinst needs root to
  expose `/dev/input` to the app jail — the launcher's plain installer can't
  do that, so controllers/keyboards won't work with a `palm-install` install)
- Game data goes to `/media/internal/xash/valve/` (pak0.pak, wads, `dlls/`,
  `cl_dlls/` — see `docs/NOTES.md` for the exact tree)

## Loading the full game

You need your own copy of Half-Life (Steam, or an old WON CD install). Then:

1. Install the app and **launch it once** — it boots to the menu on its own
   and creates its storage folder, `xash`, on the USB drive.
2. Quit, plug the TouchPad into a computer over USB, and tap **USB Drive**
   on the device — it mounts as a flash drive. Open the `xash` folder on it.
3. Copy the `valve` folder from your Half-Life install into `xash`,
   **merging into** the `valve` folder that's already there (keep the
   existing files — they're your settings). From Steam the source is
   `steamapps/common/Half-Life/valve`. The `valve_hd` / `valve_addon`
   folders are optional and not needed.
4. Eject, unplug, launch. First start takes a few seconds while the 260 MB
   `pak0.pak` is read; after that it's quick.

**The `xash` folder IS your installed game** — the copied Half-Life content
plus your saves, settings, and screenshots all live in it. Deleting or
renaming it removes the game data: the app itself still launches (you'll get
the bare engine menu over a gray background, and starting a game does
nothing), but you're back to step 2 — recopy your `valve` folder to play
again.

The TouchPad-native game logic libraries ship inside the app itself (they're
GPL hlsdk-portable builds, no Valve content), so the retail x86 binaries in
your copied `valve/dlls` are simply ignored. Power users can override the
built-in ones by placing their own `dlls/hl_armv7l.so` /
`cl_dlls/client_armv7l.so` in `xash/valve/` — the data tree always wins.

The *Uplink* demo works the same way with the demo's data (everything else
it needs, including `delta.lst`, ships with the app).

## Controls

The port supports four input styles, usable in any combination. Everything is
rebindable; the defaults below are what ships.

### Touch (always available)

- Single-tap menu navigation; in game, an on-screen button overlay plus
  touch-look (drag anywhere outside a button to aim).
- Edit the overlay in-game: Options → Touch → Touch buttons.
- **Toggle the overlay**: Options → Touch → Touch options → *Enable touch
  controls* — or press the **Guide/PS button** on a connected gamepad.

### Keyboard (USB or Bluetooth)

Physical keyboards are read directly from the kernel (webOS's own keyboard
path is unreliable for games), including the first-party **HP TouchPad
Wireless Keyboard**. Plug/pair it and it just works — no setup.

Default bindings are classic Half-Life (arrows `+forward/+back/+left/+right`,
Ctrl fire, Space jump, Shift run, Alt strafe) plus the modern letters:

| Key | Action |
|-----|--------|
| `e` | Use |
| `f` | Flashlight |
| `r` | Reload |
| `z` | Use (classic) |
| `` ` `` | Console (developer mode) |

### Gamepads (USB or Bluetooth)

Pads are auto-detected and matched to a **profile** that knows that pad's
button/axis quirks. Each profile also selects a bind scheme via
`valve/pad_<profile>.cfg`, exec'd automatically when the pad connects.

**Standard scheme** — twin-stick pads (DualShock 4, ShanWan/Xbox-style,
unknown pads):

| Control | Action |
|---------|--------|
| Left stick | Move / strafe |
| Right stick | Look |
| A / Cross | Jump |
| B / Circle | Use |
| X / Square | Reload |
| Y / Triangle | Flashlight |
| L1 | Duck |
| R1 | Fire |
| L2 (analog or click) | Run |
| R2 (analog or click) | Alt fire |
| Stick clicks | Run / Duck |
| D-pad | Spray / last weapon / prev / next |
| Start | Menu |
| Back/Share | Pause |
| **Guide / PS** | **Toggle touch overlay** |

**Quake-style stickless scheme** — pads with no analog sticks (Logitech
Precision, DragonRise "Saturn"), inherited from our sdlquake port:

| Control | Action |
|---------|--------|
| D-pad | Move / strafe |
| Face buttons | Look (left/right turn, up/down pitch) |
| L1 (or Saturn L) | Jump |
| R1 (or Saturn R) | Fire |
| L2 (btn 7 / Saturn C) | Use |
| R2 (btn 8 / Saturn Z) | Next weapon |
| Back / Select | Flashlight |
| Start | Menu |

Menus navigate with d-pad/left stick, A/Cross selects, B/Circle backs out.
Look sensitivity: Options → **Gamepad** (pitch/yaw sliders).

**Customizing a pad**: edit its `pad_<profile>.cfg` (copy from the app install
dir to `/media/internal/xash/valve/` to override without touching the
install). Menu rebinds of gamepad buttons are overwritten the next time a
different pad type connects — the cfg files are the durable customization
point.

**Unknown pads**: anything we have no profile for gets a sensible generic
mapping plus auto-detected axes. If buttons land oddly, map them yourself
from the console — no rebuild needed:

    padstatus            what pad is open, and the map in force
    padtest              echo every raw button/axis code as you press it
    padbtn <code> <pos>  bind an evdev code to a position (e.g. padbtn 0x131 face_down)
    padaxis <role> <ax>  bind an axis role to an ABS code (-1 disables)

Put the `padbtn`/`padaxis` lines in `valve/autoexec.cfg` and they apply at
every launch, surviving replugs.

### Mouse (experimental)

A Bluetooth mouse's buttons/wheel are recognized, but mouse-look is disabled
by default (`m_ignore 1`) because webOS delivers touches as mouse events. Set
`m_ignore 0` from the console to experiment at your own risk.

## Developer mode

Release installs show no on-screen console messages. To enable the developer
console + verbose on-screen log: create the marker file
`/media/internal/xash/dev` (e.g. over novacom) and relaunch — or toggle
Options → Game options → *Enable developer console* for just the console.
The engine always writes `/media/internal/xash.log`.

## Building

See `CLAUDE.md` for the build/deploy quickref and `docs/NOTES.md` for the
device-truth log (toolchain decisions, hardware findings, every gotcha).
Short version: Linaro GCC 4.9.4 + Palm PDK sysroot, `scripts/build-engine.sh`,
`scripts/build-hlsdk.sh`, `scripts/package.sh`; every shipped ELF must pass
`scripts/check-symbols.sh` (GLIBC ≤ 2.4).

Half-Life is a Valve product; this port contains no Valve assets or code —
bring your own game data. Engine under GPLv3 per Xash3D FWGS.
