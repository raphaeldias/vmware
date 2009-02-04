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
 * prefs.cc --
 *
 *    Preferences management.
 */


extern "C" {
#include <glib/gutils.h> // For g_get_user_name
}


#include "prefs.hh"

extern "C" {
#include "util.h"
}


#define VMWARE_HOME_DIR "~/.vmware"
#define PREFERENCES_FILE_NAME VMWARE_HOME_DIR"/view-preferences"
#define MAX_BROKER_MRU 10


namespace cdk {


/*
 * Initialise static data.
 */

Prefs *Prefs::sPrefs = NULL;


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::Prefs --
 *
 *      Constructor.  Initialize and load the preferences Dictionary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Prefs::Prefs()
{
   mPrefPath = Util_ExpandString(PREFERENCES_FILE_NAME);
   ASSERT(!mPrefPath.empty());

   mDict = Dictionary_Create();
   ASSERT_MEM_ALLOC(mDict);

   Msg_Reset(true);
   if (!Util_MakeSureDirExistsAndAccessible(Util_ExpandString(VMWARE_HOME_DIR),
                                            0755)) {
      Util::UserWarning("Creating ~/.vmware failed: %s\n",
                        Msg_GetMessagesAndReset());
   }

   // This may fail if the file doesn't exist yet.
   Dictionary_Load(mDict, mPrefPath.c_str(), DICT_NOT_DEFAULT);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::~Prefs --
 *
 *      Destructor.  Destroy the preferences Dictionary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Set sPrefs to NULL if this is the default Prefs instance.
 *
 *-----------------------------------------------------------------------------
 */

Prefs::~Prefs()
{
   Dictionary_Free(mDict);

   if (sPrefs == this) {
      sPrefs = NULL;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetPrefs --
 *
 *      Static accessor for the default Prefs instance.
 *
 * Results:
 *      Prefs instance.
 *
 * Side effects:
 *      sPrefs may be initialized with a new Prefs instance.
 *
 *-----------------------------------------------------------------------------
 */

Prefs *
Prefs::GetPrefs()
{
   if (!sPrefs) {
      sPrefs = new Prefs();
   }
   return sPrefs;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetString --
 *
 *      Private helper to wrap Dict_GetString.
 *
 * Results:
 *      The preference value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Prefs::GetString(Util::string key,        // IN
                 Util::string defaultVal) // IN
   const
{
   char *val = Dict_GetString(mDict, defaultVal.c_str(), key.c_str());
   Util::string retVal = val;
   free(val);
   return retVal;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetBool --
 *
 *      Private helper to wrap Dict_GetBool.
 *
 * Results:
 *      The preference value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

bool
Prefs::GetBool(Util::string key,  // IN
               bool defaultVal)  // IN
   const
{
   return Dict_GetBool(mDict, defaultVal, key.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetInt --
 *
 *      Private helper to wrap Dict_GetInt.
 *
 * Results:
 *      The preference value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int32
Prefs::GetInt(Util::string key,  // IN
              int32 defaultVal) // IN
   const
{
   return Dict_GetLong(mDict, defaultVal, key.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetString --
 *
 *      Private helper to wrap Dict_SetString & Dictionary_Write.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes the preferences file.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetString(Util::string key, // IN
                 Util::string val) // IN
{
   Dict_SetString(mDict, val.c_str(), key.c_str());
   Dictionary_Write(mDict, mPrefPath.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetBool --
 *
 *      Private helper to wrap Dict_SetBool & Dictionary_Write.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes the preferences file.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetBool(Util::string key, // IN
               bool val)        // IN
{
   Dict_SetBool(mDict, val, key.c_str());
   Dictionary_Write(mDict, mPrefPath.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetInt --
 *
 *      Private helper to wrap Dict_SetLong & Dictionary_Write.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes the preferences file.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetInt(Util::string key, // IN
              int32 val)       // IN
{
   Dict_SetLong(mDict, val, key.c_str());
   Dictionary_Write(mDict, mPrefPath.c_str());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetBrokerMRU --
 *
 *      Accessor for the broker MRU list stored in the preferences.
 *      These are the view.broker0-10 keys.
 *
 * Results:
 *      List of brokers.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
Prefs::GetBrokerMRU()
   const
{
   std::vector<Util::string> brokers;

   for (int brokerIdx = 0; brokerIdx <= MAX_BROKER_MRU; brokerIdx++) {
      Util::string key = Util::Format("view.broker%d", brokerIdx);
      Util::string val = GetString(key);
      if (!val.empty()) {
         brokers.push_back(val);
      }
   }

   return brokers;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::AddBrokerMRU --
 *
 *      Add a broker name as the view.broker0 preference key.  Rewrites the
 *      view.broker1-10 keys to remove the broker name to avoid duplicates.
 *
 *      XXX: Should pause preference writing until the end?  Maybe this is
 *      unneccessary if Dictionary_Write pauses automatically.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::AddBrokerMRU(Util::string first) // IN
{
   // Get current broker MRU so we don't overwrite the first one
   std::vector<Util::string> brokers = GetBrokerMRU();

   SetString("view.broker0", first);

   unsigned int brokerIdx = 1;
   for (std::vector<Util::string>::iterator i = brokers.begin();
        i != brokers.end() && brokerIdx <= MAX_BROKER_MRU; i++) {
      if (*i != first) {
         SetString(Util::Format("view.broker%d", brokerIdx++), *i);
      }
   }
   while (brokerIdx < brokers.size()) {
      SetString(Util::Format("view.broker%d", brokerIdx++), "");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetDefaultBroker --
 *
 *      Return the view.defaultBroker key, to be used instead of prompting for
 *      a broker.
 *
 * Results:
 *      The view.defaultBroker key if the view.allowDefaultBroker key is
 *      not set to FALSE, otherwise "".
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Prefs::GetDefaultBroker()
   const
{
   if (GetBool("view.allowDefaultBroker", true)) {
      return GetString("view.defaultBroker");
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetDefaultBroker --
 *
 *      Sets the view.defaultBroker preference key, to be used instead of
 *      prompting for a broker.  Only set if the view.allowDefaultBroker pref is
 *      not FALSE.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetDefaultBroker(Util::string val) // IN
{
   if (GetBool("view.allowDefaultBroker", true)) {
      SetString("view.defaultBroker", val);
   } else {
      Log("Not saving the default broker (view.allowDefaultBroker=false).\n");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetDefaultUser --
 *
 *      Return the view.defaultUser key, to be used as the default user for all
 *      brokers.  If no default user is set, the result of g_get_user_name() is
 *      returned.
 *
 * Results:
 *      The default username if the view.allowDefaultUser key is
 *      not set to FALSE, otherwise "".
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Prefs::GetDefaultUser()
   const
{
   if (GetBool("view.allowDefaultUser", true)) {
      Util::string ret = GetString("view.defaultUser");
      if (ret.empty()) {
         ret = g_get_user_name();
         if (ret == "root") {
            ret = "";
         }
      }
      return ret;
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetDefaultUser --
 *
 *      Sets the view.defaultUser preference key.  Only set if the
 *      view.allowDefaultUser pref is not FALSE.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetDefaultUser(Util::string val) // IN
{
   if (GetBool("view.allowDefaultUser", true)) {
      SetString("view.defaultUser", val);
   } else {
      Log("Not saving the default user (view.allowDefaultUser=false).\n");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::GetDefaultDomain --
 *
 *      Return the view.defaultDomain key, to be used as the default domain for
 *      all brokers.
 *
 * Results:
 *      The default username if the view.allowDefaultDomain key is
 *      not set to FALSE, otherwise "".
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Prefs::GetDefaultDomain()
   const
{
   if (GetBool("view.allowDefaultDomain", true)) {
      return GetString("view.defaultDomain");
   }
   return "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Prefs::SetDefaultDomain --
 *
 *      Sets the view.defaultDomain preference key.  Only set if the
 *      view.allowDefaultDomain pref is not FALSE.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
Prefs::SetDefaultDomain(Util::string val) // IN
{
   if (GetBool("view.allowDefaultDomain", true)) {
      SetString("view.defaultDomain", val);
   } else {
      Log("Not saving the default domain (view.allowDefaultDomain=false).\n");
   }
}


} // namespace cdk
