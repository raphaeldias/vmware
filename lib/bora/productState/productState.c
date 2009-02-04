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
 * productState.c --
 *
 *      ProductState is a runtime encapsulation of the identity of a product
 *      and product dependent characteristics.
 */


#include <stdio.h>

#include "productState.h"

#include "vm_basic_defs.h"
#include "vm_version.h"
#include "util.h"
#include "str.h"
#include "strutil.h"
#include "escape.h"


/*
 * Type definitions.
 */

typedef struct ProductState ProductState;

struct ProductState
{
   Product product;

   char *name;
   char *version;
   unsigned int buildNumber;
   Bool buildNumberSet;

   char *licenseName;
   char *licenseVersion;
   /* etc ? */

   ProductCaps capabilities;

   /* Derived values */
   char *fullVersion;
   char *buildNumberString;
   char *registryPath;
   unsigned int versionNumber[3];
};


/*
 * Serialization key constants.
 */

#define PRODUCTSTATE_KEY_PRODUCT        "product"
#define PRODUCTSTATE_KEY_NAME           "name"
#define PRODUCTSTATE_KEY_VERSION        "version"
#define PRODUCTSTATE_KEY_BUILDNUMBER    "buildnumber"
#define PRODUCTSTATE_KEY_CAPABILITIES   "capabilities"
#define PRODUCTSTATE_KEY_LICENSENAME    "licensename"
#define PRODUCTSTATE_KEY_LICENSEVERSION "licenseversion"


/*
 * Global state.
 */

static ProductState sProductState;
static const char *sGenericName = PRODUCT_SHORT_NAME;
#ifdef PRODUCT_VERSION_NUMBER
static const char *sGenericVersion = PRODUCT_VERSION_NUMBER;
#else
static const char *sGenericVersion = "XXX"; /* Tools do not define a version number. */
#endif
static unsigned int sGenericBuildNumber = BUILD_NUMBER_NUMERIC;
static const char *sGenericLicenseName = PRODUCT_NAME_FOR_LICENSE;
static const char *sGenericLicenseVersion = PRODUCT_VERSION_STRING_FOR_LICENSE;
static Product sGenericProduct = PRODUCT_GENERIC
#ifdef VMX86_DESKTOP
                                 | PRODUCT_WORKSTATION
#endif
#ifdef VMX86_WGS
                                 | PRODUCT_SERVER
#endif
#ifdef VMX86_SERVER
                                 | PRODUCT_ESX
#endif
                                 ;


/*
 * Static function prototypes.
 */

char * ProductStateEscapeValue(const char *key, const char *value);
char * ProductStateUnescapeValue(const char *value);


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_Set --
 *
 *      Set the product state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      ProductState will be reset first.
 *
 *-----------------------------------------------------------------------------
 */

void
ProductState_Set(Product product,               // IN:
                 const char *name,              // IN:
                 const char *version,           // IN:
                 unsigned int buildNumber,      // IN:
                 ProductCaps capabilities,      // IN:
                 const char *licenseName,       // IN:
                 const char *licenseVersion)    // IN:
{
   ProductState_Reset();

   sProductState.product = product;
   sProductState.name = Util_SafeStrdup(name);
   sProductState.version = Util_SafeStrdup(version);
   sProductState.buildNumber = buildNumber;
   sProductState.buildNumberSet = TRUE;
   sProductState.capabilities = capabilities;
   sProductState.licenseName = Util_SafeStrdup(licenseName);
   sProductState.licenseVersion = Util_SafeStrdup(licenseVersion);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_Reset --
 *
 *      Finalise the product state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Global product state is restored to the generic state.
 *
 *-----------------------------------------------------------------------------
 */

void
ProductState_Reset(void)
{
   free(sProductState.fullVersion);
   free(sProductState.buildNumberString);
   free(sProductState.registryPath);

   free(sProductState.licenseVersion);
   free(sProductState.licenseName);
   free(sProductState.version);
   free(sProductState.name);

   memset(&sProductState, 0, sizeof(ProductState));
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetProduct --
 *
 *      Get the current product.
 *
 * Results:
 *      The Product.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Product
ProductState_GetProduct(void)
{
   return sProductState.product == PRODUCT_GENERIC ? sGenericProduct :
                                                     sProductState.product;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_IsProduct --
 *
 *      Check whether the current product is one of the requested products.
 *
 * Results:
 *      TRUE if so, FALSE if not.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
ProductState_IsProduct(ProductMask product) // IN:
{
   return (product &
           (sProductState.product == PRODUCT_GENERIC ? sGenericProduct : 
                                                      sProductState.product)) != 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetName --
 *
 *      Get the current product name.
 *
 * Results:
 *      The product name.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetName(void)
{
   return sProductState.name ? sProductState.name : sGenericName;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetVersion --
 *
 *      Get the current product version.
 *
 * Results:
 *      The product version.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetVersion(void)
{
   return sProductState.version ? sProductState.version : sGenericVersion;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetLicenseName --
 *
 *      Get the name used for licence checks.
 *
 * Results:
 *      The licence name.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetLicenseName(void)
{
   return sProductState.licenseName ? sProductState.licenseName :
                                      sGenericLicenseName;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetLicenseVersion --
 *
 *      Get the version used for licence checks.
 *
 * Results:
 *      The licence version.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetLicenseVersion(void)
{
   return sProductState.licenseVersion ? sProductState.licenseVersion :
                                         sGenericLicenseVersion;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetBuildNumber --
 *
 *      Get the current product build number.
 *
 * Results:
 *      The product build number.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

unsigned int
ProductState_GetBuildNumber(void)
{
   return sProductState.buildNumberSet ? sProductState.buildNumber :
                                         sGenericBuildNumber;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetCapabilities --
 *
 *      Get the current product capabilities.
 *
 *      XXX: Maybe a separate getter for each flag?
 *      Would make the implementation of flag storage more opaque.
 *
 * Results:
 *      The product capabilities.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

ProductCaps
ProductState_GetCapabilities(void)
{
   return sProductState.capabilities;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetFullVersion --
 *
 *      Get the current full product version. This is the base version and
 *      build number.
 *
 * Results:
 *      The full product version.
 *
 * Side effects:
 *      Full version is created and cached on the first call.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetFullVersion(void)
{
   if (!sProductState.fullVersion) {
      sProductState.fullVersion =
         Str_Asprintf(NULL, "%s %s", ProductState_GetVersion(),
                      ProductState_GetBuildNumberString());
      ASSERT_MEM_ALLOC(sProductState.fullVersion);
   }

   return sProductState.fullVersion;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetBuildNumberString --
 *
 *      Get the current product build number as a string.
 *
 * Results:
 *      The product build number as a string.
 *
 * Side effects:
 *      Full version is created and cached on the first call.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetBuildNumberString(void)
{
   if (!sProductState.buildNumberString) {
      sProductState.buildNumberString =
         Str_Asprintf(NULL, "build-%05u", ProductState_GetBuildNumber());
      ASSERT_MEM_ALLOC(sProductState.buildNumberString);
   }

   return sProductState.buildNumberString;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetRegistryPath --
 *
 *      Get the current product registry path (for the windows registry).
 *
 * Results:
 *      The product registry path.
 *
 * Side effects:
 *      Full version is created and cached on the first call.
 *
 *-----------------------------------------------------------------------------
 */

const char *
ProductState_GetRegistryPath(void)
{
   if (!sProductState.registryPath) {
      sProductState.registryPath =
         Str_Asprintf(NULL, "SOFTWARE\\" COMPANY_NAME "\\%s",
                      ProductState_GetName());
      ASSERT_MEM_ALLOC(sProductState.registryPath);
   }

   return sProductState.registryPath;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetRegistryPathForProduct --
 *
 *      Get the registry path (for the windows registry) for the product
 *      as identified by 'productName'.
 *
 * Results:
 *      The registry path for 'productName'.
 *
 * Side effects:
 *      Caller should free() allocated string.
 *
 *-----------------------------------------------------------------------------
 */

char *
ProductState_GetRegistryPathForProduct(const char *productName)
{
   char *str = NULL;
   str = Str_Asprintf(NULL, "SOFTWARE\\" COMPANY_NAME "\\%s",
                    productName);
   ASSERT_MEM_ALLOC(str);
   return str;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_GetVersionNumber --
 *
 *      Get the numeric components of the version number.
 *
 * Results:
 *      The three parts of the version number as out parameters.
 *
 * Side effects:
 *      Version components are parsed and cached on the first call.
 *
 *-----------------------------------------------------------------------------
 */

void
ProductState_GetVersionNumber(unsigned int *major,      // OUT: Optional
                              unsigned int *minor,      // OUT: Optional
                              unsigned int *patchLevel) // OUT: Optional
{
   /*
    * If we ever find ourselves in a situation where we set a 0.x.y
    * version number, then we'll never consider the number cached, but
    * correctness is not affected.
    */
   if (sProductState.versionNumber[0] == 0) {
      const char *versionString = ProductState_GetVersion();

      /*
       * XXX: In the grand scheme of things, having the canonical
       * version number expressed as a string is dubious at best.
       * A past consequence of this was the need to add an additional
       * instance of the version in a numerically friendly form
       * and in a way that only yields the correct number for a
       * subset of product types. (yay.) The following logic
       * attempts to improve correctness without changing the
       * defines we use. In the long term, I want to see the
       * canonical version number be defined numerically and have
       * the string derived - but that requires waiting until
       * ProductState is used everywhere and we've abstracted the
       * gory details of vm_version.h --plangdale
       */
      if (strcmp(versionString, "e.x.p") == 0) {
         /*
          * PRODUCT_VERSION includes the build number as a forth
          * element, so we must discard it.
          */
         unsigned int productVersion[4] = {PRODUCT_VERSION};

         sProductState.versionNumber[0] = productVersion[0];
         sProductState.versionNumber[1] = productVersion[1];
         sProductState.versionNumber[2] = productVersion[2];
      } else {
         int count;
         count = sscanf(versionString, "%u.%u.%u",
                        &sProductState.versionNumber[0],
                        &sProductState.versionNumber[1],
                        &sProductState.versionNumber[2]);
         ASSERT(count == 3);
      }
   }

   if (major) {
      *major = sProductState.versionNumber[0];
   }
   if (minor) {
      *minor = sProductState.versionNumber[1];
   }
   if (patchLevel) {
      *patchLevel = sProductState.versionNumber[2];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_Serialize --
 *
 *      Serialize the selected parts of the current productState to a string.
 *
 * Results:
 *      Allocated string containing serialized productState.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
ProductState_Serialize(ProductStateSerializationFlags flags) // IN:
{
   char *product = NULL;
   char *name = NULL;
   char *version = NULL;
   char *buildnumber = NULL;
   char *capabilities = NULL;
   char *licensename = NULL;
   char *licenseversion = NULL;

   char *retVal = NULL;

   if (flags & PRODUCTSTATE_FLAG_PRODUCT) {
      product = Str_Asprintf(NULL, "%s=%u;", PRODUCTSTATE_KEY_PRODUCT,
                             ProductState_GetProduct());
      ASSERT_MEM_ALLOC(product);
   }
   if (flags & PRODUCTSTATE_FLAG_NAME) {
      name = ProductStateEscapeValue(PRODUCTSTATE_KEY_NAME,
                                     ProductState_GetName());
   }
   if (flags & PRODUCTSTATE_FLAG_VERSION) {
      version = ProductStateEscapeValue(PRODUCTSTATE_KEY_VERSION,
                                        ProductState_GetVersion());
   }
   if (flags & PRODUCTSTATE_FLAG_BUILDNUMBER) {
      buildnumber = Str_Asprintf(NULL, "%s=%u;", PRODUCTSTATE_KEY_BUILDNUMBER,
                                 ProductState_GetBuildNumber());
      ASSERT_MEM_ALLOC(buildnumber);
   }
   if (flags & PRODUCTSTATE_FLAG_CAPABILITIES) {
      capabilities = Str_Asprintf(NULL, "%s=%"FMT64"u;", PRODUCTSTATE_KEY_CAPABILITIES,
                                  ProductState_GetCapabilities());
      ASSERT_MEM_ALLOC(capabilities);
   }
   if (flags & PRODUCTSTATE_FLAG_LICENSENAME) {
      licensename = ProductStateEscapeValue(PRODUCTSTATE_KEY_LICENSENAME,
                                            ProductState_GetLicenseName());
   }
   if (flags & PRODUCTSTATE_FLAG_LICENSEVERSION) {
      licenseversion = ProductStateEscapeValue(PRODUCTSTATE_KEY_LICENSEVERSION,
                                               ProductState_GetLicenseVersion());
   }

   retVal = Str_Asprintf(NULL, "%s%s%s%s%s%s%s",
                         product ? product : "",
                         name ? name : "",
                         version ? version : "",
                         buildnumber ? buildnumber : "",
                         capabilities ? capabilities : "",
                         licensename ? licensename : "",
                         licenseversion ? licenseversion : "");
   ASSERT_MEM_ALLOC(retVal);

   free(product);
   free(name);
   free(version);
   free(buildnumber);
   free(capabilities);
   free(licensename);
   free(licenseversion);

   return retVal;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductState_Deserialize --
 *
 *      Deserialize a productState string and replace the current state with it.
 *
 * Results:
 *      Bit flags indicating which productState elements were replaced.
 *
 * Side effects:
 *      Current productState will be replaced.
 *
 *-----------------------------------------------------------------------------
 */

ProductStateSerializationFlags
ProductState_Deserialize(const char *state) // IN:
{
   Product product;
   char *name;
   char *version;
   unsigned int buildNumber;
   ProductCaps capabilities;
   char *licenseName;
   char *licenseVersion;

   unsigned int index = 0;
   const size_t length = strlen(state);
   ProductStateSerializationFlags flags = PRODUCTSTATE_FLAG_NONE;

   product = ProductState_GetProduct();
   name = Util_SafeStrdup(ProductState_GetName());
   version = Util_SafeStrdup(ProductState_GetVersion());
   buildNumber = ProductState_GetBuildNumber();
   capabilities = ProductState_GetCapabilities();
   licenseName = Util_SafeStrdup(ProductState_GetLicenseName());
   licenseVersion = Util_SafeStrdup(ProductState_GetLicenseVersion());

   while (index < length) {
      char *item = NULL;
      char *key = NULL;
      char *value = NULL;
      const char *escapedValue;
      unsigned int pairIndex = 0;

      item = StrUtil_GetNextToken(&index, state, ";");
      if (item == NULL) {
         /* while() test should return false on next iteration. */
         goto cleanup;
      }

      key = StrUtil_GetNextToken(&pairIndex, item, "=");
      if (key == NULL) {
         /* Ignore malformed item */
         goto cleanup;
      }

      /* Saves an allocation and avoids having to escape '='. */
      escapedValue = item + pairIndex + 1;
      if (escapedValue >= item + strlen(item)) {
         /* Ignore malformed item */
         goto cleanup;
      }
      value = ProductStateUnescapeValue(escapedValue);

      if (strcmp(key, PRODUCTSTATE_KEY_PRODUCT) == 0) {
         int32 numValue;

         if (StrUtil_StrToInt(&numValue, value)) {
            flags |= PRODUCTSTATE_FLAG_PRODUCT;
            product = (Product)numValue;
         }
      } else if (strcmp(key, PRODUCTSTATE_KEY_NAME) == 0) {
         flags |= PRODUCTSTATE_FLAG_NAME;
         free(name);
         name = Util_SafeStrdup(value);
      } else if (strcmp(key, PRODUCTSTATE_KEY_VERSION) == 0) {
         flags |= PRODUCTSTATE_FLAG_VERSION;
         free(version);
         version = Util_SafeStrdup(value);
      } else if (strcmp(key, PRODUCTSTATE_KEY_BUILDNUMBER) == 0) {
         int32 numValue;

         if (StrUtil_StrToInt(&numValue, value)) {
            flags |= PRODUCTSTATE_FLAG_BUILDNUMBER;
            buildNumber = (unsigned int)numValue;
         }
      } else if (strcmp(key, PRODUCTSTATE_KEY_CAPABILITIES) == 0) {
         int64 numValue;

         if (StrUtil_StrToInt64(&numValue, value)) {
            flags |= PRODUCTSTATE_FLAG_CAPABILITIES;
            capabilities = (ProductCaps)numValue;
         }
      } else if (strcmp(key, PRODUCTSTATE_KEY_LICENSENAME) == 0) {
         flags |= PRODUCTSTATE_FLAG_LICENSENAME;
         free(licenseName);
         licenseName = Util_SafeStrdup(value);
      } else if (strcmp(key, PRODUCTSTATE_KEY_LICENSEVERSION) == 0) {
         flags |= PRODUCTSTATE_FLAG_LICENSEVERSION;
         free(licenseVersion);
         licenseVersion = Util_SafeStrdup(value);
      } else {
         /* Ignore unknown key. */
      }

    cleanup:
      free(item);
      free(key);
      free(value);
   }

   ProductState_Set(product, name, version, buildNumber,
                    capabilities, licenseName, licenseVersion);

   free(name);
   free(version);
   free(licenseName);
   free(licenseVersion);

   return flags;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductStateEscapeValue --
 *
 *      Allocate a key=value string where the value has been escaped to remove
 *      the key/value pair separator.
 *
 * Results:
 *      Allocated string containinging key=value pair with escaped value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
ProductStateEscapeValue(const char *key,
                        const char *value) // IN:
{
   char *escapedValue;
   char *retItem;
   int bytesToEsc[256] = { 0, };

   bytesToEsc[';'] = 1;
   bytesToEsc['#'] = 1;

   escapedValue = Escape_Do('#', bytesToEsc, value, strlen(value), NULL);
   ASSERT_MEM_ALLOC(escapedValue);

   retItem = Str_Asprintf(NULL, "%s=%s;", key, escapedValue);
   ASSERT_MEM_ALLOC(retItem);

   free(escapedValue);

   return retItem;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ProductStateUnescapeValue --
 *
 *      Unescape a single value previously escaped with ProductStateEscapeValue.
 *
 * Results:
 *      Allocated string containing unescaped value.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

char *
ProductStateUnescapeValue(const char *value) // IN:
{
   char *retValue = Escape_Undo('#', value, strlen(value), NULL);
   ASSERT_MEM_ALLOC(retValue);

   return retValue;
}
