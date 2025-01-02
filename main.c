#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>

#include "decompress/unpacker.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t i16;
typedef int32_t i32;

typedef struct {
    u32 leOffset;
    u32 objectTableOffset;
    u32 objectTableCount;
    u32 pcodTableEntryOffset;
    u32 pcodOffset;
    u32 pcodSize;
    u32 pcodPaddedSize;
    u32 pcodPageCount;
    u32 pcodFirstPageIndex;
    u8 *pcodData;
    u32 pcodPatchOffset;
    u8 *pcodPatchData;
    u32 pageSize;
    u32 pageDataStart;
} mfContext;

#define VXD_OBJ_TABLE_ENTRY_SIZE    (0x18)
#define VXD_PAGE_TABLE_ENTRY_SIZE   (8)

#define MSMOUSE_CURVE_LENGTH        (32)

static void write8 (u8 *data, u32 offset, u8  value)    { memcpy (&data[offset], (u8*) &value, sizeof(value)); }
static void write16(u8 *data, u32 offset, u16 value)    { memcpy (&data[offset], (u8*) &value, sizeof(value)); }
static void write32(u8 *data, u32 offset, u32 value)    { memcpy (&data[offset], (u8*) &value, sizeof(value)); }

static u8   read8  (u8 *data, u32 offset)               { return *((u8*)  (&data[offset])); }
static u16  read16 (u8 *data, u32 offset)               { return *((u16*) (&data[offset])); }
static u32  read32 (u8 *data, u32 offset)               { return *((u32*) (&data[offset])); }

static u32  getAbsoluteTargetFromCallInstruction32(u8 *data, u32 eip) {
    u8 *dataAtCallInstruction = data + eip;

    assert(data != NULL);
    assert(dataAtCallInstruction[0] == 0xe8);

    return eip + read32(dataAtCallInstruction, 1) + sizeof(u32) + 1;
}

static u32  calcDestinationFromCallInstruction32(u32 eip, u32 target) {
    return target - (eip + sizeof(u32) + 1);
}

static void patchCall32(u8 *data, u32 eip, u32 newTarget) {
    u8 *dataAtCallInstruction = data + eip;

    assert(data != NULL);
    assert(dataAtCallInstruction[0] == 0xe8);

    printf("Patching call at %x to %x, ", eip, getAbsoluteTargetFromCallInstruction32(data, eip));
    write32(dataAtCallInstruction, 1, calcDestinationFromCallInstruction32(eip, newTarget));
    printf("new target: %x\n", getAbsoluteTargetFromCallInstruction32(data, eip));

}

static long filesize(FILE *f) {
    long cur = ftell(f);
    long ret = 0;
    fseek(f, 0, SEEK_END);
    ret = ftell(f);
    fseek(f, cur, SEEK_SET);
    return ret;
}

static u8 *openAndReadWholeFile(const char *fname, long *size) {
    FILE *in = NULL;
    u8 *data = NULL;

    assert(fname != NULL);
    assert(size != NULL);

    in = fopen(fname, "rb");

    if (in == NULL) {
        goto error;
    }

    *size = filesize(in);

    if (*size <= 0)
        goto error;
    
    data = calloc(*size, 1);

    if (data == NULL)
        goto error;

    if (fread(data, 1, *size, in) != *size)
        goto error;

    fclose(in);
    return data;

error:
    perror("Error reading file");
    free(data);
    fclose(in);
    *size = -1;
    return NULL;
}

static bool openAndWriteWholeFile(const char *fname, const u8 *data, long size) {
    FILE *file = NULL;

    assert(fname != NULL);
    assert(data != NULL);
    assert(size > 0);

    file = fopen(fname, "wb");

    if (file == NULL) {
        goto error;
    }

    if (fwrite(data, 1, size, file) != size)
        goto error;

    fclose(file);
    return true;

error:
    perror("Error writing file");
    
    if (file != NULL)
        fclose(file);
    return false;
}

/* Finds a byte pattern in some data, returns NULL if not found */
u8 *findBytes(const u8 *haystack, const u8 *needle, size_t haystackSize, size_t needleSize) {
    const u8 *ceiling = haystack + haystackSize;
    
    assert(haystack != NULL);
    assert(needle != NULL); 
    assert(needleSize <= haystackSize);
    assert(needleSize != 0);
    
    while (haystack + needleSize <= ceiling) {
        if (0 == memcmp(haystack, needle, needleSize)) {
            return (u8*) haystack;
        }
        haystack++;
    }

    return NULL;
}

/* Patch VMOUSE.VXD to fix mouse being faster in Windows than in DOS */
bool patchVmouseVxd(const char *fname) {
    mfContext ctx = {0};
    u8 *data = NULL;
    long dataSize = 0;

    char fname_bak[] = "VMOUSE.BAK";

    /* Open and read VMOUSE VXD file */

    printf("Patching %s\n", fname);
    data = openAndReadWholeFile(fname, &dataSize);

    if (data == NULL)
        goto cleanup;

    /* File is read, check if it is already patched */

    if (NULL != findBytes(data, (const u8*) "Oerg866", dataSize, 7)) {
        printf("ERROR: File %s is already patched!\n", fname);
        goto cleanup;
    }

    /* Back up the file before doing anything */
    
    openAndWriteWholeFile(fname_bak, data, dataSize);

    /* Open file header */

    ctx.leOffset = read16(data, 0x3C);
    if (0 != memcmp(&data[ctx.leOffset], "LE", 2)) {
        printf("Header check failed\n");
        goto cleanup;
    }

    /* Valid LE File, get object table */

    ctx.objectTableOffset = ctx.leOffset + read32(data, ctx.leOffset + 0x40);
    ctx.objectTableCount = read32(data, ctx.leOffset + 0x44);

    /* Size of 1 data / code page and offset of first page */

    ctx.pageSize = read32(data, ctx.leOffset + 0x28);
    ctx.pageDataStart = read32(data, ctx.leOffset + 0x80);


    /* Find PCOD object */

    for (u32 i = 0; i < ctx.objectTableCount; i++) {
        u32 tableEntryOffset = ctx.objectTableOffset + i * VXD_OBJ_TABLE_ENTRY_SIZE;
        u8 *entry = data + tableEntryOffset;
        if (0 == memcmp(entry + 0x14, "PCOD", 4)) {
            printf("PCOD object found at %08x\n", tableEntryOffset);
            ctx.pcodTableEntryOffset = tableEntryOffset;
            break;
        }
    }

    /* PCOD object found, let's find it in the file */

    ctx.pcodSize = read32(data, ctx.pcodTableEntryOffset + 0);
    ctx.pcodPageCount = read32(data, ctx.pcodTableEntryOffset + 0x10);
    ctx.pcodPaddedSize = ctx.pageSize * ctx.pcodPageCount;
    ctx.pcodFirstPageIndex = read32(data, ctx.pcodTableEntryOffset + 0x0C);

    printf("PCOD object size: %x, padded size: %x, page table index %x\n", ctx.pcodSize, ctx.pcodPaddedSize, ctx.pcodFirstPageIndex);

    /* Find the page in the file, page index is 1-based */

    ctx.pcodOffset = ctx.pageDataStart + ctx.pageSize * (ctx.pcodFirstPageIndex - 1);
    ctx.pcodData = data + ctx.pcodOffset;
    ctx.pcodPatchOffset = ctx.pcodOffset + ctx.pcodSize;
    ctx.pcodPatchData = data + ctx.pcodPatchOffset;

    printf("PCOD Absolute offset in file: %x\n", ctx.pcodOffset);

    /* We need enough space, 64 bytes to be sure */

    if ((ctx.pcodPaddedSize - ctx.pcodSize) < 64) {
        printf("ERROR: Not enough padding in PCOD page to patch the file\n");
        goto cleanup;
    }

    /* increase size of pcod segment so we can put our patch in */
    
    ctx.pcodSize += 64;
    write32(data, ctx.pcodTableEntryOffset, ctx.pcodSize);

    /* Find binary patterns */
    
    /*  End of initMouseSens function, the only part of it that is unique and overlaps between 98SE and ME
        PCOD:C000238F                 mov     si, ax
        PCOD:C0002392                 mov     [edx+1Ch], esi
        PCOD:C0002395                 clc
        PCOD:C0002396                 retn
    */
    const u8 initMouseSensPattern[] = { 0x66, 0x8b, 0xf0, 0x89, 0x72, 0x1c, 0xf8, 0xc3 };

    /*  Middle of ChangeMouseSens function, right inbetween the two calls to modify
        PCOD:C000147C                 mov     esi, eax
        PCOD:C000147E                 movzx   eax, word ptr [ebp+18h]
        PCOD:C0001482                 cmp     eax, edi
        PCOD:C0001484                 jb      short loc_C0001488
        PCOD:C0001486                 mov     eax, edi
        PCOD:C0001488
        PCOD:C0001488 loc_C0001488:                           ; CODE XREF: PCOD:C0001484↑j
        PCOD:C0001488                 mov     [edx+6Fh], al
    */
    const u8 changeMouseSensPattern[] = { 0x8B, 0xF0, 0x0F, 0xB7, 0x45, 0x18, 0x3B };

    u8 *initMouseSensCode = findBytes(ctx.pcodData, initMouseSensPattern, ctx.pcodSize, sizeof(initMouseSensPattern));
    u8 *changeMouseSensCode = findBytes(ctx.pcodData, changeMouseSensPattern, ctx.pcodSize, sizeof(changeMouseSensPattern));

    if (initMouseSensCode == NULL || changeMouseSensCode == NULL) {
        printf("Function not found in file.\n");
        goto cleanup;
    }

    /* Patch the 4 calls */

    u32 callsToPatch[4];

    callsToPatch[0] = initMouseSensCode - data - 0x19; /* Init Mouse Sens: call for X axis */
    callsToPatch[1] = initMouseSensCode - data - 0x05; /* Init Mouse Sens: call for Y axis */
    callsToPatch[2] = changeMouseSensCode - data - 0x05; /* Change Mouse Sens: Call for X axis */
    callsToPatch[3] = changeMouseSensCode - data + 0x0f; /* Change Mouse Sens: Call for Y axis */

    u32 originalCallDest = getAbsoluteTargetFromCallInstruction32(data, callsToPatch[0]);

    printf("Original CalculateMouseSpeed Function Location: %x\n", originalCallDest);

    for (int i = 0; i < 4; i++) {
        patchCall32(data, callsToPatch[i], ctx.pcodPatchOffset);
    }

    u8 patchCode[] = {
        0x53,                           /* push ebx */
        0x83, 0x7b, 0x0c, 0x01,         /* cmp dword ptr [ebx+0Ch], 1 */
        0x74, 0x02,                     /* jz short NoSysVM */
        0xd1, 0xe0,                     /* shl eax, 1 */
        /* NoSysVM: */
        0xe8, 0x00, 0x00, 0x00, 0x00,   /* call CalculateSensitivity ; We need to patch this Offset */
        0x5b,                           /* pop ebx */
        0xc3,                           /* ret */
        /* Signature to detect if file is already patched */
        0x4F, 0x65, 0x72, 0x67, 0x38, 0x36, 0x36 /* ASCII 'Oerg866' */
    };

    /* Copy the patch code into the patch offset in the pcod object */
    memcpy(ctx.pcodPatchData, patchCode, sizeof(patchCode));

    /* Our patch code has a call to the original function, so we need to patch in that address */
    patchCall32(data, ctx.pcodPatchOffset + 0x09, originalCallDest);

    if (!openAndWriteWholeFile(fname, data, dataSize)) {
        printf("Error writing the patched data to the file!\n");
        goto cleanup;
    }

    return true;

cleanup:
    free(data);
    return false;
}


static void msmouseOverwriteAccelProfiles(u8 *mouseCurveDataTable, const u8 *newCurve) {
    for (int i = 0; i < 4; i++) {
        u8 *curveToPatch = mouseCurveDataTable + MSMOUSE_CURVE_LENGTH * i;
        memcpy(curveToPatch, newCurve, MSMOUSE_CURVE_LENGTH);
    }
}

static u8 *msmouseFindAndPatchAccelCurveTable(u8 *data, long dataSize, const u8 *curveToFind, const u8* unacceleratedCurve) {
    u8 *curveTable = findBytes(data, curveToFind, dataSize, MSMOUSE_CURVE_LENGTH);

    if (curveTable == NULL) {
        printf("Mouse acceleration curves not found!\n");
        return NULL;
    }

    curveTable -= 3 * MSMOUSE_CURVE_LENGTH;

    printf("Patching mouse acceleration curve table at %x\n", curveTable - data);

    msmouseOverwriteAccelProfiles(curveTable, unacceleratedCurve);

    return curveTable;
}

static bool patchMsmouseVxd(const char *fname) {
    const char patchMarker[] = "MSMINI Unaccelerated by Oerg866";
    const char fname_bak[] = "MSMOUSE.BAK";
    u8 *data = NULL;
    long dataSize = 0;


    assert(fname != NULL);

    printf("Patching %s\n", fname);

    data = openAndReadWholeFile(fname, &dataSize);

    if (data == NULL)
        goto cleanup;

    /* Find version information */

    char *fileDescription = (char *) findBytes(data, (const u8 *)"FileDescription", dataSize, strlen("FileDescription") + 1);

    if (fileDescription == NULL)
        goto cleanup;

    /* Check for patch marker */

    fileDescription += strlen("FileDescription") + 1;

    printf("Driver: %s\n", fileDescription);

    if (0 == strcmp(fileDescription, patchMarker)) {
        printf("Error: File %s is already patched!\n", fname);
        goto cleanup;
    }

    /* Back up the file before doing anything */
    
    openAndWriteWholeFile(fname_bak, data, dataSize);

    /* Find mouse acceleration curves to patch*/

    u8 mouseCurveTable1Entry4[] = { 0x01, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };
    u8 mouseCurveTable2Entry4[] = { 0x10, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };

    u8 mouseCurveTable1Unaccel[] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 };
    u8 mouseCurveTable2Unaccel[] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 };

    

    /* The first curve table exists twice */

    u8 *mouseCurveData = data;

    long leftAfterSearch = dataSize;

    for (int i = 0; i < 2; i++) {
        mouseCurveData = msmouseFindAndPatchAccelCurveTable(mouseCurveData, leftAfterSearch, mouseCurveTable1Entry4, mouseCurveTable1Unaccel) + MSMOUSE_CURVE_LENGTH;
        leftAfterSearch = dataSize - (mouseCurveData - data);

        if (leftAfterSearch < 0 || mouseCurveData == NULL)
            goto cleanup;

        mouseCurveData += MSMOUSE_CURVE_LENGTH * 4;
    }

    /* Second curve only once */

    if (msmouseFindAndPatchAccelCurveTable(data, dataSize, mouseCurveTable2Entry4, mouseCurveTable2Unaccel) == NULL)
        goto cleanup;

    /* Add marker so we know the file is patched */
    sprintf(fileDescription, "%s", patchMarker);

    /* Write out patched file */

    if (!openAndWriteWholeFile(fname, data, dataSize))
        goto cleanup;

    free(data);
    return true;


cleanup:
    free(data);
    return false;
}

int main(int argc, char *argv[]) {
    char vmouseVxd[PATH_MAX] = { 0, };
    char msmouseVxd[PATH_MAX] = { 0, };
    char vmm32Vxd[PATH_MAX] = { 0, };
    char vmm32Subdir[PATH_MAX] = {0, };
    char windowsDir[PATH_MAX] =  { 0, };

    const char vmouseVxdSub[] = "\\SYSTEM\\VMM32\\VMOUSE.VXD";
    const char vmm32VxdSub[] = "\\SYSTEM\\VMM32.VXD";
    const char vmm32SubdirSub[] = "\\SYSTEM\\VMM32";
    const char msmouseVxdSub[] = "\\SYSTEM\\MSMOUSE.VXD";
    
    bool success = true;

    bool customFilenames = false;

    printf("MouseFix - Windows 98 SE / ME Mouse Driver patcher - V0.1\n");
    printf("(C) 2025 E. Voirin (oerg866)\n");
    printf("VMM32 Unpacker code (c) 2022 Jaroslav Hensl\n");
    printf("---------------------------------------------------------\n");
    printf("\n");

    if (argc == 2) {
        strncpy(windowsDir, argv[1], PATH_MAX);
    } else {
        if (getenv("WINDIR") != NULL) {
            strncpy(windowsDir, getenv("WINDIR"), PATH_MAX);
        }
    }

    /* Check if we supplied custom filenames for both, if not craft the full path for both files */

    if (argc == 3) {
        strncpy(vmouseVxd, argv[1], PATH_MAX);
        strncpy(msmouseVxd, argv[2], PATH_MAX);
        customFilenames = true;
    } else {
        printf("Using Windows directory: %s\n", windowsDir);

        snprintf(vmouseVxd, PATH_MAX, "%s%s", windowsDir, vmouseVxdSub);
        snprintf(msmouseVxd, PATH_MAX, "%s%s", windowsDir, msmouseVxdSub);
        snprintf(vmm32Vxd, PATH_MAX, "%s%s", windowsDir, vmm32VxdSub);
        snprintf(vmm32Subdir, PATH_MAX, "%s%s", windowsDir, vmm32SubdirSub);
    }

    if (!fs_file_exists(msmouseVxd)) {
        printf("Error: MSMOUSE not found in expected path (%s)\n", msmouseVxd);
        return -1;
    }

    /* if we are operating on a windows directory, we need to treat vmouse special*/

    if (!fs_file_exists(vmouseVxd) && !customFilenames && fs_file_exists(vmm32Vxd)) {
        
        /* VMOUSE does not exist but VMM32 does, so we can extract it */
        
        printf("VMOUSE not found, attempting to extract from VMM32.VXD\n");

        fs_mkdir(vmm32Subdir);        
        wx_unpack(vmm32Vxd, "VMOUSE.VXD", vmouseVxd, "tmp.tmp");
    }

    if (!fs_file_exists(vmouseVxd)) {
        printf("Error: VMOUSE not found in expected path (%s)\n", msmouseVxd);
        return -1;
    }

    /* Rudimentary usage :-) */

    if (argc == 2 && 0 == strcmp("/?", argv[1])) {
        printf("mousefix <windows_dir>\n");
        printf("mousefix <vmouse.vxd> <msmouse.vxd>\n");
        printf("\n");
        return -1;
    }

    /* Do the actual patching */

    success = patchVmouseVxd(vmouseVxd);

    if (!success) {
        printf("VMOUSE.VXD patching failed!\n");
    }
    
    printf("\n");

    success = patchMsmouseVxd(msmouseVxd);
    
    if (!success) {
        printf("MSMOUSE.VXD patching failed!\n");
    }

    return 0;
}
