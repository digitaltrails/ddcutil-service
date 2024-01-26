# Copyright (C) 2023, Michael Hamilton
CFLAGS_GIO  = $(shell pkg-config --cflags --libs gio-2.0)

# Uncomment to compile against the standard installed libddcutil:
CFLAGS_DDCUTIL = $(shell pkg-config --cflags --libs ddcutil)
# Uncomment to compile against a developer local libddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.1.1-dev/src/public -L $(HOME)/Downloads/ddcutil-2.1.1-dev/src/.libs -lddcutil

CFLAGS = -g -Wall -Werror -std=gnu11
SOURCE = ddcutil-service.c
EXE = ddcutil-service
MANPAGE = $(EXE).1
HTML = docs/html/$(MANPAGE).html
PREFIX = $(HOME)/.local
BIN_DIR = $(PREFIX)/bin
SERVICE_FILE = com.ddcutil.DdcutilService.service
SERVICES_DIR = $(PREFIX)/share/dbus-1/services

all: $(SOURCE) $(EXE) $(HTML)

$(EXE): $(SOURCE)
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO) $(CFLAGS_DDCUTIL)

install: $(EXE)
	sed 's?/usr/bin/?$(BIN_DIR)/?' < $(SERVICE_FILE) > $(SERVICE_FILE).tmp
	install $(SERVICE_FILE).tmp $(SERVICES_DIR)/$(SERVICE_FILE)
	install $(EXE) $(BIN_DIR)

$(HTML): $(MANPAGE)
	groff -mandoc -Thtml $(MANPAGE) > $(HTML)

clean:
	rm -f $(EXE)
