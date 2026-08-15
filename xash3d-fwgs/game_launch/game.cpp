/*
game.cpp -- executable to run Xash Engine
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "port.h"
#include "build.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#if XASH_POSIX
#include <dlfcn.h>
#ifdef __webos__
#include <unistd.h>
#include <sys/stat.h>
// dlopen() does not search the CWD for a bare soname; after we chdir to the
// app dir, an explicit ./ path finds our bundled engine.
#define XASHLIB "./" OS_LIB_PREFIX "xash." OS_LIB_EXT
#else
#define XASHLIB OS_LIB_PREFIX "xash." OS_LIB_EXT
#endif
#define FreeLibrary( x ) dlclose( x )
#elif XASH_WIN32
#include <shellapi.h> // CommandLineToArgvW
#define XASHLIB L"xash.dll"
#define SDL2LIB L"SDL2.dll"

extern "C"
{
// Enable NVIDIA High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// Enable AMD High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#else
#error // port me!
#endif

#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR "valve" // !!! Replace with your default (base) game directory !!!
#endif

typedef void (*pfnChangeGame)( const char *progname );
typedef int  (*pfnInit)( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
typedef void (*pfnShutdown)( void );

static pfnInit     Host_Main;
static pfnShutdown Host_Shutdown = NULL;
static int         szArgc;
static char        **szArgv;
static HINSTANCE   hEngine;

static void Launch_Error( const char *szFmt, ... )
{
	static char	buffer[16384];	// must support > 1k messages
	va_list		args;

	va_start( args, szFmt );
	vsnprintf( buffer, sizeof(buffer), szFmt, args );
	va_end( args );

#if XASH_WIN32
	MessageBoxA( NULL, buffer, "Xash Error", MB_OK );
#else
	fprintf( stderr, "Xash Error: %s\n", buffer );
#endif

	exit( 1 );
}

#if XASH_WIN32
static const char *GetStringLastError()
{
	static char buf[1024];

	FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT ),
		buf, sizeof( buf ), NULL );

	return buf;
}
#endif

static void Sys_LoadEngine( void )
{
#if XASH_WIN32
	HMODULE hSDL = LoadLibraryExW( SDL2LIB, NULL, LOAD_LIBRARY_AS_DATAFILE );

	if( !hSDL )
	{
		Launch_Error("Unable to load %ls: %s", SDL2LIB, GetStringLastError( ));
		return;
	}

	FreeLibrary( hSDL );

	hEngine = LoadLibraryW( XASHLIB );
	if( !hEngine )
	{
		Launch_Error( "Unable to load %ls: %s", XASHLIB, GetStringLastError( ));
		return;
	}

	Host_Main = (pfnInit)GetProcAddress( hEngine, "Host_Main" );

	if( !Host_Main )
	{
		Launch_Error( "%ls missed 'Host_Main' export: %s", XASHLIB, GetStringLastError( ));
		return;
	}

	Host_Shutdown = (pfnShutdown)GetProcAddress( hEngine, "Host_Shutdown" );
#elif XASH_POSIX
	hEngine = dlopen( XASHLIB, RTLD_NOW );
	if( !hEngine )
	{
		Launch_Error( "Unable to load %s: %s", XASHLIB, dlerror( ));
		return;
	}

	Host_Main = (pfnInit)dlsym( hEngine, "Host_Main" );

	if( !Host_Main )
	{
		Launch_Error( "%s missed 'Host_Main' export: %s", XASHLIB, dlerror( ));
		return;
	}

	Host_Shutdown = (pfnShutdown)dlsym( hEngine, "Host_Shutdown" );
#else
#error "port me!"
#endif
}

static void Sys_UnloadEngine( void )
{
	if( Host_Shutdown )
		Host_Shutdown( );

	if( hEngine )
		FreeLibrary( hEngine );

	hEngine = NULL;
	Host_Main = NULL;
	Host_Shutdown = NULL;
}

static void Sys_ChangeGame( const char *progname )
{
	// presence of this function tells the engine to allow change game
	// but it's never called
	return;
}

static int Sys_Start( void )
{
	int ret;

#if XASH_SAILFISH
	const char *home = getenv( "HOME" );
	char buf[1024];

	snprintf( buf, sizeof( buf ), "%s/xash", home );
	setenv( "XASH3D_BASEDIR", buf, true );
	setenv( "XASH3D_RODIR", "/usr/share/harbour-xash3d-fwgs/rodir", true );
#endif // XASH_SAILFISH

	Sys_LoadEngine();
	ret = Host_Main( szArgc, szArgv, XASH_GAMEDIR, 0, XASH_DISABLE_MENU_CHANGEGAME ? NULL : Sys_ChangeGame );
	Sys_UnloadEngine();

	return ret;
}

#if !XASH_WIN32
#ifdef __webos__
// webOS launcher glue -- all three behaviors verified on hardware (sdlquake):
//  1. A launcher-launched PDK app's stdout/stderr go nowhere (not tty, not
//     /var/log/messages). Redirect to /media/internal FIRST, before anything
//     that can fail, or nothing is diagnosable.
//  2. argv[0] is unreliable and CWD is the app dir only by convention;
//     self-locate via /proc/self/exe and chdir there so ./libxash.so,
//     ./valve, ./extras.pk3 resolve.
//  3. LunaSysMgr passes the launch JSON as argv[1] (e.g. "{}"). The engine
//     would misparse it as a command line -- replace it with our real args.
static char *webos_argv[16];
static int webos_argc;

static void WebOS_Setup( int argc, char **argv )
{
	FILE *lf = fopen( "/media/internal/xash.log", "w" );
	if( lf )
	{
		dup2( fileno( lf ), 1 );
		dup2( fileno( lf ), 2 );
		fclose( lf );
		setvbuf( stdout, NULL, _IONBF, 0 );
		setvbuf( stderr, NULL, _IONBF, 0 );
	}

	char exe[512];
	int n = readlink( "/proc/self/exe", exe, sizeof( exe ) - 1 );
	if( n > 0 )
	{
		exe[n] = 0;
		char *slash = strrchr( exe, '/' );
		if( slash )
		{
			*slash = 0;
			if( chdir( exe ) != 0 )
				fprintf( stderr, "chdir(%s) failed\n", exe );
			// RODIR = the read-only app install dir (engine assets:
			// extras.pk3). BASEDIR = rw storage for game data, saves and
			// config -- the jail bind-mounts /media/internal read-write.
			setenv( "XASH3D_RODIR", exe, 1 );
		}
	}
	// the engine hard-errors if it can't chdir into the basedir; a fresh
	// install (or a user who deleted the folder) must still reach the menu
	mkdir( "/media/internal/xash", 0777 );
	setenv( "XASH3D_BASEDIR", "/media/internal/xash", 1 );

	webos_argc = 0;
	webos_argv[webos_argc++] = argv[0];
	// keep real flags if launched from a shell; drop the launcher's JSON blob
	for( int i = 1; i < argc && webos_argc < 12; i++ )
		if( argv[i][0] != '{' && argv[i][0] != '[' )
			webos_argv[webos_argc++] = argv[i];
	// the TouchPad's only usable accelerated renderer (upstream default is
	// "gl", which fails to load and silently falls back to ref_soft)
	webos_argv[webos_argc++] = (char *)"-ref";
	webos_argv[webos_argc++] = (char *)"gles1";
	// always keep the file log; developer mode (on-screen message spam) only
	// when the marker file exists -- `touch /media/internal/xash/dev` over
	// novacom turns it on, no rebuild needed
	if( access( "/media/internal/xash/dev", F_OK ) == 0 )
	{
		webos_argv[webos_argc++] = (char *)"-dev";
		webos_argv[webos_argc++] = (char *)"2";
	}
	webos_argv[webos_argc++] = (char *)"-log";
	webos_argv[webos_argc] = NULL;
}

int main( int argc, char **argv )
{
	WebOS_Setup( argc, argv );
	szArgc = webos_argc;
	szArgv = webos_argv;

	return Sys_Start();
}
#else
int main( int argc, char **argv )
{
	szArgc = argc;
	szArgv = argv;

	return Sys_Start();
}
#endif
#else
int __stdcall WinMain( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nShow )
{
	LPWSTR* lpArgv;
	int ret, i;

	lpArgv = CommandLineToArgvW( GetCommandLineW(), &szArgc );
	szArgv = ( char** )malloc( (szArgc + 1) * sizeof( char* ));

	for( i = 0; i < szArgc; ++i )
	{
		size_t size = wcslen(lpArgv[i]) + 1;

		// just in case, allocate some more memory
		szArgv[i] = ( char * )malloc( size * sizeof( wchar_t ));
		wcstombs( szArgv[i], lpArgv[i], size );
	}
	szArgv[szArgc] = 0;

	LocalFree( lpArgv );

	ret = Sys_Start();

	for( ; i < szArgc; ++i )
		free( szArgv[i] );
	free( szArgv );

	return ret;
}
#endif
