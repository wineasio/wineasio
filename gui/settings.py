#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# WineASIO Settings GUI
# Copyright (C) 2020 Filipe Coelho <falktx@falktx.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# For a full copy of the GNU General Public License see the COPYING.GUI file

# ---------------------------------------------------------------------------------------------------------------------

import os
import sys

from PyQt5.QtCore import pyqtSlot, QDir
from PyQt5.QtWidgets import QApplication, QDialog, QDialogButtonBox

# ---------------------------------------------------------------------------------------------------------------------

from ui_settings import Ui_WineASIOSettings

# ---------------------------------------------------------------------------------------------------------------------

BUFFER_SIZE_LIST = (16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192)

HOME = QDir.homePath()

WINEPREFIX = os.getenv("WINEPREFIX")
if not WINEPREFIX:
    WINEPREFIX = os.path.join(HOME, ".wine")

WINEASIO_PREFIX = "HKEY_CURRENT_USER\Software\Wine\WineASIO"

# ---------------------------------------------------------------------------------------------------------------------

def getWineASIOKeyValue(key: str, default: str):
    wineFile = os.path.join(WINEPREFIX, "user.reg")

    if not os.path.exists(wineFile):
        return default

    wineDumpF = open(wineFile, "r")
    wineDump  = wineDumpF.read()
    wineDumpF.close()

    wineDumpSplit = wineDump.split("[Software\\\\Wine\\\\WineASIO]")

    if len(wineDumpSplit) <= 1:
        return default

    wineDumpSmall = wineDumpSplit[1].split("[")[0]
    keyDumpSplit  = wineDumpSmall.split('"%s"' % key)

    if len(keyDumpSplit) <= 1:
        return default

    keyDumpSmall = keyDumpSplit[1].split(":")[1].split("\n")[0]
    return keyDumpSmall

def smartHex(value: str, length: int):
  hexStr = hex(value).replace("0x","")

  if len(hexStr) < length:
      zeroCount = length - len(hexStr)
      hexStr = "%s%s" % ("0"*zeroCount, hexStr)

  return hexStr

# ---------------------------------------------------------------------------------------------------------------------
# Set-up GUI (Tweaks, WineASIO)

# Force Restart Dialog
class WineASIOSettingsDialog(QDialog, Ui_WineASIOSettings):
    def __init__(self):
        QDialog.__init__(self, None)
        self.setupUi(self)

        self.changed = False
        self.loadSettings()

        self.accepted.connect(self.slot_saveSettings)
        self.buttonBox.button(QDialogButtonBox.RestoreDefaults).clicked.connect(self.slot_restoreDefaults)
        self.sb_ports_in.valueChanged.connect(self.slot_flagChanged)
        self.sb_ports_out.valueChanged.connect(self.slot_flagChanged)
        self.cb_ports_connect_hw.clicked.connect(self.slot_flagChanged)
        self.cb_jack_autostart.clicked.connect(self.slot_flagChanged)
        self.cb_jack_fixed_bsize.clicked.connect(self.slot_flagChanged)
        self.cb_jack_buffer_size.currentIndexChanged[int].connect(self.slot_flagChanged)

    def loadSettings(self):
        ins  = int(getWineASIOKeyValue("Number of inputs", "00000010"), 16)
        outs = int(getWineASIOKeyValue("Number of outputs", "00000010"), 16)
        hw   = bool(int(getWineASIOKeyValue("Connect to hardware", "00000001"), 10))

        autostart    = bool(int(getWineASIOKeyValue("Autostart server", "00000000"), 10))
        fixed_bsize  = bool(int(getWineASIOKeyValue("Fixed buffersize", "00000001"), 10))
        prefer_bsize = int(getWineASIOKeyValue("Preferred buffersize", "00000400"), 16)

        for bsize in BUFFER_SIZE_LIST:
            self.cb_jack_buffer_size.addItem(str(bsize))
            if bsize == prefer_bsize:
                self.cb_jack_buffer_size.setCurrentIndex(self.cb_jack_buffer_size.count()-1)

        self.sb_ports_in.setValue(ins)
        self.sb_ports_out.setValue(outs)
        self.cb_ports_connect_hw.setChecked(hw)

        self.cb_jack_autostart.setChecked(autostart)
        self.cb_jack_fixed_bsize.setChecked(fixed_bsize)

    @pyqtSlot()
    def slot_flagChanged(self):
        self.changed = True

    @pyqtSlot()
    def slot_restoreDefaults(self):
        self.changed = True

        self.sb_ports_in.setValue(16)
        self.sb_ports_out.setValue(16)
        self.cb_ports_connect_hw.setChecked(True)

        self.cb_jack_autostart.setChecked(False)
        self.cb_jack_fixed_bsize.setChecked(True)
        self.cb_jack_buffer_size.setCurrentIndex(BUFFER_SIZE_LIST.index(1024))

    @pyqtSlot()
    def slot_saveSettings(self):
        REGFILE  = 'REGEDIT4\n'
        REGFILE += '\n'
        REGFILE += '[HKEY_CURRENT_USER\Software\Wine\WineASIO]\n'
        REGFILE += '"Autostart server"=dword:0000000%i\n' % int(1 if self.cb_jack_autostart.isChecked() else 0)
        REGFILE += '"Connect to hardware"=dword:0000000%i\n' % int(1 if self.cb_ports_connect_hw.isChecked() else 0)
        REGFILE += '"Fixed buffersize"=dword:0000000%i\n' % int(1 if self.cb_jack_fixed_bsize.isChecked() else 0)
        REGFILE += '"Number of inputs"=dword:000000%s\n' % smartHex(self.sb_ports_in.value(), 2)
        REGFILE += '"Number of outputs"=dword:000000%s\n' % smartHex(self.sb_ports_out.value(), 2)
        REGFILE += '"Preferred buffersize"=dword:0000%s\n' % smartHex(int(self.cb_jack_buffer_size.currentText()), 4)

        with open("/tmp/wineasio-settings.reg", "w") as fh:
            fh.write(REGFILE)

        os.system("regedit /tmp/wineasio-settings.reg")

# ---------------------------------------------------------------------------------------------------------------------

if __name__ == '__main__':
    # App initialization
    app = QApplication(sys.argv)
    app.setApplicationName("WineASIO Settings")
    app.setApplicationVersion("1.0.0")
    app.setOrganizationName("falkTX")

    # Show GUI
    gui = WineASIOSettingsDialog()
    gui.show()

    # Exit properly
    sys.exit(app.exec_())

# ---------------------------------------------------------------------------------------------------------------------
