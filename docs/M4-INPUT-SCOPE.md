# M4 remainder — evdev keyboard + gamepad: scope

Source-reading findings from 2026-08-08. **No device work was done**; nothing
here is hardware-verified yet. Line numbers are against the `webos` branch at
engine commit `e4399270`.

The headline: this is smaller than the milestone plan implies. The engine already
ships an evdev backend that is compiled into our binary but switched off, and
Xash's joystick *consumer* layer is richer than sdlquake's — so porting our
reference means deleting roughly half of it, not writing new code.

## What already exists

`engine/platform/linux/in_evdev.c` (469 lines, mittorn's) **is already compiled
into our `libxash.so`**: `engine/wscript:169` globs `platform/%s/*.c` and our
`DEST_OS` is `linux`. Its whole body sits behind `#if XASH_USE_EVDEV`, which is
currently 0 — confirmed by the built `.so` containing the source path but zero
occurrences of the `evdev_open` command string.

The hybrid we want is an anticipated configuration, not a fight with the engine:

- `XASH_INPUT` is exclusive (`INPUT_SDL` vs `INPUT_EVDEV`, `common/defaults.h:35`),
  but **`XASH_USE_EVDEV` is a separate flag**.
- `in_evdev.c:361` branches on `cls.key_dest == key_game || XASH_INPUT == INPUT_EVDEV`
  — i.e. it is written to run as a *supplement* to another input backend.

So SDL1 keeps owning touch while evdev adds keyboards and pads.

Symbol-collision check (both clean):

- `in_sdl1.c` does **not** define `Platform_EnableTextInput`; only `in_evdev.c` does.
- `in_evdev.c:436` defines `Platfrom_MouseMove` (sic) — misspelled, called from
  nowhere in the tree. Dead code, no clash with `in_sdl1.c:68 Platform_MouseMove`.

## Tier 1 — keyboard + mouse (nearly free)

Add `#define XASH_USE_EVDEV 1` to the existing `#ifdef __webos__` block in
`common/defaults.h:160`, keeping `XASH_INPUT INPUT_SDL`. That lights up keycode
mapping (`KeycodeFromEvdev`), `evdev_open`/`evdev_close`/`evdev_autodetect`,
mouse buttons and relative motion, and `Platform_EnableTextInput`.

One wrinkle: `Evdev_Autodetect_f` only accepts nodes advertising `BTN_MOUSE`
(`in_evdev.c:204`), so keyboards need either an explicit `evdev_open` or a small
patch — `looks_like_keyboard()` already exists in `webos/input/in_evdev.c:479`
to lift.

## Tier 2 — gamepad (the actual work)

The engine's evdev has **no** pad support: no `ABS`→axis, no `BTN_GAMEPAD`→button.
Everything downstream, however, already exists:

- `Joy_AxisMotionEvent( engineAxis_t, short )` — `engine/client/input.h:150`
- Axis roles `JOY_AXIS_SIDE/FWD/PITCH/YAW/RT/LT` — `engine/client/input.h:130`
- The full `K_A_BUTTON`…`K_DPAD_*` / `K_LSTICK` block — `engine/keydefs.h:130`
- Deadzones, sensitivity, axis→key thresholds, `joy_axis_binding`, and the rest
  of the `joy_*` cvars — `Joy_Init`, `engine/client/input/in_joy.c:572`

Today the only producer is SDL2 (`engine/platform/sdl2/joy_sdl2.c:300`), which is
the model to copy: it is a thin translator, ~415 lines including gyro and rumble
we don't need.

So the job is a translator, and `webos/input/in_evdev.c` (1302 lines, sdlquake)
already holds the hard half — per-pad profiles (DS4, Shanwan, Logitech,
DragonRise, generic), name matching, hotplug retry, axis adoption, dpad decode.

What **drops**: sdlquake's `IN_Evdev_Move` did its own deadzone / sensitivity /
usercmd math. Xash does all of that in `in_joy.c`, so our output collapses to
`Key_Event( K_*_BUTTON, down )` + `Joy_AxisMotionEvent( JOY_AXIS_*, value )`.

Genuinely new code is mostly one thing: normalizing each axis's `EVIOCGABS`
min/max into Xash's `short` range.

`Platform_JoyInit` is **not** required. It is gated `#if XASH_SDL >= 2`
(`platform/platform.h:302`), but `Joy_Init` sets `joy_initialized = true`
regardless of its return value (`in_joy.c:618`), so the consumer layer is live
and we can emit events from our own hook.

## Tier 3 — jail + permissions (mechanical, one sting)

`webos/app/control-m4-todo/{postinst,prerm}` already solve this: bind-mount
`/dev/input` into the PDK jail, install a udev 0666 rule for input event nodes,
and `udevtrigger` so already-attached devices get re-evaluated (that last part
is load-bearing — the scripts' own comments record a debugging session lost to
omitting it).

Adaptations needed:

1. Rename the sdlquake identifiers (udev rule filename, echo strings).
2. Our `appinfo` type is **`pdk`**, not Quake's `game`, so `jail_pdk.conf` is our
   file — the script patches both anyway, and coordinates removal with
   Commander Keen / `org.webosarchive.btgamepad`, so keep that logic intact.
3. `scripts/package.sh` copies from `webos/app/control/`, but the scripts live in
   `control-m4-todo/` — so they currently ship nowhere. Directory move; the copy
   loop already exists.

**The sting:** maintainer scripts only run as root under Preware / WebOS Quick
Install. `palm-install` does not run them at all. That changes our own iteration
loop, not just end-user installs.

## Bluetooth is not a risk here

Pairing and report delivery are already solved in a sibling project:
`~/Projects/webos-hardware-tests/bluetooth-shim` `LD_PRELOAD`-interposes the
HID→uinput bridge in `libPmBtBsaif.so`, because Palm's stack decodes then discards
everything that isn't a keyboard. Paired pads therefore surface as ordinary
`/dev/input/eventN` nodes and our evdev work just reads them.

Hard constraint from that project: the TouchPad radio is CSR BlueCore6-ROM,
**Bluetooth 2.1+EDR, classic BR/EDR only — no BLE, unfixable in software**. Target
era-appropriate BR/EDR pads.

## Rough effort

| Tier | Work | Estimate |
|---|---|---|
| 1 | keyboard + mouse: one `#define` + keyboard autodetect patch | short session |
| 2 | gamepad: port the device layer, delete the math half | 1–2 sessions |
| 3 | jail/udev script adaptation + package.sh | half session |

Tiers 1 and 2 can be written and cross-compiled entirely offline. Only
verification needs the device plus a real controller.
