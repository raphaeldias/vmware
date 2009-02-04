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
 *  logAux.c --
 *
 *	Logging code used for debugging.
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <ctype.h>            // For isprint()

#include "vmware.h"
#include "hostinfo.h"
#include "log.h"


/*
 *-----------------------------------------------------------------------------
 *
 * Log_HexDump --
 *
 *      Log a bunch of bytes in hex and ascii.
 *
 * Results:
 *      Log.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Log_HexDump(const char *prefix,	// IN: prefix for each log line
	    const uint8 *data,	// IN: data to log
	    int size)		// IN: number of bytes
{
   int i, j;

   for (i = 0; i < size;) {
      char hex[16 * 3 + 1];
      char ascii[16 + 1];
      memset(hex, ' ', sizeof hex - 1);
      hex[sizeof hex - 1] = 0;
      memset(ascii, ' ', sizeof ascii - 1);
      ascii[sizeof ascii - 1] = 0;
      for (j = 0; j < 16 && i < size; j++, i++) {
	 uint8 c = data[i];
	 hex[j * 3 + 0] = "0123456789abcdef"[c >> 4];
	 hex[j * 3 + 1] = "0123456789abcdef"[c & 0xf];
	 ascii[j] = isprint(c) ? c : '.';
      }
      Log("%s %03x: %s%s\n", prefix, i - j, hex, ascii);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Log_Time --
 *
 *      Measure and log elapsed real time.
 *
 *      Call with count == 0 to start timing (log message if desired).
 *      Call with count > 0 to compute and log elapsed time,
 *      and to start the next period.
 *
 * Results:
 *      Current time saved in *time.
 *      Log.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Log_Time(VmTimeRealClock *time,
         int count,
         const char *message)
{
   VmTimeRealClock now;
   uint32 elapsed;

   now = Hostinfo_SystemTimerUS();

   elapsed = (now - *time);
   *time = now;

   if (count == 0) {
      if (message != NULL) {
	 Log("%s: start timing\n", message);
      }

   } else if (count == 1) {
      Log("%s: elapsed time %uus\n", message, elapsed);

   } else {
      Log("%s: elapsed time %uus / %d = %.2fus\n",
	  message, elapsed, count, (double) elapsed / count);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Log_Histogram --
 *
 *      Mange and log a base-2 logorithmic histogram.
 *
 * Results:
 *      Histogram updated.
 *      Log.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Log_Histogram(uint32 n,
              uint32 histo[],
              int nbuckets,
              const char *message,
              int *count,
              int limit)
{
   int lg;
   int i;

   if (*count == 0) {
      memset(histo, 0, sizeof *histo * nbuckets);
   }

   /*
    * Compute lg(n), with a sort of binary search.
    */

   lg = 0;
   for (i = 16; i > 0; i >>= 1) {
      if (n >= 1 << (lg + i)) {
	 lg += i;
      }
   }
   histo[MIN(lg, nbuckets - 1)]++;

   if (++*count >= limit) {
      for (i = 0; i < nbuckets; i += 4) {
	 #define X(n) 1 << (n), histo[n]
	 switch (nbuckets - i) {
	 case 1:
	    Log("%s: %8u,%-5d\n", message, X(i));
	    break;
	 case 2:
	    Log("%s: %8u,%-5d %8u,%-5d\n", message, X(i), X(i + 1));
	    break;
	 case 3:
	    Log("%s: %8u,%-5d %8u,%-5d %8u,%-5d\n",
		message, X(i), X(i + 1), X(i + 2));
	    break;
	 default:
	    Log("%s: %8u,%-5d %8u,%-5d %8u,%-5d %8u,%-5d\n",
		message, X(i), X(i + 1), X(i + 2), X(i + 3));
	 }
	 #undef X
      }
      *count = 0;
   }
}
