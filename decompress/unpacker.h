/******************************************************************************
 * Copyright (c) 2022 Jaroslav Hensl                                          *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person                *
 * obtaining a copy of this software and associated documentation             *
 * files (the "Software"), to deal in the Software without                    *
 * restriction, including without limitation the rights to use,               *
 * copy, modify, merge, publish, distribute, sublicense, and/or sell          *
 * copies of the Software, and to permit persons to whom the                  *
 * Software is furnished to do so, subject to the following                   *
 * conditions:                                                                *
 *                                                                            *
 * The above copyright notice and this permission notice shall be             *
 * included in all copies or substantial portions of the Software.            *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,            *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES            *
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                   *
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT                *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,               *
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING               *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR              *
 * OTHER DEALINGS IN THE SOFTWARE.                                            *
 *                                                                            *
*******************************************************************************/
#ifndef __UNPACKER_INCLUDED__
#define __UNPACKER_INCLUDED__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "filesystem.h"

/*
 * Program flow constants
 */

/* error codes */
#define PATCH_OK           0
#define PATCH_E_READ       1
#define PATCH_E_WRITE      2
#define PATCH_E_CHECK      3
#define PATCH_E_MEM        4
#define PATCH_E_OVERWRITE  5
#define PATCH_E_WRONG_TYPE 6
#define PATCH_E_CONVERT    7
#define PATCH_E_NOTFOUND   8
#define PATCH_E_PATCHED    9
#define PATCH_E_NOTFOUNDINCAB    10
#define PATCH_E_NOTFOUNDINCABS   11

/* compresion control */
#define PATCH_FORCE_W3 0x01000000
#define PATCH_FORCE_W4 0x02000000

/*
 * Functions
 */
 
int wx_unpack(const char *src, const char *infilename, const char *out, const char *tmpname);
int wx_to_w3(const char *in, const char *out);
int wx_to_w4(const char *in, const char *out);

struct vxd_filelist;
typedef struct vxd_filelist vxd_filelist_t;
vxd_filelist_t *vxd_filelist_open(const char *file, const char *tmp);
const char *vxd_filelist_get(vxd_filelist_t *list);
void vxd_filelist_close(vxd_filelist_t *list);

#endif /* __UNPACKER_INCLUDED__ */
