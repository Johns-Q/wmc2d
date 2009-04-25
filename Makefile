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
#	$Id: $

#----------------------------------------------------------------------------

GIT_REV =       "`git describe --always 2>/dev/null`"

CC=	gcc
OPTIM=	-march=native -O2 -fomit-frame-pointer
CFLAGS= $(OPTIM) -W -Wall -g -pipe \
	-DGIT_REV=\"$(GIT_REV)\"
LIBS=	-ldockapp

OBJS=	wmc2d.o
FILES=	Makefile README changelog agpl-3.0.txt wmc2d.xpm

wmc2d:	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

wmc2d.o:	wmc2d.xpm

clean:
	-rm *.o *~

clobber:	clean
	-rm wmc2d

dist:
	tar cjf wmc2d-`date +%F-%H`.tar.bz2 $(FILES) $(OBJS:.o=.c) 

install:
	install -s wmc2d /usr/local/bin/
