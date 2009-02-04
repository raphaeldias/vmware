################################################################################
# Copyright 2008 VMware, Inc.  All rights reserved.
#
# This file is part of VMware View Open Client.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation version 2.1 and no later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
#################################################################################
# This is a non-recursive automake file.
#
# The style was inspired by Robert Collins, as mentioned here:
#
#     http://sources.redhat.com/ml/automake/2001-08/msg00061.html
#
# And the original Recursive Make Considered Harmful:
#
#     http://www.pcug.org.au/~millerp/rmch/recu-make-cons-harm.html
#

# Tell automake to use the full path for our object files
AUTOMAKE_OPTIONS := subdir-objects

# Tell autoreconf to run aclocal with -I m4
ACLOCAL_AMFLAGS = -I m4

NULL :=

BUILT_SOURCES :=
CLEANFILES :=

TESTS :=

AM_CPPFLAGS =
AM_CPPFLAGS += -I$(srcdir)/lib/open-vm-tools/include
AM_CPPFLAGS += -I$(srcdir)/lib/bora/include

applicationdir := $(datadir)/applications
pixmapsdir := $(datadir)/pixmaps

bin_PROGRAMS :=
application_DATA :=
dist_doc_DATA :=
dist_man_MANS :=
dist_noinst_DATA :=
dist_noinst_HEADERS :=
dist_pdf_DATA :=
noinst_LIBRARIES :=
noinst_PROGRAMS :=
pixmaps_DATA :=

include_subdirs :=

DIST_SUBDIRS := $(SUBDIRS) $(include_subdirs)

all:

include lib/open-vm-tools/dict/Makefile.inc
include lib/open-vm-tools/err/Makefile.inc
include lib/open-vm-tools/file/Makefile.inc
include lib/open-vm-tools/include/Makefile.inc
include lib/open-vm-tools/misc/Makefile.inc
include lib/open-vm-tools/panic/Makefile.inc
include lib/open-vm-tools/panicDefault/Makefile.inc
include lib/open-vm-tools/string/Makefile.inc
include lib/open-vm-tools/stubs/Makefile.inc
include lib/open-vm-tools/unicode/Makefile.inc
include lib/open-vm-tools/user/Makefile.inc

include lib/bora/asyncsocket/Makefile.inc
include lib/bora/basicHttp/Makefile.inc
include lib/bora/include/Makefile.inc
include lib/bora/log/Makefile.inc
include lib/bora/misc/Makefile.inc
include lib/bora/nothread/Makefile.inc
include lib/bora/poll/Makefile.inc
include lib/bora/pollDefault/Makefile.inc
include lib/bora/pollGtk/Makefile.inc
include lib/bora/productState/Makefile.inc
include lib/bora/sig/Makefile.inc
include lib/bora/ssl/Makefile.inc
include lib/bora/stubs/Makefile.inc
include lib/bora/unicode/Makefile.inc
include lib/bora/user/Makefile.inc

include Makefile.inc
include doc/Makefile.inc
include tunnel/Makefile.inc
