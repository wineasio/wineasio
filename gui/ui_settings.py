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

from PyQt5.QtCore import Qt, QCoreApplication, QMetaObject
from PyQt5.QtWidgets import QCheckBox, QComboBox, QDialogButtonBox, QLabel, QGroupBox, QSpinBox
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QSpacerItem, QSizePolicy

# ---------------------------------------------------------------------------------------------------------------------

class Ui_WineASIOSettings(object):
    OBJECT_NAME = "WineASIOSettings"

    def setupUi(self, WineASIOSettings):
        WineASIOSettings.setObjectName(self.OBJECT_NAME)
        WineASIOSettings.resize(400, 310)
        self.verticalLayout = QVBoxLayout(WineASIOSettings)
        self.verticalLayout.setObjectName("verticalLayout")
        self.group_ports = QGroupBox(WineASIOSettings)
        self.group_ports.setObjectName("group_ports")
        self.verticalLayout_22 = QVBoxLayout(self.group_ports)
        self.verticalLayout_22.setObjectName("verticalLayout_22")
        self.layout_ports_in = QHBoxLayout()
        self.layout_ports_in.setObjectName("layout_ports_in")
        self.label_ports_in = QLabel(self.group_ports)
        self.label_ports_in.setAlignment(Qt.AlignRight|Qt.AlignTrailing|Qt.AlignVCenter)
        self.label_ports_in.setObjectName("label_ports_in")
        self.layout_ports_in.addWidget(self.label_ports_in)
        self.sb_ports_in = QSpinBox(self.group_ports)
        self.sb_ports_in.setMaximum(128)
        self.sb_ports_in.setSingleStep(2)
        self.sb_ports_in.setObjectName("sb_ports_in")
        self.layout_ports_in.addWidget(self.sb_ports_in)
        self.verticalLayout_22.addLayout(self.layout_ports_in)
        self.layout_ports_out = QHBoxLayout()
        self.layout_ports_out.setObjectName("layout_ports_out")
        self.label_ports_out = QLabel(self.group_ports)
        self.label_ports_out.setAlignment(Qt.AlignRight|Qt.AlignTrailing|Qt.AlignVCenter)
        self.label_ports_out.setObjectName("label_ports_out")
        self.layout_ports_out.addWidget(self.label_ports_out)
        self.sb_ports_out = QSpinBox(self.group_ports)
        self.sb_ports_out.setMinimum(2)
        self.sb_ports_out.setMaximum(128)
        self.sb_ports_out.setSingleStep(2)
        self.sb_ports_out.setObjectName("sb_ports_out")
        self.layout_ports_out.addWidget(self.sb_ports_out)
        self.verticalLayout_22.addLayout(self.layout_ports_out)
        self.layout_ports_connect_hw = QHBoxLayout()
        self.layout_ports_connect_hw.setObjectName("layout_ports_connect_hw")
        spacerItem = QSpacerItem(150, 20, QSizePolicy.Fixed, QSizePolicy.Minimum)
        self.layout_ports_connect_hw.addItem(spacerItem)
        self.cb_ports_connect_hw = QCheckBox(self.group_ports)
        self.cb_ports_connect_hw.setObjectName("cb_ports_connect_hw")
        self.layout_ports_connect_hw.addWidget(self.cb_ports_connect_hw)
        self.verticalLayout_22.addLayout(self.layout_ports_connect_hw)
        self.verticalLayout.addWidget(self.group_ports)
        self.group_jack = QGroupBox(WineASIOSettings)
        self.group_jack.setObjectName("group_jack")
        self.verticalLayout_23 = QVBoxLayout(self.group_jack)
        self.verticalLayout_23.setObjectName("verticalLayout_23")
        self.layout_jack_autostart = QHBoxLayout()
        self.layout_jack_autostart.setObjectName("layout_jack_autostart")
        spacerItem1 = QSpacerItem(150, 20, QSizePolicy.Fixed, QSizePolicy.Minimum)
        self.layout_jack_autostart.addItem(spacerItem1)
        self.cb_jack_autostart = QCheckBox(self.group_jack)
        self.cb_jack_autostart.setObjectName("cb_jack_autostart")
        self.layout_jack_autostart.addWidget(self.cb_jack_autostart)
        self.verticalLayout_23.addLayout(self.layout_jack_autostart)
        self.layout_jack_fixed_bsize = QHBoxLayout()
        self.layout_jack_fixed_bsize.setObjectName("layout_jack_fixed_bsize")
        spacerItem2 = QSpacerItem(150, 20, QSizePolicy.Fixed, QSizePolicy.Minimum)
        self.layout_jack_fixed_bsize.addItem(spacerItem2)
        self.cb_jack_fixed_bsize = QCheckBox(self.group_jack)
        self.cb_jack_fixed_bsize.setObjectName("cb_jack_fixed_bsize")
        self.layout_jack_fixed_bsize.addWidget(self.cb_jack_fixed_bsize)
        self.verticalLayout_23.addLayout(self.layout_jack_fixed_bsize)
        self.layout_jack_buffer_size = QHBoxLayout()
        self.layout_jack_buffer_size.setObjectName("layout_jack_buffer_size")
        self.label_jack_buffer_size = QLabel(self.group_jack)
        self.label_jack_buffer_size.setAlignment(Qt.AlignRight|Qt.AlignTrailing|Qt.AlignVCenter)
        self.label_jack_buffer_size.setObjectName("label_jack_buffer_size")
        self.layout_jack_buffer_size.addWidget(self.label_jack_buffer_size)
        self.cb_jack_buffer_size = QComboBox(self.group_jack)
        self.cb_jack_buffer_size.setObjectName("cb_jack_buffer_size")
        self.layout_jack_buffer_size.addWidget(self.cb_jack_buffer_size)
        self.verticalLayout_23.addLayout(self.layout_jack_buffer_size)
        self.verticalLayout.addWidget(self.group_jack)
        self.buttonBox = QDialogButtonBox(WineASIOSettings)
        self.buttonBox.setOrientation(Qt.Horizontal)
        self.buttonBox.setStandardButtons(QDialogButtonBox.Cancel|QDialogButtonBox.Ok|QDialogButtonBox.RestoreDefaults)
        self.buttonBox.setObjectName("buttonBox")
        self.verticalLayout.addWidget(self.buttonBox)

        self.retranslateUi(WineASIOSettings)
        self.buttonBox.accepted.connect(WineASIOSettings.accept)
        self.buttonBox.rejected.connect(WineASIOSettings.reject)
        QMetaObject.connectSlotsByName(WineASIOSettings)

# ---------------------------------------------------------------------------------------------------------------------

    def retranslateUi(self, WineASIOSettings):
        _tr = QCoreApplication.translate
        WineASIOSettings.setWindowTitle(_tr(self.OBJECT_NAME, "WineASIO Settings"))

        # Audio Ports
        self.group_ports.setTitle(_tr(self.OBJECT_NAME, "Audio Ports"))

        self.label_ports_in.setText(_tr(self.OBJECT_NAME, "Number of inputs:"))
        self.label_ports_in.setToolTip(_tr(self.OBJECT_NAME,
                                           "Number of jack ports that wineasio will try to open.\n"
                                           "Default is 16"))

        self.sb_ports_in.setToolTip(_tr(self.OBJECT_NAME,
                                        "Number of jack ports that wineasio will try to open.\n"
                                        "Default is 16"))

        self.label_ports_out.setText(_tr(self.OBJECT_NAME, "Number of outputs:"))
        self.label_ports_out.setToolTip(_tr(self.OBJECT_NAME,
                                            "Number of jack ports that wineasio will try to open.\n"
                                            "Default is 16"))

        self.sb_ports_out.setToolTip(_tr(self.OBJECT_NAME,
                                         "Number of jack ports that wineasio will try to open.\n"
                                         "Default is 16"))

        self.cb_ports_connect_hw.setText(_tr(self.OBJECT_NAME, "Connect to hardware"))
        self.cb_ports_connect_hw.setToolTip(_tr(self.OBJECT_NAME,
                                                "Try to connect the asio channels to the\n"
                                                "physical I/O ports on your hardware.\n"
                                                "Default is on"))

        # JACK Options
        self.group_jack.setTitle(_tr(self.OBJECT_NAME, "JACK Options"))

        self.cb_jack_autostart.setText(_tr(self.OBJECT_NAME, "Autostart server"))
        self.cb_jack_autostart.setToolTip(_tr(self.OBJECT_NAME,
                                              "Enable wineasio to launch the jack server.\n"
                                              "Default is off"))

        self.cb_jack_fixed_bsize.setText(_tr(self.OBJECT_NAME, "Fixed buffersize"))
        self.cb_jack_fixed_bsize.setToolTip(_tr(self.OBJECT_NAME,
                                                "When on, an asio app will be able to change the jack buffer size.\n"
                                                "Default is off"))

        self.label_jack_buffer_size.setText(_tr(self.OBJECT_NAME, "Preferred buffersize:"))

# ---------------------------------------------------------------------------------------------------------------------
