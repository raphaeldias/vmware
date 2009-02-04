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

#ifndef _DICTIONARY_H_
#define _DICTIONARY_H_

#define INCLUDE_ALLOW_USERLEVEL
#include "includeCheck.h"

#include "unicodeTypes.h"


/*
 * Dictionary processing
 */

typedef enum DictionaryType {
   DICT_ANY,
   DICT_STRING,
   DICT_BOOL,
   DICT_LONG,
   DICT_DOUBLE,
   DICT_TRISTATE,
   DICT_INT64,

   DICT_VERSIONSPEC = 0x1000,  // Write out first in file if set.
   DICT_DONTENCRYPT = 0x2000   // Never encrypt this entry.
} DictionaryType;

/*
 * The defaultLevel of the entry indicates how the entry was created.
 * config_setXXX() creates an entry with DICT_NOT_DEFAULT
 *                 which means it is to be stored in the file.
 * config_getXXX() on a new entry creates an entry with DICT_COMPILED_DEFAULT.
 * DICT_LOADED_DEFAULT was used for the strings passed with -s option and
 *                 indicates the entry is not to be stored in a file and
 *                 does NOT generate panic when default value is changed,
 *                 but it was made obsolete by the multiple dictionaries.
 */
#define DICT_NOT_DEFAULT	0x0
#define DICT_COMPILED_DEFAULT	0x1
#define DICT_LOADED_DEFAULT	0x2
#define DICT_DEFAULT_MASK	0xf

#define DICT_LOG		0x10

#define KEYSAFE_NAME "encryption.keySafe" // Encryption KeySafe entry name
#define ENCRYPTED_DATA_NAME "encryption.data" // Encrypted data entry name

typedef struct Dictionary Dictionary;

struct KeyLocatorState;
struct KeySafeUserRing;
struct KeySafe;

EXTERN void *dictionaryNoDefault; //  use its address

extern Dictionary *Dictionary_Create(void);
extern void Dictionary_Free(Dictionary *dict);
extern void Dictionary_Clear(Dictionary *dict);
extern void Dictionary_ClearPreserveKeys(Dictionary *dict);
extern void Dictionary_SetAll(Dictionary *dict, const char *prefix,
			      DictionaryType type, void *pvalue);
extern Bool Dictionary_Rekey(Dictionary *, const struct KeySafeUserRing *);
extern struct KeySafe *Dictionary_GetKeySafe(Dictionary *);
extern Bool Dictionary_CopyCryptoState(Dictionary *targetDict,
                                       Dictionary *sourceDict);
extern Bool Dictionary_Unlock(Dictionary *, struct KeyLocatorState *,
                              const struct KeySafeUserRing *,
                              int defaultLevel);
extern Bool Dictionary_IsEncrypted(Dictionary *);

extern Bool Dictionary_VerifyExistsAndIsEnc(ConstUnicode pathName);

/*
 * Load or write a dictionary file
 */

extern Bool Dictionary_Load(Dictionary *dict,
                            ConstUnicode pathName,
			    int defaultLevel);

extern Bool Dictionary_LoadWithDefaultEncoding(Dictionary *dict,
                                               ConstUnicode pathName,
                                               int defaultLevel,
                                               StringEncoding defaultEncoding);

extern Bool Dictionary_LoadAndUnlock(Dictionary *dict,
                                     ConstUnicode pathName,
                                     struct KeyLocatorState *,
                                     const struct KeySafeUserRing *,
                                     int defaultLevel);

extern Bool Dictionary_Append(Dictionary *dict,
                              ConstUnicode pathName,
                              int defaultLevel);

extern Bool Dictionary_Write(Dictionary *dict,
                             ConstUnicode pathName);

extern Bool Dictionary_WriteSync(Dictionary *dict,
                                 ConstUnicode pathName);

extern Bool Dictionary_MakeExecutable(ConstUnicode pathName);

/*
 * Read dictionary entries from a string buffer instead of a file (entries
 * still separated by newline character)
 */
extern Bool Dictionary_LoadFromBuffer(Dictionary *dict, const char *buffer,
                                      int defaultLevel, Bool append);

extern Bool Dictionary_LoadFromBufferWithDefaultEncoding(Dictionary *dict,
	                              const char *buffer, int defaultLevel,
	                              Bool append, StringEncoding defaultEncoding);

extern Bool Dictionary_WriteToBuffer(Dictionary *dict, Bool enableEncrypt,
                                     char **outbuf, size_t *outsize);

/*
 * Find out if a dictionary variable is defined.
 */
extern Bool Dictionary_IsDefined(Dictionary *dict, const char *name);
extern Bool Dictionary_NotSet(Dictionary *dict, const char *name);

/*
 * Set a dictionary entry.  The string must have the form parameter=value.
 * This function is useful for processing command-line parameters.
 */
extern void Dictionary_SetFromString(Dictionary *dict, const char *string);
extern void Dictionary_RestoreFromString(Dictionary *dict, const char *string);

/*
 * Set a dictionary entry, adding it if necessary.
 */
extern void Dictionary_Set(Dictionary *dict, const void *value,
			   DictionaryType type,
			   const char *name);

/*
 * Unsets a dictionary entry.
 */
extern void Dictionary_Unset(Dictionary *dict, const char *name);

/*
 * Unsets all dictionary entries that have the given prefix.
 */
extern void Dictionary_UnsetWithPrefix(Dictionary *dict, const char *prefix);

/*
 * Get an entry value as a string, or NULL if not defined.
 * The string should be considered static, and not valid
 * after a subsequent call to any dictionary function.
 */
extern char *Dictionary_GetAsString(Dictionary *dict,
                                    const char *name);

/*
 * Get a pointer to an entry value.  If the entry is not in
 * the dictionary, it is added and set to defaultValue.
 */
extern void *Dictionary_Get(Dictionary *dict, const void *pDefaultValue,
                            DictionaryType type,
                            const char *name);

extern Bool Dictionary_NeedSave(Dictionary *dict);

extern void Dictionary_MarkModified(Dictionary *dict,
				    const char *name);
extern void Dictionary_LogNotDefault(Dictionary *dict);

extern Bool Dictionary_StringToBool(const char *s, Bool *error);
extern Bool Dictionary_SetExecPath(Dictionary *dict, const char *execPath);
extern void Dictionary_SetFileHeader(char const * const *comments,
				     Dictionary *dict);
extern void Dictionary_Update(Dictionary *oldDic, Dictionary *newDic);
extern int Dictionary_Iterate(Dictionary *dict,
			      void (*f)(const char *name,
					const char *value,
					int i,
					void *prv),
			      void *prv,
			      Bool doDefault);

extern void Dict_SetAny(Dictionary *dict, const char *x, const char *fmt, ...);
extern void Dict_SetString(Dictionary *dict, const char *x,
			   const char *fmt, ...);
extern void Dict_SetStringPlain(Dictionary *dict, const char *x,
                                const char *fmt, ...);
extern void Dict_SetBool(Dictionary *dict, Bool x, const char *fmt, ...);
extern void Dict_SetBoolPlain(Dictionary *dict, Bool x, const char *fmt, ...);
extern void Dict_SetLong(Dictionary *dict, int32 x, const char *fmt, ...);
extern void Dict_SetLongPlain(Dictionary *dict, int32 x, const char *fmt, ...);
extern void Dict_SetInt64(Dictionary *dict, int64 x, const char *fmt, ...);
extern void Dict_SetInt64Plain(Dictionary *dict, int64 x, const char *fmt, ...);
extern void Dict_SetDouble(Dictionary *dict, double x, const char *fmt, ...);
extern void Dict_Unset(Dictionary *dict, const char *fmt, ...);

extern char *Dict_GetString(Dictionary *dict, const char *def,
			    const char *fmt, ...);
extern char *Dict_GetStringPlain(Dictionary *dict, const char *def,
                                 const char *fmt, ...);
extern char *Dict_GetStringEnum(Dictionary *dict, const char *def,
                                const char **choices,
                                const char *fmt, ...);
extern Bool Dict_GetBool(Dictionary *dict, Bool def, const char *fmt, ...);
extern Bool Dict_GetBoolPlain(Dictionary *dict, Bool def, const char *fmt, ...);
extern int32 Dict_GetLong(Dictionary *dict, int32 def, const char *fmt, ...);
extern int32 Dict_GetLongPlain(Dictionary *dict, int32 def, const char *fmt, ...);
extern int64 Dict_GetInt64(Dictionary *dict, int64 def, const char *fmt, ...);
extern int64 Dict_GetInt64Plain(Dictionary *dict, int64 def, const char *fmt, ...);
extern double Dict_GetDouble(Dictionary *dict, double def, const char *fmt, ...);

extern void Dictionary_Marshall(Dictionary *dict, unsigned char **buffer,
				size_t *size);
extern void Dictionary_MarshallModified(Dictionary *dict, unsigned char **buffer,
		                        size_t *size);

extern char * Dictionary_DeriveFileName(Dictionary *dict, 
                                        const char *baseFileName,
                                        const char *devname,
                                        const char *attrname);
extern int Dictionary_NumberOfEntries(Dictionary *dict);

extern StringEncoding Dictionary_GetEncoding(Dictionary *dict);
extern void Dictionary_SetEncoding(Dictionary *dict,
                                   StringEncoding encoding);
extern Bool Dictionary_ChangeEncoding(Dictionary *dict,
                                      StringEncoding encoding);

#endif // _DICTIONARY_H_
