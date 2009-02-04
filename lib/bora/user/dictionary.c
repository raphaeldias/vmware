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
 * Dictionary Parameter Processing
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "safetime.h"

#include "vmware.h"
#include "productState.h"
#include "dictionary.h"
#include "msg.h"
#include "str.h"
#include "strutil.h"
#include "vm_version.h"
#include "log.h"
#include "hashTable.h"
#include "dictll.h"
#include "util.h"
#include "base64.h"
#include "dynbuf.h"
#include "crypto.h"
#include "keySafe.h"
#include "posix.h"
#include "fileIO.h"
#include "fileLock.h"
#include "codeset.h"
#include "unicodeTypes.h"
#include "unicodeOperations.h"

#define LOGLEVEL_MODULE dict
#include "loglevel_user.h"


/*
 * Data structures
 */

#define HASHTABLE_SIZE  512

#define BIG_NAME_SIZE   1024

typedef struct WriteLine WriteLine;

/* Value of an entry. */
typedef union Value {
   char *stringValue;
   Bool boolValue;
   int32 longValue;
   int64 i64Value;
   double doubleValue;
} Value;

/* A name-value pair with some auxiliary data. */
typedef struct Entry {
   char *name;
   DictionaryType type;
   Value value;
   Bool modified;
   Bool written;
   int defaultLevel;
   Bool versionSpec;
   Bool dontEncrypt;
   char *convBuffer;
   WriteLine *line;
   struct Entry *next;
} Entry;

/* Maintains the order in which entries are written to a file. */
struct WriteLine {
   char *string;
   Entry *entry;
   struct WriteLine *next;
};

/* A dictionary. */
struct Dictionary
{
   Entry *firstEntry;
   Entry *lastEntry;
   WriteLine *firstWriteLine;
   WriteLine *lastWriteLine;
   Unicode currentFile;
   int currentLine;
   Bool needSaveForSure;
   char oldExecFlags[100];
   HashTable *hashtable;
   KeySafe *keySafe;
   CryptoKey *key;
   int numEntries;
   StringEncoding encoding;
};

/*
 *  Only the address of this variable is taken,
 *  and the variable is never modified. I think
 *  it's OK not to make it thread specific -leb
 */

void *dictionaryNoDefault;  

// recommended buffer size for DictionaryConvertValueToString()
#define CONV_BUFFER_SIZE 32

/*
 * Variables that begin with . are reserved by the dictionary
 * module.
 */

#define METAVAR_PREFIX '.'
#define METAVAR_ENCODING ".encoding"


/*
 * Local functions
 */

static Entry *DictionaryFindEntry(Dictionary *dict, const char *name);
static Entry *DictionarySanitizePlaintextEntry(Dictionary *dict, Entry *e,
                                               DictionaryType typeRequested);
static Entry *DictionaryAddEntry(Dictionary *dict,  const char *name,
				 int defaultLevel,
				 const void *pvalue, 
				 DictionaryType type,
				 Bool copyStrings);
static void DictionaryModifyEntry(Entry *e, const void *value,
                                  DictionaryType newType, int defaultLevel,
                                  Bool forceModified);
static int DictionaryStringToTriState(const char *s, Bool *error);
static void DictionaryAppendWriteLine(Dictionary *dict, WriteLine *line);
static void DictionaryAddWriteLine(Dictionary *dict, char *string, Entry *e,
				   Bool atEnd);

static Bool DictionaryLoad(Dictionary *dict,
                           ConstUnicode pathName,
                           int defaultLevel,
                           Bool clearDictionary,
                           StringEncoding defaultEncoding);
static Bool DictionaryLoadFromBuffer(Dictionary *dict, const char *buffer,
                                     int defaultLevel, Bool append,
                                     StringEncoding defaultEncoding);

static Bool DictionaryLoadFile(Dictionary *dict, FILE *file, int defaultLevel);
static int DictionaryParseReadLine(Dictionary *dict, char *whole,
                                   char *name, char *value, int defaultLevel);
static int DictionaryWriteEntry(Dictionary *dict, Entry *e, DynBuf *output);
static Entry *DictionaryInternalSetFromString(Dictionary *dict,
					      const char *string,
					      int defaultLevel,
					      Bool preventRedefinition,
					      Bool setModified);
static void DictionaryNarrow(Entry *e, DictionaryType type);
static void DictionaryNarrowValue(const char *name, char *s,
				  DictionaryType type, Value *value);

static void DictionaryMarshallEx(Dictionary *dict, unsigned char **buffer,
			         size_t *size, int excludeFilter);

static void DictionaryRemoveIf(Dictionary *dict,
                               Bool (*predicate) (Entry *, void *aux),
                               void *aux);
static Bool HasGivenName(Entry *entry, void *name_);
static Bool HasGivenPrefix(Entry *entry, void *prefix_);
static Bool BacksEncryptedEnvelope(Entry *entry, void *name_);
static int DictionaryWriteToBuffer(Dictionary *dict, Bool enableEncrypt,
                                   char **outbuf, size_t *outsize);
static Bool DictionaryUseEncoding(Dictionary *dict, const char *encodingName,
                                  StringEncoding defaultEncoding);
static void DictionaryEncodingError(Dictionary *dict,
                                    const char *name, const char *value,
			            StringEncoding encoding);


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Create --
 *
 *      Creates a new dictionary.
 *
 * Results:
 *      The new dictionary.
 *
 * Side effects:
 *	The caller must destroy the dictionary wtih Dictionary_Free() when it
 *	is done with it.
 *
 *-----------------------------------------------------------------------------
 */

Dictionary *
Dictionary_Create(void)
{
   Dictionary *dict;

   dict = Util_SafeCalloc(1, sizeof *dict);

   dict->hashtable = HashTable_Alloc(HASHTABLE_SIZE, HASH_ISTRING_KEY, NULL);
   dict->encoding = STRING_ENCODING_UNKNOWN;

   return dict;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Free --
 *
 *      Destroys the given dictionary.
 *
 * Results:
 *      None
 *
 * Side effects:
 *	Destroys the given dictionary.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_Free(Dictionary *dict)
{
   if (!dict) {
      return;
   }

   Dictionary_Clear(dict);

   HashTable_Free(dict->hashtable);

   free(dict);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryNarrow --
 * 
 *      Sets the type of an entry to a new type (the "target type").  The
 *      entry's type must be previously unknown (DICT_ANY), and the target type
 *      must be a known type (not DICT_ANY).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Sets the entry's type.  Parses the entry's string value as the target
 *	type, which may produce an error message if the string is not of the
 *	correct form.  If the target type is not string, discards the entry's
 *	string value,
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryNarrow(Entry *e,              // IN: entry to narrow
                 DictionaryType type)   // IN: new type for entry
{
   char *s = e->value.stringValue;

   ASSERT(e->type == DICT_ANY && type != DICT_ANY);

   e->type = type;
   DictionaryNarrowValue(e->name, s, type, &e->value);
   if (type != DICT_STRING) {
      free(s);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryNarrowValue --
 * 
 *      Parses a string according to the specified type and sets the proper
 *      member of value accordingly.  
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets one of the members of *value.
 *      If the string does not parse correctly for the given type, emits an
 *      error message naming the specified variable.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryNarrowValue(const char *name,		// IN: name for error message
                      char *s,			// IN: value to convert
                      DictionaryType type,	// IN: type
                      Value *value)		// OUT: converted value
{
   Bool error;
   char *p;

   switch (type) {

   case DICT_STRING:
      value->stringValue = s;
      break;

   case DICT_BOOL:
      value->boolValue = Dictionary_StringToBool(s, &error);
      if (error) {
	 Msg_Post(MSG_ERROR, MSGID(dictionary.notBoolean)
                  "Value \"%s\" for variable \"%s\" "
		  "is not a valid boolean value. "
                  "Using value \"%s\".\n", s, name,
                  value->boolValue ? "TRUE" : "FALSE");
      }
      break;

   case DICT_TRISTATE:
      value->longValue = DictionaryStringToTriState(s, &error);
      if (error) {
	 Msg_Post(MSG_ERROR, MSGID(dictionary.notTristate)
                  "Value \"%s\" for variable \"%s\" "
		  "is not a valid tristate value. "
                  "Using value \"%s\".\n", s, name,
                  value->longValue == 0
                  ? "FALSE"
                  : (value->longValue == 1
                     ? "TRUE"
                     : "default"));
      }
      break;

   case DICT_LONG:
      /*
       * Just strtoul() may be sufficient, but I don't have
       * the right standards doc to verify.  -- edward
       */
      errno = 0;
      value->longValue = strtol(s, &p, 0);
      if (errno == ERANGE) {
	 errno = 0;
	 value->longValue = strtoul(s, &p, 0);
      }
      if (errno == ERANGE) {
         Msg_Post(MSG_ERROR, MSGID(dictionary.integerTooBig)
                  "Value \"%s\" for variable \"%s\" is too large. "
                  "Using value \"%d\".\n", s, name, value->longValue);
      } else if (*p != '\0') {
         Msg_Post(MSG_ERROR, MSGID(dictionary.notInteger)
                  "Value \"%s\" for variable \"%s\" "
                  "is not a valid integer value. "
                  "Using value \"%d\".\n", s, name, value->longValue);
      }
      break;

   case DICT_INT64:
      errno = 0;
#if defined(WIN32)
      value->i64Value = _strtoui64(s, &p, 0);
#elif defined(__FreeBSD__)
      value->i64Value = strtouq(s, &p, 0);
#elif defined(N_PLAT_NLM)
      /* Works for small values of str... */
      value->i64Value = strtoul(s, &p, 0);
#else
      value->i64Value = strtoull(s, &p, 0);
#endif
      if (errno == ERANGE) {
         Msg_Post(MSG_ERROR, MSGID(dictionary.integer64TooBig)
                  "Value \"%s\" for variable \"%s\" is too large. "
                  "Using value \"%" FMT64 "d\".\n", s, name, value->i64Value);
      } else if (*p != '\0') {
         Msg_Post(MSG_ERROR, MSGID(dictionary.notInteger64)
                  "Value \"%s\" for variable \"%s\" "
                  "is not a valid integer value. "
                  "Using value \"%" FMT64 "d\".\n", s, name, value->i64Value);
      }
      break;

   case DICT_DOUBLE:
      value->doubleValue = strtod(s, &p);
      if (*p != '\0') {
         /*
          * XXX: This shouldn't use %f directly, because in this case we
          * should print the floating-point value in the C locale (the
          * locale we parse in), not the user's locale.
          */
	 Msg_Post(MSG_ERROR, MSGID(dictionary.notFloat)
                  "Value \"%s\" for variable \"%s\" "
		  "is not a valid floating-point value. "
                  "Using value \"%f\".\n", s, name, value->doubleValue);
      }
      break;

   default:
      NOT_REACHED();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictIsLegalStringEnumValue --
 * 
 * Results:
 *      Returns TRUE if the specified value is found in the specified set of
 *      legal values, FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
DictIsLegalStringEnumValue(const char *value,    // IN: The value to find.
                           const char **choices) // IN: The set of legal values.
                                                 //     Must use NULL as a sentinel value.
{
   const char **choiceIter;

   ASSERT(choices != NULL);
   ASSERT(value != NULL);

   choiceIter = choices;
   while (*choiceIter != NULL) {
      if (strcmp(value, *choiceIter) == 0) {
         return TRUE;
      }
      choiceIter++;
   }
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dict_GetStringEnum --
 * 
 *      Gets a string from the dictionary and verifies that it is from a set
 *      of legal values.
 *
 * Results:
 *      The string value of the dictionary variable.  May return NULL.
 *      The caller is responsible for freeing the returned value.
 *
 * Side effects:
 *      If the value is not one of the allowed choices, emits an error message
 *      naming the specified variable.
 *
 *-----------------------------------------------------------------------------
 */

char *
Dict_GetStringEnum(Dictionary *dict,     // IN
                   const char *def,      // IN: The default value.
                   const char **choices, // IN: The set of legal values.
                                         //     Must use NULL as a sentinel value.
                   const char *fmt,      // IN
                   ...)                  // IN
{
   char name[BIG_NAME_SIZE];
   char *value = NULL;
   va_list args;

   ASSERT(dict != NULL);
   ASSERT(choices != NULL);
   ASSERT(fmt != NULL);
   ASSERT(def == NULL || DictIsLegalStringEnumValue(def, choices));

   va_start(args, fmt);
   Str_Vsnprintf(name, sizeof name, fmt, args);
   va_end(args);

   value = Dict_GetString(dict, def, name);

   if (!DictIsLegalStringEnumValue(value ? value : "", choices)) {
      if (value == NULL || value[0] == '\0') {
         // Don't complain about empty entries.
      } else if (def == NULL) {
         Msg_Post(MSG_ERROR, MSGID(dictionary.notEnumAndNoDefault)
                  "Value \"%s\" for variable \"%s\" is not a valid value.\n",
                  value, name);
      } else {
         Msg_Post(MSG_ERROR, MSGID(dictionary.notEnum)
                  "Value \"%s\" for variable \"%s\" is not a valid value. "
                  "Using value \"%s\".\n",
                  value, name, def);
      }
      free(value);
      value = Util_SafeStrdup(def);
   }

   return value;
}


#if defined(VMX86_DEBUG) && defined(VMX86_DEVEL)
/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryCompareEntryValue --
 * 
 *      Compares two values of the specified type.  The first value to be
 *      compared is in e->value.  The second is pointed to by pvalue.
 *
 * Results:
 *      A strcmp()-type result.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static int
DictionaryCompareEntryValue(Entry *e,            // IN: first value to compare
			    const void *pvalue,  // IN: second value to compare
			    DictionaryType type) // IN: value's type
{
   switch (type) {

   case DICT_ANY:
   case DICT_STRING: {

      char *s1 = e->value.stringValue;
      char *s2 = * (char **) pvalue;

      if (s1 == NULL && s2 == NULL) {
	 return 0;
      }

      if (s1 == NULL) {
	 return -1;
      }

      if (s2 == NULL) {
	 return 1;
      }

      return strcmp(s1, s2);
   }

   case DICT_BOOL: {

      Bool v1 = e->value.boolValue;
      Bool v2 = * (Bool *) pvalue;

      return v1 < v2 ? -1 : v1 == v2 ? 0 : 1;
   }

   case DICT_TRISTATE:
   case DICT_LONG: {

      int32 v1 = e->value.longValue;
      int32 v2 = * (int32 *) pvalue;

      return v1 < v2 ? -1 : v1 == v2 ? 0 : 1;
   }

   case DICT_INT64: {

      int64 v1 = e->value.i64Value;
      int64 v2 = * (int64 *) pvalue;

      return v1 < v2 ? -1 : v1 == v2 ? 0 : 1;
   }

   case DICT_DOUBLE: {

      double v1 = e->value.doubleValue;
      double v2 = * (double *) pvalue;

      return v1 < v2 ? -1 : v1 == v2 ? 0 : 1;
   }

   default:
      NOT_REACHED();
   }

   return 0;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryModifyEntry --
 *
 *      Modify the value in e by the specified value.  The type of the new
 *      value is given by newType.  If one of newType or e->type is unknown
 *      (DICT_ANY), then it will be converted to the other's type.  Otherwise,
 *      newType must not differ from e->type.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies e.
 *      If the string does not parse correctly for the given type, emits an
 *      error message naming the specified variable.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryModifyEntry(Entry *e,                // IN: entry to replace
		      const void *value,       // IN: new value
		      DictionaryType newType,  // IN: new value's type
		      int defaultLevel,        // IN: new default level
		      Bool forceModified)      // IN: mark modified
{
   DictionaryType oldType = e->type;
   Value v;

   /*
    * Narrow entries as needed.
    */

   if (newType == DICT_ANY) {
      if (oldType != DICT_ANY) {
	 DictionaryNarrowValue(e->name, *(char **) value, oldType, &v);
	 ASSERT((char **) &v == &v.stringValue);
	 ASSERT((int32 *) &v == &v.longValue);
	 ASSERT((Bool *) &v == &v.boolValue);
	 ASSERT((double *) &v == &v.doubleValue);
	 value = &v;
	 newType = oldType;
      }
   } else {
      if (oldType == DICT_ANY) {
	 DictionaryNarrow(e, newType);
	 oldType = newType;
      }
   }

   ASSERT(newType == oldType);

   switch (newType) {

   case DICT_ANY:
   case DICT_STRING: {
      const char *s = *(const char **)value;
      if (e->value.stringValue == NULL) {
	 if (s != NULL) {
	    e->value.stringValue = Util_SafeStrdup(s);
	    e->modified = TRUE;
	 }
      } else {
	 if (s == NULL || strcmp(e->value.stringValue, s) != 0) {
	    free(e->value.stringValue);
	    e->value.stringValue = Util_SafeStrdup(s);
	    e->modified = TRUE;
	 }
      }
      break;
   }

   case DICT_BOOL:
      if (e->value.boolValue != *(Bool *)value) {
	 e->modified = TRUE;
	 e->value.boolValue = *(Bool *)value;
      }
      break;

   case DICT_TRISTATE:
   case DICT_LONG:
      if (e->value.longValue != *(int32 *)value) {
	 e->modified = TRUE;
	 e->value.longValue = *(int32 *)value;
      }
      break;

   case DICT_INT64:
      if (e->value.i64Value != *(int64 *)value) {
         e->modified = TRUE;
         e->value.i64Value = *(int64 *)value;
      }
      break;

   case DICT_DOUBLE:
      if (e->value.doubleValue != *(double *)value) {
	 e->modified = TRUE;
	 e->value.doubleValue = *(double *)value;
      }
      break;

   default:
      NOT_REACHED();
   }

   e->defaultLevel = defaultLevel & DICT_DEFAULT_MASK;
   if (forceModified) {
      e->modified = TRUE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_NotSet --
 *
 *      Return TRUE if a configuration variable was not set either
 *	by loading from any file or by an explicit Dictionary_Set.
 *
 * Results:
 *      TRUE if not set.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_NotSet(Dictionary *dict,     // IN: dictionary
		  const char *name)     // IN: name to look for
{
   Entry *e;

   e = DictionaryFindEntry(dict, name);
   e = DictionarySanitizePlaintextEntry(dict, e, 0);
   return e == NULL || e->defaultLevel == DICT_COMPILED_DEFAULT;
}
   

/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_IsDefined --
 *
 *      Tests whether a name exists in the dictionary.
 *
 * Results:
 *      TRUE if the name exists, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_IsDefined(Dictionary *dict,  // IN: dictionary
                     const char *name)  // IN: name to look for
{
   Entry *e = DictionaryFindEntry(dict, name);
   e = DictionarySanitizePlaintextEntry(dict, e, 0);
   return e!= NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Set --
 *
 *      Puts an entry with the specified name, value, and type into the
 *      dictionary.  If an entry with the given name already exists, it is
 *      replace.
 *
 *      The type may include DICT_VERSIONSPEC to force the entry to be written
 *      out first for file output and prevent it from being encrypted.
 *      DICT_VERSIONSPEC also prevents the entry from being marked dirty, for
 *      an unknown reason.
 *
 *      The type may also include DICT_DONTENCRYPT, which simply prevents
 *      the entry from being encrypted, with no other side effects.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Adds the entry to the dictionary.
 *	Marks the dictionary dirty, unless DICT_VERSIONSPEC is present.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_Set(Dictionary *dict,        // IN: dictionary
               const void *pvalue,      // IN: pointer to value to set
               DictionaryType type,     // IN: type of new value
	       const char *name)        // IN: name to set
{
   Entry *e;
   Bool versionSpec = (type & DICT_VERSIONSPEC) != 0;
   Bool dontEncrypt = (type & DICT_DONTENCRYPT) != 0;

   type &= ~DICT_VERSIONSPEC & ~DICT_DONTENCRYPT;
   
   e = DictionaryFindEntry(dict, name);
   
   if (e) {
      DictionaryModifyEntry(e, pvalue, type, DICT_NOT_DEFAULT, !versionSpec);
   } else {
      e = DictionaryAddEntry(dict, name, DICT_NOT_DEFAULT, pvalue, type, TRUE);
      e->modified = !versionSpec;
   }

   if (dontEncrypt) {
      e->dontEncrypt = TRUE;
   }

   if (versionSpec) {
      e->versionSpec = TRUE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Unset --
 *
 *      Removes the entry, if any, with the specified name from the
 *      dictionary completely.
 *
 * Results:
 *
 *      None.
 *
 * Side effects:
 *
 *      The entry is removed.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_Unset(Dictionary *dict,  // IN
                 const char *name)  // IN
{
   ASSERT(dict);
   ASSERT(name);

   DictionaryRemoveIf(dict, HasGivenName, (void *) name);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_UnsetWithPrefix --
 *
 *      Removes entries, if any, with the specified prefix from the
 *      dictionary completely.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The entry is removed.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_UnsetWithPrefix(Dictionary *dict,   // IN
                           const char *prefix) // IN
{
   ASSERT(dict);
   ASSERT(prefix);

   DictionaryRemoveIf(dict, HasGivenPrefix, (void *) prefix);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryConvertValueToString --
 *
 *      Converts a pointer to a value of the given type to a string.
 *
 * Results:
 *      A stringified copy of p which may be stored in *p, *buffer, or
 *      a static literal string.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static const char *
DictionaryConvertValueToString(const void *p,       // IN: value
			       DictionaryType type, // IN: type of value
			       char *buffer,        // OUT: conversion buffer
			       size_t bufferSize)   // IN: size of buffer
{
   switch (type) {

   case DICT_ANY:
   case DICT_STRING:
      return * (const char **) p;

   case DICT_BOOL:
      return * (Bool *) p ? "true" : "false";

   case DICT_TRISTATE:
      switch (* (int32 *) p) {

      case -1:
	 return "default";

      case 1:
	 return "true";

      case 0:
	 return "false";

      default:
	 NOT_REACHED();
	 return NULL;
      }

   case DICT_LONG:
      Str_Sprintf(buffer, bufferSize, "%d", * (int32 *) p);
      return buffer;

   case DICT_INT64:
      Str_Sprintf(buffer, bufferSize, "%"FMT64"d", * (int64 *) p);
      return buffer;

   case DICT_DOUBLE:
      Str_Sprintf(buffer, bufferSize, "%g", * (double *) p);
      return buffer;

   default:
      NOT_REACHED();
   }
   return NULL;		/* compiler's happy */
}


/*
 *----------------------------------------------------------------------
 *
 * Dictionary_GetAsString --
 *      Return the value of the variable named by sprintf(fmt, args)
 *	as a string.
 *
 * Results:
 *      The value of the variable as a string.
 *	The string should be considered static, and not valid
 *	after a subsequent call to any dictionary function.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Dictionary_GetAsString(Dictionary *dict, // IN: dictionary
                       const char *name) // IN: name to query
{
   Entry *e;
   const char *value = NULL;

   ASSERT(dict);

   e = DictionaryFindEntry(dict, name);
   e = DictionarySanitizePlaintextEntry(dict, e, 0);

   if (e != NULL) {
      char buf[CONV_BUFFER_SIZE];

      value = DictionaryConvertValueToString(&e->value, e->type,
					     buf, sizeof buf);
      if (value == buf) {
	 free(e->convBuffer);
	 value = e->convBuffer = Util_SafeStrdup(value);
      }
   }

   return (char *)value;
}


/*
 *----------------------------------------------------------------------
 *
 * Dictionary_Get --
 *      Return the value of the variable named by sprintf(fmt, args).
 *
 * Results:
 *      The (new) value of the variable.
 *
 * Side effects:
 *      If the variable has no value, set it to defaultValue.
 *      If the variable is set to a default value (resulting from
 *	a previous call to Dictionary_Get) and the new value is
 *	different from the old one, signal an error.
 *----------------------------------------------------------------------
 */

#if defined VMX86_DEVEL
#  define REDEFINITION_ACTION Panic
#elif defined VMX86_DEBUG
#  define REDEFINITION_ACTION Warning
#endif

void *
Dictionary_Get(Dictionary *dict,          // IN: dictionary
	       const void *pDefaultValue, // IN: default value to set
	       DictionaryType type,       // IN: type of value
	       const char *name)          // IN: name to query/set
{
   Entry *e;
   Bool dontEncrypt;

   ASSERT(dict);

   e = DictionaryFindEntry(dict, name);
   e = DictionarySanitizePlaintextEntry(dict, e, type);
   dontEncrypt = (type & DICT_DONTENCRYPT) != 0;
   type &= ~DICT_DONTENCRYPT;

#if defined(VMX86_DEBUG) && defined(VMX86_DEVEL)
   if (e
       && pDefaultValue != &dictionaryNoDefault
       && e->defaultLevel == DICT_COMPILED_DEFAULT
       && DictionaryCompareEntryValue(e, pDefaultValue, type) != 0) {
      char oldBuf[CONV_BUFFER_SIZE];
      const char *oldValue = DictionaryConvertValueToString(&e->value, e->type,
						      oldBuf, sizeof oldBuf);
      char newBuf[CONV_BUFFER_SIZE];
      const char *newValue = DictionaryConvertValueToString(pDefaultValue,
						type, newBuf, sizeof newBuf);
      REDEFINITION_ACTION("Changing default value for %s from %s to %s\n",
			  e->name, oldValue, newValue);
      DictionaryModifyEntry(e, pDefaultValue, type,
			    DICT_COMPILED_DEFAULT, FALSE);
   }
#endif

   if (e) {
      ASSERT_BUG_DEBUGONLY(5939, e->type == type || e->type == DICT_ANY);
      if (e->type == DICT_ANY && e->type != type) {
	 DictionaryNarrow(e, type);
      }
   } else {
      ASSERT(pDefaultValue != &dictionaryNoDefault);
      e = DictionaryAddEntry(dict, name, DICT_COMPILED_DEFAULT,
			     pDefaultValue, type, TRUE);
      if (dontEncrypt) {
         e->dontEncrypt = TRUE;
      }
   }

   switch (type) {

   case DICT_ANY:
   case DICT_STRING:
      return &e->value.stringValue;

   case DICT_BOOL:
      return &e->value.boolValue;

   case DICT_TRISTATE:
   case DICT_LONG:
      return &e->value.longValue;

   case DICT_INT64:
      return &e->value.i64Value;

   case DICT_DOUBLE:
      return &e->value.doubleValue;

   default:
      NOT_REACHED();
   }
   return NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryFindEntry --
 *
 *      Finds a dictionary entry given its name.
 *
 * Results:
 *      The entry if one exists by that name, or a null pointer otherwise.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

static Entry *
DictionaryFindEntry(Dictionary *dict,   // IN: dictionary
                    const char *name)   // IN: name to query
{
   Entry *e = NULL;

   ASSERT_BUG(4947, dict);

   /* Use our hashtable instead of iterating through the list */
   if (HashTable_Lookup(dict->hashtable, name, (void **)(char*) &e)) {
      ASSERT(e);
   }

   return e;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionarySanitizePlaintextEntry --
 *
 *      Make sure that we return only encrypted entries where necessary.
 *      - Any value in a nonencrypted dictionary is legal.
 *      - In an encrypted dictionary, encrypted values are legal.
 *      - In an encrypted dictionary, plaintext values are legal if requested
 *        with DICT_DONTENCRYPT.
 *      - In an encrypted dictionary, values that are stored with
 *        DICT_DONTENCRYPT and requested without DICT_DONTENCRYPT must be
 *        hidden.
 *      Note that DICT_VERSIONSPEC entries are also written in plaintext
 *      but aren't marked DICT_DONTENCRYPT, are assumed not to be sensitive,
 *      and are not masked from any callers in any situation.
 *
 * Results:
 *      Entry or NULL
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static Entry *
DictionarySanitizePlaintextEntry(Dictionary *dict,
                                 Entry *e,
                                 DictionaryType typeRequested)
{
   ASSERT(dict);
   if (dict->keySafe && e && e->dontEncrypt &&
       !(typeRequested & DICT_DONTENCRYPT)) {
      return NULL;
   }

   return e;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryAppendEntry --
 *
 *      Appends the given entry to a dictionary's list.
 *
 *      Does not modify the dictionary's hash table.
 *
 * Results:
 *
 *      None.
 *
 * Side effects:
 *
 *	Modified the list of entry's in the dictionary.
 *      Increment entry count of the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryAppendEntry(Dictionary *dict, // IN: dictionary
                      Entry *entry)     // IN: entry to add
{
   ASSERT (dict);
   ASSERT (entry);
   
   entry->next = NULL;

   if (!dict->firstEntry) {
      dict->firstEntry = entry;
   } else {
      ASSERT(dict->lastEntry);
      dict->lastEntry->next = entry;
   }
   dict->lastEntry = entry;
   dict->numEntries++;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryAddEntry --
 *
 *      Adds an entry with the given name, default level, value, and type to
 *      a dictionary.
 *
 *      The dictionary must not already contain a entry with the given name.
 *
 *	If copyStrings, then string arguments (name and string value
 *	if any) are copied.  Otherwise, they are saved as is in the
 *	dictionary entry and will be freed along with the entry.
 *
 * Results:
 *      The entry added to the dictionary.
 *
 * Side effects:
 *	Adds an entry to the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

static Entry *
DictionaryAddEntry(Dictionary *dict,    // IN: dictionary
                   const char *name,    // IN: name to add
                   int defaultLevel,    // IN: default level to add at
                   const void *pvalue,  // IN: pointer to value to add
                   DictionaryType type, // IN: value's type
		   Bool copyStrings)    // IN: make a copy of strings
{
   Entry *e;

   ASSERT(dict);
   ASSERT(*name != '\0');		// caller's responsibility
   ASSERT(*name != METAVAR_PREFIX);	// not supported

   e = Util_SafeCalloc(1, sizeof *e);

   DictionaryAppendEntry(dict, e);
   
   e->name = copyStrings ? Util_SafeStrdup(name) : (char *) name;
   e->modified = FALSE;
   e->defaultLevel = defaultLevel & DICT_DEFAULT_MASK;
   e->type = type;

   switch (type) {
   case DICT_ANY:
   case DICT_STRING:
      e->value.stringValue = copyStrings ?
			     Util_SafeStrdup(*(char **) pvalue) :
			     *(char **) pvalue;
      break;

   case DICT_BOOL:
      e->value.boolValue = * (Bool *) pvalue;
      break;

   case DICT_TRISTATE:
   case DICT_LONG:
      e->value.longValue = * (int32 *) pvalue;
      break;

   case DICT_INT64:
      e->value.i64Value = * (int64 *) pvalue;
      break;

   case DICT_DOUBLE:
      e->value.doubleValue = * (double *) pvalue;
      break;

   default:
      NOT_REACHED();
   }

   /* Add the entry to the hashtable for quick lookup */
   HashTable_Insert(dict->hashtable, e->name, e);
   
   return e;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryInternalSetFromString --
 *
 *      Adds or modifies an entry in a dictionary.  The entry name and value
 *      are extracted from the given string.  If the string is of the form
 *      "a=b", then "a" is the name and "b" is the value.  Otherwise, the
 *      entire string is the name and the value is a null string.
 *
 *      If preventRedefinition is nonzero, then duplicate names provoke an
 *      error message and nothing is changed.  Otherwise, any entry with a
 *      duplicate name is replaced.
 *
 *      If setModified is nonzero, then the dictionary is marked as modified.
 *
 * Results:
 *      The added or modified entry (or the one that would have been modified
 *      had preventRedefinition been zero).
 *
 * Side effects:
 *	Modifies dict and may emit an error message.
 *
 *-----------------------------------------------------------------------------
 */

static Entry *
DictionaryInternalSetFromString(Dictionary *dict,         // IN: dictionary
				const char *string,       // IN: string to add
				int defaultLevel,         // IN: level to add
				Bool preventRedefinition, // IN: dups ok?
				Bool setModified)         // IN: mark changed?
{
   char *name;
   char *value;
   size_t nameLen;
   Entry *e = NULL;

   ASSERT(dict);
   ASSERT(string);

   nameLen = strcspn(string, "=");
   name = Util_SafeStrndup(string, nameLen);

   if (string[nameLen] && string[nameLen + 1]) {
      value = Util_SafeStrdup(string + nameLen + 1);
   } else {
      value = Util_SafeStrdup("");
   }
   
   e = DictionaryFindEntry(dict, name);
   
   if (e) {
      if (preventRedefinition) {
	 Msg_Post(MSG_ERROR, MSGID(dictionary.alreadyDefined.string)
                  "Variable \"%s\" is already defined.\n",
		  name);
      } else {
	 DictionaryModifyEntry(e, &value, DICT_ANY,
			       defaultLevel, setModified);
      }
      free(value);
      free(name);
   } else {
      e = DictionaryAddEntry(dict, name, defaultLevel, &value,
			     DICT_ANY, FALSE);
      if (setModified) {
	 e->modified = TRUE;
      }
   }
   return e;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_RestoreFromString --
 *
 *      Adds or modifies an entry in a dictionary.  The entry name and value
 *      are extracted from the given string.  If the string is of the form
 *      "a=b", then "a" is the name and "b" is the value.  Otherwise, the
 *      entire string is the name and the value is a null string.
 *
 *      Entries are added at DICT_NOT_DEFAULT level.
 *
 *      Any entry with a duplicate name is replaced.
 *
 *      The dictionary is marked as modified.
 *
 * Results:
 *      The added or modified entry.
 *
 * Side effects:
 *	Modifies dict.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_RestoreFromString(Dictionary *dict,   // IN: dictionary
                             const char *string) // IN: string to restore
{
   DictionaryInternalSetFromString(dict, string, DICT_NOT_DEFAULT,
				   FALSE, TRUE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_SetFromString --
 *
 *      Adds an entry to a dictionary.  The entry name and value are extracted
 *      from the given string.  If the string is of the form "a=b", then "a" is
 *      the name and "b" is the value.  Otherwise, the entire string is the
 *      name and the value is a null string.
 *
 *      Entries are added at DICT_LOADED_DEFAULT level.
 *
 *      Duplicate names provoke an error message and nothing is changed.
 *
 *      The dictionary is not marked as modified.
 *
 * Results:
 *      The added entry (or the one that would have been modified if we were
 *      willing to modify entries).
 *
 * Side effects:
 *	Modifies dict and may emit an error message.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_SetFromString(Dictionary *dict,      // IN: dictionary
                         const char *string)    // IN: string to set
{
   DictionaryInternalSetFromString(dict, string, DICT_LOADED_DEFAULT,
				   TRUE, FALSE);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_LoadFromBuffer --
 *
 *      Adds entries to a dictionary.  The input buffer is a
 *      newline-separated list of dictionary entries in the form you'd put
 *      in a dictionary file.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Modifies dict
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_LoadFromBuffer(Dictionary *dict,      // IN: dictionary
                          const char *buffer,    // IN: strings to set
                          int defaultLevel,      // IN: how to add the entry
                          Bool append)           // IN: don't clear dict first
{
   return DictionaryLoadFromBuffer(dict, buffer, defaultLevel, append,
				   STRING_ENCODING_DEFAULT);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_LoadFromBufferWithDefaultEncoding --
 *
 *      Dictionary_LoadFromBuffer using defaultEncoding if no encoding
 *	is specified in the buffer or (if appending) the dictionary
 *	already has an encoding.,
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Modifies dict
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_LoadFromBufferWithDefaultEncoding(
   Dictionary *dict,			// IN: dictionary
   const char *buffer,			// IN: strings to set
   int defaultLevel,			// IN: how to add the entry
   Bool append,				// IN: don't clear dict first
   StringEncoding defaultEncoding)	// IN: default encoding
{
   return DictionaryLoadFromBuffer(dict, buffer, defaultLevel, append,
				   defaultEncoding);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryLoadFromBuffer --
 *
 *      Adds entries to a dictionary.  The input buffer is a
 *      newline-separated list of dictionary entries in the form you'd put
 *      in a dictionary file.
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Modifies dict
 *
 *-----------------------------------------------------------------------------
 */

static Bool
DictionaryLoadFromBuffer(Dictionary *dict,      // IN: dictionary
                         const char *buffer,    // IN: strings to set
                         int defaultLevel,      // IN: how to add the entry
                         Bool append,           // IN: don't clear dict first
			 StringEncoding defaultEncoding)
						// IN: default encoding
{
   Bool success = TRUE;
   const char *remain = buffer;
   size_t size;
   
   ASSERT(dict);

   if (!append) {
      Dictionary_Clear(dict);
   }

   if (!buffer) {
      return TRUE;
   }
   size = strlen(buffer);

   while (remain && *remain) {
      char *whole;
      char *name;
      char *value;
      const char *next;
      char *sep;
      Bool mungedSep = FALSE;
      int status;

      /* Cope with DOS text files which have \r\n instead of \n. */
      sep = Str_Strchr(remain, '\n');
      if (sep > remain && sep[-1] == '\r') {
         mungedSep = TRUE;
         sep[-1] = '\n';
      }

      /* Grab next line from buffer. */
      next = DictLL_UnmarshalLine(remain, size, &whole, &name, &value);
      size -= (next - remain);
      if (mungedSep) {
         sep[-1] = '\r';
         next++;
         size--;
      }

      /*
       * This should have the same error semantics as DictionaryLoadFile.
       * Even if DictionaryUnmarshalLine presents us with NULL name and
       * value, we pass this along to DictionaryParseReadLine so
       * that it can decide whether this is valid.
       */
      status = DictionaryParseReadLine(dict, whole, name, value,
                                       defaultLevel);
      switch (status) {
      case 0:
         /* Good. */
         break;
      case 1:
         /* Duplicate name: continue parsing but flag error. */
         success = FALSE;
         break;
      case 2:
         /* Syntax error: give up immediately. */
         return FALSE;
      default:
         NOT_REACHED();
      }

      remain = next;
   }

   if (success && dict->encoding == STRING_ENCODING_UNKNOWN) {
      success = DictionaryUseEncoding(dict, NULL, defaultEncoding);
      if (!success) {
	 Msg_Append(MSGID(dictionary.badDefaultEncodingNoFile)
		    "Failed to decode dictionary in the default character "
		    "encoding.\n");
      }
   }

   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_WriteToBuffer --
 *
 *      Exports a dictionary into a buffer. Pairs are delimited by
 *      newlines, and are in the format 'name = value'.
 *
 *      If 'enableEncrypt' is TRUE and the dictionary is encrypted, the
 *      output will be encrypted. Otherwise it will be plaintext.
 *
 *
 * Results:
 *      TRUE if successful, FALSE on failure. Caller must free 'outbuf'
 *      on success.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_WriteToBuffer(Dictionary *dict,      // IN: dictionary
                         Bool enableEncrypt,    // IN: can encrypt output
                         char **outbuf,         // OUT: buffer
                         size_t *outsize)       // OUT: size in bytes
{
   /*
    * If DictionaryWriteToBuffer can't encode the dictionary
    * in the current encoding, silently upgrade it to UTF-8.
    */

   for (;;) {
      switch (DictionaryWriteToBuffer(dict, enableEncrypt, outbuf, outsize)) {
      case 0:
	 return TRUE;
      case 1:
	 return FALSE;
      case 2:
	 ASSERT(dict->encoding != STRING_ENCODING_UTF8);
	 Msg_Reset(TRUE);
	 Log("%s: upgrading encoding from %s to %s\n",
	     __FUNCTION__,
	     Unicode_EncodingEnumToName(dict->encoding),
	     Unicode_EncodingEnumToName(STRING_ENCODING_UTF8));
         Dictionary_ChangeEncoding(dict, STRING_ENCODING_UTF8);
	 break;
      default:
	 NOT_REACHED();
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryFreeEntry --
 *
 *      Destroys an entry.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Destroys an entry. Decrement the entry count of the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryFreeEntry(Dictionary *dict,   // IN: dictionary
                    Entry *e)           // IN: entry to free
{
   free(e->name);
   if (e->type == DICT_STRING || e->type == DICT_ANY) {
      free(e->value.stringValue);
   }
   free(e->convBuffer);
   free(e);
   dict->numEntries--;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Clear --
 *
 *      Clears the contents of a dictionary.
 *
 *      Does not modify the dictionary's modified flag.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Clears the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_Clear(Dictionary *dict)      // IN: dictionary to clear
{
   ASSERT(dict);

   HashTable_Clear(dict->hashtable);

   dict->needSaveForSure = FALSE;
   dict->oldExecFlags[0] = '\0';
   dict->oldExecFlags[sizeof dict->oldExecFlags - 1] = '\0';	// detect ofl

   while (dict->firstEntry) {
      Entry *e = dict->firstEntry;
      dict->firstEntry = e->next;
      DictionaryFreeEntry(dict, e);
   }

   dict->lastEntry = NULL;

   while (dict->firstWriteLine) {
      WriteLine *l = dict->firstWriteLine;

      if (l->string) {
	 free(l->string);
      }

      dict->firstWriteLine = l->next;

      free(l);
   }

   dict->lastWriteLine = NULL;

   KeySafe_Destroy(dict->keySafe);
   dict->keySafe = NULL;

   CryptoKey_Free(dict->key);
   dict->key = NULL;

   dict->encoding = STRING_ENCODING_UNKNOWN;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_ClearPreserveKeys --
 *
 *      Like Dictionary_Clear, but preserves the dictionary's KeySafe so
 *      that subsequent Dictionary_Write's will respect the original
 *      cryptographic state of the dictionary.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Clears the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_ClearPreserveKeys(Dictionary *dict)   // IN: dictionary to clear
{
   CryptoKey *key;
   KeySafe *keySafe;

   ASSERT(dict);

   keySafe = dict->keySafe;
   dict->keySafe = NULL;

   key = dict->key;
   dict->key = NULL;

   Dictionary_Clear(dict);

   dict->key = key;
   dict->keySafe = keySafe;
}


/*
 *----------------------------------------------------------------------
 *
 * Dictionary_SetAll --
 *
 *	Set all entries of type TYPE in DICT starting with PREFIX
 *	to VALUE.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Change dictionary.
 *
 *----------------------------------------------------------------------
 */
  
void
Dictionary_SetAll(Dictionary *dict,     // IN: dictionary
                  const char *prefix,   // IN: prefix of names to set
		  DictionaryType type,  // IN: type of new value
                  void *pvalue)         // IN: pointer to new value
{
   Entry *e;
   size_t l = strlen(prefix);

   ASSERT(dict);

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (strncmp(e->name, prefix, l) == 0) {
	 if (e->type == DICT_ANY || e->type == type) {
	    DictionaryModifyEntry(e, pvalue, type, DICT_NOT_DEFAULT, TRUE);
	 }
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Rekey --
 *
 *      First, the given dictionary is decrypted, if it was encrypted before.
 *      Then, if encryptionKeys is non-null, the dictionary is (re-)encrypted
 *      so that it can be decrypted with any of the keys in the given keyring.
 *
 *      Entries marked in a dictionary as version specs (DICT_VERSIONSPEC) or
 *      as "don't encrypt" (DICT_DONTENCRYPT) are never encrypted.
 *
 * Results:
 *
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *
 *	Decrypts and/or encrypts the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_Rekey(Dictionary *dict,                       // IN
                 const KeySafeUserRing *encryptionKeys)  // IN
{
   ASSERT(dict != NULL);

   if (!Dictionary_NotSet(dict, KEYSAFE_NAME)) {
      /*
       * It doesn't make much sense to try to rekey a locked, encrypted
       * dictionary, because doing so will simply discard all the data (except
       * for any version spec).  It would make more sense to create a new
       * dictionary and encrypt it.
       */
      Warning("%s: called on a locked, encrypted dictionary.", __FUNCTION__);

      /*
       * Make sure that KEYSAFE_NAME and ENCRYPTED_DATA_NAME normally appear in
       * a dictionary only if it is encrypted and locked.
       */
      Dictionary_Unset(dict, KEYSAFE_NAME);
      Dictionary_Unset(dict, ENCRYPTED_DATA_NAME);
   }

   /* Make the dictionary plaintext. */
   KeySafe_Destroy(dict->keySafe);
   dict->keySafe = NULL;

   CryptoKey_Free(dict->key);
   dict->key = NULL;

   /* Generate a new key. */
   if (encryptionKeys != NULL) {
      KeySafeError keySafeError;

      /* Create a key safe and a key. */
      keySafeError = KeySafe_Seal(encryptionKeys, &dict->key, &dict->keySafe,
                                  NULL, NULL);
      if (!KeySafeError_IsSuccess(keySafeError)) {
         return FALSE;
      }
   }

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_GetKeySafe --
 *
 *      Returns a pointer to the KeySafe protecting this dictionary.  If the
 *      dictionary is not encrypted, NULL is returned.
 *
 * Results:
 *      See above.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

struct KeySafe *
Dictionary_GetKeySafe(Dictionary *dict)
{
   return dict->keySafe;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_CopyCryptoState --
 *
 *      Copies the key safe and unlocked key (if any) from the source
 *      dictionary to the target dictionary.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      See above.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_CopyCryptoState(Dictionary *target, // IN
                           Dictionary *source) // IN
{
   Bool ret = FALSE;

   ASSERT(target);
   ASSERT(source);

   KeySafe_Destroy(target->keySafe);
   target->keySafe = NULL;

   CryptoKey_Free(target->key);
   target->key = NULL;

   if (source->keySafe) {
      KeySafeError ksError = KeySafe_Clone(source->keySafe, &target->keySafe);

      if (!KeySafeError_IsSuccess(ksError)) {
         goto exit;
      }
   }

   if (source->key) {
      target->key = CryptoKey_Clone(source->key);

      if (!target->key) {
         if (target->keySafe) {
            KeySafe_Destroy(target->keySafe);
            target->keySafe = NULL;
         }

         goto exit;
      }
   }
         
   ret = TRUE;

  exit:
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryRemoveIf --
 *
 *      Removes entries from the dictionary for which the specified predicate
 *      returns nonzero.  The predicate is passed an entry plus arbitrary
 *      user-specified auxiliary data.
 *
 * Results:
 *
 *      None.
 *
 * Side effects:
 *
 *	Removes entries from the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryRemoveIf(Dictionary *dict,                       // IN: dictionary
                   Bool (*predicate) (Entry *, void *aux), // IN: predicate
                   void *aux)                              // IN: aux data
{
   WriteLine *curLine, *nextLine;
   Entry *curEntry, *nextEntry;

   /*
    * Delete all the WriteLines for which the predicate is nonzero.  Iteration
    * must be done carefully because appending an entry modifies its `next'
    * member.
    */
   curLine = dict->firstWriteLine;
   dict->firstWriteLine = dict->lastWriteLine = NULL;
   for (; curLine != NULL; curLine = nextLine) {
      nextLine = curLine->next;
      if (predicate (curLine->entry, aux)) {
         free (curLine->string);
         free (curLine);
      } else {
         DictionaryAppendWriteLine(dict, curLine); 
      }
   }

   /*
    * Delete all the entries for which the predicate is nonzero.  Again, we
    * must iterate carefully.
    */
   HashTable_Clear(dict->hashtable);
   curEntry = dict->firstEntry;
   dict->firstEntry = dict->lastEntry = NULL;
   for (; curEntry != NULL; curEntry = nextEntry) {
      nextEntry = curEntry->next;
      if (predicate (curEntry, aux)) {
         DictionaryFreeEntry(dict, curEntry);
      } else {
         DictionaryAppendEntry(dict, curEntry);
         HashTable_Insert(dict->hashtable, curEntry->name, curEntry);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * HasGivenName --
 *
 *      Predicate function for DictionaryRemoveIf.
 *
 * Results:
 *
 *      Returns TRUE if the passed name matches entry's name.
 *
 * Side effects:
 *
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
HasGivenName(Entry *entry,  // IN: entry
             void *name_)   // IN: name to check for
{
   const char *name = name_;

   return (entry && !Str_Strcasecmp(entry->name, name));
}


/*
 *-----------------------------------------------------------------------------
 *
 * HasGivenPrefix --
 *
 *      Predicate function for DictionaryRemoveIf. Used to test if an entry has
 *      the given prefix.
 *
 * Results:
 *      Returns TRUE if the entry's name begins with the prefix string.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
HasGivenPrefix(Entry *entry,  // IN
               void *prefix_) // IN
{
   const char *prefix = prefix_;

   return (entry && StrUtil_CaselessStartsWith(entry->name, prefix));
}


/*
 *-----------------------------------------------------------------------------
 *
 * BacksEncryptedEnvelope --
 *
 *      Predicate function for DictionaryRemoveIf.
 *
 * Results:
 *      Returns TRUE if given entry is used to store the encrypted data
 *      in the plaintext dictionary file.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
BacksEncryptedEnvelope(Entry *entry, void *name_)
{
   return (entry &&
       (Str_Strcasecmp(entry->name, KEYSAFE_NAME) == 0 ||
        Str_Strcasecmp(entry->name, ENCRYPTED_DATA_NAME) == 0));
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Unlock --
 *
 *      Attempts to extract data from the encrypted portion of the dictionary,
 *      using the given set of keys.  If successful, all unencrypted non-version
 *      spec entries and non-"don't encrypt" entries will be deleted and
 *      replaced by those obtained from the encrypted part of the dictionary.
 *      On failure, the dictionary is unchanged.
 *
 *      Either 'klState' (for following KeyLocators) or 'encryptionKeys'
 *      (for directly unlocking the KeySafe) is required.
 *
 * Results:
 *
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *
 *      If successful, all unencrypted non-version spec entries and non-"don't
 *      encrypt" entries will be deleted and replaced by those obtained from the
 *      encrypted part of the dictionary.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_Unlock(Dictionary *dict,                       // IN: dictionary
                  KeyLocatorState *klState,               // IN (OPT): state
                  const struct KeySafeUserRing *encryptionKeys, // IN (OPT)
                  int defaultLevel)                       // IN: new data level
{
   char *keySafeString;
   KeySafeError keySafeError;
   char *cipherTextString;
   uint8 *cipherText;
   size_t cipherTextSize;
   uint8 *plaintextData;
   size_t plaintextDataSize;
   const uint8 *plaintextPos;
   CryptoError cryptoError;
   CryptoKeyedHash *keyedHash;
   Entry *entry;
   Bool success;
   
   ASSERT(dict);
   
   /* If there is a key safe, the dictionary is already unlocked. */
   if (dict->keySafe != NULL) {
      return TRUE;
   }

   /* Get key safe string from dictionary and unseal key safe. */
   if (Dictionary_NotSet(dict, KEYSAFE_NAME)) {
      /* succeed if not encrypted */
      return TRUE;
   }

   /* must have something to unlock the dictionary with */
   if (!klState && !encryptionKeys) {
      return FALSE;
   }

   keySafeString = Dict_GetString(dict, NULL, KEYSAFE_NAME);
   keySafeError = KeySafe_Unseal(klState, keySafeString, strlen(keySafeString),
                                 encryptionKeys, &dict->keySafe, &dict->key);
   free(keySafeString);
   if (!KeySafeError_IsSuccess(keySafeError)) {
      /* let caller log if he cares, he could have a retry in hand */
      goto error;
   }

   /* Obtain encrypted part of dictionary. */
   if (Dictionary_NotSet(dict, ENCRYPTED_DATA_NAME)) {
      /* There's no encrypted data.  That's okay, I guess. */
      Dictionary_Unset(dict, KEYSAFE_NAME);
      return TRUE;
   }
   cipherTextString = Dict_GetString(dict, NULL, ENCRYPTED_DATA_NAME);

   /* Base-64 decode encrypted part of dictionary. */
   success = Base64_EasyDecode(cipherTextString, &cipherText, &cipherTextSize);
   free(cipherTextString);

   if (!success) {
      Warning("%s: base-64 decoding failed", __FUNCTION__);
      goto error;
   }

   /* Decrypt data. */
   cryptoError = CryptoKeyedHash_FromString(CryptoKeyedHashName_HMAC_SHA_1,
                                            &keyedHash);
   if (!CryptoError_IsSuccess(cryptoError)) {
      Warning("%s: CryptoKeyedHash_FromString failed: %s.\n", __FUNCTION__,
              CryptoError_ToString(cryptoError));
      goto error;
   }

   cryptoError = CryptoKey_DecryptWithMAC(dict->key, keyedHash, cipherText,
                                          cipherTextSize, &plaintextData,
                                          &plaintextDataSize);
   free(cipherText);
   if (!CryptoError_IsSuccess(cryptoError)) {
      Warning("%s: CryptoKey_DecryptWithMAC failed: %s.\n", __FUNCTION__,
              CryptoError_ToString(cryptoError));
      goto error;
   }

   /* Remove the entries used to implement the encrypted part. */
   DictionaryRemoveIf(dict, BacksEncryptedEnvelope, NULL);
   /* Mark remaining entries already in dictionary as DICT_DONTENCRYPT. */
   for (entry = dict->firstEntry; entry != NULL; entry = entry->next) {
      if (!entry->versionSpec) {
         entry->dontEncrypt = TRUE;
      }
   }

   /* Unmarshal decrypted contents into dictionary. */
   plaintextPos = plaintextData;
   for (;;) {
      char *line, *name, *value;
      size_t bytesLeft;

      bytesLeft = (plaintextData + plaintextDataSize) - plaintextPos;
      plaintextPos = DictLL_UnmarshalLine((char *) plaintextPos, bytesLeft,
                                          &line, &name, &value);
      if (plaintextPos == NULL) {
         break;
      }
      DictionaryParseReadLine(dict, line, name, value, defaultLevel);
   }

   Crypto_Free(plaintextData, plaintextDataSize);

   return TRUE;

 error:
   KeySafe_Destroy(dict->keySafe);
   dict->keySafe = NULL;

   CryptoKey_Free(dict->key);
   dict->key = NULL;

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_IsEncrypted --
 *
 *      Return TRUE if 'dict' is encrypted, FALSE otherwise. Works both before
 *      and after the dictionary is unlocked.
 *
 * Results:
 *      See above.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_IsEncrypted(Dictionary *dict)
{
   ASSERT(dict);

   return dict->keySafe ||
      (Dictionary_IsDefined(dict, ENCRYPTED_DATA_NAME) &&
       Dictionary_IsDefined(dict, KEYSAFE_NAME));
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Load --
 *
 *      Read contents of the specified file into a newly created dictionary.
 *
 * Results:
 *      The new dictionary if successful, or a null pointer on failure.
 *
 * Side effects:
 *      Disk I/O.
 *
 *-----------------------------------------------------------------------------
 */
  
Bool
Dictionary_Load(Dictionary *dict,       // IN: dictionary object
                ConstUnicode pathName,  // IN: message/dictionary file
                int defaultLevel)       // IN: how the entry was added
{
   return DictionaryLoad(dict, pathName, defaultLevel, TRUE,
                         STRING_ENCODING_DEFAULT);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_LoadWithDefaultEncoding --
 *
 *      Read contents of the specified file into a newly created dictionary.
 *
 *	defaultEncoding is used if the file doesn't specify one.
 *
 * Results:
 *      The new dictionary if successful, or a null pointer on failure.
 *
 * Side effects:
 *      Disk I/O.
 *
 *-----------------------------------------------------------------------------
 */
  
Bool
Dictionary_LoadWithDefaultEncoding(
   Dictionary *dict,      // IN: dictionary object
   ConstUnicode pathName, // IN: dictionary file
   int defaultLevel,      // IN: default level
   StringEncoding defaultEncoding)
                          // IN: encoding to use if not specified in file
{
   return DictionaryLoad(dict, pathName, defaultLevel, TRUE, defaultEncoding);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_LoadAndUnlock --
 *
 *      Loads and unlocks a dictionary all in one step.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_LoadAndUnlock(Dictionary *dict,                        // IN:
                         ConstUnicode pathName,                   // IN (OPT):
                         struct KeyLocatorState *klState,         // IN (OPT):
                         const struct KeySafeUserRing *userRing,  // IN (OPT):
                         int defaultLevel)                        // IN:
{
   Bool succeeded = FALSE;

   ASSERT(dict);

   if (!Dictionary_Load(dict, pathName, defaultLevel)) {
      goto exit;
   }

   /* unlock */
   if (!Dictionary_Unlock(dict, klState, userRing, defaultLevel)) {
      goto exit;
   }

   /* succeeded */
   succeeded = TRUE;

  exit:
   return succeeded;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_VerifyExistsAndIsEnc --
 *
 *      Returns TRUE if the specified file exists, is a dictionary file,
 *      and is encrypted, otherwise returns FALSE.
 *
 * Results:
 *
 *      See above.
 *
 * Side effects:
 *
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_VerifyExistsAndIsEnc(ConstUnicode pathName)  // IN:
{
   Bool existsAndIsEnc = FALSE;
   Dictionary *dict = NULL;

   ASSERT(pathName);

   dict = Dictionary_Create();
   ASSERT(dict);

   if (!Dictionary_Load(dict, pathName, 0) ||
       !Dictionary_IsEncrypted(dict)) {
      goto exit;
   }

   existsAndIsEnc = TRUE;

  exit:
   Dictionary_Free(dict);

   return existsAndIsEnc;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Append --
 *
 *      Read contents of the specified file & populate the exist dictionary.
 *
 *      Appending from an encrypted dictionary file is not directly supported.
 *      Dictionary_Load() followed by Dictionary_Update() should be roughly
 *      equivalent.
 *
 * Results:
 *      TRUE on success: dictionary is populated from the specified file. 
 *      FALSE on failure: 
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
  
Bool
Dictionary_Append(Dictionary *dict,       // IN: dictionary object
                  ConstUnicode pathName,  // IN: message/dictionary file
                  int defaultLevel)       // IN: indicates how the entry was created
{
   return DictionaryLoad(dict, pathName, defaultLevel, FALSE,
			 STRING_ENCODING_DEFAULT);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryLoad --
 *
 *      Loads the contents of a file into a dictionary at the given level.  If
 *      clearDictionary is nonzero, the previous contents of the dictionary are
 *      cleared first.
 *
 * Results:
 *      TRUE if successful, FALSE otherwise.
 *	Duplicate keys are errors.
 *
 * Side effects:
 *	Loads the contents of a file into the dictionary.
 *	On failure, the contents of the dictionary are consistent, but
 *	unpredictable.
 *	Emits error messages.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
DictionaryLoad(Dictionary *dict,       // IN: dictionary object
               ConstUnicode pathName,  // IN: message/dictionary file
               int defaultLevel,       // IN: indicates how the entry was created
               Bool clearDictionary,   // IN: set TRUE to reinitialize dictionary
	       StringEncoding defaultEncoding)
                                       // IN: encoding to use if none specified
{
   FILE *file;
   Bool status;
   Bool hasUTF8BOM;

   ASSERT(dict);

   file = NULL;

   if (pathName) {
      if (Unicode_Compare(pathName, "-") == 0) {
	 file = stdin;
	 pathName = "<stdin>";
      } else {
         struct stat statbuf;

	 if (Posix_Stat(pathName, &statbuf) == -1) {
	    Msg_Append(MSGID(dictionary.load.statFailed)
		       "Unable to get information about file \"%s\": %s.\n",
		       UTF8(pathName), Msg_ErrString());
            return FALSE;
	 }

	 if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
	    Msg_Append(MSGID(dictionary.load.isDirectory)
		       "\"%s\" is a directory.\n", UTF8(pathName));
            return FALSE;
	 }

	 if (!(file = Posix_Fopen(pathName, "r"))) {
	    Msg_Append(MSGID(dictionary.load.openFailed)
		       "Cannot open file \"%s\": %s.\n",
		       UTF8(pathName), Msg_ErrString());
            return FALSE;
	 }
      }
   }

   if (clearDictionary) {
      Dictionary_Clear(dict);
   }

   if (pathName == NULL) {
      return TRUE;
   }

   ASSERT(!dict->currentFile);
   dict->currentFile = Unicode_Duplicate(pathName);
   dict->currentLine = 0;

   hasUTF8BOM = DictLL_ReadUTF8BOM(file);

   status = DictionaryLoadFile(dict, file, defaultLevel);

   // squash line numbers for error messages after end of file
   dict->currentLine = 0;

   if (status && dict->encoding == STRING_ENCODING_UNKNOWN) {
      status = FALSE;
      
      /*
       * Note that we don't need to do anything special for a UTF-8 BOM if
       * the default encoding is already UTF-8.
       */
      if (   hasUTF8BOM 
          && Unicode_ResolveEncoding(defaultEncoding) != STRING_ENCODING_UTF8) {
         status = DictionaryUseEncoding(dict, NULL, STRING_ENCODING_UTF8);
         if (!status) {
            // No need to keep error messages from our failed UTF-8 attempt.
            Msg_Reset(FALSE);
         }
      }
      
      if (!status) {
         status = DictionaryUseEncoding(dict, NULL, defaultEncoding);
      }

      if (!status) {
	 Msg_Append(MSGID(dictionary.badDefaultEncoding)
		    "File \"%s\": Failed to decode file "
		    "in the default character encoding.\n",
		    dict->currentFile);
      }
   }

   Unicode_Free(dict->currentFile);
   dict->currentFile = NULL;

   if (file != stdin) {
      fclose(file);
   }

   return status;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryLoadFile --
 *
 *      Adds the contents of the given file into the dictionary at the given
 *      level.
 *
 * Results:
 *      TRUE if successful, FALSE otherwise.
 *      Duplicate keys are an error.
 *
 * Side effects:
 *	Modifies the dictionary.
 *	Emit error messages on duplicate keys.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
DictionaryLoadFile(Dictionary *dict,    // IN: dictionary to add to
                   FILE *file,          // IN: file to read
                   int defaultLevel)    // IN: level to add at
{
   Bool errors = FALSE;
   
   ASSERT(dict);

   for (;;) {
      char *whole;
      char *name;
      char *value;

      switch (DictLL_ReadLine(file, &whole, &name, &value)) {
      case 0: /* Error --hpreg */
         Msg_Append(MSGID(dictionary.read.readError)
                    "File \"%s\" line %d: %s.\n", UTF8(dict->currentFile),
                    dict->currentLine, Msg_ErrString());
         return FALSE;

      case 1: /* End of stream --hpreg */
         return !errors;

      case 2: { /* Success --hpreg */ 
         int status = DictionaryParseReadLine(dict, whole, name, value,
                                              defaultLevel);
         if (status != 0) {
            if (status == 1) {
               /* Duplicate name: continue parsing but flag error. */
               errors = TRUE;
            } else if (status == 2) {
               /* Syntax error: give up immediately. */
               return FALSE;
            } else {
               NOT_REACHED();
            } 
         } 
         break;
      }

      default:
         NOT_IMPLEMENTED();
         break;
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryParseReadLine --
 *
 *    Parses the given line, which has already been divided into a name/value
 *    pair, and adds it to the dictionary's read and write lines.
 *
 * Results:
 *    0 on success, 1 on soft failure (continue parsing),
 *    2 on hard failure (stop parsing).
 *
 * Side effects:
 *    On success, increment the dictionary line number
 *
 *-----------------------------------------------------------------------------
 */
  
static int
DictionaryParseReadLine(Dictionary *dict, // IN: dictionary
                        char *whole,      // IN: whole line
                        char *name,       // IN: name, if any, or NULL
                        char *value,      // IN: value, if any, or NULL
                        int defaultLevel) // IN: level to add names at
{
   ASSERT(dict);
   ASSERT(whole);

   /* name and value must be both null or both non-null. */
   ASSERT((name != NULL) == (value != NULL));

   dict->currentLine++;

   if (name) {
      /* Found a line that specifies name/value pair --hpreg */
      int status = 0;

      ASSERT(value);

      // XXX recognize old names until they're gone
      if (Str_Strcasecmp(name, "config.encoding") == 0 ||
	  Str_Strcasecmp(name, "preferences.encoding") == 0 ||
	  Str_Strcasecmp(name, "vmlist.encoding") == 0 ||
	  Str_Strcasecmp(name, "snapshot.encoding") == 0) {
	 goto checkEncoding;
      }

      if (*name == METAVAR_PREFIX) {
	 if (Str_Strcasecmp(name, METAVAR_ENCODING) == 0) {
checkEncoding:
	    if (!DictionaryUseEncoding(dict, value, STRING_ENCODING_UNKNOWN)) {
	       status = 1;
	    }
	} else {
	    // ignore the unknown
	    Log("%s: \"%s\" line %d: unrecognized metavariable \"%s\"\n",
		__FUNCTION__, UTF8(dict->currentFile), dict->currentLine,
		name);
	 }
	 free(whole);
	 free(name);
	 free(value);

      } else if (DictionaryFindEntry(dict, name)) {
	 if (dict->currentFile == NULL) {
	    Msg_Append(MSGID(dictionary.alreadyDefinedNoFile)
		       "Variable \"%s\" is already defined.\n",
		       name);
	 } else if (dict->currentLine <= 0) {
	    Msg_Append(MSGID(dictionary.alreadyDefinedNoLine)
		       "File \"%s\": Variable \"%s\" is already defined.\n",
		       UTF8(dict->currentFile), name);
	 } else {
	    Msg_Append(MSGID(dictionary.alreadyDefined)
		       "File \"%s\" line %d: "
		       "Variable \"%s\" is already defined.\n",
		       UTF8(dict->currentFile), dict->currentLine, name);
	 }
         free(name);
         free(value);
	 DictionaryAddWriteLine(dict, whole, NULL, TRUE);
         status = 1;

      } else {
	 Entry *e = NULL;
         if (dict->encoding != STRING_ENCODING_UNKNOWN) {
            char *oldValue = value;
            if (!Unicode_IsBufferValid(value, -1, dict->encoding)) {
	       DictionaryEncodingError(dict, name, value, dict->encoding);
               free(whole);
               free(name);
               free(value);
               return 2;
            }
            value = Unicode_Alloc(value, dict->encoding);
            free(oldValue);
         }
	 e = DictionaryAddEntry(dict, name, defaultLevel, &value,
				DICT_ANY, FALSE);
	 DictionaryAddWriteLine(dict, whole, e, TRUE);
      }

      return status;
   } else {
      /* Found a line that does not specify a name = value pair --hpreg */
      char const *tmp;

      tmp = whole;
      while (*tmp == ' ' || *tmp == '\t') {
         tmp++;
      }
      if (*tmp == '\0' || *tmp == '#') { 
         DictionaryAddWriteLine(dict, whole, NULL, TRUE);
         return 0;
      } else {
         free(whole);
	 if (dict->currentFile == NULL) {
	    Msg_Append(MSGID(dictionary.read.syntaxErrorNoFile)
		       "Syntax error.\n");
	 } else if (dict->currentLine <= 0) {
	    Msg_Append(MSGID(dictionary.read.syntaxErrorNoLine)
		       "File \"%s\": Syntax error.\n",
		       UTF8(dict->currentFile));
	 } else {
	    Msg_Append(MSGID(dictionary.read.syntaxError)
		       "File \"%s\" line %d: Syntax error.\n",
		       UTF8(dict->currentFile), dict->currentLine);
	 }
         return 2;
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryAppendWriteLine --
 *
 *      Appends line to the dictionary's list of write lines.
 *
 * Results:
 *
 *      None.
 *
 * Side effects:
 *
 *	Modifies the dictionary's write lines.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryAppendWriteLine(Dictionary *dict,     // IN: dictionary
                          WriteLine *line)      // IN: write lines to add
{
   ASSERT(dict);
   ASSERT(line);
   
   line->next = NULL;
   if (!dict->firstWriteLine) {
      dict->firstWriteLine = line;
   } else {
      ASSERT(dict->lastWriteLine);
      dict->lastWriteLine->next = line;
   }
   dict->lastWriteLine = line;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryAddWriteLine --
 *
 *      Adds the specified string to the list of lines to write out,
 *      associating it with the given entry.  If atEnd is nonzero, the line is
 *      added at the end of the list.  Otherwise, it is added at the beginning
 *      of the list.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Modifies the dictionary's list of lines to write out.
 *
 *-----------------------------------------------------------------------------
 */

static void
DictionaryAddWriteLine(Dictionary *dict, // IN: dictionary
                       char *string,     // IN: string to add as write line
                       Entry *e,         // IN: entry corresponding to string
                       Bool atEnd)       // IN: add at end or beginning?
{
   WriteLine *line;

   ASSERT(dict);

   line = Util_SafeMalloc(sizeof *line);
   line->string = string;
   line->entry = e;

   if (e) {
      e->line = line;
   }

   if (atEnd) {
      DictionaryAppendWriteLine(dict, line);
   } else {
      line->next = dict->firstWriteLine;
      dict->firstWriteLine = line;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryEncryptData --
 *
 *      Encrypts plainText and base-64 encodes it and puts it into output.
 *
 * Results:
 *
 *      TRUE if successful, FALSE otherwise.
 *
 * Side effects:
 *
 *	Initializes output and adds the output to it if successful.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
DictionaryEncryptData(Dictionary *dict,       // IN: dictionary
                      const char *plainText,  // IN: plaintext to encrypt
                      size_t plainTextSize,   // IN: size of plaintext in bytes
                      DynBuf *output)         // OUT: output buffer
{
   uint8 *cipherText;
   size_t cipherTextSize;
   char *cipherTextString;

   char *keySafeString;
   size_t keySafeStringLen;

   CryptoError cryptoError;
   KeySafeError keySafeError;
   CryptoKeyedHash *keyedHash;
   Bool success;

   ASSERT(dict != NULL);
   ASSERT(dict->keySafe != NULL);
   ASSERT(dict->key != NULL);
   ASSERT(plainText != NULL);
   ASSERT(plainTextSize > 0);
   ASSERT(output != NULL);

   DynBuf_Init(output);

   /* Encrypt plainText into cipherText. */
   cryptoError = CryptoKeyedHash_FromString(CryptoKeyedHashName_HMAC_SHA_1,
                                            &keyedHash);
   if (!CryptoError_IsSuccess(cryptoError)) {
      Warning("%s: CryptoKeyedHash_FromString failed: %s.\n", __FUNCTION__,
              CryptoError_ToString(cryptoError));
      return FALSE;
   }

   cryptoError = CryptoKey_EncryptWithMAC(dict->key, keyedHash,
                                          (uint8 *) plainText, plainTextSize,
                                          &cipherText, &cipherTextSize);
   if (!CryptoError_IsSuccess(cryptoError)) {
      Warning("%s: error encrypting data: %s.\n", __FUNCTION__,
              CryptoError_ToString(cryptoError));
      return FALSE;
   }

   /* Pass cipherText through base-64 encoding. */
   if (!Base64_EasyEncode(cipherText, cipherTextSize, &cipherTextString)) {
      NOT_REACHED();
   }
   Crypto_Free(cipherText, cipherTextSize);

   /* Serialize key safe. */
   keySafeError = KeySafe_Export(dict->keySafe,
                                 &keySafeString, &keySafeStringLen);
   if (!KeySafeError_IsSuccess(keySafeError)) {
      Warning("%s: error exporting key safe: %s.\n", __FUNCTION__,
              KeySafeError_ToString(keySafeError));
      free(cipherTextString);
      return FALSE;
   }

   /* Dump encrypted data and key safe to output. */
   success = (DictLL_MarshalLine(output, KEYSAFE_NAME, keySafeString) &&
              DictLL_MarshalLine(output, ENCRYPTED_DATA_NAME,
                                 cipherTextString));
   free(cipherTextString);
   Util_ZeroFree(keySafeString, keySafeStringLen);
   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Write --
 *
 *      Writes out a dictionary under the given path name.
 *
 * Results:
 *      TRUE if successful, FALSE on failure.
 *
 * Side effects:
 *	Disk I/O.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_Write(Dictionary *dict,       // IN: dictionary
                 ConstUnicode pathName)  // IN: file to write
{
   char *outbuf = NULL;
   size_t outsize;
   FILE *file = NULL;
   Bool success = FALSE, openSuccess = FALSE;

   ASSERT(dict);
   ASSERT(pathName);

   /*
    * Construct output buffer.
    * Do this first so we don't truncate the file then
    * fail to write.
    */

   if (!Dictionary_WriteToBuffer(dict, TRUE, &outbuf, &outsize)) {
      Msg_Append(MSGID(dictionary.export)
                 "Error exporting dictionary to buffer.\n");
      goto done;
   }

   /* keep the extra parens or gcc nags us */
   if ((file = Posix_Fopen(pathName, "w"))) {
      openSuccess = TRUE;
   } else {
#if defined(_WIN32)
     /* PR 49698: "w" can't be used to open hidden files, truncate manually */
      if ((file = Posix_Fopen(pathName, "r+")) &&
          (0 == _chsize(_fileno(file), 0))) {
         openSuccess = TRUE;
      }
#endif
   }

   if (!openSuccess) {
      if (errno == ENAMETOOLONG) {
         /* 
          * XXX Since the filename is already too long, displaying the filename on a
          *     small message box is kind of useless. We need to find a better way to
          *     fix other similar problems.
          */
         Msg_Append(MSGID(dictionary.nameTooLong) "%s", Msg_ErrString());
      } else {
         Msg_Append(MSGID(dictionary.open)
                    "Cannot open configuration file \"%s\": %s.\n",
                    UTF8(pathName), Msg_ErrString());
      }
      goto done;
   }

   /* Write out the accumulated dictionary. */
   if (1 != fwrite(outbuf, outsize - 1, 1, file)) {
      Msg_Append(MSGID(dictionary.write)
                 "Error writing to configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
      goto done;
   }

   if (0 != fflush(file)) {
      Msg_Append(MSGID(dictionary.flush)
                 "Error flushing configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
   }

   if (0 != fsync(fileno(file))) {
      Msg_Append(MSGID(dictionary.sync)
                 "Error syncing configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
   }

   success = TRUE;

 done:
   free(outbuf);
   if ((file != NULL) && (fclose(file) == EOF)) {
      Msg_Append(MSGID(dictionary.close)
                 "Error closing configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
      success = FALSE;
   }
   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_WriteSync --
 *
 *      Writes out a dictionary under the given filename, using writethrough
 *      output.
 *
 * Results:
 *      TRUE if successful, FALSE on failure.
 *
 * Side effects:
 *	Disk I/O.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_WriteSync(Dictionary *dict,       // IN: dictionary
                     ConstUnicode pathName)  // IN: file to write
{
   char *outbuf = NULL;
   size_t outsize;
   size_t written;
   FileIODescriptor file;
   FileIOResult ret;
   Bool success = FALSE;

   ASSERT(dict);
   ASSERT(pathName);
   FileIO_Invalidate(&file);

   /*
    * Construct output buffer.
    * Do this first so we don't truncate the file then
    * fail to write.
    */

   if (!Dictionary_WriteToBuffer(dict, TRUE, &outbuf, &outsize)) {
      Msg_Append(MSGID(dictionary.export)
                 "Error exporting dictionary to buffer.\n");
      goto done;
   }

   ret = FileIO_Open(&file, pathName,
                     FILEIO_OPEN_SYNC | FILEIO_OPEN_ACCESS_WRITE,
                     FILEIO_OPEN_CREATE_EMPTY);
   if (!FileIO_IsSuccess(ret)) {
      Warning("File I/O error: %s\n", FileIO_ErrorEnglish(ret));
      Msg_Append(MSGID(dictionary.open)
                 "Cannot open configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
      goto done;
   }

   /* Write out the accumulated dictionary. */
   ret = FileIO_Write(&file, outbuf, outsize - 1, &written);
   if (!FileIO_IsSuccess(ret)) {
      Warning("File I/O error: %s\n", FileIO_ErrorEnglish(ret));
      Msg_Append(MSGID(dictionary.write)
                 "Error writing to configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
      goto done;
   }

   success = TRUE;

 done:
   free(outbuf);
   if (FileIO_IsValid(&file) && FileIO_Close(&file) != 0) {
      Msg_Append(MSGID(dictionary.close)
                 "Error closing configuration file \"%s\": %s.\n",
                 UTF8(pathName), Msg_ErrString());
      success = FALSE;
   }
   return success;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_MakeExecutable --
 *
 *      Marks the given file as executable.  Each execute bit (user, group,
 *      other) will be set if the corresponding read bit is set.
 *
 *      Does nothing and always successful on Windows.
 *
 * Results:
 *      TRUE if successful, FALSE on failure.
 *
 * Side effects:
 *
 *	SideEffects
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_MakeExecutable(ConstUnicode pathName) // IN: file to modify
{
#if !defined(_WIN32)
   struct stat statbuf;
   mode_t mode = 0;

   if (Posix_Stat(pathName, &statbuf) == -1) {
      Log("%s: cannot stat configuration file %s: %s\n",
	  __FUNCTION__, UTF8(pathName), Err_ErrString());
      return FALSE;
   }
   /*
    * Set the execute bit only if the read bit is 1.
    */
   mode = statbuf.st_mode | ((statbuf.st_mode >> 2) & 0111);

   if (Posix_Chmod(pathName, mode) == -1) {
      Log("%s: cannot change mode of file %s: %s\n",
	  __FUNCTION__, UTF8(pathName), Msg_ErrString());
      return FALSE;
   }
#endif

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryWriteEntry --
 *
 *    Appends a dictionary entry to a buffer that accumulates the contents of a
 *    dictionary file.
 *
 * Results:
 *    0 on success, 1 on memory allocation failure,
 *    2 on encoding failure.
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

int
DictionaryWriteEntry(Dictionary *dict, // IN: dictionary
                     Entry *e,         // IN: entry to write
                     DynBuf *output)   // IN: output buffer
{
   ASSERT(e);

   switch (e->type) {
   case DICT_STRING:
   case DICT_ANY: {
      char *val;
      int ret;

      // no need to convert if using UTF-8 or string is empty
      if (dict->encoding == STRING_ENCODING_UTF8 ||
	  e->value.stringValue == NULL ||
	  *e->value.stringValue == '\0') {
         return !DictLL_MarshalLine(output, e->name,
                                    e->value.stringValue ?
                                    e->value.stringValue : "");
      }

      val = Unicode_GetAllocBytes(e->value.stringValue, dict->encoding);
      if (!val) {
	 Msg_Append(MSGID(dictionary.badEncodedOutput)
		   "Value \"%s\" for variable \"%s\" "
		   "is not valid in \"%s\" encoding.\n",
		    e->value.stringValue, e->name,
		    Unicode_EncodingEnumToName(dict->encoding));
	 return 2;
      }
      ret = !DictLL_MarshalLine(output, e->name, val);
      free(val);
      return ret;
   }
   case DICT_BOOL:
      return !DictLL_MarshalLine(output, e->name,
				 e->value.boolValue ? "TRUE" : "FALSE");

   case DICT_TRISTATE:
      return !DictLL_MarshalLine(output, e->name, e->value.longValue == -1
                                 ? "DEFAULT"
                                 : e->value.longValue == 1 ? "TRUE" : "FALSE");

   case DICT_LONG:
      {
         unsigned char buf[CONV_BUFFER_SIZE];

         Str_Snprintf(buf, sizeof(buf) / sizeof(buf[0]), "%i",
                      e->value.longValue);
         return !DictLL_MarshalLine(output, e->name, buf);
      }

   case DICT_INT64:
      {
         unsigned char buf[CONV_BUFFER_SIZE];

         Str_Snprintf(buf, sizeof(buf), "%"FMT64"d", e->value.i64Value);
         return !DictLL_MarshalLine(output, e->name, buf);
      }

   case DICT_DOUBLE:
      {
         unsigned char buf[CONV_BUFFER_SIZE];

         Str_Snprintf(buf, sizeof(buf) / sizeof(buf[0]), "%g",
                      e->value.doubleValue);
         return !DictLL_MarshalLine(output, e->name, buf);
      }

   default:
      Panic("Dictionary internal error - unknown parameter type\n");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_StringToBool --
 *
 *      Converts a string value into a Bool.  Accepts various forms of positive
 *      and negative boolean strings.
 *
 * Results:
 *      The boolean value, either FALSE or TRUE.  If the string is not
 *      recognizable as a boolean, return FALSE.
 *
 * Side effects:
 *	If error is non-null, then *error is set to TRUE if the string is not
 *	recognizable as a boolean, FALSE otherwise.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_StringToBool(const char *s, Bool *error)
{
   Bool value;

   if (error != NULL) {
      *error = FALSE;
   }
   if (!s) {
      if (error != NULL) {
	 *error = TRUE;
      }
      value = FALSE;
   } else if (!*s ||
       !Str_Strcasecmp(s, "true") ||
       !Str_Strcasecmp(s, "t") ||
       !Str_Strcasecmp(s, "yes") ||
       !Str_Strcasecmp(s, "y") ||
       !Str_Strcasecmp(s, "on") ||
       !strcmp(s, "1")) {
      value = TRUE;
   } else if (!Str_Strcasecmp(s, "false") ||
	    !Str_Strcasecmp(s, "f") ||
	    !Str_Strcasecmp(s, "no") ||
	    !Str_Strcasecmp(s, "n") ||
	    !Str_Strcasecmp(s, "off") ||
	    !strcmp(s, "0")) {
      value = FALSE;
   } else {
      if (error != NULL) {
	 *error = TRUE;
      }
      value = FALSE;
   }
   return value;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryStringToTriState --
 *
 *      Converts a string value into a tri-state true/false/default value.
 *      Accepts various string representations.
 *
 * Results:
 *      The tri-state value, one of FALSE, TRUE, or -1.  If the string is not
 *      recognizable as a boolean, returns FALSE.
 *
 * Side effects:
 *	If error is non-null, then *error is set to TRUE if the string is not
 *	recognizable as a tri-state, FALSE otherwise.
 *
 *-----------------------------------------------------------------------------
 */

int
DictionaryStringToTriState(const char *s, // IN: string to parse
                           Bool *error)   // OUT: TRUE on error, else FALSE.
{
   Bool b = Dictionary_StringToBool(s, error);

   if (! *error) {
      return b ? 1 : 0;
   }

   /*
    * *error is set.
    */
   if (s == NULL) {
      return FALSE;
   }

   if (Str_Strcasecmp(s, "default") == 0 ||
       Str_Strcasecmp(s, "dontcare") == 0 ||
       Str_Strcasecmp(s, "auto") == 0) {
       *error = FALSE;
       return -1;
   }

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_NeedSave --
 *
 *      Determines whether the dictionary needs to be saved (because it has
 *      been modified).
 *
 * Results:
 *      TRUE if the dictionary should be saved, FALSE otherwise.
 *      A TRUE return is conservative.  There may be no actual changes.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_NeedSave(Dictionary *dict) // IN: dictionary
{
   Entry *e;

   ASSERT(dict);

   if (dict->needSaveForSure) {
      return TRUE;
   }

   for (e = dict->firstEntry; e != NULL; e = e->next) {

      if (e->modified) {
	 return TRUE;
      }

   }
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_MarkModified --
 *
 *      Marks a dictionary entry as modified.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Marks a dictionary entry as modified.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_MarkModified(Dictionary *dict,       // IN: dictionary
                        const char *name)       // IN: name to mark modified
{
   Entry *e = DictionaryFindEntry(dict, name);

   if (e != NULL) {
      e->modified = TRUE;
      e->defaultLevel = DICT_NOT_DEFAULT;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_LogNotDefault --
 *
 *      Dumps the dictionary's entries to the log.
 *
 *      For security, entries ending in *.key in encrypted dictionaries
 *      do not have their values dumped.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Dumps the dictionary's entries to the log
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_LogNotDefault(Dictionary *dict) // IN: dictionary
{
   Bool isEncrypted = (dict->keySafe || dict->key);
   Entry *e;
   
   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (e->defaultLevel != DICT_COMPILED_DEFAULT) {
         Bool sensitive = FALSE;
         size_t len = strlen(e->name);
         if (0 == Str_Strcasecmp(e->name, "dataFileKey")) {
            sensitive = TRUE;
         } else if (len > 4 && 0 == Str_Strcasecmp(e->name + len - 4, ".key")) {
            sensitive = TRUE;
         } else if (0 == Str_Strcasecmp(e->name, "annotation")) {
            /* 
             * Many customers store things like a passwords in the Notes field
             * not expecting it to show up in the vmware.log
             */
            sensitive = TRUE;
         }

         if (isEncrypted && sensitive) {
            Log("DICT %25s = <not printed>\n", e->name);
            continue;
         }

	 switch (e->type) {

	 case DICT_STRING:
	 case DICT_ANY:
	    Log("DICT %25s = %s\n", e->name,
		e->value.stringValue == NULL? "" : e->value.stringValue);
	    break;

	 case DICT_BOOL:
	    Log("DICT %25s = %s\n", e->name,
		e->value.boolValue ? "TRUE" : "FALSE");
	    break;

	 case DICT_TRISTATE:
	 case DICT_LONG:
	    Log("DICT %25s = %i\n", e->name, e->value.longValue);
	    break;

         case DICT_INT64:
            Log("DICT %25s = %"FMT64"d\n", e->name, e->value.i64Value);
            break;

	 case DICT_DOUBLE:
	    Log("DICT %25s = %g\n", e->name, e->value.doubleValue);
	    break;

	 default:
	    Log("DICT %25s = <unknown parameter type>\n", e->name);
	 }
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_SetExecPath --
 *
 *    Set the #! line in a dictionary.
 *
 * Results:
 *    TRUE if line is changed or added.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_SetExecPath(Dictionary *dict,        // IN: dictionary
                       const char *execPath)    // IN: path to use
{
#ifdef _WIN32
   NOT_REACHED();
   return FALSE;	// make compiler happy
#else
   WriteLine *line;
   unsigned char *buf;

   line = dict->firstWriteLine;
   if (line != NULL && line->string != NULL &&
       line->string[0] == '#' && line->string[1] == '!') {
      return FALSE;
   }

   buf = Str_Asprintf(NULL, "#!%s%s", execPath, dict->oldExecFlags);
   ASSERT_MEM_ALLOC(buf);
   DictionaryAddWriteLine(dict, buf, NULL, FALSE);
   return TRUE;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Dictionary_SetFileHeader --
 *
 *      If DICT does not begin with COMMENTS, insert COMMENTS
 *	at the beginning of DICT.
 *
 * Results:
 *      (Possibly) modify the dictionary argument.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void
Dictionary_SetFileHeader(char const * const *comments,	// IN: comment lines
			 Dictionary *dict)		// IN/OUT: dictionary
{
   WriteLine *line;
   char const * const *c = comments;
   
   ASSERT_BUG(3120,dict);

   for (line = dict->firstWriteLine;
	*c != NULL;
	c++, line = line->next) {

      if (line == NULL) {
	 goto add;
      }

      if (line->string == NULL || strcmp(line->string, *c) != 0) {
	 goto add;
      }
   }
   /*
    * Lines already match.
    */
   return;

 add:
   for (; *c != NULL; c++);

   for (c--; c >= comments; c--) {
      DictionaryAddWriteLine(dict, Util_SafeStrdup(*c), NULL, FALSE);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * Dictionary_Update --
 *
 *      Change dictionary OLD to reflect the content of NEW.
 *	NEW must define a superset of the variables in OLD.
 *      If OLD is encrypted, then adding an entry that didn't use to exist
 *      is not allowed if it's plaintext. If the entry existed in OLD,
 *      the new value is accepted if it's either encrypted or both
 *      old and new entries are plaintext.
 *	Variables that are unique to OLD are not changed.  
 *
 * Results:
 *	OLD is changed.
 *
 * Side effects:
 *	None.  oldDic's keys, if any, are not changed.
 *
 *----------------------------------------------------------------------
 */

void
Dictionary_Update(Dictionary *oldDic,   // IN: dictionary to modify
		  Dictionary *newDic)   // IN: source dictionary
{
   Entry *newe, *olde;
   Bool oldDicEncrypted = (oldDic->keySafe != NULL);
   Bool newDicEncrypted = (newDic->keySafe != NULL);
   Bool oldeEncrypted = FALSE;
   Bool neweEncrypted = FALSE;

   for (newe = newDic->firstEntry; newe != NULL; newe = newe->next) {
      neweEncrypted = (newDicEncrypted && !newe->dontEncrypt);

      olde = DictionaryFindEntry(oldDic, newe->name);
      if (olde) {
         oldeEncrypted = (oldDicEncrypted && !olde->dontEncrypt);

	 if (olde->type == DICT_ANY && newe->type != DICT_ANY) {
	    DictionaryNarrow(olde, newe->type);
	 }
	 if (olde->type != DICT_ANY && newe->type == DICT_ANY) {
	    DictionaryNarrow(newe, olde->type);
	 }
	 if (olde->type != newe->type) {
	    Warning("type mismatch updating %s -- not changing\n", olde->name);
	    continue;
	 }
	
         /* If old entry is encrypted, then only accept new entry if it is too. */
         if (oldeEncrypted && !neweEncrypted) {
            continue;
         }

         /*
	  * This is unnecessarily conservative.
	  * The value could be unchanged and it could be default.
	  */
	 DictionaryModifyEntry(olde, &newe->value, olde->type,
			       DICT_NOT_DEFAULT, TRUE);
      } else {
         /* Do not accept new unique plaintext entries into old encrypted dictionary */
         if (!neweEncrypted && oldDicEncrypted) {
            continue;
         } else {
	    DictionaryAddEntry(oldDic, newe->name, DICT_NOT_DEFAULT,
	   		       &newe->value, newe->type, TRUE);
         }
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Iterate --
 *
 *      Invoke F for each variable in DICT (or only the
 *	noteworthy ones), passing it the name of the variable.
 *
 * Results:
 *      The number of variables processed.
 *
 * Side effects:
 *	Those of F.
 *
 *-----------------------------------------------------------------------------
 */

int
Dictionary_Iterate(Dictionary *dict,            // IN: dictionary
		   void (*f)(const char *name,  // IN: processor function
			     const char *value,
			     int i,
			     void *prv),
		   void *prv,                   // IN: auxiliary
		   Bool doDefault)              // IN: pass default values?
{
   Entry *e;
   int i = 0;

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (e->defaultLevel != DICT_COMPILED_DEFAULT || doDefault) {
	 char buf[CONV_BUFFER_SIZE];
	 const char *v = DictionaryConvertValueToString(&e->value, e->type,
						        buf, sizeof buf);
	 f(e->name, v, i++, prv);
      }
   }
   return i;
}

/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_NumberOfEntries --
 *
 *      Return the number of entries in the dictionary.
 *
 * Results:
 *      Number of entries in the dictionary.
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

int
Dictionary_NumberOfEntries(Dictionary *dict)            // IN: dictionary
{
   return dict->numEntries;
}

/*
 * Config-like dictionary operations.
 */

#define TYPE		const char *
#define TYPE_TAG	DICT_ANY
#undef  DICT_GET
#define DICT_SET	Dict_SetAny

#include "dictType.c"

#define TYPE		const char *
#define RETURN_TYPE	char *
#define TYPE_TAG	DICT_STRING
#define DICT_GET	Dict_GetString
#define DICT_SET	Dict_SetString
#define COPIER		Util_SafeStrdup

#include "dictType.c"

#define TYPE		const char *
#define RETURN_TYPE	char *
#define TYPE_TAG	DICT_STRING | DICT_DONTENCRYPT
#define DICT_GET	Dict_GetStringPlain
#define DICT_SET	Dict_SetStringPlain
#define COPIER		Util_SafeStrdup

#include "dictType.c"

#define TYPE		Bool
#define TYPE_TAG	DICT_BOOL
#define DICT_GET	Dict_GetBool
#define DICT_SET	Dict_SetBool

#include "dictType.c"

#define TYPE		Bool
#define TYPE_TAG	DICT_BOOL | DICT_DONTENCRYPT
#define DICT_GET	Dict_GetBoolPlain
#define DICT_SET	Dict_SetBoolPlain

#include "dictType.c"

#define TYPE		int32
#define TYPE_TAG	DICT_LONG
#define DICT_GET	Dict_GetLong
#define DICT_SET	Dict_SetLong

#include "dictType.c"

#define TYPE		int32
#define TYPE_TAG	DICT_LONG | DICT_DONTENCRYPT
#define DICT_GET	Dict_GetLongPlain
#define DICT_SET	Dict_SetLongPlain

#include "dictType.c"

#define TYPE		int64
#define TYPE_TAG	DICT_INT64
#define DICT_GET	Dict_GetInt64
#define DICT_SET	Dict_SetInt64

#include "dictType.c"

#define TYPE		int64
#define TYPE_TAG	DICT_INT64 | DICT_DONTENCRYPT
#define DICT_GET	Dict_GetInt64Plain
#define DICT_SET	Dict_SetInt64Plain

#include "dictType.c"

#define TYPE		double
#define TYPE_TAG	DICT_DOUBLE
#define DICT_GET	Dict_GetDouble
#define DICT_SET	Dict_SetDouble

#include "dictType.c"


/*
 *-----------------------------------------------------------------------------
 *
 * Dict_Unset --
 *
 *      Config-like vararg wrapper for Dictionary_Unset().
 *
 * Results:
 *      void
 *
 * Side effects:
 *      Dictionary_Unset.
 *
 *-----------------------------------------------------------------------------
 */

void
Dict_Unset(Dictionary *dict,
           const char *fmt,
           ...)
{
   char name[BIG_NAME_SIZE];

   va_list args;
   va_start(args, fmt);
   Str_Vsnprintf(name, BIG_NAME_SIZE, fmt, args);
   va_end(args);
   
   Dictionary_Unset(dict, name);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_Marshall --
 *
 *      Marshall a dictionary into a buffer.
 *
 * Results:
 *      The buffer and its size.
 *
 * Side effects:
 *	Allocate the buffer.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_Marshall(Dictionary *dict,           // IN: dictionary
		    unsigned char **buffer,     // OUT: marshalling buffer
		    size_t *size)               // OUT: buffer size
{
   /*
    * Use defaultLevel = -1 to include all items
    */
   DictionaryMarshallEx(dict, buffer, size, -1);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_MarshallModified --
 *
 *      Marshall modified items a dictionary into a buffer.
 *
 * Results:
 *      The buffer and its size.
 *
 * Side effects:
 *	Allocate the buffer.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_MarshallModified(Dictionary *dict,       // IN: dictionary
		            unsigned char **buffer, // OUT: marshalling buffer
		            size_t *size)           // OUT: buffer size
{
   DictionaryMarshallEx(dict, buffer, size, DICT_COMPILED_DEFAULT);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryMarshallEx --
 *
 *      Marshall a dictionary into a buffer.
 *      excludeFilter is used to ignore items that has the same defaultLevel
 *      excludeFilter = -1 to include all items
 *      eg. DICT_COMPILED_DEFAULT can be used to filter out non-modified
 *
 * Results:
 *      The buffer and its size.
 *
 * Side effects:
 *	Allocate the buffer.
 *
 *-----------------------------------------------------------------------------
 */

void
DictionaryMarshallEx(Dictionary *dict,          // IN: dictionary
	             unsigned char **buffer,    // OUT: marshalling buffer
		     size_t *size,              // OUT: buffer size
                     int excludeFilter)         // IN: level to ignore
{
   Entry *e;
   size_t count = 0;
   char *p;
   const char *q;

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (excludeFilter == -1 || e->modified || 
          e->defaultLevel != excludeFilter) {
	 char buf[CONV_BUFFER_SIZE];
         const char *string = DictionaryConvertValueToString(&e->value,
						   e->type, buf, sizeof buf);
         size_t length = string != NULL ? strlen(string) + 1 : 0;
         count += strlen(e->name)	// length of variable name
	    + 1			// terminating \0
	    + 1			// null string indicator
	    + length;		// length of value string (including \0)
      }
   }

   *size = count;

   if (count == 0) {
      return;
   }

   *buffer = Util_SafeMalloc(count);
   p = (char *) *buffer;

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (excludeFilter == -1 || e->modified || 
          e->defaultLevel != excludeFilter) {
	 char buf[CONV_BUFFER_SIZE];
         const char *string = DictionaryConvertValueToString(&e->value,
						   e->type, buf, sizeof buf);
         size_t length = string != NULL ? strlen(string) + 1 : 0;
         for (q = e->name; *q != '\0'; q++, p++) {
	    *p = *q;
         }
         *p++ = '\0';
         if (length == 0) {
	    *p++ = 1;
         } else {
	    *p++ = 0;
	    for (q = string; *q != '\0'; q++, p++) {
	       *p = *q;
	    }
	    *p++ = '\0';
         }
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_DeriveFileName --
 *
 *      This function is admittedly weird.  The basic idea is that we
 *      have a path to the vmx file.  We need to read disk (i.e.,
 *      "vmdk") paths from the dictionary, but the values in the
 *      registry are a path that's relative to the path of the vmx file.
 *
 *      This function reads a specified file path from the dictionary,
 *      and then concatenates this result onto the end of the path to
 *      the dictionary file.  This final result is then returned to the
 *      caller.
 *
 * Results:
 *      Pointer to string (on success, caller should free string), 
 *      otherwise NULL.
 *
 * Side effects:
 *      Allocates memory to be freed by caller.
 *
 *-----------------------------------------------------------------------------
 */

char *
Dictionary_DeriveFileName(
      Dictionary *dict,          // IN: dictionary
      const char *baseFileName,  // IN: path to dict file (incl filename)
      const char *devname,       // IN: device name to read from registry
      const char *attrname)      // IN: optional attr to read
				 //     (typically "filename" or "redo")
{
   char *fname = NULL;    // path read from dictionary
   char *filename = NULL; // filename to return to caller

   /* read vmdk path from dictionary */
   if (attrname) {
      fname = Dict_GetString(dict, NULL, "%s.%s", devname, attrname);
   } else {
      fname = Dict_GetString(dict, NULL, "%s", devname);
   }

   if (fname == NULL || fname[0] == '\0') {
      free(fname);
      return NULL;
   }
   filename = Util_DeriveFileName(baseFileName, fname, NULL);
   free(fname);

   if (filename == NULL || filename[0] == '\0') {
      Warning("%s: couldn't get filename\n", __FUNCTION__);
      free(filename);
      return NULL;
   }
   return filename;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryWriteToBuffer --
 *
 *      Exports a dictionary to a buffer, with pairs delimited by
 *      newlines, of the format "name" = "value". The buffer is
 *      null-terminated.
 *
 *      If 'enableEncrypt' is TRUE and the dictionary is encrypted, the
 *      output will be encrypted. Otherwise it will be plaintext.
 *
 * Results:
 *      0 on success, 1 on memory allocation failure,
 *      2 on encoding failure.
 *      Caller must free 'outbuf' on success.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

int
DictionaryWriteToBuffer(Dictionary *dict,      // IN: dictionary
                        Bool enableEncrypt,    // IN: can encrypt result
                        char **outbuf,         // OUT: buffer
                        size_t *outsize)       // OUT: size in bytes
{
   WriteLine *line;
   WriteLine *firstLine;
   WriteLine *lastline;
   Entry *e;
   DynBuf finalOutput, output;
   Bool doEncrypt;
   int retval;

   ASSERT(dict);
   ASSERT(outbuf);
   ASSERT(outsize);

   *outbuf = NULL;
   *outsize = 0;

   DynBuf_Init(&finalOutput);
   DynBuf_Init(&output);

   /*
    * Resolve encoding if necessary.
    */

   if (dict->encoding == STRING_ENCODING_UNKNOWN) {
      dict->encoding = Unicode_GetCurrentEncoding();
   }

   /*
    * Mark all entries as unwritten.
    * Because we make several passes over the entries, writing
    * different ones each time, this helps us keep track of which
    * entries have already been written so a later pass doesn't
    * have to duplicate the logic of previous passes to avoid
    * writing the same entry again.
    */

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      e->written = FALSE;
   }

   /*
    * Write these lines first, in this order:
    *    any initial comments
    *    encoding
    *    newly added version specs,
    *       or all version specs if this is an encrypted dictionary
    */

   for (firstLine = dict->firstWriteLine; 
        firstLine != NULL && firstLine->string != NULL && 
        firstLine->string[0] == '#';
        firstLine = firstLine->next) {
      ASSERT(firstLine->entry == NULL);
      if (!DictLL_MarshalLine(&output, NULL, firstLine->string)) {
	 retval = 1;
         goto done;
      }
   }

   if (!DictLL_MarshalLine(&output, METAVAR_ENCODING,
			   Unicode_EncodingEnumToName(dict->encoding))) {
      retval = 1;
      goto done;
   }

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (e->written) {
	 continue;
      }
      if (!e->versionSpec) {
	 continue;
      }
      if (e->line == NULL || dict->keySafe) {
	 e->written = TRUE;
	 if ((retval = DictionaryWriteEntry(dict, e, &output)) != 0) {
	    goto done;
	 }
      }
   }

   /*
    * Write out the "don't encrypt" entries next, after the version specs
    * (if we're actually encrypting, that is).
    *
    * XXX should we write lines (from the firstWriteLine list) first, then
    * entries that didn't have lines, like we do for encrypted entries
    * and plaintext dictionaries?  That's only important for ordering, and
    * it's kind of a mess keeping both sets of entries in the same list
    * anyway, order wise.  That said, this function could be factored to
    * do the same thing twice: iterate first the writelines and then the
    * remaining entries that didn't have writelines, and do that whole
    * process first for encrypted entries and then for unencrypted entries.
    */

   doEncrypt = (enableEncrypt && (dict->keySafe != NULL));
   if (doEncrypt) {
      for (e = dict->firstEntry; e != NULL; e = e->next) {
	 if (e->written) {
	    continue;
	 }
	 if (!e->dontEncrypt) {
	    continue;
	 }
	 e->written = TRUE;
         if ((retval = DictionaryWriteEntry(dict, e, &output)) != 0) {
            goto done;
         }
      }
   }

   /*
    * If we are encrypting: We've already written all the entries to keep in
    * plaintext (initial comments, version number, and entries specifically
    * marked "don't encrypt"), and all remaining entries will be encrypted.
    *
    * So move what we have so far (the plaintext entries) into our final
    * output buffer, clear our working buffer so we can marshal the
    * encrypted entries into it, and then we'll stick the blob representing
    * the encrypted subdictionary into the final output.
    */

   if (doEncrypt) {
      if (DynBuf_GetSize(&output) > 0) {
         DynBuf_Attach(&finalOutput, DynBuf_GetSize(&output),
                       DynBuf_Get(&output));
         DynBuf_Detach(&output);
      }

      /* Clear output. */
      DynBuf_Destroy(&output);
      DynBuf_Init(&output);
   }

   /*
    * Keep dictionary in its original order to the extent possible: first,
    * write out each line in the dictionary that we originally imported,
    * (skipping lines for "don't encrypt" entries we already wrote).
    */

   for (line = firstLine, lastline = NULL; line != NULL; lastline = line,
        line = line->next) {
      Entry *e = line->entry;

      if (e != NULL && e->written) {
	 continue;
      }

      /*
       * If we didn't change the info, just dump the original line back,
       * otherwise, construct the line from the entry.
       */

      if (e != NULL) {
	 e->written = TRUE;
      }
      if (e == NULL || !e->modified || e->defaultLevel == DICT_COMPILED_DEFAULT) {
         retval = !DictLL_MarshalLine(&output, NULL, line->string);
      } else {
	 retval = DictionaryWriteEntry(dict, e, &output);
      }
      if (retval != 0) {
         goto done;
      }
   }

   /*
    * Now add all remaining entries (those not represented by WriteLines,
    * i.e. those not originally imported at read time), skipping over the
    * version specifier and "don't encrypt" entries if we've already added
    * them.
    */

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if (e->written) {
	 continue;
      }
      // any entry with an original line should have been written already
      ASSERT(e->line == NULL);

      /*
       * Write the entry out:
       *    Don't have to write anything if entry has default value.
       *    Write a blank line separating this chunk from the previous.
       *    Then write the entry.
       */

      e->written = TRUE;
      if (e->defaultLevel != DICT_NOT_DEFAULT) {
	 continue;
      }
      if (lastline != NULL && lastline->string[0]) {
	 lastline = NULL;
	 if (!DictLL_MarshalLine(&output, NULL, "")) {
	    retval = 1;
	    goto done;
	 }
      }
      if ((retval = DictionaryWriteEntry(dict, e, &output)) != 0) {
	 goto done;
      }
   }

   /*
    * Everything should have been written at this point
    */

#ifdef VMX86_DEBUG
   for (e = dict->firstEntry; e != NULL; e = e->next) {
      ASSERT(e->written);
   }
#endif

   /* We need SOMETHING in the output buffer */
   // XXX why?  -- edward
   if (0 == DynBuf_GetSize(&output)) {
      if (!DynBuf_Append(&output, "\n", 1)) {
	 retval = 1;
         goto done;
      }
   }

   /* Encrypt dictionary if required. */
   if (doEncrypt) {
      DynBuf cipherText;
      if (!DictionaryEncryptData(dict,
                                 DynBuf_Get(&output), DynBuf_GetSize(&output),
                                 &cipherText)) {
	 retval = 1;
         goto done;
      }

      /* Replace output by ciphertext. */
      DynBuf_Destroy(&output);
      output = cipherText;
   }

   /* Output the dictionary. */
   DynBuf_Append(&finalOutput, DynBuf_Get(&output), DynBuf_GetSize(&output));
   DynBuf_Append(&finalOutput, "\0", 1);

   *outsize = DynBuf_GetSize(&finalOutput);
   *outbuf = DynBuf_Detach(&finalOutput);

   retval = 0;

 done:
   DynBuf_Destroy(&output);
   DynBuf_Destroy(&finalOutput);

   return retval;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_GetEncoding --
 *
 *      Get the encoding for a dictionay.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      As described.
 *
 *-----------------------------------------------------------------------------
 */

StringEncoding
Dictionary_GetEncoding(Dictionary *dict)	// IN: the dictionary
{
   ASSERT(dict != NULL);

   return dict->encoding;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_SetEncoding --
 *
 *      Set the encoding for a dictionay.
 *
 *      The encoding must be a supported encoding, not
 *      STRING_ENCODING_DEFAULT or STRING_ENCODING_UNKNOWN.
 *	The dictionary must not already have an encoding.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      As described.
 *
 *-----------------------------------------------------------------------------
 */

void
Dictionary_SetEncoding(Dictionary *dict,	// IN/OUT: the dictionary
                       StringEncoding encoding) // IN: the encoding
{
   ASSERT(dict != NULL);
   ASSERT(dict->encoding == STRING_ENCODING_UNKNOWN);
   ASSERT(Unicode_IsEncodingValid(encoding));

   dict->encoding = encoding;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Dictionary_ChangeEncoding --
 *
 *      Change the string encoding for the dictionary.
 *
 *      All subsequent writes of entries of type DICT_STRING
 *      are converted from Unicode (UTF-8) to this encoding.
 *      The encoding must be a supported encoding, not
 *      STRING_ENCODING_DEFAULT or STRING_ENCODING_UNKNOWN.
 *      The dictionary must already have an encoding.
 *
 *      A non-destructive validity check is performed.
 *
 *      While the validity check ensures current dictionary 
 *      entries can be represented in the requested encoding,
 *      there is no assurance that subsequently added entries
 *      can be represented in the outbound encoding.  This needs
 *      to be handled at write time.
 *
 *      If the encoding changes entries that are encoded are marked modified.
 *
 * Results:
 *      TRUE if successful, FALSE on failure.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
Dictionary_ChangeEncoding(Dictionary *dict,         // IN: dictionary
                          StringEncoding encoding)  // IN: new encoding
{
   Entry *e;

   ASSERT(dict != NULL);
   ASSERT(dict->encoding != STRING_ENCODING_UNKNOWN);
   ASSERT(Unicode_IsEncodingValid(encoding));

   /*
    * Verify the current string entries can be represented in the 
    * requested encoding
    */

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if ((e->type == DICT_STRING || e->type == DICT_ANY)
          && e->value.stringValue) {
         if (!Unicode_CanGetBytesWithEncoding(e->value.stringValue, encoding)) {
            return FALSE;
         }
      }
   }

   /*
    * Mark all string entries as modified when the encoding changes.
    */

   if (encoding != dict->encoding) {
      for (e = dict->firstEntry; e != NULL; e = e->next) {
         if ((e->type == DICT_STRING || e->type == DICT_ANY)
             && e->value.stringValue) {
            e->modified = TRUE;
         }
      }
   }

   dict->encoding = encoding;

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryUseEncoding --
 *
 *	Set the encoding for a dictionary,
 *	and convert existing entries.
 *
 *	If encodingName is supplied, then it is used.
 *	Otherwise, defaultEncoding is used.
 *
 * Results:
 *      TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Dictionary string entries will be converted to unicode on success.
 *      Msg_Append is called on failure.
 *
 *-----------------------------------------------------------------------------
 */

static Bool
DictionaryUseEncoding(Dictionary *dict,            // IN: dictionary
                      const char *encodingName,    // IN: encoding
		      StringEncoding defaultEncoding) // IN: default encoding
{
   StringEncoding encodingEnum;
   Entry *e;

   ASSERT(dict != NULL);

   if (encodingName == NULL) {
      encodingEnum = Unicode_ResolveEncoding(defaultEncoding);
      encodingName = Unicode_EncodingEnumToName(encodingEnum);
   } else {
      encodingEnum = Unicode_EncodingNameToEnum(encodingName);
      if (!Unicode_IsEncodingValid(encodingEnum)) {

	 if (dict->currentFile == NULL) {
	    Msg_Append(MSGID(dictionary.unknownEncodingNoFile)
		       "Character encoding \"%s\" is not supported.\n",
		       encodingName);
	 } else if (dict->currentLine <= 0) {
	    Msg_Append(MSGID(dictionary.unknownEncodingNoLine)
		       "File \"%s\": "
		       "Character encoding \"%s\" is not supported.\n",
		       dict->currentFile, encodingName);
	 } else {
	    Msg_Append(MSGID(dictionary.unknownEncoding)
		       "File \"%s\" line %d: "
		       "Character encoding \"%s\" is not supported.\n",
		       dict->currentFile, dict->currentLine, encodingName);
	 }
	 return FALSE;
      }
   }

   if (dict->encoding != STRING_ENCODING_UNKNOWN &&
       dict->encoding != encodingEnum) {
      const char *e = Unicode_EncodingEnumToName(dict->encoding);
      // can also happen when encoding was set in code rather than in the file
      if (dict->currentFile == NULL) {
	 Msg_Append(MSGID(dictionary.hasEncodingNoFile)
		    "Dictionary already has an encoding \"%s\".\n",
		    e);
      } else if (dict->currentLine <= 0) {
	 Msg_Append(MSGID(dictionary.hasEncodingNoLine)
		    "File \"%s\": "
		    "File already has an encoding \"%s\".\n",
		    dict->currentFile, e);
      } else {
	 Msg_Append(MSGID(dictionary.hasEncoding)
		    "File \"%s\" line %d: "
		    "File already has an encoding \"%s\".\n",
		    dict->currentFile, dict->currentLine, e);
      }
      return FALSE;
   }
  
   /*
    * Check whether the encoding is invalid, then convert
    */ 

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if ((e->type == DICT_ANY || e->type == DICT_STRING) &&
           e->value.stringValue) {
         if (!Unicode_IsBufferValid(e->value.stringValue, -1, encodingEnum)) {
	    DictionaryEncodingError(dict, e->name, e->value.stringValue,
				    encodingEnum);
            return FALSE;
         }
      }
   }

   for (e = dict->firstEntry; e != NULL; e = e->next) {
      if ((e->type == DICT_ANY || e->type == DICT_STRING) &&
           e->value.stringValue) {
         char *tmp = e->value.stringValue;
         e->value.stringValue = Unicode_Alloc(e->value.stringValue,
                                              encodingEnum);
         free(tmp);
      }
   }

   dict->encoding = encodingEnum;

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * DictionaryEncodingError --
 *
 *	Msg_Append a nice message about encoding error.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      As described.
 *
 *-----------------------------------------------------------------------------
 */

void
DictionaryEncodingError(Dictionary *dict,
                        const char *name,
			const char *value,
			StringEncoding encoding)
{
   char *tmp = Unicode_EscapeBuffer(value, -1, encoding);

   if (dict->currentFile == NULL) {
      Msg_Append(MSGID(dictionary.badEncodedInputNoFile)
		 "Value \"%s\" for variable \"%s\" "
		 "is not valid in encoding \"%s\".\n",
		 tmp, name, Unicode_EncodingEnumToName(encoding));
   } else if (dict->currentLine <= 0) {
      Msg_Append(MSGID(dictionary.badEncodedInputNoLine)
		 "File \"%s\": Value \"%s\" for variable \"%s\" "
		 "is not valid in encoding \"%s\".\n",
		 dict->currentFile,
		 tmp, name, Unicode_EncodingEnumToName(encoding));
   } else {
      Msg_Append(MSGID(dictionary.badEncodedInput)
		 "File \"%s\" line %d: Value \"%s\" for variable \"%s\" "
		 "is not valid in encoding \"%s\".\n",
		 dict->currentFile, dict->currentLine,
		 tmp, name, Unicode_EncodingEnumToName(encoding));
   }

   free(tmp);
}
