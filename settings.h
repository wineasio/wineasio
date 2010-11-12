/*
 * Copyright (C) 2010 Peter L Jones
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

static const char* ENVVAR_INPUTS = "_INPUTS";
static const char* ENVVAR_OUTPUTS = "_OUTPUTS";
static const char* ENVVAR_INPORTNAMEPREFIX = "INPORTNAME";
static const char* ENVVAR_OUTPORTNAMEPREFIX = "OUTPORTNAME";
static const char* ENVVAR_INMAP = "_INPORT";
static const char* ENVVAR_OUTMAP = "_OUTPORT";
static const char* ENVVAR_AUTOCONNECT = "_AUTOCONNECT";
static const char* DEFAULT_PREFIX = "ASIO";
static const char* DEFAULT_INPORT = "input_";
static const char* DEFAULT_OUTPORT = "output_";
static const int   DEFAULT_NUMINPUTS = 2;
static const int   DEFAULT_NUMOUTPUTS = 2;
static const int   DEFAULT_AUTOCONNECT = -1;
static const char* USERCFG = ".wineasiocfg";
static const char* SITECFG = "/etc/default/wineasiocfg";
