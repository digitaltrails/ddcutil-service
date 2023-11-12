# Copyright (C) 2023, Michael Hamilton
CFLAGS_DBUS = $(shell pkg-config --cflags --libs dbus-1)
CFLAGS_DBUS_GLIB = $(shell pkg-config --cflags --libs dbus-glib-1)
CFLAGS_GIO  = $(shell pkg-config --cflags --libs gio-2.0)
CFLAGS_GUNIX  = $(shell pkg-config --cflags --libs gio-unix-2.0)

CFLAGS = -g -Wall -Werror -lddcutil -std=gnu11 #-I ddcutil-2.0.0/src/public


all: ddcutil-dbus-server

ddcutil-dbus-server: ddcutil-dbus-server.c
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_DBUS) $(CFLAGS_GIO) $(CFLAGS_GUNIX) # -I ddcutil-2.0.0/src/public
	
clean:
	rm -f ddcutil-dbus-server



.PHONY: all clean
