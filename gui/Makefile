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

define UI_IMPORTS
try:\\n
   from PyQt6.QtCore import Qt, QCoreApplication, QMetaObject\\n
   from PyQt6.QtWidgets import QCheckBox, QComboBox, QDialogButtonBox, QLabel, QGroupBox, QSpinBox\\n
   from PyQt6.QtWidgets import QHBoxLayout, QVBoxLayout, QSpacerItem, QSizePolicy\\n
   Qt.AlignRight = Qt.AlignmentFlag.AlignRight\\n
   Qt.AlignTrailing = Qt.AlignmentFlag.AlignTrailing\\n
   Qt.AlignVCenter = Qt.AlignmentFlag.AlignVCenter\\n
   Qt.Horizontal = Qt.Orientation.Horizontal\\n
   QSizePolicy.Fixed = QSizePolicy.Policy.Fixed\\n
   QSizePolicy.Minimum = QSizePolicy.Policy.Minimum\\n
   QDialogButtonBox.Cancel = QDialogButtonBox.StandardButton.Cancel\\n
   QDialogButtonBox.Ok = QDialogButtonBox.StandardButton.Ok\\n
   QDialogButtonBox.RestoreDefaults = QDialogButtonBox.StandardButton.RestoreDefaults\\n
except ImportError:\\n
   from PyQt5.QtCore import Qt, QCoreApplication, QMetaObject\\n
   from PyQt5.QtWidgets import QCheckBox, QComboBox, QDialogButtonBox, QLabel, QGroupBox, QSpinBox\\n
   from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QSpacerItem, QSizePolicy
endef

export UI_IMPORTS

regen: ui_settings.py

ui_%.py: %.ui
	$(PYUIC) $< -o $@
	sed -i 's/QtCore.//g' $@
	sed -i 's/QtGui.//g' $@
	sed -i 's/QtWidgets.//g' $@
	sed -i 's/_translate = QCoreApplication.translate/_tr = QCoreApplication.translate/' $@
	sed -i 's/_translate(/_tr(/g' $@
	sed -i 's/"WineASIOSettings"/self.OBJECT_NAME/g' $@
	sed -i 's/ # type: ignore//g' $@
	sed -i 's/WineASIOSettings(object):/WineASIOSettings(object):\n    OBJECT_NAME = "WineASIOSettings"\n/' $@
	sed -i 's/from PyQt5 import   QtWidgets/$(shell echo "$$UI_IMPORTS")/' $@
	sed -i 's/ except ImportError:/except ImportError:/' $@

# ---------------------------------------------------------------------------------------------------------------------

clean:
	rm -f *~ *.pyc

destroy: clean
	rm -f ui_*.py

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
