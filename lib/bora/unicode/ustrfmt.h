/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *********************************************************/
/*
**********************************************************************
*   Copyright (C) 2001-2006, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*/

#ifndef USTRFMT_H
#define USTRFMT_H

#include "unicode/utypes.h"

U_CAPI int32_t U_EXPORT2
uprv_itou (UChar * buffer, int32_t capacity, uint32_t i, uint32_t radix, int32_t minwidth);


#endif
