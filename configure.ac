#                                               -*- Autoconf -*-
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

# http://www.mail-archive.com/autoconf@gnu.org/msg16720.html
AC_INIT([VMware-view-open-client],
	m4_esyscmd([echo ${VDM_VERSION:-e.x.p}-${BUILD_NUMBER:-00000} | tr -d '\n']),
	[http://code.google.com/p/vmware-view-open-client/issues/entry],
	[VMware-view-open-client-source])
echo "Configuring $PACKAGE_STRING"
AC_CONFIG_SRCDIR([tunnel/tunnelMain.c])

# Keep the top-level directory tidy by putting auxiliary build tools and local
# macros in separate subdirectories.
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([/usr])

AC_CANONICAL_HOST

# Quote the regular expressions
RPM_ARCH="$host_cpu"
case "$host_cpu" in
   [i[3456]86])
      RPM_ARCH="i386"
      ;;
esac

AC_ARG_WITH([toolchain],
   [AS_HELP_STRING([--with-toolchain@<:@=DIR@:>@],
      [search for dependencies in the toolchain root DIR])],
   [TCROOT=$with_toolchain])
AC_SUBST(DISTCHECK_CONFIGURE_FLAGS,["--with-toolchain=$with_toolchain"])

AC_ARG_WITH([buildtype],
   [AS_HELP_STRING([--with-buildtype=obj|beta|release],
      [configures debugging and optimization flags])],
   [],[with_buildtype="obj"])

AC_MSG_CHECKING([for toolchain])
if test "$TCROOT" ; then
   CFLAGS="-gstabs+ -Wall -Werror -Wno-unused-value -fno-strict-aliasing"
   CPPFLAGS=
   CXXFLAGS=
   LDFLAGS=
   LIBS=

   if test "$TCROOT" = "yes"; then
      TCROOT="/build/toolchain"
   fi
   case "$build_cpu-$build_os" in
      [i[3456]86-linux*])
         BUILD_TCROOT="$TCROOT/lin32"
	 ;;
      [x86_64-linux*])
         # 32-bit binaries will run on 64-bit machines, so just use
         # them
         BUILD_TCROOT="$TCROOT/lin32"
	 ;;
      [*])
         AC_MSG_RESULT([not supported])
         AC_MSG_ERROR([$build_cpu-$build_os is not yet supported for a toolchain build.])
	 ;;
   esac
   case "$host_cpu-$host_os" in
      [i[3456]86-linux*])
	 binutilsversion="2.16.1-vt"
         ccflavor="i686-linux"
	 ccversion="4.1.1"
         # -march=i586 is required to run on some VIA C3 chips
         CFLAGS="$CFLAGS -march=i586"
         HOST_TCROOT="$TCROOT/lin32"
	 ;;
      [x86_64-linux*])
         ccflavor="x86_64-linux"
	 HOST_TCROOT="$TCROOT/lin64"
	 ;;
      [*])
         AC_MSG_RESULT([not supported])
         AC_MSG_ERROR([$host_cpu-$host_os is not yet supported for a toolchain host.])
	 ;;
   esac
   AC_MSG_RESULT([build: $BUILD_TCROOT host: $HOST_TCROOT])

   case "$with_buildtype" in
      [obj])
         CFLAGS="$CFLAGS -O0 -g3"
         ;;
      [release|beta|*])
         CFLAGS="$CFLAGS -O2 -g3"
         ;;
   esac
   CXXFLAGS="$CFLAGS"
   # C-only warning options
   CFLAGS="$CFLAGS -Wno-pointer-sign -Wno-strict-prototypes"

   AMTAR="$BUILD_TCROOT/tar-1.20/bin/tar"
   AWK="$BUILD_TCROOT/gawk-3.1.5/bin/awk"
   CC="$BUILD_TCROOT/gcc-$ccversion/bin/$ccflavor-gcc"
   CXX="$BUILD_TCROOT/gcc-$ccversion/bin/$ccflavor-g++"
   DPKG_DEB="$BUILD_TCROOT/dpkg-1.13.24/bin/dpkg-deb"
   GZIP="$BUILD_TCROOT/gzip-1.3.5/bin/gzip"
   FAKEROOT="$BUILD_TCROOT/fakeroot-1.5.9ubuntu1-1/bin/fakeroot"
   LD="$BUILD_TCROOT/gcc-$ccversion/bin/$ccflavor-gcc"
   LN="$BUILD_TCROOT/coreutils-5.97/bin/ln"
   LN_S="$BUILD_TCROOT/coreutils-5.97/bin/ln -s"
   MKSQUASHFS="$BUILD_TCROOT/squashfs-tools-3.1-r2/bin/mksquashfs"
   RPMBUILD="$BUILD_TCROOT/rpm-4.0.5/bin/rpmbuild"
   SED="$BUILD_TCROOT/sed-4.1.5/bin/sed"

   case "$host_os" in
      [linux*])
	 AR="$BUILD_TCROOT/binutils-$binutilsversion/bin/$ccflavor-ar"
	 LDFLAGS="$LDFLAGS -Wl,-rpath-link -Wl,$HOST_TCROOT/gcc-$ccversion/lib"
	 RANLIB="$BUILD_TCROOT/binutils-$binutilsversion/bin/$ccflavor-ranlib"
	 STRIP="$BUILD_TCROOT/binutils-$binutilsversion/bin/$ccflavor-strip"
	 X11="xorg-7.1"
	 ;;
   esac

   PATH="$BUILD_TCROOT/coreutils-5.97/bin:$BUILD_TCROOT/gawk-3.1.6/bin:$PATH"

   for dir in \
      $X11/include \
      zlib-1.2.3-3/include \
      libpng-1.2.22/include \
      fontconfig-2.2.3/include \
      freetype-2.1.9/include/freetype2 \
      cairo-1.2.4/include/cairo \
      glib-2.6.6/include/glib-2.0 \
      glib-2.6.6/lib/glib-2.0/include \
      pango-1.4.1/include/pango-1.0 \
      atk-1.9.0/include/atk-1.0 \
      gtk+-2.4.14/include/gtk-2.0 \
      gtk+-2.4.14/lib/gtk-2.0/include \
   ; do
      GTK_CFLAGS="$GTK_CFLAGS -I$HOST_TCROOT/$dir"
   done

   for dir in \
      $X11/lib \
      zlib-1.2.3-3/lib \
      libpng-1.2.22/lib \
      fontconfig-2.2.3/lib \
      freetype-2.1.9/lib \
      expat-1.95.8/lib \
      cairo-1.2.4/lib \
      glib-2.6.6/lib \
      pango-1.4.1/lib \
      atk-1.9.0/lib \
      gtk+-2.4.14/lib \
   ; do
      case "$host_os" in
         [linux*])
	    GTK_LIBS="$GTK_LIBS -Wl,-rpath-link -Wl,$HOST_TCROOT/$dir"
	    ;;
      esac
      GTK_LIBS="$GTK_LIBS -L$HOST_TCROOT/$dir"
   done
   GTK_LIBS="$GTK_LIBS -lglib-2.0 -lgobject-2.0 -latk-1.0 -lgtk-x11-2.0 -lgdk-x11-2.0 -lgdk_pixbuf-2.0 -lpng"

   XML_CFLAGS="-I$HOST_TCROOT/libxml2-2.6.30/include/libxml2"
   XML_LIBS="-L$HOST_TCROOT/libxml2-2.6.30/lib -lxml2"

   BOOST_CPPFLAGS="-I$HOST_TCROOT/boost-1.34.1/include"
   CURL_CFLAGS="-I$HOST_TCROOT/curl-7.18.0/include"
   SSL_CFLAGS="-I$HOST_TCROOT/openssl-0.9.8h/include"
   SSL_VERSION_NUM="0.9.8"

   case "$host_os" in
      [linux*])
         SSL_CFLAGS="$SSL_CFLAGS -DSSL_SRC_DIR=\\\"$HOST_TCROOT/openssl-0.9.8h/lib\\\""
         SSL_LIBS="-ldl"
         for sym in `$CC -DOPENSSL_098 -I$srcdir/lib/bora/include -E $srcdir/lib/bora/ssl/sslLinkWrapper.h | \
            changequote(,)dnl
            $AWK '/^[^#]/ { print $0 }'` ; do
            changequote([,])dnl
            SSL_LIBS="$SSL_LIBS -Wl,--wrap -Wl,$sym"
         done
         CURL_LIBS="-L$HOST_TCROOT/curl-7.18.0/lib -Wl,-Bstatic -lcurl -Wl,-Bdynamic"
         BOOST_LDFLAGS="-Wl,-rpath-link -Wl,$HOST_TCROOT/boost-1.34.1/lib -L$HOST_TCROOT/boost-1.34.1/lib"
         BOOST_LDFLAGS="$BOOST_LDFLAGS -Wl,-Bstatic -lboost_signals-gcc41-mt-1_34_1 -Wl,-Bdynamic"
	 ;;
   esac

   LIB_FILE_CPPFLAGS="$LIB_FILE_CPPFLAGS -DGLIBC_VERSION_21"
   LIB_SIG_CPPFLAGS="$LIB_SIG_CPPFLAGS -DGLIBC_VERSION_22"
   LIB_STRING_CPPFLAGS="$LIB_STRING_CPPFLAGS -DGLIBC_VERSION_22"
   LIB_MISC_CPPFLAGS="$LIB_MISC_CPPFLAGS -DGLIBC_VERSION_21"
   LIB_MISC_CPPFLAGS="$LIB_MISC_CPPFLAGS -DGLIBC_VERSION_22"
else
   AC_MSG_RESULT([not requested])
   if test -z "$GZIP" ; then
      GZIP=gzip
   fi
   if test -z "$MKSQUASHFS" ; then
      MKSQUASHFS=mksquashfs
   fi
   if test -z "$RPMBUILD" ; then
      RPMBUILD=rpmbuild
   fi

   PKG_CHECK_MODULES(GTK, [gtk+-x11-2.0 >= 2.4.0])
   PKG_CHECK_MODULES(XML, [libxml-2.0 >= 2.6.0])
   PKG_CHECK_MODULES(CURL, [libcurl >= 7.16.0])
   PKG_CHECK_MODULES(SSL, [libssl >= 0.9.8 libcrypto >= 0.9.8])

   SSL_CFLAGS="$SSL_CFLAGS -DSSL_SRC_DIR=\\\"`$PKG_CONFIG --variable libdir libssl`\\\""
fi

case "$host_os" in
   [linux*])
      LIBEXT="so"
      ;;
esac

# Checks for programs.
# This needs to be after TCROOT is set to pick up the correct paths
# for mawk et. al.
AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LN_S
AC_PROG_RANLIB
AC_PROG_SED
AC_PATH_PROG([AR],[ar])
AC_PATH_PROG([DATE],[date])
AC_PATH_PROG([DPKG_DEB],[dpkg-deb])
AC_PATH_PROG([FAKEROOT],[fakeroot])
AC_PATH_PROG([GZIP],[gzip])
AC_PATH_PROG([MKSQUASHFS],[mksquashfs])
AC_PATH_PROG([RPMBUILD],[rpmbuild])
AC_PATH_PROG([STRIP],[strip])
# These need to be after AC_PROG_SED
if test -z "$TCROOT" ; then
   changequote(,)dnl
   SSL_VERSION_NUM="`$PKG_CONFIG --modversion libssl | $SED -e 's/[a-zA-Z]*$//'`"
   changequote([,])dnl
fi
SSL_CFLAGS="$SSL_CFLAGS -DLIBCRYPTO_SO=libcrypto.$LIBEXT.$SSL_VERSION_NUM"
SSL_CFLAGS="$SSL_CFLAGS -DLIBSSL_SO=libssl.$LIBEXT.$SSL_VERSION_NUM"
SSL_CFLAGS="$SSL_CFLAGS -DOPENSSL_`echo $SSL_VERSION_NUM | $SED -e 's/\.//g'`"

AC_ARG_WITH([icons-dir],
   [AS_HELP_STRING([--with-icons-dir=DIR],
      [Use icon files found in DIR])],
   [],[with_icons_dir="$srcdir/icons/open"])
CPPFLAGS="$CPPFLAGS -I$with_icons_dir"
ICONS_DIR="$with_icons_dir"
AC_CHECK_HEADERS([view_16x.h view_32x.h view_48x.h view_client_banner.h],[],
	         [AC_MSG_ERROR([Some required icon files were not found.])],
                 [#define guint8 unsigned char])

AC_ARG_WITH([packaging-name],
   [AS_HELP_STRING([--with-packages-name=NAME],
      [use NAME for the base name of output packages])],
   [PACKAGING_NAME=$with_packaging_name],[PACKAGING_NAME=$PACKAGE_NAME])
changequote(,)dnl
DEB_PACKAGING_NAME=`echo $PACKAGING_NAME | tr [:upper:] [:lower:]`
changequote([,])dnl
if test "$PACKAGING_NAME" != "VMware-view-client" ; then
   DEB_REPLACES="$DEB_REPLACES, vmware-view-client"
   DEB_CONFLICTS="$DEB_CONFLICTS, vmware-view-client"
   RPM_CONFLICTS="$RPM_CONFLICTS VMware-view-client"
   RPM_CONFLICTS_LITE="$RPM_CONFLICTS_LITE VMware-view-client-lite"
fi
if test "$PACKAGING_NAME" != "VMware-view-open-client" ; then
   DEB_REPLACES="$DEB_REPLACES, vmware-view-open-client"
   DEB_CONFLICTS="$DEB_CONFLICTS, vmware-view-open-client"
   RPM_CONFLICTS="$RPM_CONFLICTS VMware-view-open-client"
   RPM_CONFLICTS_LITE="$RPM_CONFLICTS_LITE VMware-view-open-client-lite"
fi

AC_ARG_WITH([product-name],
   [AS_HELP_STRING([--with-product-name=NAME],
      [use NAME as the user-visable product name])],
   [PRODUCT_VIEW_CLIENT_NAME=$with_product_name],
   [PRODUCT_VIEW_CLIENT_NAME="VMware View Open Client"])
AC_DEFINE_UNQUOTED([PRODUCT_VIEW_CLIENT_NAME],["$PRODUCT_VIEW_CLIENT_NAME"])

BUILD_THINCLIENT_RPM=0
case "$host_os" in
   [linux*])
      if test "$RPMBUILD" ; then
         if test "$MKSQUASHFS" ; then
            BUILD_THINCLIENT_RPM=1
         else
            AC_MSG_WARN([mksquashfs was not found; disabling thinclient RPM])
         fi
      else
         AC_MSG_WARN([rpmbuild was not found; disabling RPMs])
      fi
      if test "$DPKG_DEB" ; then
         if test -z "$FAKEROOT" ; then
            AC_MSG_WARN([fakeroot was not found; disabling deb])
            DPKG_DEB=
         fi
      else
         AC_MSG_WARN([dpkg-deb was not found; disabling deb])
      fi
      ;;
   [*])
      RPMBUILD=
      DPKG_DEB=
      ;;
esac

VIEW_VERSION=`echo $PACKAGE_VERSION | $SED -e 's/-.*//'`
changequote(,)dnl
BUILD_NUMBER=`echo $PACKAGE_VERSION | $SED -e 's/[^-].*-//'`
changequote([,])dnl
VIEW_VERSION_NUM=m4_esyscmd([echo ${VDM_VERSION_NUM:-2.1.1} | tr -d '\n'])
DATE_R=`$DATE -R`
PRODUCT_BUILD_NUMBER=m4_esyscmd([echo -n ${PRODUCT_BUILD_NUMBER:-0}])

AC_DEFINE_UNQUOTED([VIEW_CLIENT_VERSION_NUMBER],["$VIEW_VERSION"])

AC_DEFINE(VMX86_TOOLS)
if test "$with_buildtype" = "obj" -o "$with_buildtype" = "beta" ; then
   AC_DEFINE(VMX86_DEBUG)
fi
if test "$with_buildtype" = "obj" -o "$with_buildtype" = "opt" ; then
   AC_DEFINE(VMX86_DEVEL)
   AC_DEFINE(VMX86_STATS)
   AC_DEFINE(DEBUG_STUB_ACCESS_IO_PORTS)
fi
if test "$with_buildtype" = "obj" ; then
   AC_DEFINE(VMX86_LOG)
fi
if test "$with_buildtype" = "release" ; then
   AC_DEFINE(VMX86_RELEASE)
fi
if test "$with_buildtype" = "beta" ; then
   AC_DEFINE(VMX86_BETA)
fi
CPPFLAGS="$CPPFLAGS -DBUILT_BY_$USER"
AC_DEFINE(DEBUG_STUB)
case "$host_os" in
   [linux*])
      AC_DEFINE(_XOPEN_SOURCE,[500])
      AC_DEFINE(_BSD_SOURCE)
      AC_DEFINE(_SVID_SOURCE)
      AC_DEFINE(_LARGEFILE64_SOURCE)
      AC_DEFINE(_FILE_OFFSET_BITS,[64])
      ;;
   [solaris*])
      LIB_USER_CPPFLAGS="$LIB_USER_CPPFLAGS -D_POSIX_PTHREAD_SEMANTICS"
      ;;
esac

if test ! "$TCROOT" ; then
   AC_CHECK_FUNCS([lseek],[LIB_FILE_CPPFLAGS="$LIB_FILE_CPPFLAGS -DGLIBC_VERSION_21"])
   AC_CHECK_HEADER([wchar.h],[LIB_STRING_CPPFLAGS="$LIB_STRING_CPPFLAGS -DGLIBC_VERSION_22"])
   # Check to see if SYS_setuid32 is defined. If SYS_setuid32 is unknown, we will
   # certainly error out.
   case "$host_cpu-$host_os" in
      [i[3456]86-linux*])
         AC_MSG_CHECKING([for SYS_setuid32])
         AC_TRY_COMPILE(
            [#include <sys/syscall.h>
             #include <unistd.h>],
            [
               syscall(SYS_setuid32, 1);
            ],
            [HAVE_SYS_SETUID="yes"],
            [HAVE_SYS_SETUID="no"])
         AC_MSG_RESULT($HAVE_SYS_SETUID)
	 ;;
      [x86_64-linux*])
         HAVE_SYS_SETEUID="yes"
         ;;
   esac
   if test "$HAVE_SYS_SETUID" = "yes"; then
      LIB_MISC_CPPFLAGS="$LIB_MISC_CPPFLAGS -DGLIBC_VERSION_21"
   fi

   # Newer iconv (from glibc 2.2 and later on Linux) takes a char ** as the second
   # argument. Here, we test for the older form and expect a compiler warning/error
   # if the newer form is defined (because, in that case, we would be attempting to
   # remove const'ness without an explicit cast).
   AC_MSG_CHECKING([for the new format of iconv])
   ORIGINAL_CFLAGS="$CFLAGS"
   CFLAGS="$CFLAGS -Werror"
   AC_TRY_COMPILE(
      [#include <iconv.h>],
      [
         iconv_t cd;
         char const *inbuf;
         size_t inbytesleft;
         char *outbuf;
         size_t outbytesleft;

         iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
      ],
      [HAVE_NEW_ICONV="no"],
      [HAVE_NEW_ICONV="yes"])
   CFLAGS="$ORIGINAL_CFLAGS"
   AC_MSG_RESULT($HAVE_NEW_ICONV)

   case "$host_os" in
      [linux*])
         AC_MSG_CHECKING([whether sys/ucontext.h defines REG_EAX])
         AC_COMPILE_IFELSE(
           [AC_LANG_PROGRAM(
               [[#define _GNU_SOURCE
                 #include <signal.h>
	         #include <sys/ucontext.h>]],
	       [[int foo = REG_EAX;]])],
            [
               LIB_SIG_CPPFLAGS="$LIB_SIG_CPPFLAGS -DGLIBC_VERSION_22"
	       AC_MSG_RESULT([yes])
            ],
            [
               AC_MSG_RESULT([no])
            ])
	 ;;
   esac

   AX_BOOST_BASE([1.34.1])
   AX_BOOST_SIGNALS
fi

RPM_STAGE_ROOT="`pwd`/rpm-stage"
RPM_OUTPUT_DIR="`pwd`/rpm-output"

# docdir defaults to the tarball name; we don't want to include
# "-source"
docdir='${datarootdir}/doc/${PACKAGING_NAME}'

# workarounds for automake/gnumake's keyword collisions
ENDIF=endif

AC_SUBST(LIB_FILE_CPPFLAGS)
AC_SUBST(LIB_MISC_CPPFLAGS)
AC_SUBST(LIB_SIG_CPPFLAGS)
AC_SUBST(LIB_STRING_CPPFLAGS)
AC_SUBST(LIB_USER_CPPFLAGS)

AC_SUBST(VIEW_VERSION)
AC_SUBST(VIEW_VERSION_NUM)
AC_SUBST(BUILD_NUMBER)
AC_SUBST(PRODUCT_BUILD_NUMBER)
AC_SUBST(PACKAGING_NAME)
AC_SUBST(PRODUCT_VIEW_CLIENT_NAME)
AC_SUBST(ICONS_DIR)

AC_SUBST(DEB_PACKAGING_NAME)
AC_SUBST(DEB_REPLACES)
AC_SUBST(DEB_CONFLICTS)
AC_SUBST(RPM_CONFLICTS)
AC_SUBST(RPM_CONFLICTS_LITE)

AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

AC_SUBST(XML_CFLAGS)
AC_SUBST(XML_LIBS)

AC_SUBST(CURL_CFLAGS)
AC_SUBST(CURL_LIBS)

AC_SUBST(SSL_CFLAGS)
AC_SUBST(SSL_LIBS)

AC_SUBST(BUILD_THINCLIENT_RPM)
AC_SUBST(DATE_R)
AC_SUBST(RPM_STAGE_ROOT)
AC_SUBST(RPM_OUTPUT_DIR)
AC_SUBST(RPM_ARCH)

AC_SUBST(ENDIF)

AC_CONFIG_FILES([
Makefile
VMware-view-open-client.control
VMware-view-open-client.spec
buildNumber.h
doc/changelog.Debian
vmware-view.desktop
])

AC_OUTPUT
