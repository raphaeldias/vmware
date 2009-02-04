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

libUnicode_a_SOURCES += lib/bora/unicode/cmemory.c
libUnicode_a_SOURCES += lib/bora/unicode/cmemory.h
libUnicode_a_SOURCES += lib/bora/unicode/cstring.c
libUnicode_a_SOURCES += lib/bora/unicode/cstring.h
libUnicode_a_SOURCES += lib/bora/unicode/cwchar.c
libUnicode_a_SOURCES += lib/bora/unicode/cwchar.h
libUnicode_a_SOURCES += lib/bora/unicode/locmap.c
libUnicode_a_SOURCES += lib/bora/unicode/locmap.h
libUnicode_a_SOURCES += lib/bora/unicode/putil.c
libUnicode_a_SOURCES += lib/bora/unicode/putilimp.h
libUnicode_a_SOURCES += lib/bora/unicode/uarrsort.c
libUnicode_a_SOURCES += lib/bora/unicode/uarrsort.h
libUnicode_a_SOURCES += lib/bora/unicode/uassert.h
libUnicode_a_SOURCES += lib/bora/unicode/ucase.h
libUnicode_a_SOURCES += lib/bora/unicode/ucln.h
libUnicode_a_SOURCES += lib/bora/unicode/ucln_cmn.c
libUnicode_a_SOURCES += lib/bora/unicode/ucln_cmn.h
libUnicode_a_SOURCES += lib/bora/unicode/ucmndata.c
libUnicode_a_SOURCES += lib/bora/unicode/ucmndata.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnv.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv2022.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_bld.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_bld.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_cb.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_cnv.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_cnv.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_err.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_ext.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_ext.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_imp.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_io.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_io.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_lmb.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_u16.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_u32.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_u7.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnv_u8.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnvbocu.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnvhz.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnvisci.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnvlat1.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnvmbcs.c
libUnicode_a_SOURCES += lib/bora/unicode/ucnvmbcs.h
libUnicode_a_SOURCES += lib/bora/unicode/ucnvscsu.c
libUnicode_a_SOURCES += lib/bora/unicode/udata.c
libUnicode_a_SOURCES += lib/bora/unicode/udatamem.c
libUnicode_a_SOURCES += lib/bora/unicode/udatamem.h
libUnicode_a_SOURCES += lib/bora/unicode/udataswp.c
libUnicode_a_SOURCES += lib/bora/unicode/udataswp.h
libUnicode_a_SOURCES += lib/bora/unicode/uenum.c
libUnicode_a_SOURCES += lib/bora/unicode/uenumimp.h
libUnicode_a_SOURCES += lib/bora/unicode/uhash.c
libUnicode_a_SOURCES += lib/bora/unicode/uhash.h
libUnicode_a_SOURCES += lib/bora/unicode/uinvchar.c
libUnicode_a_SOURCES += lib/bora/unicode/uinvchar.h
libUnicode_a_SOURCES += lib/bora/unicode/umapfile.c
libUnicode_a_SOURCES += lib/bora/unicode/umapfile.h
libUnicode_a_SOURCES += lib/bora/unicode/umath.c
libUnicode_a_SOURCES += lib/bora/unicode/umutex.c
libUnicode_a_SOURCES += lib/bora/unicode/umutex.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/brkiter.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/caniter.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/chariter.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/dbbi.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/docmain.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/locid.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/normlzr.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/parseerr.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/parsepos.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/platform.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ppalmos.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/putil.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/pwin32.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/rbbi.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/rep.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/resbund.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/schriter.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/strenum.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/symtable.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ubidi.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ubrk.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ucasemap.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ucat.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uchar.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uchriter.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uclean.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ucnv.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ucnv_cb.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ucnv_err.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uconfig.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/udata.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/udeprctd.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/udraft.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uenum.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uidna.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uintrnal.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uiter.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uloc.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/umachine.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/umisc.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/unifilt.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/unifunct.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/unimatch.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uniset.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/unistr.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/unorm.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uobject.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uobslete.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/urename.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/urep.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ures.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uscript.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uset.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ushape.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/usprep.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/ustring.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/usystem.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utext.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utf.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utf16.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utf32.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utf8.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utf_old.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utrace.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/utypes.h
libUnicode_a_SOURCES += lib/bora/unicode/unicode/uversion.h
libUnicode_a_SOURCES += lib/bora/unicode/unormimp.h
libUnicode_a_SOURCES += lib/bora/unicode/uset_imp.h
libUnicode_a_SOURCES += lib/bora/unicode/ustr_cnv.c
libUnicode_a_SOURCES += lib/bora/unicode/ustr_cnv.h
libUnicode_a_SOURCES += lib/bora/unicode/ustr_imp.h
libUnicode_a_SOURCES += lib/bora/unicode/ustrfmt.c
libUnicode_a_SOURCES += lib/bora/unicode/ustrfmt.h
libUnicode_a_SOURCES += lib/bora/unicode/ustring.c
libUnicode_a_SOURCES += lib/bora/unicode/utf_impl.c
libUnicode_a_SOURCES += lib/bora/unicode/utracimp.h
libUnicode_a_SOURCES += lib/bora/unicode/utrie.c
libUnicode_a_SOURCES += lib/bora/unicode/utrie.h
libUnicode_a_SOURCES += lib/bora/unicode/wintz.c
libUnicode_a_SOURCES += lib/bora/unicode/wintz.h

libUnicode_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/lib/bora/unicode
