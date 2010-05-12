#
#	@file Makefile		Core 2 Duo temperature dockapp.
#
#	Copyright (c) 2004,2009 by Lutz Sammer.  All Rights Reserved.
#
#	Contributor(s):
#
#	License: AGPLv3
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU Affero General Public License as
#	published by the Free Software Foundation, either version 3 of the
#	License.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU Affero General Public License for more details.
#
#	$Id$
#----------------------------------------------------------------------------

VERSION =	"2.02"
GIT_REV =	$(shell git describe --always 2>/dev/null)

CC=	gcc
OPTIM=	-march=native -O2 -fomit-frame-pointer
CFLAGS= $(OPTIM) -W -Wall -g -pipe \
	-DVERSION='$(VERSION)'  $(if $(GIT_REV), -DGIT_REV='"$(GIT_REV)"')
LIBS=	`pkg-config --libs xcb-icccm xcb-shape xcb-shm xcb-image xcb`

OBJS=	wmc2d.o
FILES=	Makefile README Changelog AGPL-3.0.txt wmc2d.xpm

wmc2d:	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

wmc2d.o:	wmc2d.xpm Makefile

#----------------------------------------------------------------------------
#	Developer tools

indent:
	for i in $(OBJS:.o=.c) $(HDRS); do \
		indent $$i; unexpand -a $$i > $$i.up; mv $$i.up $$i; \
	done

clean:
	-rm *.o *~

clobber:	clean
	-rm wmc2d

dist:
	tar cjCf .. wmc2d-`date +%F-%H`.tar.bz2 \
		$(addprefix wmc2d/, $(FILES) $(OBJS:.o=.c))

install:
	strip --strip-unneeded -R .comment wmc2d
	install -s wmc2d /usr/local/bin/
