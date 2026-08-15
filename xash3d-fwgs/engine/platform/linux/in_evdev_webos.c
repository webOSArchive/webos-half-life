/*
in_evdev_webos.c - webOS direct-evdev input: USB & Bluetooth game controllers
and physical keyboards, injected into the engine's own input pipeline.

Ported from the sdlquake TouchPad port's in_evdev.c (hardware-verified there);
the pad-profile layer is kept intact, the Quake-side action scheme is replaced
by the engine's native gamepad vocabulary:

  buttons -> Key_Event( K_A_BUTTON ... K_DPAD_* ) -- default HL binds and
             mainui menu navigation both already speak these keys
  sticks/triggers -> Joy_AxisMotionEvent( 0..5 ) in the engine's default
             "sfpyrl" hardware-axis order (side fwd pitch yaw rt lt)
  keyboards -> Key_Event + CL_CharEvent when host.textmode

Why this exists (three webOS 3.0.5 platform facts):
  1. There is NO joydev -- SDL_INIT_JOYSTICK finds nothing. Pads appear only
     as /dev/input/eventN. So we read evdev directly.
  2. webOS routes physical keyboards through hidd, and the PDK/SDL webOS key
     path is unreliable for external keyboards. Reading the keyboard's evdev
     node ourselves (and EVIOCGRAB-ing it) guarantees keys reach the game
     regardless, with no double-input.
  3. The PDK app jail hides /dev/input; the package's postinst bind-mounts it
     in and 0666's the nodes (uid 5003). Without that this module opens
     nothing -- which is exactly why a game works from a shell but not the
     launcher.

MAPPING MODEL

Pads disagree wildly about which evdev code a given physical button sends:
the Bluetooth DS4 (via the webos-bt-shim) packs its report order into
0x130..0x13d; the ShanWan "Xbox knock-off" uses the standard BTN_ gamepad
names; the Logitech Precision reports on the generic BTN_JOYSTICK (0x120)
block. Hard-coding one of those orders is what made the mapping feel
"unpredictable" on every pad but the one it was written for.

So decoding happens in two stages:
  1. A per-pad PROFILE (padprofile_t, matched on the evdev device name) maps
     raw evdev codes/axes onto pad-independent PHYSICAL POSITIONS -- face
     left/down/right/up, shoulders, triggers, back/start/guide, stick clicks.
  2. ONE mapping binds those positions to engine gamepad keys/axes. Every pad
     therefore behaves identically, and adding a pad means adding a profile,
     never touching the mapping code.
*/
#include "platform/platform.h"
#ifdef __webos__

#include "common.h"
#include "input.h"
#include "client.h"
#include "keydefs.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

// The PDK's kernel headers predate the BTN_SOUTH gamepad naming; fall back to
// the classic codes (all identical values).
#ifndef BTN_SOUTH
#define BTN_SOUTH    0x130   // == BTN_A / BTN_GAMEPAD
#endif
#ifndef BTN_GAMEPAD
#define BTN_GAMEPAD  0x130
#endif
#ifndef BTN_JOYSTICK
#define BTN_JOYSTICK 0x120
#endif

// ==========================================================================
//  Device table
// ==========================================================================
#define MAX_DEVS   8
#define DEV_NONE   0
#define DEV_PAD    1
#define DEV_KBD    2

typedef struct
{
	int  fd;
	int  kind;      // DEV_PAD / DEV_KBD
	int  idx;       // /dev/input/eventN index we hold (-1 = free slot)
} evdev_dev_t;

static evdev_dev_t devs[MAX_DEVS];

// Nodes probed and found uninteresting (the TouchPad's own gpio-keys,
// pmic8058_pwrkey and headset). MEASURED (sdlquake): open() on those three
// costs 22-71 ms EACH -- their drivers do real work in the open handler -- so
// probing them once a second cost ~180 ms and showed up as a periodic
// half-second hitch that looked for all the world like a rendering problem.
// Probe a node once, then skip it until it actually disappears.
#define MAX_NODES 16
static byte   node_boring[MAX_NODES];
static double last_scan;
static int    scan_verbose = 3;   // chatty right after a disconnect, then quiet

// ==========================================================================
//  Physical positions -- the pad-independent vocabulary. DS4 names are the
//  canonical labels.
// ==========================================================================
enum
{
	P_NONE = 0,     // must be 0: unmapped codes decode to this
	P_FACE_LEFT,    // Square    / X (Xbox)  / btn1
	P_FACE_DOWN,    // Cross     / A         / btn2
	P_FACE_RIGHT,   // Circle    / B         / btn3
	P_FACE_UP,      // Triangle  / Y         / btn4
	P_SHOULDER_L,   // L1        / LB        / btn5
	P_SHOULDER_R,   // R1        / RB        / btn6
	P_TRIGGER_L,    // L2        / LT        / btn7
	P_TRIGGER_R,    // R2        / RT        / btn8
	P_BACK,         // Share     / Back      / btn9
	P_START,        // Options   / Start     / btn10
	P_GUIDE,        // PS        / Guide
	P_CLICK_L,      // L3
	P_CLICK_R,      // R3
	P_EXTRA,        // DS4 touchpad click, spare buttons
	P_NPOS
};

// Raw button codes we track: the generic BTN_JOYSTICK block (0x120..0x12f) and
// the gamepad block (0x130..0x13f), 32 codes in one array.
#define BTN_BASECODE  BTN_JOYSTICK          // 0x120
#define BTN_NRAW      32
#define RAWOF( code ) ((code) - BTN_BASECODE)

// Some pads report the d-pad as four BUTTONS rather than a hat or an axis
// pair. These codes sit outside the two blocks above and mean exactly one
// thing each, so they are decoded directly and need no profile entry.
#ifndef BTN_DPAD_UP
#define BTN_DPAD_UP    0x220
#define BTN_DPAD_DOWN  0x221
#define BTN_DPAD_LEFT  0x222
#define BTN_DPAD_RIGHT 0x223
#endif

// ==========================================================================
//  Per-pad profile
// ==========================================================================
#define AX_NONE (-1)

// One entry per physical button a pad actually has; the list ends at code 0.
// Anything not listed decodes to P_NONE and is ignored.
typedef struct { short code; short pos; } btnmap_t;

typedef struct
{
	const char      *match;         // substring of the evdev device name; NULL = default
	const char      *label;         // for the console line
	const char      *cfg;           // per-pad bind profile, exec'd on connect
	const btnmap_t  *btns;
	int  move_x, move_y;            // left stick (also the d-pad on stickless pads)
	int  look_x, look_y;            // right stick (AX_NONE if the pad has none)
	int  trig_l, trig_r;            // analog trigger axes (AX_NONE = digital only)
	int  dpad_x, dpad_y;            // d-pad axes; the hat is always used too
	// Adopt UNCLAIMED centre-resting axes as a d-pad? A heuristic for pads we
	// have no table for -- and only for those. A known profile already names
	// every axis that matters, and pads advertise junk axes that never move
	// (the DragonRise has three), which adoption would wire straight into
	// movement or menu navigation.
	int  adopt_axes;
} padprofile_t;

/* ------------------------------------------------------------------------
 * DS4 (Bluetooth via webos-bt-shim, and USB). Verified on device E
 * 2026-07-29 (sdlquake) -- the shim emits its HID report order onto
 * BTN_SOUTH+i, which is NOT the standard BTN_ ordering, so this table is the
 * truth, not <linux/input.h>. Analog L2/R2 land on ABS_RX/RY (resting at an
 * extreme).
 * ---------------------------------------------------------------------- */
static const btnmap_t btns_ds4[] =
{
	{ 0x130, P_FACE_LEFT  },   /* Square   */
	{ 0x131, P_FACE_DOWN  },   /* Cross    */
	{ 0x132, P_FACE_RIGHT },   /* Circle   */
	{ 0x133, P_FACE_UP    },   /* Triangle */
	{ 0x134, P_SHOULDER_L },   /* L1       */
	{ 0x135, P_SHOULDER_R },   /* R1       */
	{ 0x136, P_TRIGGER_L  },   /* L2       */
	{ 0x137, P_TRIGGER_R  },   /* R2       */
	{ 0x138, P_BACK       },   /* Share    */
	{ 0x139, P_START      },   /* Options  */
	{ 0x13a, P_CLICK_L    },   /* L3       */
	{ 0x13b, P_CLICK_R    },   /* R3       */
	{ 0x13c, P_GUIDE      },   /* PS       */
	{ 0x13d, P_EXTRA      },   /* Touchpad */
	{ 0, P_NONE }
};
static const padprofile_t prof_ds4 =
{
	"Wireless Controller", "DS4", "pad_ds4.cfg", btns_ds4,
	ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_RX, ABS_RY, AX_NONE, AX_NONE, 0
};

/* ------------------------------------------------------------------------
 * ShanWan "Xbox knock-off" wireless pad (0079:181c). Standard BTN_ gamepad
 * naming; the analog triggers are ABS_BRAKE (left) / ABS_GAS (right) -- axes
 * the old DS4-shaped code never read at all, which is why LT/RT did nothing.
 * Truth table captured on device E, 2026-07-29 (sdlquake).
 * ---------------------------------------------------------------------- */
static const btnmap_t btns_shanwan[] =
{
	{ 0x130, P_FACE_DOWN  },   /* BTN_SOUTH  = A  */
	{ 0x131, P_FACE_RIGHT },   /* BTN_EAST   = B  */
	{ 0x133, P_FACE_LEFT  },   /* BTN_NORTH  = X  */
	{ 0x134, P_FACE_UP    },   /* BTN_WEST   = Y  */
	{ 0x136, P_SHOULDER_L },   /* BTN_TL     = LB */
	{ 0x137, P_SHOULDER_R },   /* BTN_TR     = RB */
	{ 0x138, P_TRIGGER_L  },   /* BTN_TL2    = LT digital half */
	{ 0x139, P_TRIGGER_R  },   /* BTN_TR2    = RT digital half */
	{ 0x13a, P_BACK       },   /* BTN_SELECT = Back  */
	{ 0x13b, P_START      },   /* BTN_START  = Start */
	{ 0x13c, P_GUIDE      },   /* BTN_MODE   = Guide */
	{ 0x13d, P_CLICK_L    },   /* BTN_THUMBL = LS click */
	{ 0x13e, P_CLICK_R    },   /* BTN_THUMBR = RS click */
	{ 0, P_NONE }
};
static const padprofile_t prof_shanwan =
{
	"ShanWan", "ShanWan/Xbox", "pad_shanwan.cfg", btns_shanwan,
	ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_BRAKE, ABS_GAS, AX_NONE, AX_NONE, 0
};

/* ------------------------------------------------------------------------
 * Generic BTN_JOYSTICK block: printed button N -> code 0x120+(N-1). This is
 * the Logitech Precision Gamepad (046d:c21a) -- 10 buttons, d-pad on ABS_X/Y,
 * no analog sticks, no triggers -- and the shape most simple HID pads use.
 * ---------------------------------------------------------------------- */
static const btnmap_t btns_generic10[] =
{
	{ 0x120, P_FACE_LEFT  },   /* button 1  */
	{ 0x121, P_FACE_DOWN  },   /* button 2  */
	{ 0x122, P_FACE_RIGHT },   /* button 3  */
	{ 0x123, P_FACE_UP    },   /* button 4  */
	{ 0x124, P_SHOULDER_L },   /* button 5  */
	{ 0x125, P_SHOULDER_R },   /* button 6  */
	{ 0x126, P_TRIGGER_L  },   /* button 7  */
	{ 0x127, P_TRIGGER_R  },   /* button 8  */
	{ 0x128, P_BACK       },   /* button 9  */
	{ 0x129, P_START      },   /* button 10 */
	{ 0, P_NONE }
};
static const padprofile_t prof_logitech =
{
	"Precision", "Logitech Precision", "pad_logitech.cfg", btns_generic10,
	ABS_X, ABS_Y, AX_NONE, AX_NONE, AX_NONE, AX_NONE, AX_NONE, AX_NONE, 0
};

/* ------------------------------------------------------------------------
 * DragonRise "Sega Saturn style" wireless pad (0079:0011), which enumerates as
 * "SWITCH CO.,LTD. USB Gamepad". Captured on device E 2026-07-29 (sdlquake).
 *
 * Ten buttons on the generic BTN_JOYSTICK block in the Saturn console's own
 * report order -- X, A, B, Y, C, Z, then L, R, Select, Start -- so the six face
 * buttons are INTERLEAVED across the two rows, not laid out row by row.
 * The 8-way d-pad is the ABS_RX/ABS_RY pair (0 = left/up, 128 = centre,
 * 255 = right/down; diagonals set both), NOT a hat and NOT the ABS_MISC axis it
 * also advertises. There is no analog stick and no analog trigger.
 *
 * It advertises THREE axes that never move: ABS_X pegged at 1 of 0-255, plus
 * ABS_Y and ABS_Z resting dead centre. ABS_X is what made the generic profile
 * strafe the player into a wall forever (see stick_ok), and ABS_Y/ABS_Z are why
 * this profile sets adopt_axes = 0.
 * ---------------------------------------------------------------------- */
static const btnmap_t btns_dragonrise[] =
{
	{ 0x120, P_FACE_LEFT  },   /* X  top-left     */
	{ 0x121, P_FACE_DOWN  },   /* A  bottom-left  */
	{ 0x122, P_FACE_RIGHT },   /* B  bottom-mid   */
	{ 0x123, P_FACE_UP    },   /* Y  top-mid      */
	{ 0x124, P_TRIGGER_L  },   /* C  bottom-right */
	{ 0x125, P_TRIGGER_R  },   /* Z  top-right    */
	{ 0x126, P_SHOULDER_L },   /* L trigger       */
	{ 0x127, P_SHOULDER_R },   /* R trigger       */
	{ 0x128, P_BACK       },   /* Select          */
	{ 0x129, P_START      },   /* Start           */
	{ 0, P_NONE }
};
static const padprofile_t prof_dragonrise =
{
	"SWITCH CO.", "DragonRise/Saturn", "pad_dragonrise.cfg", btns_dragonrise,
	/* the d-pad IS the movement control here -- it reads as a stick at full
	 * deflection, exactly as the Logitech's ABS_X/Y d-pad does */
	ABS_RX, ABS_RY, AX_NONE, AX_NONE, AX_NONE, AX_NONE, AX_NONE, AX_NONE, 0
};

/* ------------------------------------------------------------------------
 * Default: any pad we have no truth table for. Covers BOTH code blocks --
 * standard BTN_ names for the gamepad block, button-order for the generic
 * block -- since a pad advertises one or the other, never both. Axes are
 * probed, so a d-pad on ABS_RX/RY (DragonRise/Saturn style) is still found by
 * the centre-resting test in axes_setup().
 * ---------------------------------------------------------------------- */
static const btnmap_t btns_default[] =
{
	{ 0x120, P_FACE_LEFT  }, { 0x121, P_FACE_DOWN  },
	{ 0x122, P_FACE_RIGHT }, { 0x123, P_FACE_UP    },
	{ 0x124, P_SHOULDER_L }, { 0x125, P_SHOULDER_R },
	{ 0x126, P_TRIGGER_L  }, { 0x127, P_TRIGGER_R  },
	{ 0x128, P_BACK       }, { 0x129, P_START      },
	/* on a 12-button generic pad, 11 and 12 are nearly always the clicks */
	{ 0x12a, P_CLICK_L    }, { 0x12b, P_CLICK_R    },
	{ 0x130, P_FACE_DOWN  }, { 0x131, P_FACE_RIGHT },
	{ 0x133, P_FACE_LEFT  }, { 0x134, P_FACE_UP    },
	{ 0x136, P_SHOULDER_L }, { 0x137, P_SHOULDER_R },
	{ 0x138, P_TRIGGER_L  }, { 0x139, P_TRIGGER_R  },
	{ 0x13a, P_BACK       }, { 0x13b, P_START      },
	{ 0x13c, P_GUIDE      }, { 0x13d, P_CLICK_L    },
	{ 0x13e, P_CLICK_R    },
	{ 0, P_NONE }
};
static const padprofile_t prof_default =
{
	NULL, "generic", "pad_generic.cfg", btns_default,
	ABS_X, ABS_Y, ABS_Z, ABS_RZ, AX_NONE, AX_NONE, AX_NONE, AX_NONE, 1
};

static const padprofile_t *profiles[] =
{
	&prof_ds4, &prof_shanwan, &prof_logitech, &prof_dragonrise, &prof_default
};
#define NPROFILES ((int)( sizeof( profiles ) / sizeof( profiles[0] )))

static const padprofile_t *prof = &prof_default;

// ==========================================================================
//  EFFECTIVE MAP = profile, then auto-detection, then user overrides.
//
//  Nothing below reads prof-> directly at runtime. The effective map is what
//  the decoder uses, which is what lets an UNKNOWN pad be corrected in the
//  field from the console (padbtn / padaxis) with no rebuild -- put those in
//  valve/autoexec.cfg and they apply at every launch.
// ==========================================================================
static byte pos_of[BTN_NRAW];       // effective code -> position
enum { AXR_MOVE_X, AXR_MOVE_Y, AXR_LOOK_X, AXR_LOOK_Y,
       AXR_TRIG_L, AXR_TRIG_R, AXR_DPAD_X, AXR_DPAD_Y, AXR_N };
static int ax_role[AXR_N];          // effective role -> ABS code (AX_NONE = unused)

// User overrides, kept separately so they survive re-selecting a profile on
// every hotplug. P_NONE / AX_NONE mean "no override".
static short pos_ovr[BTN_NRAW];
static int   ax_ovr[AXR_N];
static int   overrides_loaded;

static const char *axr_name[AXR_N] =
{
	"move_x", "move_y", "look_x", "look_y",
	"trig_l", "trig_r", "dpad_x", "dpad_y"
};

static const struct { const char *name; short pos; } posnames[] =
{
	{ "none",       P_NONE       }, { "face_left",  P_FACE_LEFT  },
	{ "face_down",  P_FACE_DOWN  }, { "face_right", P_FACE_RIGHT },
	{ "face_up",    P_FACE_UP    }, { "shoulder_l", P_SHOULDER_L },
	{ "shoulder_r", P_SHOULDER_R }, { "trigger_l",  P_TRIGGER_L  },
	{ "trigger_r",  P_TRIGGER_R  }, { "back",       P_BACK       },
	{ "start",      P_START      }, { "guide",      P_GUIDE      },
	{ "click_l",    P_CLICK_L    }, { "click_r",    P_CLICK_R    },
	{ "extra",      P_EXTRA      }, { NULL, P_NONE }
};

static const char *pos_name( int p )
{
	int i;
	for( i = 0; posnames[i].name; i++ )
		if( posnames[i].pos == p ) return posnames[i].name;
	return "?";
}

static void axes_roles_from_profile( void )
{
	ax_role[AXR_MOVE_X] = prof->move_x;  ax_role[AXR_MOVE_Y] = prof->move_y;
	ax_role[AXR_LOOK_X] = prof->look_x;  ax_role[AXR_LOOK_Y] = prof->look_y;
	ax_role[AXR_TRIG_L] = prof->trig_l;  ax_role[AXR_TRIG_R] = prof->trig_r;
	ax_role[AXR_DPAD_X] = prof->dpad_x;  ax_role[AXR_DPAD_Y] = prof->dpad_y;
}

static void profile_select( const char *name )
{
	const btnmap_t *m;
	int i;

	prof = &prof_default;
	for( i = 0; i < NPROFILES; i++ )
		if( profiles[i]->match && strstr( name, profiles[i]->match ))
		{
			prof = profiles[i];
			break;
		}

	memset( pos_of, P_NONE, sizeof( pos_of ));
	for( m = prof->btns; m->code; m++ )
	{
		int r = RAWOF( m->code );
		if( r >= 0 && r < BTN_NRAW ) pos_of[r] = (byte)m->pos;
	}
	for( i = 0; i < BTN_NRAW; i++ )              // user overrides win
		if( pos_ovr[i] != P_NONE ) pos_of[i] = (byte)pos_ovr[i];

	axes_roles_from_profile();
}

// ==========================================================================
//  Pad state
// ==========================================================================
static byte btn_raw[BTN_NRAW];      // indexed by RAWOF(code)
static byte pos_down[P_NPOS];       // positions, rebuilt from btn_raw each frame

// Every advertised absolute axis, with the range and the value it rested at
// when we opened the node. `rest` is what separates a stick/d-pad axis
// (rests centred) from an analog trigger (rests at one end) -- and it lets a
// pad whose range is not 0-255 work unchanged.
// stick_ok: the axis rests near its centre, so it is safe to read as a stick.
// A pad can advertise an axis that is permanently pegged at one end (the
// DragonRise/Saturn pad's ABS_X rests at 1 of 0-255 while all its real axes
// rest at 128). Read as a stick that is full deflection FOREVER -- the player
// walks or strafes into a wall and never stops. So a stick role only accepts a
// centre-resting axis; anything else is ignored for movement/look. Triggers
// are unaffected: trig_norm() measures from whichever end the axis rests at.
typedef struct { int have, stick_ok, min, max, center, half, rest, val; } axis_t;
static axis_t axes[ABS_MAX + 1];

static byte dpad_btn[4];            // BTN_DPAD_UP/DOWN/LEFT/RIGHT
static byte btn_have[BTN_NRAW];     // which codes this pad ADVERTISES
static char pad_name[80];           // for padstatus
static int  pad_test;               // "padtest": echo raw events to the console
static int  axes_retry;             // >0: re-probe a held pad's axes (hotplug race)
static int  hatx, haty;             // ABS_HAT0X/Y, -1/0/+1
static int  dpad_ax, dpad_ay;       // extra centre-resting axes acting as a d-pad
static int  have_right_stick;
static int  prev_have_pad;          // to zero the engine axes when the pad goes

// ==========================================================================
//  Key reconciliation
//    Pad-driven keys are rebuilt into want[] every frame and diffed against
//    have[]; multiple sources for one key never race on release, and a
//    disappearing pad cleanly drops everything it held down.
// ==========================================================================
#define QK_MAX 256
static byte qk_want[QK_MAX];
static byte qk_have[QK_MAX];

static void qk_mark( int key ) { if( key > 0 && key < QK_MAX ) qk_want[key] = 1; }

static void qk_reconcile( void )
{
	int k;
	for( k = 0; k < QK_MAX; k++ )
	{
		if( qk_want[k] != qk_have[k] )
		{
			qk_have[k] = qk_want[k];
			Key_Event( k, qk_want[k] ? true : false );
		}
	}
}

// ==========================================================================
//  Capability probing
// ==========================================================================
#define NLONGS( bits ) ((((bits) - 1) / ( 8 * sizeof( long ))) + 1 )

static int bit_set( const unsigned long *b, int i )
{
	return ( b[i / ( 8 * sizeof( long ))] >> ( i % ( 8 * sizeof( long )))) & 1UL;
}

static int has_key_cap( int fd, int code )
{
	unsigned long bits[NLONGS( KEY_MAX )];
	memset( bits, 0, sizeof( bits ));
	if( ioctl( fd, EVIOCGBIT( EV_KEY, sizeof( bits )), bits ) < 0 ) return 0;
	return bit_set( bits, code );
}

// A keyboard-ish device: advertises letter keys but is NOT a pad. (We also skip
// the TouchPad's built-in button nodes by name in scan_devices.)
static int looks_like_keyboard( int fd )
{
	return has_key_cap( fd, KEY_A ) && has_key_cap( fd, KEY_Z ) &&
	       has_key_cap( fd, KEY_ENTER ) && !has_key_cap( fd, BTN_GAMEPAD ) &&
	       !has_key_cap( fd, BTN_JOYSTICK );
}

// Read every advertised absolute axis: range, centre, and resting value.
// Any axis the profile does NOT claim, but which rests centred and is not a
// hat, is adopted as an extra digital d-pad axis -- that is what catches a
// DragonRise/Saturn pad whose d-pad sits on ABS_RX/RY, while correctly
// ignoring a DS4's analog triggers there (they rest at an extreme).
// Returns how many usable axes were found. ZERO means "ask again shortly":
// a freshly hotplugged pad can hand us its node before the kernel has
// published its axis info, and a pad held with no axes has no movement at all
// until it is unplugged. Safe to re-run -- it rebuilds the axis roles from the
// profile first, so adoption never accumulates.
static int axes_setup( int fd )
{
	unsigned long bits[NLONGS( ABS_MAX )];
	int i, have_bits, found = 0;

	memset( axes, 0, sizeof( axes ));
	dpad_ax = dpad_ay = AX_NONE;
	hatx = haty = 0;
	axes_roles_from_profile();

	memset( bits, 0, sizeof( bits ));
	// If the bitmap query fails, probe every code instead of giving up -- the
	// per-axis EVIOCGABS below is the real authority either way.
	have_bits = ( ioctl( fd, EVIOCGBIT( EV_ABS, sizeof( bits )), bits ) >= 0 );

	for( i = 0; i <= ABS_MAX; i++ )
	{
		struct input_absinfo ai;
		axis_t *a = &axes[i];
		int quarter, claimed, r;

		if( have_bits && !bit_set( bits, i )) continue;
		if( ioctl( fd, EVIOCGABS( i ), &ai ) != 0 ) continue;
		if( ai.maximum <= ai.minimum ) continue;

		a->have   = 1;
		a->min    = ai.minimum;
		a->max    = ai.maximum;
		a->center = ( ai.minimum + ai.maximum ) / 2;
		a->half   = ( ai.maximum - ai.minimum ) / 2;
		if( a->half < 1 ) a->half = 1;
		a->rest   = ai.value;
		a->val    = ai.value;
		found++;

		quarter = a->half / 2;
		if( quarter < 1 ) quarter = 1;
		a->stick_ok = ( a->rest >= a->center - quarter && a->rest <= a->center + quarter );

		// A NAMED profile is a truth table, so believe it over the resting-value
		// guess. The guess reads ai.value, which the kernel initialises to 0 and
		// only fills in once the device has actually reported -- so a pad nobody
		// has touched since it enumerated claims to rest at 0, one END of a 0-255
		// axis, and gets written off as permanently pegged. That failure is
		// silent: the pad opens, every button works, and the axes simply never do
		// anything. It cost the Logitech Precision its whole d-pad, because on
		// that pad the d-pad IS move_x/move_y -- there is no hat and no stick to
		// fall back on. rest is corrected too: a stick rests centred by
		// definition, and leaving rest at 0 would make pad_reset_state() park the
		// axis at full deflection and walk the player into a wall.
		// The heuristic still guards prof_default, which is where it earns its
		// keep -- an unknown pad's axes are a guess by definition (see the
		// DragonRise's phantom ABS_X above).
		if( prof != &prof_default )
		{
			for( r = AXR_MOVE_X; r <= AXR_LOOK_Y; r++ )
				if( i == ax_role[r] )
				{
					a->stick_ok = 1;
					a->rest = a->val = a->center;
					break;
				}
		}

		if( i == ABS_HAT0X || i == ABS_HAT0Y ) continue;

		claimed = 0;
		for( r = 0; r < AXR_N; r++ ) if( i == ax_role[r] ) { claimed = 1; break; }
		if( claimed ) continue;

		if( !prof->adopt_axes ) continue;     // known pad: its profile names every axis

		// UNKNOWN PAD -- adopt what we can from how each axis RESTS:
		//   centre-resting -> a stick / d-pad axis, usable for direction
		//   end-resting    -> an analog trigger
		// Between them these cover every pad measured so far: triggers turn up
		// on ABS_RX/RY (DS4), on ABS_GAS/ABS_BRAKE (ShanWan) and on ABS_Z/RZ
		// (XInput-style HID pads) -- far too much variety to hard-code, but all
		// three share the giveaway that a trigger rests at one END of its range.
		if( a->stick_ok )
		{
			if( dpad_ax == AX_NONE )      dpad_ax = i;
			else if( dpad_ay == AX_NONE ) dpad_ay = i;
		}
		else if( a->max - a->min >= 16 )      // skip tiny enum-style axes
		{
			if( ax_role[AXR_TRIG_L] == AX_NONE )      ax_role[AXR_TRIG_L] = i;
			else if( ax_role[AXR_TRIG_R] == AX_NONE ) ax_role[AXR_TRIG_R] = i;
		}
	}

	if( ax_role[AXR_DPAD_X] != AX_NONE ) dpad_ax = ax_role[AXR_DPAD_X];
	if( ax_role[AXR_DPAD_Y] != AX_NONE ) dpad_ay = ax_role[AXR_DPAD_Y];

	for( i = 0; i < AXR_N; i++ )                  // user overrides beat everything
		if( ax_ovr[i] != AX_NONE ) ax_role[i] = ax_ovr[i];
	if( ax_ovr[AXR_DPAD_X] != AX_NONE ) dpad_ax = ax_ovr[AXR_DPAD_X];
	if( ax_ovr[AXR_DPAD_Y] != AX_NONE ) dpad_ay = ax_ovr[AXR_DPAD_Y];

	// "beat everything" has to include the resting-value heuristic. A padaxis is
	// the user telling us what an axis IS; without this, stick_ok could veto the
	// very override written to work around a bad guess, and the field escape
	// hatch would quietly do nothing.
	for( i = AXR_MOVE_X; i <= AXR_LOOK_Y; i++ )
	{
		int c = ax_ovr[i];
		if( c == AX_NONE || c < 0 || c > ABS_MAX || !axes[c].have ) continue;
		axes[c].stick_ok = 1;
		axes[c].rest = axes[c].val = axes[c].center;
	}

	have_right_stick = ( ax_role[AXR_LOOK_X] != AX_NONE && axes[ax_role[AXR_LOOK_X]].stick_ok &&
	                     ax_role[AXR_LOOK_Y] != AX_NONE && axes[ax_role[AXR_LOOK_Y]].stick_ok );
	return found;
}

// A stick role whose axis EXISTS but was written off as pegged is the signature
// of a probe that beat the device's first report (see axes_setup). Worth asking
// again a second later, which is what an UNKNOWN pad -- the one case with no
// profile to believe instead -- has to rely on. A genuinely phantom axis just
// fails the same way each time and we stop asking.
static int axes_unsettled( void )
{
	int r;
	for( r = AXR_MOVE_X; r <= AXR_LOOK_Y; r++ )
	{
		int c = ax_role[r];
		if( c != AX_NONE && c <= ABS_MAX && axes[c].have && !axes[c].stick_ok )
			return 1;
	}
	return 0;
}

// The ABS code actually serving a role, or -1 if that role is unavailable --
// either unmapped, absent on this pad, or (for a stick role) an axis that
// doesn't rest at centre and so must not be read as a stick.
static int eff_axis( int role )
{
	int c = ax_role[role];
	if( c == AX_NONE || c > ABS_MAX || !axes[c].have ) return -1;
	if( role <= AXR_LOOK_Y && !axes[c].stick_ok ) return -1;   // stick roles only
	return c;
}

static void pad_reset_state( void )
{
	int i;
	memset( btn_raw, 0, sizeof( btn_raw ));
	memset( dpad_btn, 0, sizeof( dpad_btn ));
	memset( pos_down, 0, sizeof( pos_down ));
	for( i = 0; i <= ABS_MAX; i++ ) axes[i].val = axes[i].rest;
	hatx = haty = 0;
}

// ==========================================================================
//  Scan for new devices (~1/s). Grabs pads AND keyboards exclusively so the
//  system doesn't also process them (no double input). The touch panel is NOT
//  on /dev/input here, so grabbing is safe -- we only skip the three built-in
//  button nodes by name.
// ==========================================================================
static void scan_devices( void )
{
	char path[32], name[80];
	int i, s, one = 1, seen = 0;
	int log = ( scan_verbose > 0 );

	// Re-scan event0..15, opening only nodes we DON'T already hold. Skipping
	// held nodes matters: re-opening + re-grabbing an active Bluetooth HID node
	// every second churns the BT stack and stalls the frame. We remember each
	// held node's index, so a held device is never touched by the scan again.
	for( i = 0; i < MAX_NODES; i++ )
	{
		int fd, kind, slot = -1, held = 0;
		snprintf( path, sizeof( path ), "/dev/input/event%d", i );

		for( s = 0; s < MAX_DEVS; s++ )
			if( devs[s].fd >= 0 && devs[s].idx == i ) { held = 1; break; }
		if( held ) { seen++; continue; }

		// access() only stats the node; it does not enter the driver, so it is
		// cheap where open() is not. A node that has gone away forfeits its
		// "boring" mark, so a replacement device at the same index is probed.
		if( access( path, F_OK ) != 0 ) { node_boring[i] = 0; continue; }
		if( node_boring[i] ) { seen++; continue; }

		fd = open( path, O_RDONLY | O_NONBLOCK );
		if( fd < 0 )
		{
			/* ENOENT = node absent (pad asleep); EACCES = a root-only built-in
			 * node (gpio-keys etc.) we intentionally can't read. Both expected. */
			if( log && errno != ENOENT && errno != EACCES )
				Con_Printf( "evdev: open %s: %s\n", path, strerror( errno ));
			continue;
		}
		seen++;
		name[0] = 0;
		ioctl( fd, EVIOCGNAME( sizeof( name )), name );

		// Never grab the TouchPad's built-in buttons / power / headset keys.
		if( !strcmp( name, "gpio-keys" ) || !strcmp( name, "pmic8058_pwrkey" ) ||
		    !strcmp( name, "headset" )) { node_boring[i] = 1; close( fd ); continue; }

		if( has_key_cap( fd, BTN_GAMEPAD ) || has_key_cap( fd, BTN_JOYSTICK ))
			kind = DEV_PAD;
		else if( looks_like_keyboard( fd ))
			kind = DEV_KBD;
		else { node_boring[i] = 1; close( fd ); continue; }

		// Grab exclusively so the system doesn't also process the device. Best-
		// effort: if the grab fails, keep the fd and read shared -- webOS
		// ignores a pad's BTN_*/ABS_* codes, so no double-input there at least.
		if( ioctl( fd, EVIOCGRAB, &one ) != 0 )
			Con_Printf( "evdev: grab %s failed: %s (reading shared)\n",
			            path, strerror( errno ));

		// Find a free slot.
		for( s = 0; s < MAX_DEVS; s++ )
			if( devs[s].fd < 0 ) { slot = s; break; }
		if( slot < 0 ) { int zero = 0; ioctl( fd, EVIOCGRAB, &zero ); close( fd ); continue; }

		if( kind == DEV_PAD )
		{
			int b;
			profile_select( name );
			// A pad hotplugged into a running game can present its node before
			// its axis info exists -- or before it has reported a value for the
			// axes it does advertise; retry for a few seconds rather than hold it
			// forever with no movement.
			axes_retry = ( axes_setup( fd ) && !axes_unsettled( )) ? 0 : 5;
			Q_strncpy( pad_name, name, sizeof( pad_name ));
			for( b = 0; b < BTN_NRAW; b++ )      // for padstatus
				btn_have[b] = has_key_cap( fd, BTN_BASECODE + b ) ? 1 : 0;
			pad_reset_state();
			Con_Printf( "evdev: gamepad '%s' on %s -- profile %s (rstick=%d)\n",
			            name, path, prof->label, have_right_stick );
			// Which axes actually made it through, so a pad that misbehaves can
			// be diagnosed from the log alone (see the stick_ok note above).
			Con_Printf( "evdev:  move=%d/%d look=%d/%d trig=%d/%d dpad=%d/%d\n",
			            eff_axis( AXR_MOVE_X ), eff_axis( AXR_MOVE_Y ),
			            eff_axis( AXR_LOOK_X ), eff_axis( AXR_LOOK_Y ),
			            eff_axis( AXR_TRIG_L ), eff_axis( AXR_TRIG_R ),
			            dpad_ax, dpad_ay );
			// Name the axis we threw away and why. A stick role dropped for
			// resting off-centre is the one failure here that is otherwise
			// completely mute -- the pad works, minus all movement.
			for( b = AXR_MOVE_X; b <= AXR_LOOK_Y; b++ )
			{
				int c = ax_role[b];
				if( c == AX_NONE || c > ABS_MAX || !axes[c].have ) continue;
				if( axes[c].stick_ok ) continue;
				Con_Printf( "evdev:  %s: axis %d rests at %d, not centre (%d) --"
				            " ignored; override with \"padaxis %s %d\"\n",
				            axr_name[b], c, axes[c].rest, axes[c].center,
				            axr_name[b], c );
			}
			Con_Printf( "evdev:  \"padstatus\" in the console shows the full map\n" );
		}
		else
		{
			Con_Printf( "evdev: keyboard '%s' on %s\n", name, path );
		}
		devs[slot].fd = fd;
		devs[slot].kind = kind;
		devs[slot].idx = i;
		scan_verbose = 3;
	}

	if( log && seen == 0 )
	{
		Con_Printf( "evdev: no controller/keyboard connected yet\n" );
		scan_verbose--;
	}
}

// ==========================================================================
//  Keyboard: Linux KEY_* -> engine key
// ==========================================================================
static int kbd_translate( int code )
{
	// returns engine key (ascii or K_*), 0 if unmapped
	switch( code )
	{
		case KEY_ESC:        return K_ESCAPE;
		case KEY_ENTER:      return K_ENTER;
		case KEY_KPENTER:    return K_KP_ENTER;
		case KEY_TAB:        return K_TAB;
		case KEY_BACKSPACE:  return K_BACKSPACE;
		case KEY_DELETE:     return K_DEL;
		case KEY_INSERT:     return K_INS;
		case KEY_HOME:       return K_HOME;
		case KEY_END:        return K_END;
		case KEY_PAGEUP:     return K_PGUP;
		case KEY_PAGEDOWN:   return K_PGDN;
		case KEY_UP:         return K_UPARROW;
		case KEY_DOWN:       return K_DOWNARROW;
		case KEY_LEFT:       return K_LEFTARROW;
		case KEY_RIGHT:      return K_RIGHTARROW;
		case KEY_LEFTALT:
		case KEY_RIGHTALT:   return K_ALT;
		case KEY_LEFTCTRL:
		case KEY_RIGHTCTRL:  return K_CTRL;
		case KEY_LEFTSHIFT:
		case KEY_RIGHTSHIFT: return K_SHIFT;
		case KEY_CAPSLOCK:   return K_CAPSLOCK;
		case KEY_SPACE:      return K_SPACE;
		case KEY_F1:  return K_F1;   case KEY_F2:  return K_F2;
		case KEY_F3:  return K_F3;   case KEY_F4:  return K_F4;
		case KEY_F5:  return K_F5;   case KEY_F6:  return K_F6;
		case KEY_F7:  return K_F7;   case KEY_F8:  return K_F8;
		case KEY_F9:  return K_F9;   case KEY_F10: return K_F10;
		case KEY_F11: return K_F11;  case KEY_F12: return K_F12;
		case KEY_PAUSE: return K_PAUSE;
		// keypad
		case KEY_KP0: return K_KP_INS;       case KEY_KP1: return K_KP_END;
		case KEY_KP2: return K_KP_DOWNARROW; case KEY_KP3: return K_KP_PGDN;
		case KEY_KP4: return K_KP_LEFTARROW; case KEY_KP5: return K_KP_5;
		case KEY_KP6: return K_KP_RIGHTARROW; case KEY_KP7: return K_KP_HOME;
		case KEY_KP8: return K_KP_UPARROW;   case KEY_KP9: return K_KP_PGUP;
		case KEY_KPDOT: return K_KP_DEL;     case KEY_KPSLASH: return K_KP_SLASH;
		case KEY_KPMINUS: return K_KP_MINUS; case KEY_KPPLUS: return K_KP_PLUS;
		case KEY_KPASTERISK: return '*';
		// letters (engine wants lowercase ascii)
		case KEY_A: return 'a'; case KEY_B: return 'b'; case KEY_C: return 'c';
		case KEY_D: return 'd'; case KEY_E: return 'e'; case KEY_F: return 'f';
		case KEY_G: return 'g'; case KEY_H: return 'h'; case KEY_I: return 'i';
		case KEY_J: return 'j'; case KEY_K: return 'k'; case KEY_L: return 'l';
		case KEY_M: return 'm'; case KEY_N: return 'n'; case KEY_O: return 'o';
		case KEY_P: return 'p'; case KEY_Q: return 'q'; case KEY_R: return 'r';
		case KEY_S: return 's'; case KEY_T: return 't'; case KEY_U: return 'u';
		case KEY_V: return 'v'; case KEY_W: return 'w'; case KEY_X: return 'x';
		case KEY_Y: return 'y'; case KEY_Z: return 'z';
		// number row
		case KEY_1: return '1'; case KEY_2: return '2'; case KEY_3: return '3';
		case KEY_4: return '4'; case KEY_5: return '5'; case KEY_6: return '6';
		case KEY_7: return '7'; case KEY_8: return '8'; case KEY_9: return '9';
		case KEY_0: return '0';
		case KEY_MINUS:      return '-';
		case KEY_EQUAL:      return '=';
		case KEY_LEFTBRACE:  return '[';
		case KEY_RIGHTBRACE: return ']';
		case KEY_SEMICOLON:  return ';';
		case KEY_APOSTROPHE: return '\'';
		case KEY_GRAVE:      return '`';   // console toggle
		case KEY_BACKSLASH:  return '\\';
		case KEY_COMMA:      return ',';
		case KEY_DOT:        return '.';
		case KEY_SLASH:      return '/';
		default: return 0;
	}
}

static int kbd_shift;

static int kbd_poll( int fd )    // returns 0 ok, -1 node died
{
	struct input_event iev[64];
	int n, k;
	for( ;; )
	{
		n = read( fd, iev, sizeof( iev ));
		if( n < 0 && ( errno == EAGAIN || errno == EINTR )) break;   // drained
		// EOF / ENODEV: the node died (BT keyboard sleep). MUST free the slot:
		// the keyboard usually returns on the SAME eventN index, and a held
		// dead fd makes the scan skip that index forever -- the keyboard then
		// silently falls through to the Luna/SDL path (hardware-observed:
		// arrows came back as Palm syms 18-21 after a sleep cycle).
		if( n <= 0 ) return -1;
		for( k = 0; k < n / (int)sizeof( iev[0] ); k++ )
		{
			struct input_event *e = &iev[k];
			int qkey, down;
			if( e->type != EV_KEY ) continue;
			qkey = kbd_translate( e->code );
			// truth-table logging for oddball keyboards (the HP BT keyboard's
			// hidd node may not emit standard KEY_* codes): dev mode only
			if( e->value == 1 )
				Con_Reportf( "evdev: kbd code %d -> key %d\n", e->code, qkey );
			if( !qkey ) continue;
			down = e->value ? true : false;   // value 2 = autorepeat, still down
			Key_Event( qkey, down );
			if( qkey == K_SHIFT ) kbd_shift = down;
			// printable chars for console / message mode / OSK-less typing
			if( down && host.textmode && qkey >= 32 && qkey < 127 )
				CL_CharEvent( kbd_shift ? Key_ToUpper( qkey ) : qkey );
		}
		if( n < (int)sizeof( iev )) break;
	}
	return 0;
}

// ==========================================================================
//  Axis reading
// ==========================================================================
// A stick/d-pad axis scaled to the engine's SDL-convention short range.
// No software dead zone here: the engine applies joy_*_deadzone itself.
static short stick_scaled( int code )
{
	const axis_t *a;
	float v;
	if( code < 0 || code > ABS_MAX ) return 0;
	a = &axes[code];
	if( !a->have || !a->stick_ok ) return 0;   // never trust a pegged axis
	v = (float)( a->val - a->center ) / (float)a->half;
	if( v >  1.0f ) v =  1.0f;
	if( v < -1.0f ) v = -1.0f;
	return (short)( v * 32767.0f );
}

// An analog trigger as 0..32767 measured from the end it rests at. Pads that
// rest at min (ShanWan's ABS_GAS/BRAKE) and pads that rest at max both work.
// The engine's Joy_ProcessTrigger presses K_JOY1/K_JOY2 past joy_*_threshold.
static short trig_scaled( int code )
{
	const axis_t *a;
	float v;
	if( code < 0 || code > ABS_MAX ) return 0;
	a = &axes[code];
	if( !a->have ) return 0;
	if( a->rest <= a->center ) v = (float)( a->val - a->min ) / (float)( a->max - a->min );
	else                       v = (float)( a->max - a->val ) / (float)( a->max - a->min );
	if( v < 0.0f ) v = 0.0f;
	if( v > 1.0f ) v = 1.0f;
	return (short)( v * 32767.0f );
}

// A centre-resting axis pressed past halfway, as -1/0/+1 (adopted d-pads).
static int dig_axis( int code )
{
	const axis_t *a;
	int quarter;
	if( code == AX_NONE || code < 0 || code > ABS_MAX ) return 0;
	a = &axes[code];
	if( !a->have || !a->stick_ok ) return 0;
	quarter = a->half / 2;
	if( quarter < 1 ) quarter = 1;
	if( a->val < a->center - quarter ) return -1;
	if( a->val > a->center + quarter ) return  1;
	return 0;
}

// ==========================================================================
//  Position decode -> engine gamepad keys and axes
// ==========================================================================
static void pos_rebuild( void )
{
	int i;
	memset( pos_down, 0, sizeof( pos_down ));
	for( i = 0; i < BTN_NRAW; i++ )
		if( btn_raw[i] && pos_of[i] != P_NONE && pos_of[i] < P_NPOS )
			pos_down[pos_of[i]] = 1;
}

// Positions -> the engine's gamepad keys (Xbox naming). The engine's default
// bind table (in_keys.c) and mainui's menu navigation both consume these, so
// every pad gets the stock HL gamepad scheme with no action code here at all.
static const struct { byte pos; byte key; } poskeys[] =
{
	{ P_FACE_DOWN,  K_A_BUTTON    }, { P_FACE_RIGHT, K_B_BUTTON  },
	{ P_FACE_LEFT,  K_X_BUTTON    }, { P_FACE_UP,    K_Y_BUTTON  },
	{ P_SHOULDER_L, K_L1_BUTTON   }, { P_SHOULDER_R, K_R1_BUTTON },
	{ P_BACK,       K_BACK_BUTTON }, { P_START,      K_START_BUTTON },
	{ P_GUIDE,      K_MODE_BUTTON }, { P_CLICK_L,    K_LSTICK    },
	{ P_CLICK_R,    K_RSTICK      }, { P_EXTRA,      K_TOUCHPAD  },
};

// Hardware axis ids in the engine's default joy_axis_binding order "sfpyrl":
// 0=side 1=fwd 2=pitch 3=yaw 4=right-trigger 5=left-trigger.
#define HW_AXIS_SIDE  0
#define HW_AXIS_FWD   1
#define HW_AXIS_PITCH 2
#define HW_AXIS_YAW   3
#define HW_AXIS_RT    4
#define HW_AXIS_LT    5

static void pad_axes_zero( void )
{
	int i;
	for( i = 0; i <= HW_AXIS_LT; i++ )
		Joy_AxisMotionEvent( i, 0 );
}

static void pad_recompute( void )
{
	int i;
	int ax = dig_axis( dpad_ax ), ay = dig_axis( dpad_ay );

	pos_rebuild();

	memset( qk_want, 0, sizeof( qk_want ));

	for( i = 0; i < (int)( sizeof( poskeys ) / sizeof( poskeys[0] )); i++ )
		if( pos_down[poskeys[i].pos] ) qk_mark( poskeys[i].key );

	// Trigger buttons stand in only when the pad has no analog axis for that
	// trigger; with an axis, Joy_ProcessTrigger presses K_JOY1/K_JOY2, which
	// carry the same default binds as K_L2/K_R2 -- one source, no release races.
	if( eff_axis( AXR_TRIG_L ) < 0 && pos_down[P_TRIGGER_L] ) qk_mark( K_L2_BUTTON );
	if( eff_axis( AXR_TRIG_R ) < 0 && pos_down[P_TRIGGER_R] ) qk_mark( K_R2_BUTTON );

	// The true d-pad: hat, d-pad buttons, or adopted centre-resting axes.
	// (The left stick is NOT folded in: the engine already turns it into menu
	// arrows past 75% deflection, and in game it is analog movement.)
	if(( hatx < 0 ) || ( ax < 0 ) || dpad_btn[2] ) qk_mark( K_DPAD_LEFT );
	if(( hatx > 0 ) || ( ax > 0 ) || dpad_btn[3] ) qk_mark( K_DPAD_RIGHT );
	if(( haty < 0 ) || ( ay < 0 ) || dpad_btn[0] ) qk_mark( K_DPAD_UP );
	if(( haty > 0 ) || ( ay > 0 ) || dpad_btn[1] ) qk_mark( K_DPAD_DOWN );

	qk_reconcile();

	Joy_AxisMotionEvent( HW_AXIS_SIDE,  stick_scaled( eff_axis( AXR_MOVE_X )));
	Joy_AxisMotionEvent( HW_AXIS_FWD,   stick_scaled( eff_axis( AXR_MOVE_Y )));
	Joy_AxisMotionEvent( HW_AXIS_PITCH, stick_scaled( eff_axis( AXR_LOOK_Y )));
	Joy_AxisMotionEvent( HW_AXIS_YAW,   stick_scaled( eff_axis( AXR_LOOK_X )));
	Joy_AxisMotionEvent( HW_AXIS_RT,    trig_scaled( eff_axis( AXR_TRIG_R ) >= 0 ? ax_role[AXR_TRIG_R] : AX_NONE ));
	Joy_AxisMotionEvent( HW_AXIS_LT,    trig_scaled( eff_axis( AXR_TRIG_L ) >= 0 ? ax_role[AXR_TRIG_L] : AX_NONE ));
}

// A missed release event leaves a button stuck down until it is pressed
// again (seen on device: DS4/ShanWan over Bluetooth, occasional). This
// 2.6.35 kernel predates SYN_DROPPED, so an evdev buffer overflow drops
// events with no notification at all. Cheap insurance: once a second, ask
// the kernel for the pad's ACTUAL key state and reconcile.
static void pad_resync_buttons( int fd )
{
	unsigned long bits[NLONGS( KEY_MAX )];
	int i;
	memset( bits, 0, sizeof( bits ));
	if( ioctl( fd, EVIOCGKEY( sizeof( bits )), bits ) < 0 ) return;
	for( i = 0; i < BTN_NRAW; i++ )
		btn_raw[i] = bit_set( bits, BTN_BASECODE + i ) ? 1 : 0;
	for( i = 0; i < 4; i++ )
		dpad_btn[i] = bit_set( bits, BTN_DPAD_UP + i ) ? 1 : 0;
}

static int pad_poll( int fd )     // returns 0 ok, -1 node died
{
	struct input_event iev[64];
	int n, k;
	for( ;; )
	{
		n = read( fd, iev, sizeof( iev ));
		if( n > 0 )
		{
			for( k = 0; k < n / (int)sizeof( iev[0] ); k++ )
			{
				struct input_event *e = &iev[k];
				if( e->type == EV_KEY )
				{
					int r = RAWOF( e->code );
					if( r >= 0 && r < BTN_NRAW ) btn_raw[r] = e->value ? 1 : 0;
					else if( e->code >= BTN_DPAD_UP && e->code <= BTN_DPAD_RIGHT )
						dpad_btn[e->code - BTN_DPAD_UP] = e->value ? 1 : 0;
					if( pad_test )
						Con_Printf( "pad: button 0x%03x %s  -> %s\n", e->code,
						            e->value ? "DOWN" : "up",
						            ( r >= 0 && r < BTN_NRAW ) ? pos_name( pos_of[r] ) : "dpad" );
				}
				else if( e->type == EV_ABS )
				{
					if( e->code == ABS_HAT0X )      hatx = e->value;
					else if( e->code == ABS_HAT0Y ) haty = e->value;
					else if( e->code >= 0 && e->code <= ABS_MAX )
						axes[e->code].val = e->value;
					if( pad_test && e->code >= 0 && e->code <= ABS_MAX &&
					    ( axes[e->code].have || e->code == ABS_HAT0X || e->code == ABS_HAT0Y ) &&
					    ( e->value < axes[e->code].rest - axes[e->code].half / 2 ||
					      e->value > axes[e->code].rest + axes[e->code].half / 2 ||
					      e->code == ABS_HAT0X || e->code == ABS_HAT0Y ))
						Con_Printf( "pad: axis 0x%02x = %d (rest %d)\n",
						            e->code, e->value, axes[e->code].rest );
				}
			}
			if( n == (int)sizeof( iev )) continue;    // buffer was full, more?
			break;
		}
		if( n < 0 && ( errno == EAGAIN || errno == EINTR )) break;   // drained
		return -1;                                   // EOF / ENODEV: node died
	}
	return 0;
}

// ==========================================================================
//  Console commands -- map an UNKNOWN pad without rebuilding anything.
//
//  The whole point: someone with a controller we have never seen can fix it
//  themselves, on the device, with no toolchain and no Linux host.
//
//      padstatus            what pad is open, and the map in force
//      padtest              toggle: echo every raw button/axis to the console
//      padbtn <code> <pos>  bind an evdev code to a position
//      padaxis <role> <ax>  bind an axis role to an ABS code (-1 to disable)
//
//  Put the padbtn/padaxis lines in valve/autoexec.cfg and they apply at every
//  launch, and survive unplugging and replugging the pad.
// ==========================================================================
static void Pad_Status_f( void )
{
	int i, n;

	if( !pad_name[0] ) { Con_Printf( "padstatus: no controller open\n" ); return; }
	Con_Printf( "pad  : \"%s\"\n", pad_name );
	Con_Printf( "prof : %s%s\n", prof->label,
	            ( prof == &prof_default ) ? "  (no profile for this pad -- guessed)" : "" );
	Con_Printf( "axes : move=%d/%d look=%d/%d trig=%d/%d dpad=%d/%d\n",
	            eff_axis( AXR_MOVE_X ), eff_axis( AXR_MOVE_Y ),
	            eff_axis( AXR_LOOK_X ), eff_axis( AXR_LOOK_Y ),
	            eff_axis( AXR_TRIG_L ), eff_axis( AXR_TRIG_R ), dpad_ax, dpad_ay );

	Con_Printf( "buttons this pad reports:\n" );
	for( i = n = 0; i < BTN_NRAW; i++ )
	{
		if( !btn_have[i] ) continue;
		Con_Printf( "  0x%03x -> %s%s\n", BTN_BASECODE + i, pos_name( pos_of[i] ),
		            pos_ovr[i] != P_NONE ? "  (override)" : "" );
		n++;
	}
	if( !n ) Con_Printf( "  (none in the ranges we decode)\n" );
	Con_Printf( "unmapped buttons do nothing -- use padtest, then padbtn\n" );
}

static void Pad_Test_f( void )
{
	pad_test = !pad_test;
	Con_Printf( "padtest %s\n", pad_test ?
	            "ON -- press each control; its code is printed here" : "off" );
}

static void Pad_Btn_f( void )
{
	int code, i, r;
	const char *want;

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( "usage: padbtn <code> <position>\n" );
		Con_Printf( "positions:" );
		for( i = 0; posnames[i].name; i++ ) Con_Printf( " %s", posnames[i].name );
		Con_Printf( "\nexample: padbtn 0x131 face_right\n" );
		return;
	}
	code = (int)strtol( Cmd_Argv( 1 ), NULL, 0 );      // accepts 0x131 or 305
	r = RAWOF( code );
	if( r < 0 || r >= BTN_NRAW )
	{
		Con_Printf( "padbtn: code 0x%03x is outside 0x%03x-0x%03x\n",
		            code, BTN_BASECODE, BTN_BASECODE + BTN_NRAW - 1 );
		return;
	}
	want = Cmd_Argv( 2 );
	for( i = 0; posnames[i].name; i++ )
		if( !Q_stricmp( posnames[i].name, want ))
		{
			pos_ovr[r] = posnames[i].pos;
			pos_of[r]  = (byte)posnames[i].pos;
			overrides_loaded = 1;
			Con_Printf( "padbtn: 0x%03x -> %s\n", code, posnames[i].name );
			return;
		}
	Con_Printf( "padbtn: unknown position \"%s\" (run padbtn for the list)\n", want );
}

static void Pad_Axis_f( void )
{
	int i, code;

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( "usage: padaxis <role> <abs-code>\n" );
		Con_Printf( "roles:" );
		for( i = 0; i < AXR_N; i++ ) Con_Printf( " %s", axr_name[i] );
		Con_Printf( "\nABS codes: x=0 y=1 z=2 rx=3 ry=4 rz=5 gas=9 brake=10\n" );
		Con_Printf( "example: padaxis look_x 3   (-1 disables the role)\n" );
		return;
	}
	code = (int)strtol( Cmd_Argv( 2 ), NULL, 0 );
	if( code < -1 || code > ABS_MAX )
	{
		Con_Printf( "padaxis: axis %d out of range (-1..%d)\n", code, ABS_MAX );
		return;
	}
	for( i = 0; i < AXR_N; i++ )
		if( !Q_stricmp( axr_name[i], Cmd_Argv( 1 )))
		{
			ax_ovr[i]  = code;
			ax_role[i] = code;
			if( i == AXR_DPAD_X ) dpad_ax = code;
			if( i == AXR_DPAD_Y ) dpad_ay = code;
			// Apply to the pad that is already open, so the effect is visible
			// now rather than only after the next replug (axes_setup does the
			// same for a pad opened later, e.g. from autoexec.cfg).
			if( i <= AXR_LOOK_Y && code >= 0 && code <= ABS_MAX && axes[code].have )
			{
				axes[code].stick_ok = 1;
				axes[code].rest = axes[code].val = axes[code].center;
			}
			overrides_loaded = 1;
			Con_Printf( "padaxis: %s -> %d\n", axr_name[i], code );
			return;
		}
	Con_Printf( "padaxis: unknown role \"%s\" (run padaxis for the list)\n", Cmd_Argv( 1 ));
}

// ==========================================================================
//  Public API
// ==========================================================================
static int evdev_initialized;

static void Evdev_WebOS_Init( void )
{
	int i;
	for( i = 0; i < MAX_DEVS; i++ ) { devs[i].fd = -1; devs[i].kind = DEV_NONE; devs[i].idx = -1; }
	memset( qk_have, 0, sizeof( qk_have ));
	memset( node_boring, 0, sizeof( node_boring ));
	memset( axes, 0, sizeof( axes ));
	pad_name[0] = 0;
	memset( btn_have, 0, sizeof( btn_have ));
	if( !overrides_loaded )           // keep any set from autoexec.cfg
	{
		for( i = 0; i < BTN_NRAW; i++ ) pos_ovr[i] = P_NONE;
		for( i = 0; i < AXR_N; i++ )    ax_ovr[i]  = AX_NONE;
	}
	profile_select( "" );
	dpad_ax = dpad_ay = AX_NONE;
	last_scan = 0;
	scan_verbose = 3;

	Cmd_AddRestrictedCommand( "padstatus", Pad_Status_f, "show open controller and its mapping" );
	Cmd_AddRestrictedCommand( "padtest",   Pad_Test_f,   "toggle raw controller event echo" );
	Cmd_AddRestrictedCommand( "padbtn",    Pad_Btn_f,    "bind an evdev button code to a pad position" );
	Cmd_AddRestrictedCommand( "padaxis",   Pad_Axis_f,   "bind a pad axis role to an ABS code" );

	evdev_initialized = 1;
}

void Evdev_WebOS_Shutdown( void )
{
	int i, zero = 0;
	if( !evdev_initialized ) return;
	for( i = 0; i < MAX_DEVS; i++ )
	{
		if( devs[i].fd >= 0 )
		{
			ioctl( devs[i].fd, EVIOCGRAB, &zero );
			close( devs[i].fd );
			devs[i].fd = -1; devs[i].kind = DEV_NONE; devs[i].idx = -1;
		}
	}
	memset( qk_want, 0, sizeof( qk_want ));
	qk_reconcile();
}

void Evdev_WebOS_Poll( void )
{
	double now = host.realtime;
	double poll_t0 = Sys_DoubleTime();
	int i, have_pad = 0;

	if( !evdev_initialized )
		Evdev_WebOS_Init();

	if( now - last_scan >= 1.0 )     // (re)scan for hotplugged devices ~1/s
	{
		double t0 = Sys_DoubleTime(), dt;
		last_scan = now;
		scan_devices();
		dt = Sys_DoubleTime() - t0;
		// The hotplug scan opens up to 16 device nodes. If that ever costs real
		// time it shows up as a periodic hitch once a second, which is exactly
		// the kind of stall that gets blamed on the renderer.
		if( dt >= 0.020 )
			Con_Printf( "evdev: SCAN TOOK %ums\n", (unsigned)( dt * 1000.0 ));
		for( i = 0; i < MAX_DEVS; i++ )
			if( devs[i].fd >= 0 && devs[i].kind == DEV_PAD )
				pad_resync_buttons( devs[i].fd );
		if( axes_retry > 0 )
		{
			for( i = 0; i < MAX_DEVS; i++ )
			{
				if( devs[i].fd < 0 || devs[i].kind != DEV_PAD ) continue;
				if( axes_setup( devs[i].fd ) > 0 && !axes_unsettled( ))
				{
					axes_retry = 0;
					Con_Printf( "evdev:  axes ready: move=%d/%d look=%d/%d "
					            "trig=%d/%d dpad=%d/%d\n",
					            eff_axis( AXR_MOVE_X ), eff_axis( AXR_MOVE_Y ),
					            eff_axis( AXR_LOOK_X ), eff_axis( AXR_LOOK_Y ),
					            eff_axis( AXR_TRIG_L ), eff_axis( AXR_TRIG_R ),
					            dpad_ax, dpad_ay );
				}
				else if( --axes_retry == 0 )
				{
					Con_Printf( "evdev:  giving up on the axis probe -- "
					            "\"padstatus\" shows what was found\n" );
				}
				break;
			}
		}
	}

	for( i = 0; i < MAX_DEVS; i++ )
	{
		if( devs[i].fd < 0 ) continue;
		if( devs[i].kind == DEV_KBD )
		{
			if( kbd_poll( devs[i].fd ) < 0 )
			{
				int zero = 0;
				ioctl( devs[i].fd, EVIOCGRAB, &zero );
				close( devs[i].fd );
				devs[i].fd = -1; devs[i].kind = DEV_NONE; devs[i].idx = -1;
				Con_Printf( "evdev: keyboard node closed -- will re-open\n" );
				if( kbd_shift ) { kbd_shift = 0; Key_Event( K_SHIFT, false ); }
			}
		}
		else if( devs[i].kind == DEV_PAD )
		{
			if( pad_poll( devs[i].fd ) < 0 )
			{
				int zero = 0;
				ioctl( devs[i].fd, EVIOCGRAB, &zero );
				close( devs[i].fd );
				devs[i].fd = -1; devs[i].kind = DEV_NONE; devs[i].idx = -1;
				Con_Printf( "evdev: gamepad node closed -- will re-open\n" );
				pad_reset_state();
				continue;
			}
			have_pad = 1;
		}
	}

	if( have_pad )
	{
		// Per-pad bind profile: exec valve/pad_<profile>.cfg once per profile
		// change, so swapping between e.g. a DS4 and a stickless Logitech
		// re-binds the gamepad keys to a scheme that suits the hardware.
		// Deferred until the archived config has executed -- a pad connected
		// at launch must not have its profile stomped by config.cfg running
		// after it. Binds are archived, so the cfg files are the
		// customization point; menu rebinds of pad keys are overwritten on
		// the next profile switch.
		static const padprofile_t *cfg_prof;
		if( prof != cfg_prof && host.config_executed )
		{
			if( FS_FileExists( prof->cfg, true ))
			{
				Con_Printf( "evdev: execing %s\n", prof->cfg );
				Cbuf_AddTextf( "exec \"%s\"\n", prof->cfg );
			}
			else
				Con_Printf( "evdev: no %s (gamedironly=%d anypath=%d) -- keeping current binds\n",
				            prof->cfg, FS_FileExists( prof->cfg, true ),
				            FS_FileExists( prof->cfg, false ));
			cfg_prof = prof;
		}
		pad_recompute();
	}
	else if( prev_have_pad )
	{
		// pad went away: release everything it held, recentre the engine axes
		memset( qk_want, 0, sizeof( qk_want ));
		qk_reconcile();
		pad_axes_zero();
	}
	prev_have_pad = have_pad;

	{
		double dt = Sys_DoubleTime() - poll_t0;
		if( dt >= 0.020 )
			Con_Printf( "evdev: POLL TOOK %ums\n", (unsigned)( dt * 1000.0 ));
	}
}

#endif // __webos__
