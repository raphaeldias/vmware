/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * urlTable.h --
 *
 *    List of all URLs used in our binaries
 *
 *    If you (engineer) need to add a new URL, here is the process to follow:
 *    1) Write the english text you want to appear on that web page in a text
 *       file
 *    2) Give that text file to the pubs group. They will HTML-ize it using the
 *       company's template, and they will take care of having it localized if
 *       needed
 *    3) Give that HTML file to the website group (email <website>), as well
 *       as a description of the content of that page, and what products it
 *       applies to. In exchange, they will give you a UrlId
 *    4) Add a URLTABLEENTRY() anywhere in this file, using that UrlId
 *
 *  --hpreg
 *
 */

/* This file may be included more than once. */

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"


/* RedHat page on memory corrupting Linux kernels used as a host OS */
URLTABLEENTRY(221450, 1, 0, "RedhatMemoryCorruption")

/* VMware, Inc.'s Web site */
URLTABLEENTRY(VMWARE, 2, 0, "vmware")

/* Download XFree86 4.0 driver to run in a Linux guest */
URLTABLEENTRY(XFREE86, 3, 0, "xfree86")

/* Products page */
URLTABLEENTRY(PRODUCTS, 4, 0, "products")

/* Product registration page */
URLTABLEENTRY(REGISTERNOW, 5, 0, "registerNow")

/* Pentium III Coppermine bug on the host */
URLTABLEENTRY(BIOS_UPDATE, 6, 0, "biosUpdate")

/* Request technical support at any time */
URLTABLEENTRY(REPORT, 7, 0, "report")

/* Request support when we detect an abnormal condition */
URLTABLEENTRY(INCIDENT, 8, (1 << URLAPPEND_LOGFILE) | (1 << URLAPPEND_CORELOCATION), "incident")

/* Netware 6.0 Tools download and installation instructions */
URLTABLEENTRY(NETWARE_TOOLS, 9, 0, "netwareTools")

/* Main support page == our database of technical tips/issues/solutions */
URLTABLEENTRY(SUPPORT, 10, 0, "support")

/*
 * Support related to running on a kernel w/ CONFIG_BIGMEM in Linux used as a
 * host OS
 */
URLTABLEENTRY(BIGMEM, 11, 0, "bigMem")

/* Support related to cow disk repair issues */
URLTABLEENTRY(DSKCHK, 12, 0, "diskCheck")

/* Support related to virtual disk upgrade issues */
#if defined(VMX86_DESKTOP)
   URLTABLEENTRY(DSKUPG, 13, 0, "diskUpdate")
#elif defined(VMX86_WGS)
   URLTABLEENTRY(DSKUPG, 14, 0, "diskUpdate")
#else /* Placeholder for other products - URL doesn't exist */
   URLTABLEENTRY(DSKUPG, 0, 0, "diskUpdate")
#endif

/* Support related to using Win2K and WinXP's Dynamic Disks on a raw disk */
#if defined(VMX86_DESKTOP)
   URLTABLEENTRY(DYNAMICDISK, 127, 0, "dynamicDisk")
#elif defined(VMX86_WGS)
   URLTABLEENTRY(DYNAMICDISK, 78, 0, "dynamicDisk")   /* used by gsx3 */
#else /* Placeholder for other products - URL doesn't exist */
   URLTABLEENTRY(DYNAMICDISK, 0, 0, "dynamicDisk")
#endif

/*
 * Support related to using CD-ROMs on 2.2.16-[3,22] Linux kernels as a host
 * OS
 */
URLTABLEENTRY(KERNEL_CD, 17, 0, "kernleCD")

/* Support related to MBR */
URLTABLEENTRY(MBR, 18, 0, "mbr")

/* Support related to memory usage issues */
URLTABLEENTRY(MEMORY, 19, 0, "memory")

/* Support related to Microsoft's Windows XP activation as a guest OS */
#if defined(VMX86_DESKTOP)
   URLTABLEENTRY(XP_ACTIVATION, 50, 0, "XPactivation")
#elif defined(VMX86_WGS)
   URLTABLEENTRY(XP_ACTIVATION, 21, 0, "XPactivation")
#elif defined(VMX86_SERVER)
   /* Use generic web site page for ESX Server */
   URLTABLEENTRY(XP_ACTIVATION, 2, 0, "XPactivation")
#else /* Placeholder for other products - URL doesn't exist */
   URLTABLEENTRY(XP_ACTIVATION, 0, 0, "XPactivation")
#endif

/* Support related to using Win2K with a raw disk on a ACPI-capable host */
#if defined(VMX86_DESKTOP)
   URLTABLEENTRY(W2K_ACPI, 128, 0, "W2Kacpi")
#elif defined(VMX86_WGS)
   URLTABLEENTRY(W2K_ACPI, 25, 0, "W2Kacpi")
#else /* Placeholder for other products - URL doesn't exist */
   URLTABLEENTRY(W2K_ACPI, 0, 0, "W2Kacpi")
#endif

/* Support related to using Win2K with a raw disk on a ACPI-capable host */
URLTABLEENTRY(W2K_APM, 26, 0, "W2Kapm")

/* Serial number (or license) obtention/renewal */
#if defined(VMX86_WGS)
   URLTABLEENTRY(LICENSE, 175, 0, "license")
#else
   URLTABLEENTRY(LICENSE, 27, 0, "license")
#endif

/* Pointer to documentation on the current level of support for PAE guest in
 the product. */
URLTABLEENTRY(PAE_SUPPORT, 28, 0, "PAEsupport")

/* Kernel CD-Raw support */
URLTABLEENTRY(KERNEL_CDRAW, 33, 0, "kernelCDraw")

/*
 * Knowledge base article on recompiling the host Linux kernel
 * with a higher HZ setting, to work around cases where /dev/rtc
 * cannot be used.  See PR 19266, 18639, 43074.
 */
URLTABLEENTRY(LINUX_HZ, 34, 0, "linuxHz")

/* P2V - Windows Service Packs */
URLTABLEENTRY(P2V_SRVPACK, 35, 0, "P2Vsrvpack")

/* Support related to WS3 and WS2 to WS4 upgrade issues */
/* Now also overload GSX upgrade issues */
#ifdef _WIN32
   #if defined(VMX86_DESKTOP)
      URLTABLEENTRY(WS4UPGRADE, 38, 0, "WS4upgrade")
   #else
      URLTABLEENTRY(WS4UPGRADE, 79, 0, "WS4upgrade")
   #endif
#else
   #if defined(VMX86_DESKTOP)
      URLTABLEENTRY(WS4UPGRADE, 39, 0, "WS4upgrade")
   #else
      URLTABLEENTRY(WS4UPGRADE, 80, 0, "WS4upgrade")
   #endif
#endif

/* Support related to WS3 and WS2 to WS4 upgrade issues for vms with mixed-mode disks */
#if defined(VMX86_DESKTOP)
   URLTABLEENTRY(WS4MIXEDUPGRADE, 40, 0, "WS4MixedUpgrade")
#else
   URLTABLEENTRY(WS4MIXEDUPGRADE, 81, 0, "WS4MixedUpgrade") /* used by gsx3 */
#endif

/* Promote ESX and VC (VIN) to VMserver users. */
URLTABLEENTRY(UPGRADE_OPTIONS, 176, 0, "upgradeOptions")

/* VM Center of prebuilt, downloadable virtual machines */
URLTABLEENTRY(VM_CENTER, 177, 0, "vmCenter")
URLTABLEENTRY(VM_CENTER_FUSION, 596, 0, "")

/*
 * Support for problems launching through authd in TS on 64-bit Windows.
 */
URLTABLEENTRY(TS_64_LAUNCH_PROB, 154, 0, "ts64LaunchProb")

/*
 * Guest driver download page.
 */
URLTABLEENTRY(GUEST_DRIVER_DOWNLOAD, 43, 0, "guestDriverDownload")

/* Product registration page */
#if defined(VMX86_DESKTOP)
URLTABLEENTRY(CHECK_UPADTE, 47, 0, "checkUpdate")
#else /* Placeholder for products for which this is not relevant */
   URLTABLEENTRY(CHECK_UPADTE, 2, 0, "checkUpdate")
#endif

/* Workarounds for Guest accessing overlapping I/O (PR14552) */
URLTABLEENTRY(IOOVERLAP, 46, 0, "IOOverlap")

/*
 * ESX memory resource management documentation.  Used in error message
 * MSGID(memVmnix.admitFailed) when unable to allocate sufficient memory
 * to power on a VM.  See PR 22763.  Note that for ESX 2.0.x, the proper
 * URL ID is 51, but that for ESX 2.1, the proper ID is 69.  This seems
 * like a very fragile numbering scheme; see PR 39390.
 */
#if defined(VMX86_SERVER)
URLTABLEENTRY(ESX_MEM_ADMIT, 69, 0, "ESXMemAdmit")
#else /* Placeholder for other products - URL doesn't exist */
URLTABLEENTRY(ESX_MEM_ADMIT, 0, 0, "ESXMemAdmit")
#endif

/*
 * Product web update IDs
 *
 * build 6802  (Workstation 4.1.0 Beta 2)       56, 58
 * build 6809  (GSX 3.0.0 Beta 2)               57, 59
 * build 6979  (Workstation 4.5.0 RC1)          56, 58
 * build 7070  (Workstation 4.5.0 RC2)          60, 62
 * build 7126  (GSX 3.0.0 Beta 3)               61, 63
 * build 7174  (Workstation 4.5.0 Release)      64, 66
 * build 7234  (GSX 3.0.0 Beta 4)               65, 67
 * build 7257  (GSX 3.0.0 RC1)                  72, 73
 * build 7503  (GSX 3.0.0 RC2)                  74, 75
 * build 7568  (Workstation 4.5.1 Release)      70, 71
 * build 7885  (GSX 3.0.0 Release)              76, 77
 * build 8061  (Workstation 4.5.2 beta)         83, 84
 * build 8130  (GSX 3.1 beta)                   85, 86
 * build 8465  (GSX 3.1 RC1)                    89, 90
 * build 8848  (Workstation 4.5.2 Release)      87, 88
 * build 9089  (GSX 3.1 Release)                93, 94
 * build 9336  (WSEE Lithium 1.0 F&F)           87, 88
 * build 9882  (WSEE Lithium beta 1)            91, 92
 * build 10152 (WSEE Lithium beta 2)            95, 96
 * build 10131 (Workstation 5.0 F&F)            101, 102
 * build 10628 (WSEE Lithium beta 3)            103, 104
 * build 10737 (Workstation 5.0 beta1)          105, 106
 * build 10833 (WSEE Lithium RC1)               107, 108
 * build 11346 (WSEE Lithium RC2)               111, 112
 * build 11608 (Workstation 5.0 beta2)          113, 114
 * build 11627 (WSEE Lithium GA)                115, 116
 * build 12206 (Workstation 5.0 RC1)            117, 118
 * build 12544 (Workstation 5.0 RC2)            119, 120
 * build 12888 (Workstation 5.0 RC3)            121, 122
 * build 13124 (Workstation 5.0 GA)             123, 124
 * build 13451 (GSX 3.2 beta1)                  125, 126
 * build 13836 (GSX 3.2 RC1)                    129, 130
 * build 14497 (GSX 3.2 GA)                     131, 132
 * build 14166 (WSEE ACE 1.0.1 beta1)           133, 134
 * build 14573 (WSEE ACE 1.0.1 RC1)             135, 136
 * build 14996 (WSEE ACE 1.0.1 GA)              137, 138
 * build 14704 (Workstation 5.5 F&F)            139, 140
 * build 15576 (Workstation 5.5 beta1)          141, 142
 * build 16325 (Workstation 5.5 beta2)          143, 144
 * build 16954 (Workstation 5.1 RC1)            145, 146
 * build 18007 (Workstation 5.5 RC2)            147, 148
 * build 18463 (Workstation 5.5 GA)             149, 150
 * build 16981 (VMplayer 1.0 beta1)             155, 156
 * build 18007 (VMplayer 1.0 RC)                157, 158
 * build 18587 (VMplayer 1.0 RTM)               159, 160
 * build 19175 (Workstation 5.5.1)              163, 164
 * build 19317 (VMplayer 1.0.1)                 165, 166
 * build 19206 (WSEE ACE 1.0.2)                 167, 168
 * build 19281 (GSX 3.2.1)                      169, 170
 * build 19414 (Workstation 4.5.3)              171, 172
 * build 20923/20925 (VMware Server 1.0 beta1)  173, 174
 * build 22874 (VMware Server 1.0 beta2)        179, 180
 * build 23869 (VMware Server 1.0 beta3)        181, 182
 * build 24927 (VMware Server 1.0 RC1)          183, 184
 * build 27828 (VMware Server 1.0 RC2)          185, 186
 * build 28343 (VMware Server 1.0 GA)           187, 188
 * build 29772 (Workstation 5.5.2 GA)           197, 198
 * build 29634 (VMplayer 1.0.2 GA)              199, 200
 * build 29996 (VMware Server 1.0.1 GA)         210, 211
 * build 32652 (VMware Fusion F&F)              213, 214
 * build 32740 (Workstation 6.0 F&F)            218, 219
 * build 32740 (VMplayer 2.0 F&F)               220, 221
 * build 33975 (Workstation 6.0 Beta1)          222, 223
 * build 33975 (VMplayer 2.0 Beta1)             224, 225
 * build 36983 (Workstation 6.0 Beta2)          226, 227
 * build 36983 (VMplayer 2.0 Beta2)             228, 229
 * build 36932 (VMware Fusion Beta 1)           233, 234
 * build 39849 (Workstation 6.0 Beta3)          254, 255
 * build 40211 (WSEE ACE 6.0 Beta2)             256, 257
 * build 39849 (VMplayer 2.0 Beta3)             258, 259
 * build 42757 (Workstation 6.0 RC1)            260, 261
 * build 42757 (WSEE ACE 6.0 Beta3)             262, 263
 * build 42757 (VMplayer 2.0 RC1)               264, 265  <-- Externally known as Beta1
 * build 44426 (Workstation 6.0 RC2)            266, 267
 * build 44426 (WSEE ACE 6.0 RC1)               268, 269
 * build 44426 (VMplayer 2.0 RC2)               270, 271  <-- Externally known as Beta2
 * build 41385 (VMware Fusion Beta 2)           279, 280
 * build 45731 (Workstation 6.0 RTM)            272, 273
 * build 45731 (WSEE ACE 6.0 RTM)               274, 275
 * build 45731 (VMplayer 2.0 RTM)               276, 277

 * build 43733 (VMware Fusion Beta 3)           281, 282
 * build 48339 (VMware Fusion Beta 4)           283, 284
 * build 50460 (VMware Fusion RC1)              285, 286
 * build 51348 (VMware Fusion RTM)              287, 288
 * build 57919 (VMware Fusion 1.1 Beta)         449, 455
 * build 61385 (VMware Fusion 1.1 RC)           463, 464
 * build 62573 (VMware Fusion 1.1 GA)           471, 472
 * build 72241 (VMware Fusion 1.1.1 GA)         495, 496
 * build 87978 (VMware Fusion 1.1.2 GA)         529, 530
 * build 89933 (VMware Fusion 2.0 Beta1)        536, 535
 * build TBA   (VMware Fusion 2.0 Beta2)        534, 535

 * build 84113 (Workstation 6.5 Beta1)          505, 506
 * build 84113 (VMplayer 2.5 Beta1)             513, 514
 * build 84113 (WSEE ACE 2.5 Beta1)             509, 510
 * build 91182 (Workstation 6.5 Beta1 Refresh)  557, 558
 * build 91182 (VMplayer 2.5 Beta1 Refresh)     562, 563
 * build 91182 (WSEE ACE 2.5 Beta1 Refresh)     560, 561
 * build 99530 (Workstation 6.5 Beta2)          588, 589
 * build 99530 (VMplayer 2.5 Beta2)             594, 595
 * build 99530 (WSEE ACE 2.5 Beta2)             592, 593
 * build TBA   (Workstation 6.5 RC1)            712, 713
 * build TBA   (VMplayer 2.5 RC1)               718, 719
 * build TBA   (WSEE ACE 2.5 RC1)               716, 717
 */

/* Product web update check info page */
URLTABLEENTRY(WS_UPDATE_INFO, 712, 0, "WSUpdateInfo")
URLTABLEENTRY(ACE_UPDATE_INFO, 716, 0, "ACEUpdateInfo")
URLTABLEENTRY(GSX_UPDATE_INFO, 210, 0, "GSXUpdateInfo")
URLTABLEENTRY(PLAYER_UPDATE_INFO, 718, 0, "PlayerUpdateInfo")
URLTABLEENTRY(SHOCKWAVE_UPDATE_INFO, 534, 0, "ShockwaveUpdateInfo")

/* Product web update download page */
URLTABLEENTRY(WS_UPDATE_DOWNLOAD, 713, 0, "WSUpdateDownload")
URLTABLEENTRY(ACE_UPDATE_DOWNLOAD, 717, 0, "ACEUpdateDownload")
URLTABLEENTRY(GSX_UPDATE_DOWNLOAD, 211, 0, "GSXUpdateDownload")
URLTABLEENTRY(PLAYER_UPDATE_DOWNLOAD, 719, 0, "PlayerUpdateDownload")
URLTABLEENTRY(SHOCKWAVE_UPDATE_DOWNLOAD, 535, 0, "ShockwaveUpdateDownload")

/*
 * Knowledge base article describing workaround to make sure the guest clock
 * runs correctly on machines with SpeedStep enabled.
 * See PR 31366, KB article 1227.
 */
URLTABLEENTRY(CPU_SPEED_MISMATCH, 97, 0, "cpuSpeedMismatch")

/* Virtual hardware upgrade information. */
URLTABLEENTRY(VIRTUALHW_UPGRADE, 98 , 0, "virtualHWUpgrade")

/* Tools update/installation information. */
URLTABLEENTRY(TOOLS_UPDATE, 99 , 0, "toolsUpdate")

/*
 * Intermediate upgrade information, explains how to get an older version of
 * our product to upgrade an unsupported VM in two steps.
 */
URLTABLEENTRY(INTERMEDIATE_UPGRADE, 100 , 0, "intermediateUpgrade")

/*
 * Player -> Workstation upgrade advertising page
 */
URLTABLEENTRY(PLAYER_WS_UPGRADE_INFO, 151, 0, "PlayerWSUpgradeInfo")
URLTABLEENTRY(PLAYER_WS_UPGRADE_INFO_JA, 438, 0, "PlayerWSUpgradeInfo-ja")

/* Info about 64-bit guest support */
URLTABLEENTRY(LONGMODE_SUPPORT, 152, 0, "longmodeSupportInfo")

/* Information about Web Update problems */
URLTABLEENTRY(WEB_UPDATE_PROXY_ERROR, 153, 0, "webUpdateProxyError")

/* Information about promiscuous mode being denied on Linux host */
URLTABLEENTRY(PROMISCUOUS_DENIED, 161, 0, "promiscuousDenied")

/* Buy support for VMware Server. */
URLTABLEENTRY(BUY_SUPPORT, 178, 0, "buySupport")

/* Online product documentation. */
URLTABLEENTRY(GSX_ONLINE_DOCUMENATATION, 195, 0, "GSXOnlineDocumentation")
URLTABLEENTRY(SHOCKWAVE_DOCUMENTATION, 215, 0, "ShockwaveOnlineDocumentation")
URLTABLEENTRY(WS_ONLINE_DOCUMENTATION, 710, 0, "WSOnlineDocumentation")
URLTABLEENTRY(WS_ACE_ONLINE_DOCUMENTATION, 714, 0, "WSACEOnlineDocumentation")
URLTABLEENTRY(WS_ONLINE_GUESTOS_INSTALLATION_GUIDE, 238, 0, "WSOnlineGuestOSInstallationGuide")

/* VMTN forums for products. */
URLTABLEENTRY(SHOCKWAVE_FORUMS, 249, 0, "ShockwaveForums")
URLTABLEENTRY(SHOCKWAVE_BETA_FORUMS, 555, 0, "ShockwaveBetaForums")

/* vTunes in Player. */
URLTABLEENTRY(PLAYER_VTUNES_LINUX, 290, 0, "vTunesLinux")
URLTABLEENTRY(PLAYER_VTUNES_WIN_EN, 291, 0, "vTunesWindowsEnglish")
URLTABLEENTRY(PLAYER_VTUNES_WIN_JP, 253, 0, "vTunesWindowsJapanese")

/* Buy ACE Client License for Player. */
URLTABLEENTRY(PLAYER_ACE_CLIENT_LICENSE, 292, 0, "aceClientLicense")
URLTABLEENTRY(PLAYER_ACE_CLIENT_LICENSE_JA, 439, 0, "aceClientLicense-ja")

/* Configure the pocket ACE cache. */
URLTABLEENTRY(PLAYER_POCKET_ACE_CACHE, 301, 0, "pocketACESessionDirectory")

/* Online product URL for JA. */
URLTABLEENTRY(VMWARE_JA, 421, 0, "vmware-ja")
URLTABLEENTRY(SUPPORT_JA, 422, 0, "support-ja")
URLTABLEENTRY(PRODUCTS_JA, 423, 0, "products-ja")
URLTABLEENTRY(LICENSE_JA, 424, 0, "license-ja")
URLTABLEENTRY(REGISTERNOW_JA, 425, 0, "registerNow-ja")
URLTABLEENTRY(REPORT_JA, 426, 0, "report-ja")

/* Online product documentation for JA. */
URLTABLEENTRY(WS_ONLINE_DOCUMENTATION_JA, 711, 0, "WSOnlineDocumentation-ja")
URLTABLEENTRY(WS_ACE_ONLINE_DOCUMENTATION_JA, 715, 0, "WSACEOnlineDocumentation-ja")
URLTABLEENTRY(WS_ONLINE_GUESTOS_INSTALLATION_GUIDE_JA, 437, 0, "WSOnlineGuestOSInstallationGuide-ja")

URLTABLEENTRY(APPLIANCE, 448, 0, "appliance")
URLTABLEENTRY(APPLIANCE_JA, 441, 0, "appliance-ja")

/* Fusion Webpages */
URLTABLEENTRY(FUSION_COMMUNITY, 720, 0, "")
URLTABLEENTRY(FUSION_HOME_PAGE, 721, 0, "")
URLTABLEENTRY(FUSION_SUPPORT, 722, 0, "")

/* Fusion Registration and Buy Now URLs. */
URLTABLEENTRY(FUSION_REGISTRATION, 992, 0, "FusionRegistration")
URLTABLEENTRY(FUSION_VIDEO_TUTORIAL, 723, 0, "FusionVideoTutorial")

/* Fusion: Parallels Blue Pill */
URLTABLEENTRY(BLUE_PILL, 549, 0, "")

/* Fusion: migrate PC */
URLTABLEENTRY(FUSION_MIGRATE_PC_TUTORIAL, 700, 0, "FusionMigratePCTutorial")
