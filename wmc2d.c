///
///	@name wmc2d.c	-	Dockapp for core 2 duo temperature
///
///	Copyright (c) 2004, 2009 by Lutz Sammer.  All Rights Reserved.
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

#include "wmc2d.xpm"

////////////////////////////////////////////////////////////////////////////

xcb_connection_t *Connection;		///< connection to X11 server
xcb_screen_t *Screen;			///< our screen
xcb_window_t Window;			///< our window
xcb_gcontext_t ForegroundGC;		///< foreground graphic context
xcb_gcontext_t NormalGC;		///< normal graphic context
xcb_pixmap_t Pixmap;			///< our background pixmap

//xcb_window_t IconWindow;		///< our icon window
xcb_shm_segment_info_t ShmInfo;		///< shared memory info of our pixmap

xcb_pixmap_t Image;			///< drawing data

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
**	@param[OUT] mask	bitmap mask for transparent
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

    // printf("%dx%d #%d*%d\n", w, h, colors, bytes_per_color);

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
	    // printf("Color %d %c %d %d %d\n", id, type, r, g, b);

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
	// printf("pixels(%d) %x\n", i, pixels[i]);
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
    // printf("Image allocated\n");

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
    // printf("Mask build\n");

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
**	@param[OUT] mask	Pixmap for data
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
    xcb_image_put(Connection, pixmap, ForegroundGC, image, 0, 0, 0);
    //xcb_request_check(Connection, cookie);

    // printf("Image %dx%dx%d\n", image->width, image->height, image->depth);

    xcb_image_destroy(image);

    return pixmap;
}

////////////////////////////////////////////////////////////////////////////

/**
**	Create an empty pixmap with dockapp size.
*/
xcb_pixmap_t MakePixmap(void)
{
    xcb_pixmap_t pixmap;

    pixmap = xcb_generate_id(Connection);
    xcb_create_pixmap(Connection, Screen->root_depth, pixmap, Window, 64, 64);

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

    fds[0].fd = xcb_get_file_descriptor(Connection);
    fds[0].events = POLLIN | POLLPRI;

    // printf("Loop\n");
    for (;;) {
	n = poll(fds, 1, 1500);
	if (n < 0) {
	    return;
	}
	if (n) {
	    if (fds[0].revents & (POLLIN | POLLPRI)) {
		// printf("%d: ready\n", fds[0].fd);
		if ((event = xcb_poll_for_event(Connection))) {

		    switch (event->
			response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
			case XCB_EXPOSE:
			    // FIXME: collapse multi expose
			    // printf("Expose\n");
			    xcb_clear_area(Connection, 0, Window, 0, 0, 64,
				64);
			    //xcb_clear_area(Connection, 0, IconWindow, 0, 0, 64, 64);
			    // flush the request
			    xcb_flush(Connection);
			    break;
			case XCB_DESTROY_NOTIFY:
			    // printf("destroy\n");
			    return;
			case 0:
			    // printf("error %x\n", event->response_type);
			    // error_code
			    break;
			default:
			    // printf("unknown %x\n", event->response_type);
			    // Unknown event type, ignore it
			    break;
		    }

		    free(event);
		} else {
		    // No event, can happen, but we must check for close
		    // printf("no event ready\n");
		    if (xcb_connection_has_error(Connection)) {
			// printf("closed\n");
			return;
		    }
		}
	    }
	} else {
	    //printf("Timeout\n");
	    Timeout();
	}
    }
}

/**
**	Init
*/
int Init(int argc, const char *argv[])
{
    const char *display_name;
    xcb_connection_t *connection;
    const xcb_setup_t *setup;
    xcb_shm_query_version_reply_t *shm_query_ver;
    uint8_t format;
    xcb_screen_iterator_t iter;
    xcb_screen_t *screen;
    xcb_gcontext_t foreground;
    xcb_gcontext_t normal;
    uint32_t mask;
    uint32_t values[3];
    xcb_image_t *image;
    xcb_pixmap_t pixmap;
    xcb_window_t window;
    xcb_size_hints_t size_hints;
    xcb_wm_hints_t wm_hints;
    int i;
    int n;
    char *s;
    int shared;

    display_name = getenv("DISPLAY");

    //	Open the connection to the X server.
    //	use the DISPLAY environment variable as the default display name
    connection = xcb_connect(NULL, NULL);
    if (!connection || xcb_connection_has_error(connection)) {
	fprintf(stderr, "Can't connect to X11 server on %s\n", display_name);
	return -1;
    }
    //
    //	Check needed externsions.
    //
    if (!(shm_query_ver =
	    xcb_shm_query_version_reply(connection,
		xcb_shm_query_version(connection), NULL))) {
	fprintf(stderr, "video: No shmem extension on %s\n", display_name);
	return -1;
    }
    // printf("video: shmem extension version %i.%i\n",
    //	    shm_query_ver->major_version, shm_query_ver->minor_version);
    format = shm_query_ver->pixmap_format;
    if (shm_query_ver->shared_pixmaps) {
	shared = 1;
    } else {
	shared = 0;
    }
    free(shm_query_ver);

    //	Get the first screen
    setup = xcb_get_setup(connection);
    iter = xcb_setup_roots_iterator(setup);
    screen = iter.data;

    //	Create black (foreground) graphic context
    foreground = xcb_generate_id(connection);
    mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    values[0] = screen->black_pixel;
    values[1] = 0;
    xcb_create_gc(connection, foreground, screen->root, mask, values);

    //	Create normal graphic context
    normal = xcb_generate_id(connection);
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    values[0] = screen->black_pixel;
    values[1] = screen->white_pixel;
    values[2] = 0;
    xcb_create_gc(connection, normal, screen->root, mask, values);

    //	Shared pixmap
    //		format from above, little overhead
    //		can use format_iter to find bytes for shared memory.
    /*
	setup->bitmap_format_scanline_pad;
	xcb_bytes_per_line  =  ((bpp * width + pad - 1) & -pad) >> 3;
	image->bytes_per_line = xcb_bytes_per_line(
	image->bitmap_format_scanline_pad, width, image->bits_per_pixel);
     */

    pixmap = xcb_generate_id(connection);
    if (shared) {
    image =
	xcb_image_create_native(connection, 64, 64, format, screen->root_depth,
	NULL, ~0, NULL);
    if (!image) {
	fprintf(stderr, "can't create image\n");
	return -1;
    }
    // printf("%d x %d\n", image->height, image->stride);

    ShmInfo.shmid =
	shmget(IPC_PRIVATE, image->height * image->stride, IPC_CREAT | 0777);
    if (ShmInfo.shmid == -1U) {
	fprintf(stderr, "error shmget()\n");
	return -1;
    }
    ShmInfo.shmaddr = shmat(ShmInfo.shmid, 0, 0);
    if (!ShmInfo.shmaddr) {
	fprintf(stderr, "error shmat()\n");
	return -1;
    }
    ShmInfo.shmseg = xcb_generate_id(connection);
    xcb_shm_attach(connection, ShmInfo.shmseg, ShmInfo.shmid, 0);
    shmctl(ShmInfo.shmid, IPC_RMID, 0);

    // printf("%p\n", ShmInfo.shmaddr);
    memset(ShmInfo.shmaddr, 0x8F, image->height * image->stride);

    xcb_shm_create_pixmap(connection, pixmap, screen->root, 64, 64,
	screen->root_depth, ShmInfo.shmseg, 0);

    xcb_image_destroy(image);
    } else {
	xcb_create_pixmap(connection, screen->root_depth, pixmap,
	screen->root, 64, 64);
    }

    //	Create the window
    window = xcb_generate_id(connection);
    // IconWindow = xcb_generate_id(connection);

    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = screen->white_pixel;
    mask = XCB_CW_BACK_PIXMAP | XCB_CW_EVENT_MASK;
    values[0] = pixmap;			// screen->white_pixel;
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

    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = screen->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE;
    // create icon win
    /*
	xcb_create_window(connection,	// Connection
	XCB_COPY_FROM_PARENT,		// depth (same as root)
	IconWindow,			// window Id
	window,				// parent window
	0, 0,				// x, y
	64, 64,				// width, height
	0,				// border_width
	XCB_WINDOW_CLASS_INPUT_OUTPUT,	// class
	screen->root_visual,		// visual
	mask, values);			// mask, values
     */

    // XSetWMNormalHints
    size_hints.flags = 0;		// FIXME: bad lib design
    xcb_size_hints_set_position(&size_hints, 1, 0, 0);
    xcb_size_hints_set_size(&size_hints, 1, 64, 64);
    xcb_set_wm_normal_hints(connection, window, &size_hints);

    // XSetClassHint from xc/lib/X11/SetHints.c
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, WM_CLASS,
	STRING, 8, strlen("wmc2d,wmc2d"), "wmc2d\0wmc2d");

    xcb_set_wm_name(connection, window, STRING, strlen("wmc2d"), "wmc2d");
    xcb_set_wm_icon_name(connection, window, STRING, strlen("wmc2d"), "wmc2d");

    // XSetWMHints
    wm_hints.flags = 0;
    //xcb_wm_hints_set_icon_window(&wm_hints, IconWindow);
    xcb_wm_hints_set_icon_pixmap(&wm_hints, pixmap);
    xcb_wm_hints_set_window_group(&wm_hints, window);
    xcb_wm_hints_set_withdrawn(&wm_hints);
    if (argc > 1) {
	xcb_wm_hints_set_none(&wm_hints);
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

    //	SHAPE
    if (0) {
	const int bytes = ((64 / 8) + 1) * 64;
	uint8_t *mask;
	xcb_pixmap_t pm;

	mask = malloc(bytes);
	memset(mask, 0xFF, bytes);
	mask[0] = 0x00;
	pm = xcb_create_pixmap_from_bitmap_data(connection, window, mask, 64,
	    64, 1, 0, 0, NULL);
	// printf("mask %d\n", pm);
	xcb_shape_mask(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
	    window, 0, 0, pm);
	xcb_free_pixmap(connection, pm);
	free(mask);
    }
    //	Map the window on the screen
    xcb_map_window(connection, window);

    //	Make sure commands are sent
    xcb_flush(connection);

    Connection = connection;
    Screen = screen;
    Window = window;
    ForegroundGC = foreground;
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
    // xcb_destroy_window(Connection, IconWindow);
    // IconWindow = 0;

    // FIXME: Free of shared pixmap correct?
    xcb_free_pixmap(Connection, Pixmap);
    xcb_shm_detach(Connection, ShmInfo.shmseg);
    shmdt(ShmInfo.shmaddr);

    if (Image) {
	xcb_free_pixmap(Connection, Pixmap);
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
    // xcb_clear_area(Connection, 0, IconWindow, 0, 0, 64, 64);
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
**	Main entry point.
*/
int main(int argc, const char *argv[])
{
    if (argc>1) {
	printf(
	"wmc2d core2duo dockapp Version 2.01 (GIT-" GIT_REV
	"), (c) 2004,2009 by Lutz Sammer\n"
	"\tLicense AGPLv3: GNU Affero General Public License version 3\n"
	"Usage: wmc2d\n");
	return 0;
    }
    Init(argc, argv);

    PrepareData();
    Loop();
    Exit();

    return 0;
}
