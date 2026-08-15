/* fakepad.c -- create a fake gamepad via /dev/uinput so the engine's evdev
 * hotplug/profile/exec path can be exercised with no physical controller.
 * Run as root OUTSIDE the jail (novacom); the jailed game sees the new
 * /dev/input/eventN through the bind mount, and the udev rule 0666s it.
 *
 *   fakepad [seconds] [name]     default: 30 "ShanWan Wireless Gamepad"
 *
 * Advertises the ShanWan shape: gamepad-block buttons 0x130..0x13e, sticks on
 * ABS_X/Y (move) + ABS_Z/RZ (look), triggers on ABS_GAS/ABS_BRAKE, hat.
 * Presses A (0x130) once a second so button flow is visible in padtest.
 * Uses the legacy uinput_user_dev API -- all this 2.6.35 kernel has.
 */
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

static void emit( int fd, int type, int code, int value )
{
    struct input_event e;
    memset( &e, 0, sizeof( e ));
    e.type = type; e.code = code; e.value = value;
    write( fd, &e, sizeof( e ));
}

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 30;
    const char *name = argc > 2 ? argv[2] : "ShanWan Wireless Gamepad";
    struct uinput_user_dev ud;
    int fd, i, t;
    static const int absaxes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE,
                                   ABS_HAT0X, ABS_HAT0Y };

    fd = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );
    if( fd < 0 )
        fd = open( "/dev/input/uinput", O_WRONLY | O_NONBLOCK );  /* TouchPad location */
    if( fd < 0 ) { perror( "open uinput" ); return 1; }

    ioctl( fd, UI_SET_EVBIT, EV_KEY );
    for( i = 0x130; i <= 0x13e; i++ )
        ioctl( fd, UI_SET_KEYBIT, i );
    ioctl( fd, UI_SET_EVBIT, EV_ABS );
    for( i = 0; i < (int)( sizeof( absaxes ) / sizeof( absaxes[0] )); i++ )
        ioctl( fd, UI_SET_ABSBIT, absaxes[i] );

    memset( &ud, 0, sizeof( ud ));
    snprintf( ud.name, sizeof( ud.name ), "%s", name );
    ud.id.bustype = BUS_USB;
    ud.id.vendor = 0x0079; ud.id.product = 0x181c; ud.id.version = 1;
    for( i = 0; i < (int)( sizeof( absaxes ) / sizeof( absaxes[0] )); i++ )
    {
        int a = absaxes[i];
        if( a == ABS_HAT0X || a == ABS_HAT0Y ) { ud.absmin[a] = -1; ud.absmax[a] = 1; }
        else { ud.absmin[a] = 0; ud.absmax[a] = 255; }
    }
    if( write( fd, &ud, sizeof( ud )) != sizeof( ud )) { perror( "write dev" ); return 1; }
    if( ioctl( fd, UI_DEV_CREATE ) < 0 ) { perror( "UI_DEV_CREATE" ); return 1; }
    printf( "fakepad: '%s' up for %d s\n", name, secs );
    fflush( stdout );

    /* rest sticks at centre, triggers at 0, so the axis probe sees sane values */
    for( i = 0; i < 4; i++ ) emit( fd, EV_ABS, absaxes[i], 128 );
    emit( fd, EV_SYN, SYN_REPORT, 0 );

    for( t = 0; t < secs; t++ )
    {
        sleep( 1 );
        emit( fd, EV_KEY, 0x130, 1 );  /* A down */
        emit( fd, EV_SYN, SYN_REPORT, 0 );
        usleep( 100000 );
        emit( fd, EV_KEY, 0x130, 0 );  /* A up */
        emit( fd, EV_SYN, SYN_REPORT, 0 );
    }

    ioctl( fd, UI_DEV_DESTROY );
    close( fd );
    printf( "fakepad: down\n" );
    return 0;
}
