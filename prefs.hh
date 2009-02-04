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
 * prefs.hh --
 *
 *    Preferences management.
 */

#ifndef PREFS_HH
#define PREFS_HH


#include <vector>


#include "util.hh"

extern "C" {
#include "dictionary.h"
}


namespace cdk {


class Prefs
{
public:
   Prefs();
   ~Prefs();

   static Prefs *GetPrefs();

   std::vector<Util::string> GetBrokerMRU() const;
   void AddBrokerMRU(Util::string first);

   Util::string GetDefaultBroker() const;
   void SetDefaultBroker(Util::string val);

   Util::string GetDefaultUser() const;
   void SetDefaultUser(Util::string val);

   Util::string GetDefaultDomain() const;
   void SetDefaultDomain(Util::string val);

private:
   static Prefs *sPrefs;

   Util::string GetString(Util::string key, Util::string defaultVal = "") const;
   bool GetBool(Util::string key, bool defaultVal = false) const;
   int32 GetInt(Util::string key, int32 defaultVal = 0) const;

   void SetString(Util::string key, Util::string val);
   void SetBool(Util::string key, bool val);
   void SetInt(Util::string key, int32 val);

   Dictionary *mDict;
   Util::string mPrefPath;
};


} // namespace cdk


#endif // PREFS_HH
