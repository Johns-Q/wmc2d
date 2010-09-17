#
#	@file Makefile	@brief Core 2 Duo temperature dockapp make file.
#
#	Copyright (c) 2004, 2009, 2010 by Lutz Sammer.  All Rights Reserved.
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

VERSION =	"2.04"
GIT_REV =	$(shell git describe --always 2>/dev/null)

CC=	gcc
OPTIM=	-march=native -O2 -fomit-frame-pointer
CFLAGS= $(OPTIM) -W -Wall -Wextra -g -pipe \
	-DVERSION='$(VERSION)'  $(if $(GIT_REV), -DGIT_REV='"$(GIT_REV)"')
#STATIC= --static
LIBS=	$(STATIC) `pkg-config --libs $(STATIC) \
	xcb-screensaver xcb-icccm xcb-shape xcb-shm xcb-image xcb` -lpthread

OBJS=	wmc2d.o
FILES=	Makefile README Changelog AGPL-3.0.txt wmc2d.doxyfile wmc2d.xpm wmc2d.1

all:	wmc2d

wmc2d:	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

wmc2d.o:	wmc2d.xpm Makefile

#----------------------------------------------------------------------------
#	Developer tools

doc:	$(SRCS) $(HDRS) wmc2d.doxyfile
	(cat wmc2d.doxyfile; \
	echo 'PROJECT_NUMBER=${VERSION} $(if $(GIT_REV), (GIT-$(GIT_REV)))') \
	| doxygen -

indent:
	for i in $(OBJS:.o=.c) $(HDRS); do \
		indent $$i; unexpand -a $$i > $$i.up; mv $$i.up $$i; \
	done

clean:
	-rm *.o *~

clobber:	clean
	-rm -rf wmc2d www/html

dist:
	tar cjCf .. wmc2d-`date +%F-%H`.tar.bz2 \
		$(addprefix wmc2d/, $(FILES) $(OBJS:.o=.c))

install: wmc2d wmc2d.1
	strip --strip-unneeded -R .comment wmc2d
	install -s wmc2d /usr/local/bin/
	install -D wmc2d.1 /usr/local/share/man/man1/wmc2d.1

help:
	@echo "make all|doc|indent|clean|clobber|dist|install|help"
