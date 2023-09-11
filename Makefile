# Copyright 2023, ZhongOu Huang, zongou@outlook.com
# SPDX-License-Identifier: MIT

VERSION := 0.1

CFLAGS += -DVERSION=\"${VERSION}\"

BIN_PATH := /usr/local/bin/
LIBS := ${shell pkg-config --cflags --libs colord lcms2 uuid}
SYSTEMD_DIR := /lib/systemd/system/
# bear for clangd
BEAR := $(shell command -v bear >/dev/null && echo bear --)

all: icc-brightness

icc-brightness: src/icc-brightness.c 
	@echo LIBS=$(LIBS)
	@echo bear = $(bear) $(BEAR)
	$(BEAR) $(CC) -W -Wall $(CFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f icc-brightness
	rm -f compile_commands.json

install: all
	mkdir -p $(DESTDIR)$(BIN_PATH)
	install -m 755 icc-brightness $(DESTDIR)$(BIN_PATH)
	install -m 755 icc-brightness.service $(SYSTEMD_DIR)icc-brightness.service
	systemctl daemon-reload
	systemctl enable icc-brightness.service
	systemctl restart icc-brightness.service

uninstall: 
	rm -f $(DESTDIR)$(BIN_PATH)icc-brightness
	systemctl daemon-reload
	systemctl stop icc-brightness.service
	systemctl disable icc-brightness.service
	rm -f $(SYSTEMD_DIR)icc-brightness.service

local-install: BIN_PATH=~/.local/bin/
local-install: install

local-uninstall: BIN_PATH=~/.local/bin/
local-uninstall: uninstall

