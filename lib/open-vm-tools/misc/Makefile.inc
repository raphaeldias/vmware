# -*- Makefile -*-
################################################################################
### Copyright 2007 VMware, Inc.  All rights reserved.
###
### This program is free software; you can redistribute it and/or modify
### it under the terms of version 2 of the GNU General Public License as
### published by the Free Software Foundation.
###
### This program is distributed in the hope that it will be useful,
### but WITHOUT ANY WARRANTY; without even the implied warranty of
### MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
### GNU General Public License for more details.
###
### You should have received a copy of the GNU General Public License
### along with this program; if not, write to the Free Software
### Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
################################################################################

noinst_LIBRARIES += libMisc.a

libMisc_a_SOURCES =
libMisc_a_SOURCES += lib/open-vm-tools/misc/atomic.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/base64.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/codeset.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/codesetOld.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/codesetOld.h
libMisc_a_SOURCES += lib/open-vm-tools/misc/dbllnklst.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/dynarray.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/dynbuf.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/escape.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/hashTable.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/hostinfo_misc.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/hostname.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/idLinux.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/iovector.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/machineID.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/miscSolaris.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/posixDlopen.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/posixInt.h
libMisc_a_SOURCES += lib/open-vm-tools/misc/posixPosix.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/random.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/timeutil.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/util_misc.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/vmstdio.c
libMisc_a_SOURCES += lib/open-vm-tools/misc/strutil.c

libMisc_a_CPPFLAGS := $(AM_CPPFLAGS) $(LIB_MISC_CPPFLAGS)
