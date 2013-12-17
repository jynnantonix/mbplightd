# mbplightd: Simple keyboard backlight and screen brightness control daemon

PROGRAM  = mbplightd
SERVICE  = mbplightd.service

SRC      = mbplightd.c
OBJ      = mbplightd.o

CFLAGS  := -std=c99 -pedantic -Wall -Wextra $(CFLAGS)
LDLIBS   = -lm


all: $(PROGRAM) $(SERVICE)

install: all
	install -D -m755 mbplightd $(DESTDIR)/usr/bin/mbplightd
	install -D -m644 mbplightd.service $(DESTDIR)/usr/lib/systemd/system/mbplightd.service

clean:
	$(RM) $(OBJ) $(PROGRAM)
