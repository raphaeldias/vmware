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
 * procHelper.hh --
 *
 *    Child process helper.
 */

#ifndef PROC_HELPER_HH
#define PROC_HELPER_HH


#include <boost/signal.hpp>
#include <vector>


#include "util.hh"


namespace cdk {


class ProcHelper
{
public:
   ProcHelper();
   virtual ~ProcHelper();

   void Start(Util::string procName, Util::string procPath,
              std::vector<Util::string> args = std::vector<Util::string>(),
              Util::string stdIn = "", int skipFd1 = 0, int skipFd2 = 0);
   void Kill();

   bool IsRunning() const { return mPid > -1; }
   pid_t GetPID() const { return mPid; }

   boost::signal1<void, int> onExit;
   boost::signal1<void, Util::string> onErr;

private:
   static void OnErr(void *data);

   void ResetProcessState(int stdIn, int stdOut, int stdErr,
                          int skipFd1, int skipFd2) const;

   Util::string mProcName;
   pid_t mPid;
   int mErrFd;
   Util::string mErrPartialLine;
};


} // namespace cdk


#endif // PROC_HELPER_HH
