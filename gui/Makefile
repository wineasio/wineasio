#!/usr/bin/make -f
# Makefile for WineASIO Settings GUI #
# ---------------------------------- #
# Created by falkTX
#

PREFIX = /usr

PYUIC ?= pyuic5
PYRCC ?= pyrcc5

# ---------------------------------------------------------------------------------------------------------------------

all:

# ---------------------------------------------------------------------------------------------------------------------
# UI code

regen: ui_settings.py

ui_%.py: %.ui
	$(PYUIC) $< -o $@

# ---------------------------------------------------------------------------------------------------------------------

clean:
	rm -f *~ *.pyc ui_*.py

# ---------------------------------------------------------------------------------------------------------------------

install:
	# Create directories
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(PREFIX)/share/wineasio/

	# Install script files and binaries
	install -m 755 \
		wineasio-settings \
		$(DESTDIR)$(PREFIX)/bin/

	# Adjust PREFIX value in script files
	sed -i "s?X-PREFIX-X?$(PREFIX)?" \
		$(DESTDIR)$(PREFIX)/bin/wineasio-settings

	# Install code
	install -m 644 *.py $(DESTDIR)$(PREFIX)/share/wineasio/

# ---------------------------------------------------------------------------------------------------------------------

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wineasio-settings
	rm -rf $(DESTDIR)$(PREFIX)/share/wineasio/

# ---------------------------------------------------------------------------------------------------------------------
