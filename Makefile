# Copyright (C) 2023, Michael Hamilton
CFLAGS_GIO = $(shell pkg-config --cflags --libs gio-2.0)

# Uncomment to compile against the standard installed libddcutil:
CFLAGS_DDCUTIL = $(shell pkg-config --cflags --libs ddcutil)
# Uncomment to compile against a developer local libddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.1.5-dev/src/public -L $(HOME)/Downloads/ddcutil-2.1.5-dev/src/.libs -lddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.1.5-dev-clion/build/src/public -isystem  $(HOME)/Downloads/ddcutil-2.1.5-dev-clion/src/public -L $(HOME)/Downloads/ddcutil-2.1.5-dev-clion/build/src/.libs -lddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.1.4-dev/src/public -L $(HOME)/Downloads/ddcutil-2.1.4-dev/src/.libs -lddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.0.0/src/public -L $(HOME)/Downloads/ddcutil-2.0.0/src/.libs -lddcutil
#CFLAGS_DDCUTIL = -isystem $(HOME)/Downloads/ddcutil-2.2.0/src/public -L $(HOME)/Downloads/ddcutil-2.2.0/src/.libs -lddcutil

OPT_LEVEL = -Og
CFLAGS += -g -std=gnu11 -Werror -Wall -Wformat-security $(OPT_LEVEL)

PREFIX = $(HOME)/.local
BIN_DIR = $(PREFIX)/bin
SERVICE_FILE = com.ddcutil.DdcutilService.service
SERVICES_DIR = $(PREFIX)/share/dbus-1/services
MAN_DIR = $(PREFIX)/share/man

.PHONY: all
all: ddcutil-service ddcutil-client

ddcutil-service: ddcutil-service.c
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO) $(CFLAGS_DDCUTIL)

# The client is an optional deployable
ddcutil-client: ddcutil-client.c
	gcc $< -o $@ $(CFLAGS) $(CFLAGS_GIO)

.PHONY: install
install: ddcutil-service ddcutil-service.1 ddcutil-service.7 com.ddcutil.DdcutilService.service
	install -Dm644 com.ddcutil.DdcutilService.service -t $(SERVICES_DIR)
	sed -i 's?/usr/bin/?$(BIN_DIR)/?g' $(SERVICES_DIR)/com.ddcutil.DdcutilService.service
	install -Dm755 ddcutil-service -t $(BIN_DIR)
	install -Dm644 ddcutil-service.1 -t $(MAN_DIR)/man1
	install -Dm644 ddcutil-service.7 -t $(MAN_DIR)/man7

.PHONY: install-client
install-client: ddcutil-client ddcutil-client.1
	install -Dm755 ddcutil-client -t $(BIN_DIR)
	install -Dm644 ddcutil-client.1 -t $(MAN_DIR)/man1

.PHONY: install-all
install-all: install install-client

.PHONY: clean
clean:
	rm -f ddcutil-client ddcutil-service
