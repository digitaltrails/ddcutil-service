# Copyright (C) 2023, Michael Hamilton
CFLAGS_GIO  = $(shell pkg-config --cflags --libs gio-2.0)
#CFLAGS_DDCUTIL = $(shell pkg-config --cflags --libs ddcutil)
CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.0.2-dev/src/public -L $(HOME)/Downloads/ddcutil-2.0.2-dev/src/.libs -lddcutil
CFLAGS = -g -Wall -Werror -std=gnu11
SOURCE = ddcutil-dbus-server.c
EXE = ddcutil-dbus-server
PREFIX = $(HOME)/.local
BIN_DIR = $(PREFIX)/bin
SERVICE_FILE = com.ddutil.DdcutilService.service
SERVICES_DIR = $(PREFIX)/share/dbus-1/services

all: $(SOURCE) $(EXE)

$(EXE): $(SOURCE)
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO) $(CFLAGS_DDCUTIL)

install: $(EXE)
	sed 's?/usr/bin/?$(BIN_DIR)/?' < $(SERVICE_FILE) > $(SERVICE_FILE).tmp
	install $(SERVICE_FILE).tmp $(SERVICES_DIR)/$(SERVICE_FILE)
	install $(EXE) $(BIN_DIR)

clean:
	rm -f $(EXE)
