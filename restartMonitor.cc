/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
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
 * restartMonitor.cc --
 *
 *      Utility class to limit some action to a number of times in
 *      some duration of time - a throttle.
 */


#include "restartMonitor.hh"


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RestartMonitor::RestartMonitor --
 *
 *      Initializes fields.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      RestartMonitor is initialized.
 *
 *-----------------------------------------------------------------------------
 */

RestartMonitor::RestartMonitor(int restartsAllowed, // IN
                               time_t threshold)    // IN
   : mRestarts(0),
     mRestartsAllowed(restartsAllowed),
     mStartTime(0),
     mThreshold(threshold)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RestartMonitor::ShouldThrottle --
 *
 *      Check if this has been called more than mRestartsAllowed times
 *      in the last mThreshold seconds.
 *
 * Results:
 *      true if this has been called too many times.
 *
 * Side effects:
 *      May reset if threshold time has been reached.
 *
 *-----------------------------------------------------------------------------
 */

bool
RestartMonitor::ShouldThrottle()
{
   if (time(NULL) - mStartTime > mThreshold) {
      Reset();
      return false;
   }
   return ++mRestarts >= mRestartsAllowed;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::RestartMonitor::Reset --
 *
 *      Clear results of past ShouldThrottle() calls.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Reset to initial state.
 *
 *-----------------------------------------------------------------------------
 */

void
RestartMonitor::Reset()
{
   mStartTime = time(NULL);
   mRestarts = 0;
}


} // namespace cdk
