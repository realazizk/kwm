# kwm - dynamic window manager
# See LICENSE file for copyright and license details.

include config.mk

SRC = kwm.c drw.c util.c
OBJ = ${SRC:.c=.o}

all: options kwm

options:
	@echo kwm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

kwm: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f kwm ${OBJ} kwm-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p kwm-${VERSION}
	@cp -R LICENSE TODO BUGS Makefile README config.def.h config.mk \
		kwm.1 drw.h util.h ${SRC} kwm.png transient.c kwm-${VERSION}
	@tar -cf kwm-${VERSION}.tar kwm-${VERSION}
	@gzip kwm-${VERSION}.tar
	@rm -rf kwm-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f kwm ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/kwm
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < kwm.1 > ${DESTDIR}${MANPREFIX}/man1/kwm.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/kwm.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/kwm
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/kwm.1

.PHONY: all options clean dist install uninstall
