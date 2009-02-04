# -*- Makefile -*-
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
################################################################################

bin_PROGRAMS += vmware-view-tunnel

vmware_view_tunnel_SOURCES :=
vmware_view_tunnel_SOURCES += tunnel/stubs.c
vmware_view_tunnel_SOURCES += tunnel/tunnelMain.c
vmware_view_tunnel_SOURCES += tunnel/tunnelProxy.c
vmware_view_tunnel_SOURCES += tunnel/tunnelProxy.h
vmware_view_tunnel_SOURCES += lib/open-vm-tools/misc/base64.c
vmware_view_tunnel_SOURCES += lib/open-vm-tools/misc/dynbuf.c
vmware_view_tunnel_SOURCES += lib/open-vm-tools/misc/strutil.c

vmware_view_tunnel_LDADD :=
vmware_view_tunnel_LDADD += libAsyncSocket.a
vmware_view_tunnel_LDADD += libPollDefault.a
vmware_view_tunnel_LDADD += libPoll.a
vmware_view_tunnel_LDADD += libSsl.a
vmware_view_tunnel_LDADD += libString.a
vmware_view_tunnel_LDADD += $(SSL_LIBS)
