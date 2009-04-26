///                                                                       
///     @name wmc2d.c   -       Dockapp for core 2 duo temperature
///                                                                         
///     Copyright (c) 2005,2009 by Lutz Sammer.  All Rights Reserved.       
///                                                                         
///     Contributor(s):                                                     
///     	Bitmap and design based on wmbp6.
///                                                                         
///     This file is part of wmc2d                                          
///                                                                         
///	License: AGPLv3
///
///	This program is free software: you can redistribute it and/or modify
///	it under the terms of the GNU Affero General Public License as
///	published by the Free Software Foundation, either version 3 of the
///	License.
///
///	This program is distributed in the hope that it will be useful,
///	but WITHOUT ANY WARRANTY; without even the implied warranty of
///	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///	GNU Affero General Public License for more details.
///
///	$Id: $
////////////////////////////////////////////////////////////////////////////

// also includes Xlib, Xresources, XPM, stdlib and stdio
#include <dockapp.h>

// system includes
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

// include the pixmap to use
#include "wmc2d.xpm"

// ------------------------------------------------------------------------- //

/**
**      Exec0   argument.
*/
char *OptionExec0;

/**
**	Options to be parsed.
*/
DAProgramOption Options[] = {
#define OptionB		0
    {
	    "-b", "--backlight",
	    "turn on back-light, not written.",
	    DONone,
	    0,
	    {NULL}
	},
#define OptionE0	1
    {
	    "-e", "--exec0",
	    "<command> execute command, not used.",
	    DOString,
	    0,
	    {&OptionExec0}
	},
};

// ------------------------------------------------------------------------- //

/**
**	Pixmap contains all stuff.
*/
DAShapedPixmap *Image;

/**
**	Backgound of dockapp.
*/
DAShapedPixmap *Background;

/**
**	Backlight enabled.
*/
int Backlight;

time_t TimeNow;				/// Timestamp of current frame

// ------------------------------------------------------------------------- //

/**
**	Draw a string at given cordinates.
**
**	@param	s	String
**	@param	x	x pixel position
**	@param	y	y pixel position
*/
void DrawString(const char *s, int x, int y)
{
    int dx;
    int c;

    dx = x;
    while (*s) {
	c = toupper(*s);
	if (c == ' ') {
	    DASPCopyArea(Image, Background, 0, 65, 6, 7, dx, y);
	} else if ('A' <= c && c <= 'Z') {	// is a letter
	    c -= 'A';
	    DASPCopyArea(Image, Background, 1 + c * 6, 75, 6, 7, dx, y);
	} else {			// is a number or symbol
	    c -= '\'';
	    DASPCopyArea(Image, Background, 1 + c * 6, 65, 6, 7, dx, y);
	}
	dx += 6;
	++s;
    }
}

/**
**	Draw a number at given cordinates.
**
**	@param	num	unsigned number
**	@param	x	x pixel position
**	@param	y	y pixel position
*/
void DrawNumber(unsigned num, int x, int y)
{
    char buf[32];

    sprintf(buf, "%02d", num);
    DrawString(buf, x, y);
}

/**
**	Draw a number at given cordinates with small font.
**
**	@param	n	unsigned number
**	@param	x	x pixel position
**	@param	y	y pixel position
*/
void DrawSmallNumber(unsigned num, int x, int y)
{
    int n1000;
    int n100;
    int n10;
    int n1;

    n1 = num % 10;
    n10 = (num / 10) % 10;
    n100 = (num / 100) % 10;
    n1000 = (num / 1000) % 10;

    if (n1000) {
	DASPCopyArea(Image, Background, n1000 * 4, 85, 4, 6, x, y);
	x += 4;
    }
    if (n1000 || n100) {
	DASPCopyArea(Image, Background, n100 * 4, 85, 4, 6, x, y);
	x += 4;
    }
    if (n1000 || n100 || n10) {
	DASPCopyArea(Image, Background, n10 * 4, 85, 4, 6, x, y);
	x += 4;
    }
    DASPCopyArea(Image, Background, n1 * 4, 85, 4, 6, x, y);
}

/**
**	Draw a number at given cordinates with LCD font.
**
**	@param	n	unsigned number
**	@param	x	x pixel position
**	@param	y	y pixel position
*/
void DrawLcdNumber(unsigned num, int x, int y)
{
    int n100;
    int n10;
    int n1;

    n1 = num % 10;
    n10 = (num / 10) % 10;
    n100 = (num / 100) % 10;

    if (n100) {
	DASPCopyArea(Image, Background, n100 * 5, 92, 5, 7, x, y);
    } else {
	DASPCopyArea(Image, Background, x, y, 5, 7, x, y);
    }
    x += 6;
    if (n100 || n10) {
	DASPCopyArea(Image, Background, n10 * 5, 92, 5, 7, x, y);
    } else {
	DASPCopyArea(Image, Background, x, y, 5, 7, x, y);
    }
    x += 7;
    DASPCopyArea(Image, Background, n1 * 5, 92, 5, 7, x, y);
}

/**
**	Draw time
**
**	@param	x	x pixel position
**	@param	y	y pixel position
**	@param	h	hours of time
**	@param	m	minutes of time
**	@param	s	seconds of time
*/
void DrawTime(int x, int y, int h, int m, int s)
{
    DrawNumber(h, x, y);
    DrawString(":", x + 12, y);
    DrawNumber(m, x + 17, y);
    DrawString(":", x + 29, y);
    DrawNumber(s, x + 34, y);
}

// ------------------------------------------------------------------------- //

#if 0
/**
**	Execute command without waiting.
*/
static int System(char *cmd)
{
    int pid;
    extern char **environ;

    if (!cmd) {				// No command
	return 1;
    }
    if ((pid = fork()) == -1) {		// fork failed
	return -1;
    }
    if (!pid) {				// child
	if (!(pid = fork())) {		// child of child
	    char *argv[4];

	    argv[0] = "sh";
	    argv[1] = "-c";
	    argv[2] = cmd;
	    argv[3] = 0;
	    execve("/bin/sh", argv, environ);
	    exit(0);
	}
	exit(0);
    }
    return 0;
}
#endif

// ------------------------------------------------------------------------- //

/**
**	Read number
**
**	@param file	Name of file containing only the number.
*/
int ReadNumber(const char *file)
{
    int fd;
    int n;
    char buf[32];

    n = -1;
    if ((fd = open(file, O_RDONLY)) >= 0) {
	n = read(fd, buf, sizeof(buf));
	if (n > 0) {
	    buf[n] = '\0';
	    n = atol(buf);
	}
	close(fd);
    }
    return n;
}

/**
**	Draw temperatures. cpu0, cpu1, chipset
*/
void DrawTemperaturs(void)
{
    int n;

    n = ReadNumber("/sys/devices/platform/coretemp.0/temp1_input");
    DrawLcdNumber(n / 100, 30, 4);
    n = ReadNumber("/sys/devices/platform/coretemp.1/temp1_input");
    DrawLcdNumber(n / 100, 30, 18);
    n = ReadNumber("/sys/class/thermal/thermal_zone0/temp");
    DrawLcdNumber(n / 100, 30, 32);
}

/**
**	Draw frequency
*/
void DrawFrequency(void)
{
    int n;
    char buf[32];

    n = ReadNumber("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    sprintf(buf, "%4d", n / 1000);
    DrawString(buf, 3, 48);

    n = ReadNumber("/sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq");
    sprintf(buf, "%4d", n / 1000);
    DrawString(buf, 31, 48);
    // Draw Mhz
    //DASPCopyArea(Image, Background, 89, Backlight, 15, 7, 30, 47);
}

/**
**	Do something.
*/
void DoTimeout(void)
{
    //
    // Update  everything
    //
    DrawTemperaturs();
    DrawFrequency();

    DASetPixmap(Background->pixmap);
}

// ------------------------------------------------------------------------- //

#if 0

/**
**	Button pressed.
*/
void DoButtonPress(int button, int state, int x, int y)
{
    printf("#%d, %d, (%d, %d)\n", button, state, x, y);
}

/**
**	Mouse moved.
*/
void DoMouseMove(int x, int y)
{
    printf("(%d, %d)\n", x, y);
}

/**
**	Mouse leaved dockup.
*/
void DoMouseLeave(void)
{
    printf("Leaved window\n");
}

#endif

/**
**	Cleanup used stuff.
*/
void Cleanup(void)
{
    DAFreeShapedPixmap(Image);
    DAFreeShapedPixmap(Background);

    fprintf(stderr, "Destroyed!\n");	// exit is done by libdockapp
}

/**
**	main entry point
**
**	@param	argc	Argument count
**	@param	argv	Argument vector
*/
int main(int argc, char **argv)
{
    static DACallbacks eventCallbacks = {
	Cleanup,			// destroy
	NULL,				// DoButtonPress,       // buttonPress
	NULL,				// buttonRelease
	NULL,				// DoMouseMove, // motion (mouse)
	NULL,				// mouse enters window
	NULL,				// DoMouseLeave,        // mouse leaves window
	DoTimeout			// timeout
    };

    //  provide standard command-line options
    DAParseArguments(argc, argv, Options, sizeof(Options) / sizeof(*Options),
	"wmc2d - core2duo temperature dockapp\n", "1.00 (" GIT_REV ")" );

    //  Tell libdockapp what version we expect it to be (a date from the
    //  ChangeLog should do).
    DASetExpectedVersion(20050522);

    //  Connect to default display, create icon
    DAInitialize(NULL, "wmc2d", 58, 58, argc, argv);

    //  Handle options
    if (Options[OptionB].used) {
	Backlight = 100;
    }

    if (Options[OptionE0].used) {
	printf("%s\n", OptionExec0);
    }

    Image = DAMakeShapedPixmapFromData(wmc2d_xpm);

    //  The pixmap that makes up the background of the dockapp
    Background = DAMakeShapedPixmap();
    DASPCopyArea(Image, Background, 0, Backlight, 58, 58, 0, 0);
    DASPSetPixmap(Background);

    //  Initial setup

    //  Draw something
    DoTimeout();

    // Respond to destroy and timeout events (the ones not NULL in the
    // eventCallbacks variable.
    DASetCallbacks(&eventCallbacks);

    // Set the time for timeout events (in msec)
    DASetTimeout(1000);

    DAShow();				// Show the dockapp window.

    // Process events and keep the dockapp running
    DAEventLoop();

    // not reached
    exit(EXIT_SUCCESS);
}
