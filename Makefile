
CFLAGS_DBUS = $(shell pkg-config --cflags --libs dbus-1)
CFLAGS_DBUS_GLIB = $(shell pkg-config --cflags --libs dbus-glib-1)
CFLAGS_GIO  = $(shell pkg-config --cflags --libs gio-2.0)
CFLAGS_GUNIX  = $(shell pkg-config --cflags --libs gio-unix-2.0)

CFLAGS = -g -Wall -Werror -lddcutil -std=gnu11 #-I ddcutil-2.0.0/src/public


all: gdbus-example-server # gdbus-example-client

#dbus-server: dbus-server.c
#	gcc $< -o $@ $(CFLAGS) $(CFLAGS_DBUS) $(CFLAGS_DBUS_GLIB)

#dbus-client: dbus-client.c
#	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO)

gdbus-example-server: gdbus-example-server.c
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_DBUS) $(CFLAGS_GIO) $(CFLAGS_GUNIX) # -I ddcutil-2.0.0/src/public
	
#gdbus-example-client: gdbus-example-client.c
#	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO)

#gdbus-testserver: gdbus-testserver.c
#	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO)

#gdbus-example-unix-fd-client: gdbus-example-unix-fd-client.c
#	gcc $< -o $@ $(CFLAGS) $(CFLAGS_DBUS) $(CFLAGS_GIO) $(CFLAGS_GUNIX)

clean:
	rm -f dbus-server
	rm -f dbus-client
	rm -f gdbus-example-server
	rm -f gdbus-testserver
	rm -f gdbus-example-unix-fd-client
	rm -f gdbus-example-client


.PHONY: all clean
