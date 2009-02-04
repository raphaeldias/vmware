VMware View Open Client 2.1.1
Build 144835, 2009-01-29

VMware and the VMware "boxes" logo and design are registered
trademarks or trademarks (the "Marks") of VMware, Inc. in the United
States and/or other jurisdictions and are not licensed to you under
the terms of the LGPL version 2.1. If you distribute VMware View Open
Client unmodified in either binary or source form or the accompanying
documentation unmodified, you may not remove, change, alter or
otherwise modify the Marks in any manner.  If you make minor
modifications to VMware View Open Client or the accompanying
documentation, you may, but are not required to, continue to
distribute the unaltered Marks with your binary or source
distributions.  If you make major functional changes to VMware View
Open Client or the accompanying documentation, you may not distribute
the Marks with your binary or source distribution and you must remove
all references to the Marks contained in your distribution.  All other
use or distribution of the Marks requires the prior written consent of
VMware.  All rights reserved.

Copyright (c) 1998-2009 VMware, Inc. All rights reserved. Protected
by one or more U.S. Patent Nos. 6,397,242, 6,496,847, 6,704,925, 6,711,672,
6,725,289, 6,735,601, 6,785,886, 6,789,156, 6,795,966, 6,880,022, 6,944,699,
6,961,806, 6,961,941, 7,069,413, 7,082,598, 7,089,377, 7,111,086, 7,111,145,
7,117,481, 7,149,843, 7,155,558, 7,222,221, 7,260,815, 7,260,820, 7,269,683,
7,275,136, 7,277,998, 7,277,999, 7,278,030, 7,281,102, 7,290,253, 7,356,679,
7,409,487, 7,412,492, 7,412,702, 7,424,710, and 7,428,636; patents pending.


WELCOME
-------

Welcome to the release of VMware View Open Client 2.1.1.

VMware View Open Client was formerly known as VMware VDM Client for
Linux.  It was renamed simply to fit in with the current product name,
and does not have any View 3.0 feature improvements at this time.

The primary purpose of this 2.1.1 release is to make the sources of
the Linux client available under the GNU Lesser General Public License
version 2.1 (LGPL v 2.1).

For VMware View partners, official builds of VMware View Client are
still available through VMware Partner Engineering. If you don't feel
you can use an official build, perhaps because you are targeting a
processor architecture other than x86 or have functional requirements
that don't fit with our official build, we hope that this open source
release will help you produce a custom client. Whether you are using
an official build, a client derived from the View Open Client sources,
or a client written from scratch, contact Partner Engineering to
certify your product as View compatible.

VMware View Open Client is optimized for thin client devices: the
installed disk footprint is less than two megabytes, it has minimal
RAM requirements, and it has only a few library dependencies.

For more details on the features and capabilities of this client, please
refer to the end user documentation and the administrator's guide, both
of which are available in the installation package.


STATUS
------

This is the RTM release of VMware View Open Client 2.1.1.

CONTACT
-------

Contact information is available at the Google Code site for this
project:

http://code.google.com/p/vmware-view-open-client/


VIEW BROKER COMPATIBILITY
-------------------------

This release is compatible with VDM 2.0, VDM 2.1, and View 3.0.


SYSTEM REQUIREMENTS
-------------------

VMware View Open Client requires an i586 compatible processor, two
megabytes of secondary storage, and 128 megabytes of RAM.

VMware is actively testing this build against SUSE Linux Enterprise Thin
Client (SLETC) and Debian 4.0r3, however it should work with any Linux
distribution that meets the minimum library requirements listed below.

Required Version    Libraries
----------------    ---------
glibc 2.x           libc.so.6, libdl.so.2
gcc 3.4.x           libstdc++.so.6, libgcc_s.so.1
glib 2.8            libglib-2.0.so.0, libgobject-2.0.so.0
gtk+2 2.8           libgtk-x11-2.0.so.0, libgdk-x11.2.0.so.0,
                    libgdk_pixbuf-2.0.so.0
libpng 1.2.x        libpng12.so.0
openssl 0.9.8       libssl.so.0.9.8, libcrypto.so.0.9.8
libxml 2.6.x        libxml2.so.2
zlib 1.2.3          libz.so.1

In addition, rdesktop is required. VMware View Open Client has been
tested against rdesktop versions 1.4.1, 1.5.0, and 1.6.0.


INSTALLATION
------------

VMware View Open Client is distributed in four forms, as binary and
source tar gzips, as a regular RPM, and as a Novell SLETC Add-on RPM.

In order to install the binary tar gzip, simply unpack the tarball:

$ tar zxf VMware-view-open-client-2.1.1-144835.tar.gz

To run, navigate to the 'bin' subdirectory and run directly from the
command line:

$ cd VMware-view-open-client-2.1.1-144835/bin
$ ./vmware-view

The regular RPM can be installed using 'rpm -i'. The Novell SLETC
Add-on RPM should be installed from an HTTP server using the Novell
Add-on Manager; see the SLETC documentation for full details.

See the View Compatibility and System Requirements sections above for
information about compatibility.

To discover the command line arguments available, type

$ ./vmware-view --help

Note that, as with most Linux command-line programs, options must be
properly quoted to avoid being interpreted by the command shell.

General instructions for building from source can be found in the
INSTALL file.  You will need the following packages installed, whose
method will vary depending on your distribution:

Project          Version
-------------    -------
Gtk+             2.4.0
libxml2          2.6.0
libcurl          7.18.0
OpenSSL          0.9.8
Boost.Signals    1.34.1

If running configure fails, please check config.log for more details.


ISSUES RESOLVED IN THIS RELEASE
-------------------------------

*) Freeze when desktop selector window is resized (333688)
*) Disclaimer text is not word-wrapped
*) Disclaimer buttons grow vertically when window is resized
*) Missing Linux Admin Guide (348728)
*) Remote desktop does not get Alt+Enter or Control+Enter keypresses


ISSUES RESOLVED IN PREVIOUS RELEASES
------------------------------------

Version 2.1.0:

*) No bundled End-User Documentation (324486)
*) In one report, use of --fullscreen corrupted the rdesktop color palette
   (323057, resolved as unable to duplicate)
*) VDM client hangs when guest firewall blocks RDP port (325327)
*) Use 2.1 as version number (327092)
*) Tunnel client assert when restarting connected desktop (325330)
*) Window manager key bindings are not inhibited on Novell SLETC (313232)


KNOWN ISSUES IN THIS RELEASE
----------------------------

*) No sound with Linux client (351637)
   Workaround: pass '-r sound:local' option to vmware-view process
*) The desktop list cannot be refreshed (291918)
*) In certain cases, an "access denied" error may be seen when trying to
   reconnect to a desktop that has been reset or powered off from the
   administrative UI (330941)
*) The client may not time out when attempting to reach an unreachable
   broker. (322204, 325803)
*) There is no log collection script in this release (315390)
*) Debian package not available (333101)
*) A useless error is shown if the tunnel is unreachable (308760)


TROUBLESHOOTING
---------------

The log file path is given at launch:

$ ./vmware-view
Using log file /tmp/vmware-$USER/vmware-view-XXXXX.log

Please include the log file with any issue reported to VMware. As noted,
we have not included a log collection script in this release; one will
be added in a subsequent release.


SUPPORT
-------

At the time of this release, official builds of VMware View Client for
Linux are only available through certified partners. Support is
available through them. A list of certified partners can be found on
vmware.com.

Official builds have a blue banner, with the text, "VMware View,"
while versions based on the source release have an orange banner, with
the text, "VMware View Open Client."

Informal support for the source distribution may be found at:

http://code.google.com/p/vmware-view-open-client/


OPEN SOURCE
-----------

VMware View Open Client includes software which may be covered
under one or more open source agreements. For more information please
see the open_source_licenses.txt file located in the doc directory.


FEATURES
--------

*) Password authentication
*) SSL support
*) Secure tunnel
*) RSA authentication
*) View Broker version detection
*) Notification of forced disconnect and timeout
*) Full command-line interface
*) Confirmations, warnings, errors
*) Fullscreen mode
*) Disclaimer dialog
*) Password change dialog
*) Desktop reset (right-click entry in desktop selector)
*) Generic RPM package
*) Novell SLETC Add-on RPM package

The following features of the VDM Client for Windows 2.1 are not
implemented in this release.

*) USB redirection
*) Multiple desktop sessions
*) Windowed sessions
*) Multi-monitor sessions
*) Fullscreen toolbar
*) F5 Refresh of desktop list (291918)
*) Log collection script (315390)
