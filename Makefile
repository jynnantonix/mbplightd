# mbplightd: Simple keyboard backlight and screen brightness control daemon

PROGRAM  = mbplightd
SERVICE  = mbplightd.service
CONF     = mbplightd.conf

SRC      = mbplightd.c
OBJ      = mbplightd.o

CFLAGS  := -std=c99 -pedantic -Wall -Wextra $(CFLAGS)
LDLIBS   = -lm -liniparser


all: $(PROGRAM) $(SERVICE) $(CONF)

install: all
	install -D -m755 mbplightd $(DESTDIR)/usr/bin/mbplightd
	install -D -m644 mbplightd.service $(DESTDIR)/usr/lib/systemd/system/mbplightd.service
	install -D -m644 mbplightd.conf $(DESTDIR)/etc/mbplightd.conf

clean:
	$(RM) $(OBJ) $(PROGRAM)
