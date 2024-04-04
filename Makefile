# Copyright (C) 2023, Michael Hamilton
CFLAGS_GIO = $(shell pkg-config --cflags --libs gio-2.0)

# Uncomment to compile against the standard installed libddcutil:
CFLAGS_DDCUTIL = $(shell pkg-config --cflags --libs ddcutil)
# Uncomment to compile against a developer local libddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.1.5-dev/src/public -L $(HOME)/Downloads/ddcutil-2.1.5-dev/src/.libs -lddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.1.4-dev/src/public -L $(HOME)/Downloads/ddcutil-2.1.4-dev/src/.libs -lddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.0.0/src/public -L $(HOME)/Downloads/ddcutil-2.0.0/src/.libs -lddcutil

CFLAGS += -g -std=gnu11 -Werror -Wall -Wformat-security
SOURCE = ddcutil-service.c
EXE = ddcutil-service
PREFIX = $(HOME)/.local
BIN_DIR = $(PREFIX)/bin
SERVICE_FILE = com.ddcutil.DdcutilService.service
SERVICES_DIR = $(PREFIX)/share/dbus-1/services

all: $(SOURCE) $(EXE) $(HTML)

$(EXE): $(SOURCE)
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO) $(CFLAGS_DDCUTIL)

install: $(EXE)
	sed 's?/usr/bin/?$(BIN_DIR)/?' < $(SERVICE_FILE) > $(SERVICE_FILE).tmp
	mkdir -p $(SERVICES_DIR) $(BIN_DIR)
	install $(SERVICE_FILE).tmp $(SERVICES_DIR)/$(SERVICE_FILE)
	install $(EXE) $(BIN_DIR)

clean:
	rm -f $(EXE)
