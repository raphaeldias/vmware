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
 * restartMonitor.hh --
 *
 *      Utility class to limit some action to a number of times in
 *      some duration of time - a throttle.
 */


#ifndef RESTART_MONITOR_HH
#define RESTART_MONITOR_HH


#include <time.h>


namespace cdk {


class RestartMonitor
{
public:
   static const int DEFAULT_RESTARTS_ALLOWED = 2;
   static const time_t DEFAULT_THRESHOLD = 60;

   RestartMonitor(int restartsAllowed = DEFAULT_RESTARTS_ALLOWED,
                  time_t threshold = DEFAULT_THRESHOLD);
   ~RestartMonitor() { }
   bool ShouldThrottle();
   void Reset();

private:
   int mRestarts;
   int mRestartsAllowed;
   time_t mStartTime;
   time_t mThreshold;
};


} // namespace cdk


#endif // RESTART_MONITOR_HH
