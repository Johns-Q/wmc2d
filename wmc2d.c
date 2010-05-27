///
///	@file wmc2d.c		@brief	Dockapp for core 2 duo temperature
///
///	Copyright (c) 2004, 2009, 2010 by Lutz Sammer.  All Rights Reserved.
///
///	Contributor(s):
///		Bitmap and design based on wmbp6.
///
///	This file is part of wmc2d
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
///	$Id$
////////////////////////////////////////////////////////////////////////////

/**
**	@mainpage
**
**	This is a small dockapp, which shows the core 2 duo temperature and
**	the temperature of ACPI thermal zone 0, which is normaly the
**	motherboard temperature.
**
**	@n
**	To compile you must have libxcb (xcb-dev) installed.
**
**	@n
**	The source is a single file with less than 1000 lines. The sources
**	are (hopefully) good documented.  They can be used as an example,
**	how to write your own dockapp, applet or widget.
*/

////////////////////////////////////////////////////////////////////////////

#define SCREENSAVER			///< config support screensaver

////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <poll.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/shape.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_pixel.h>
#ifdef SCREENSAVER
#include <xcb/screensaver.h>
#endif

#include "wmc2d.xpm"

////////////////////////////////////////////////////////////////////////////

xcb_connection_t *Connection;		///< connection to X11 server
xcb_screen_t *Screen;			///< our screen
xcb_window_t Window;			///< our window
xcb_gcontext_t NormalGC;		///< normal graphic context
xcb_pixmap_t Pixmap;			///< our background pixmap

xcb_pixmap_t Image;			///< drawing data

#ifdef SCREENSAVER
int ScreenSaverEventId;			///< screen saver event ids
#endif

static int Rate;			///< update rate in ms
static char WindowMode;			///< start in window mode
static char UseSleep;			///< use sleep while screensaver runs

extern void Timeout(void);		///< called from event loop

////////////////////////////////////////////////////////////////////////////
//	XPM Stuff
////////////////////////////////////////////////////////////////////////////

/**
**	Convert XPM graphic to xcb_image.
**
**	@param connection	XCB connection to X11 server
**	@param colormap		window colormap
**	@param depth		image depth
**	@param transparent	pixel for transparent color
**	@param data		XPM graphic data
**	@param[out] mask	bitmap mask for transparent
**
**	@returns image create from the XPM data.
**
**	@warning supports only a subset of XPM formats.
*/
xcb_image_t *XcbXpm2Image(xcb_connection_t * connection,
    xcb_colormap_t colormap, uint8_t depth, uint32_t transparent,
    const char *const *data, uint8_t ** mask)
{
    // convert table: ascii hex nibble to binary
    static const uint8_t hex[128] =
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
	0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    int w;
    int h;
    int colors;
    int bytes_per_color;
    char dummy;
    int i;
    xcb_alloc_color_cookie_t cookies[256];
    int color_to_pixel[256];
    uint32_t pixels[256];
    xcb_image_t *image;
    int mask_width;
    const char *line;
    int x;
    int y;

    if (sscanf(*data, "%d %d %d %d %c", &w, &h, &colors, &bytes_per_color,
	    &dummy) != 4) {
	fprintf(stderr, "unparsable XPM header\n");
	abort();
    }
    if (colors < 1 || colors > 255) {
	fprintf(stderr, "XPM: wrong number of colors %d\n", colors);
	abort();
    }
    if (bytes_per_color != 1) {
	fprintf(stderr, "%d-byte XPM files not supported\n", bytes_per_color);
	abort();
    }
    data++;

    //
    //	Read color table, send alloc color requests
    //
    for (i = 0; i < colors; i++) {
	int id;

	line = *data;
	id = *line++;
	color_to_pixel[id] = i;		// maps xpm color char to pixel
	cookies[i].sequence = 0;
	while (*line) {			// multiple choices for color
	    int r;
	    int g;
	    int b;
	    int n;
	    int type;

	    n = strspn(line, " \t");	// skip white space
	    type = line[n];
	    if (!type) {
		continue;		// whitespace upto end of line
	    }
	    if (type != 'c' && type != 'm') {
		fprintf(stderr, "unknown XPM pixel type '%c' in \"%s\"\n",
		    type, *data);
		abort();
	    }
	    line += n + 1;
	    n = strspn(line, " \t");	// skip white space
	    line += n;
	    if (!strncasecmp(line, "none", 4)) {
		line += 4;
		color_to_pixel[id] = -1;	// transparent
		continue;
	    } else if (*line != '#') {
		fprintf(stderr, "unparsable XPM color spec: \"%s\"\n", line);
		abort();
	    } else {
		line++;
		r = (hex[line[0] & 0xFF] << 4) | hex[line[1] & 0xFF];
		line += 2;
		g = (hex[line[0] & 0xFF] << 4) | hex[line[1] & 0xFF];
		line += 2;
		b = (hex[line[0] & 0xFF] << 4) | hex[line[1] & 0xFF];
		line += 2;
	    }

	    // 8bit rgb -> 16bit
	    r = (65535 * (r & 0xFF) / 255);
	    b = (65535 * (b & 0xFF) / 255);
	    g = (65535 * (g & 0xFF) / 255);

	    // FIXME: should i use _unchecked here?
	    if ((depth != 1 && type == 'c') || (depth == 1 && type == 'm')) {
		if (cookies[i].sequence) {
		    fprintf(stderr, "XPM multiple color spec: \"%s\"\n", line);
		    abort();
		}
		cookies[i] =
		    xcb_alloc_color_unchecked(connection, colormap, r, g, b);
	    }
	}
	data++;
    }

    //
    //	Fetch the replies
    //
    for (i = 0; i < colors; i++) {
	xcb_alloc_color_reply_t *reply;

	if (cookies[i].sequence) {
	    reply = xcb_alloc_color_reply(connection, cookies[i], NULL);
	    if (!reply) {
		fprintf(stderr, "unable to allocate XPM color\n");
		abort();
	    }
	    pixels[i] = reply->pixel;
	    free(reply);
	} else {
	    // transparent or error
	    pixels[i] = 0UL;
	}
    }

    if (depth == 1) {
	transparent = 1;
    }

    image =
	xcb_image_create_native(connection, w, h,
	(depth == 1) ? XCB_IMAGE_FORMAT_XY_BITMAP : XCB_IMAGE_FORMAT_Z_PIXMAP,
	depth, NULL, 0L, NULL);
    if (!image) {			// failure
	return image;
    }

    //
    //	Allocate empty mask (if mask is requested)
    //
    mask_width = (w + 7) / 8;		// make gcc happy
    if (mask) {
	i = mask_width * h;
	*mask = malloc(i);
	if (!mask) {			// malloc failure
	    mask = NULL;
	} else {
	    memset(*mask, 255, i);
	}
    }

    //
    //	Copy each pixel from xpm into the image, while creating the mask
    //
    for (y = 0; y < h; y++) {
	line = *data++;
	for (x = 0; x < w; x++) {
	    i = color_to_pixel[*line++ & 0xFF];
	    if (i == -1) {		// marks transparent
		xcb_image_put_pixel(image, x, y, transparent);
		if (mask) {
		    (*mask)[(y * mask_width) + (x >> 3)] &= (~(1 << (x & 7)));
		}
	    } else {
		xcb_image_put_pixel(image, x, y, pixels[i]);
	    }
	}
    }
    return image;
}

////////////////////////////////////////////////////////////////////////////

/**
**	Create pixmap.
**
**	@param data		XPM data
**	@param[out] mask	Pixmap for data
**
**	@returns pixmap created from data.
*/
xcb_pixmap_t CreatePixmap(const char *const *data, xcb_pixmap_t * mask)
{
    xcb_pixmap_t pixmap;
    uint8_t *bitmap;
    xcb_image_t *image;

    image =
	XcbXpm2Image(Connection, Screen->default_colormap, Screen->root_depth,
	0UL, data, mask ? &bitmap : NULL);
    if (!image) {
	fprintf(stderr, "Can't create image\n");
	abort();
    }
    if (mask) {
	*mask =
	    xcb_create_pixmap_from_bitmap_data(Connection, Window, bitmap,
	    image->width, image->height, 1, 0, 0, NULL);
	free(bitmap);
    }
    // now get data from image and build a pixmap...
    pixmap = xcb_generate_id(Connection);
    xcb_create_pixmap(Connection, Screen->root_depth, pixmap, Window,
	image->width, image->height);
    xcb_image_put(Connection, pixmap, NormalGC, image, 0, 0, 0);

    xcb_image_destroy(image);

    return pixmap;
}

////////////////////////////////////////////////////////////////////////////

/**
**	Loop
*/
void Loop(void)
{
    struct pollfd fds[1];
    xcb_generic_event_t *event;
    int n;
    int delay;

    fds[0].fd = xcb_get_file_descriptor(Connection);
    fds[0].events = POLLIN | POLLPRI;

    delay = Rate;			// default 1500ms delay between updates
    for (;;) {
	n = poll(fds, 1, delay);
	if (n < 0) {
	    return;
	}
	if (n) {
	    if (fds[0].revents & (POLLIN | POLLPRI)) {
		if ((event = xcb_poll_for_event(Connection))) {

		    switch (event->
			response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
			case XCB_EXPOSE:
			    // background pixmap no need to redraw?
#if 0
			    // collapse multi expose
			    if (!((xcb_expose_event_t*)event)->count) {
				 xcb_clear_area(Connection, 0, Window, 0, 0, 64, 64);
				// flush the request
				xcb_flush(Connection);
			    }
#endif
			    break;
			case XCB_DESTROY_NOTIFY:
			    // window destroyed, exit application
			    return;
			case 0:
			    // error_code
			    // printf("error %x\n", event->response_type);
			    break;
			default:
			    // Unknown event type, ignore it
			    // printf("unknown %x\n", event->response_type);
#ifdef SCREENSAVER
			    if (XCB_EVENT_RESPONSE_TYPE(event) ==
				ScreenSaverEventId) {
				xcb_screensaver_notify_event_t *sse;

				sse = (xcb_screensaver_notify_event_t *) event;
				if (sse->code == XCB_SCREENSAVER_STATE_ON) {
				    // screensave on, stop updates
				    delay = -1;
				} else {
				    // screensave off, resume updates
				    delay = Rate;
				    Timeout();	// show latest info
				}
				break;
			    }
#endif
			    break;
		    }

		    free(event);
		} else {
		    // No event, can happen, but we must check for close
		    if (xcb_connection_has_error(Connection)) {
			return;
		    }
		}
	    }
	} else {
	    Timeout();
	}
    }
}

/**
**	Init
**
**	@param argc	number of arguments
**	@param argv	arguments vector
*/
int Init(int argc, char *const argv[])
{
    const char *display_name;
    xcb_connection_t *connection;
    xcb_screen_iterator_t iter;
    int screen_nr;
    xcb_screen_t *screen;
    xcb_gcontext_t normal;
    uint32_t mask;
    uint32_t values[3];
    xcb_pixmap_t pixmap;
    xcb_window_t window;
    xcb_size_hints_t size_hints;
    xcb_wm_hints_t wm_hints;
    int i;
    int n;
    char *s;

    display_name = getenv("DISPLAY");

    //	Open the connection to the X server.
    //	use the DISPLAY environment variable as the default display name
    connection = xcb_connect(NULL, &screen_nr);
    if (!connection || xcb_connection_has_error(connection)) {
	fprintf(stderr, "Can't connect to X11 server on %s\n", display_name);
	return -1;
    }
    //	Get the requested screen number
    iter = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (i=0; i<screen_nr; ++i) {
	xcb_screen_next(&iter);
    }
    screen = iter.data;


    //	Create normal graphic context
    normal = xcb_generate_id(connection);
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    values[0] = screen->white_pixel;
    values[1] = screen->black_pixel;
    values[2] = 0;
    xcb_create_gc(connection, normal, screen->root, mask, values);

    //	Pixmap
    //		We use a background pixmap, nice window move and expose.

    pixmap = xcb_generate_id(connection);
    xcb_create_pixmap(connection, screen->root_depth, pixmap, screen->root,
	64, 64);

    //	Create the window
    window = xcb_generate_id(connection);

    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = screen->white_pixel;
    mask = XCB_CW_BACK_PIXMAP | XCB_CW_EVENT_MASK;
    values[0] = pixmap;
    values[1] = XCB_EVENT_MASK_EXPOSURE;

    xcb_create_window(connection,	// Connection
	XCB_COPY_FROM_PARENT,		// depth (same as root)
	window,				// window Id
	screen->root,			// parent window
	0, 0,				// x, y
	64, 64,				// width, height
	0,				// border_width
	XCB_WINDOW_CLASS_INPUT_OUTPUT,	// class
	screen->root_visual,		// visual
	mask, values);			// mask, values

    // XSetWMNormalHints
    size_hints.flags = 0;		// FIXME: bad lib design
    // xcb_size_hints_set_position(&size_hints, 0, 0, 0);
    // xcb_size_hints_set_size(&size_hints, 0, 64, 64);
    xcb_size_hints_set_min_size(&size_hints, 64, 64);
    xcb_size_hints_set_max_size(&size_hints, 64, 64);
    xcb_set_wm_normal_hints(connection, window, &size_hints);

    // XSetClassHint from xc/lib/X11/SetHints.c
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, WM_CLASS,
	STRING, 8, strlen("wmc2d,wmc2d"), "wmc2d\0wmc2d");

    xcb_set_wm_name(connection, window, STRING, strlen("wmc2d"), "wmc2d");
    xcb_set_wm_icon_name(connection, window, STRING, strlen("wmc2d"), "wmc2d");

    // XSetWMHints
    wm_hints.flags = 0;
    xcb_wm_hints_set_icon_pixmap(&wm_hints, pixmap);
    xcb_wm_hints_set_window_group(&wm_hints, window);
    xcb_wm_hints_set_withdrawn(&wm_hints);
    if (WindowMode) {
	// xcb_wm_hints_set_none(&wm_hints);
	xcb_wm_hints_set_normal(&wm_hints);
    }
    xcb_set_wm_hints(connection, window, &wm_hints);

    // XSetCommand (see xlib source)
    for (n = i = 0; i < argc; ++i) {	// length of string prop
	n += strlen(argv[i]) + 1;
    }
    s = alloca(n);
    for (n = i = 0; i < argc; ++i) {	// copy string prop
	strcpy(s + n, argv[i]);
	n += strlen(s + n) + 1;
    }
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, WM_COMMAND,
	STRING, 8, n, s);

#ifdef SCREENSAVER
    //
    //	Prepare screensaver notify.
    //
    if (UseSleep) {
	const xcb_query_extension_reply_t *reply_screensaver;

	reply_screensaver =
	    xcb_get_extension_data(connection, &xcb_screensaver_id);
	if (reply_screensaver) {
	    ScreenSaverEventId =
		reply_screensaver->first_event + XCB_SCREENSAVER_NOTIFY;

	    xcb_screensaver_select_input(connection, window,
		XCB_SCREENSAVER_EVENT_NOTIFY_MASK);
	}
    }
#endif

    //	Map the window on the screen
    xcb_map_window(connection, window);

    //	Make sure commands are sent
    xcb_flush(connection);

    //	Move local vars for global use
    Connection = connection;
    Screen = screen;
    Window = window;
    NormalGC = normal;
    Pixmap = pixmap;

    return 0;
}

/**
**	Exit
*/
void Exit(void)
{
    xcb_destroy_window(Connection, Window);
    Window = 0;

    xcb_free_pixmap(Connection, Pixmap);

    if (Image) {
	xcb_free_pixmap(Connection, Image);
    }

    xcb_disconnect(Connection);
    Connection = NULL;
}

////////////////////////////////////////////////////////////////////////////
//	App Stuff
////////////////////////////////////////////////////////////////////////////

/**
**	Draw a string at given cordinates.
**
**	@param	s	String
**	@param	x	x pixel position
**	@param	y	y pixel position
**
**	Text is written in upper case.
*/
void DrawString(const char *s, int x, int y)
{
    int dx;
    int c;

    dx = x;
    while (*s) {
	c = toupper(*s);
	if (c == ' ') {
	    xcb_copy_area(Connection, Image, Pixmap, NormalGC, 0, 65, dx, y, 6,
		7);
	} else if ('A' <= c && c <= 'Z') {	// is a letter
	    c -= 'A';
	    xcb_copy_area(Connection, Image, Pixmap, NormalGC, 1 + c * 6, 75,
		dx, y, 6, 7);
	} else {			// is a number or symbol
	    c -= '\'';
	    xcb_copy_area(Connection, Image, Pixmap, NormalGC, 1 + c * 6, 65,
		dx, y, 6, 7);
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
**
**	With leading 0, if 0-9.
*/
void DrawNumber(unsigned num, int x, int y)
{
    char buf[32];

    sprintf(buf, "%02d", num);
    DrawString(buf, x, y);
}

#if 0
/**
**	Draw a number at given cordinates with small font.
**
**	@param	num	unsigned number
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
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n1000 * 4, 41,
	    x, y, 4, 6);
	x += 4;
    }
    if (n1000 || n100) {
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n100 * 4, 41,
	    x, y, 4, 6);
	x += 4;
    }
    if (n1000 || n100 || n10) {
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n10 * 4, 41, x,
	    y, 4, 6);
	x += 4;
    }
    xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n1 * 4, 41, x, y,
	4, 6);
}
#endif

/**
**	Draw a number at given cordinates with LCD font.
**
**	@param	num	unsigned number
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
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n100 * 5, 34,
	    x, y, 5, 7);
    } else {
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, x, y, x, y, 5, 7);
    }
    x += 6;
    if (n100 || n10) {
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n10 * 5, 34, x,
	    y, 5, 7);
    } else {
	xcb_copy_area(Connection, Image, Pixmap, NormalGC, x, y, x, y, 5, 7);
    }
    x += 7;
    xcb_copy_area(Connection, Image, Pixmap, NormalGC, 64 + n1 * 5, 34, x, y,
	5, 7);
}

#if 0
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
    DrawLcdNumber(n / 100, 33, 6);
    n = ReadNumber("/sys/devices/platform/coretemp.1/temp1_input");
    DrawLcdNumber(n / 100, 33, 21);
    n = ReadNumber("/sys/class/thermal/thermal_zone0/temp");
    DrawLcdNumber(n / 100, 33, 36);
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
    DrawString(buf, 6, 50);

    n = ReadNumber("/sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq");
    sprintf(buf, "%4d", n / 1000);
    DrawString(buf, 35, 50);
}

// ------------------------------------------------------------------------- //

/**
**	Timeout call back.
*/
void Timeout(void)
{
    //
    // Update  everything
    //
    DrawTemperaturs();
    DrawFrequency();

    xcb_clear_area(Connection, 0, Window, 0, 0, 64, 64);
    // flush the request
    xcb_flush(Connection);
}

/**
**	Prepare our graphic data.
*/
void PrepareData(void)
{
    xcb_pixmap_t shape;

    Image = CreatePixmap((void *)wmc2d_xpm, &shape);
    // Copy background part
    xcb_copy_area(Connection, Image, Pixmap, NormalGC, 0, 0, 0, 0, 64, 64);
    if (shape) {
	xcb_shape_mask(Connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
	    Window, 0, 0, shape);
	xcb_free_pixmap(Connection, shape);
    }

    Timeout();
}

// ------------------------------------------------------------------------- //

/**
**	Print version.
*/
static void PrintVersion(void)
{
    printf("wmc2d core2duo dockapp Version " VERSION
#ifdef GIT_REV
	"(GIT-" GIT_REV ")"
#endif
	",\n\t(c) 2004, 2009, 2010 by Lutz Sammer\n"
	"\tLicense AGPLv3: GNU Affero General Public License version 3\n");
}

/**
**	Print usage.
*/
static void PrintUsage(void)
{
    printf("Usage: wmc2d [-r rate] [-s] [-w]\n"
	"\t-r rate\trefresh rate (in milliseconds, default 1500 ms)\n"
	"\t-s\tsleep while screen-saver is running or video is blanked\n"
	"\t-w\tStart in window mode\n" "Only idiots print usage on stderr!\n");
}

/**
**	Main entry point.
**
**	@param argc	number of arguments
**	@param argv	arguments vector
*/
int main(int argc, char *const argv[])
{
    Rate = 1500;			// 1500 ms default update rate

    //
    //	Parse arguments.
    //
    for (;;) {
	switch (getopt(argc, argv, "h?-r:sw")) {
	    case 'r':			// update rate
		Rate = atoi(optarg);
		continue;
	    case 's':			// sleep while screensaver running
		UseSleep = 1;
		continue;
	    case 'w':			// window mode
		WindowMode = 1;
		continue;

	    case EOF:
		break;
	    case '?':
	    case 'h':			// help usage
		PrintVersion();
		PrintUsage();
		return 0;
	    case '-':
		fprintf(stderr, "We need no long options\n");
		PrintUsage();
		return -1;
	    case ':':
		PrintVersion();
		fprintf(stderr, "Missing argument for option '%c'\n", optopt);
		return -1;
	    default:
		PrintVersion();
		fprintf(stderr, "Unkown option '%c'\n", optopt);
		return -1;
	}
	break;
    }
    if (optind < argc) {
	PrintVersion();
	while (optind < argc) {
	    fprintf(stderr, "Unhandled argument '%s'\n", argv[optind++]);
	}
	return -1;
    }

    Init(argc, argv);

    PrepareData();
    Loop();
    Exit();

    return 0;
}
