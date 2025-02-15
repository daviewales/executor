/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#undef _DARWIN_NO_64_BIT_INODE

/* #include <base/common.h> */


#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#if defined(MACOSX_)
#include <libc.h>
#endif

#if defined(__BORLANDC__)
#include <io.h>
#define write _write /* Yahoo! "_" needed for "binary" output */
#endif

#include "mkvol_internal.h"
#include "mkvol.h"

#if !defined(static)

#endif

#if !defined(noErr)
#define noErr 0
#endif

#if !defined(ioErr)
#define ioErr (-36)
#endif

typedef short int INTEGER;

typedef int LONGINT;

#ifndef true
typedef enum { false,
               true } bool;
#endif

using namespace Executor;

static bool write_zeros = false;

static inline void
my_bzero(void *ptr, size_t nbytes)
{
    memset(ptr, 0, nbytes);
}

/*
 * NOTE: we can't inherit PACKED from our mac stuff because we actually
 *   have to let something sit on an odd address, which means we
 *   need to use __attribute__((packed)) even on the 68k, but
 *   normally we have PACKED mean nothing special on the 68k
 */

#if defined(__GNUC__)
#undef PACKED
#define PACKED __attribute__((packed))
#endif /* defined(__GNUC__) */

#define SECSIZE 512

enum
{
    EX_SUCCESS,
    EX_NARGS,
    EX_NEGARG,
    EX_LONGNAME,
    EX_CANTOPEN,
    EX_BADSIZE,
    EX_EXISTS,
    EX_WRITE_ERROR,
    EX_WE_ARE_BROKEN,
    EX_BAD_USER_INPUT,
    EX_BAD_HFV_NAME,
};

typedef struct
{
    write_funcp_t writefuncp;
    int user_arg;
    GUEST<uint32_t> time;
    const char *volume_name;
    int32_t nsecs_left;
    int32_t nsecs_in_map;
    int32_t nsecs_in_alblock;
    int32_t nalblocks_in_tree;
    int32_t nalblocks_in_desktop;
} info_t;

/*
 * write_startup writes two sectors of zeros since we're not creating a
 * bootable volume.
 */

#define WRITE_AND_RETURN_ERROR(infop, buf, length)                      \
    if((infop)->writefuncp((infop)->user_arg, (buf), length) != length) \
    return ioErr

static int
write_startup(info_t *infop) /* 0 - 1023 */
{
    char buf[2 * SECSIZE];

    assert(infop->nsecs_left >= 2);
    my_bzero(buf, sizeof(buf));
    WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    infop->nsecs_left -= 2;
    return noErr;
}

#define NMAPBITS (SECSIZE * 8)

static union {
    char buf[SECSIZE];
    volumeinfo vi;
} bufu;

static int
write_volume_info(info_t *infop)
{
    unsigned short nalblks;

    assert(infop->nsecs_left >= 1);
    assert(sizeof(bufu) == SECSIZE);
    my_bzero(&bufu, sizeof(bufu));

    infop->nsecs_in_alblock = (infop->nsecs_left + (1L << 16) - 1) / (1L << 16);
    /*
 * the -3 below is to take into account this sector and two reserved sectors
 * at the end of the medium.
 */
    nalblks = ((infop->nsecs_left - 3 + infop->nsecs_in_alblock - 1)
               / infop->nsecs_in_alblock);
    infop->nsecs_in_map = (int32_t)(((long)nalblks + NMAPBITS - 1) / NMAPBITS);
    nalblks = (infop->nsecs_left - infop->nsecs_in_map - 3
               + infop->nsecs_in_alblock - 1)
        / infop->nsecs_in_alblock;
    infop->nalblocks_in_tree = nalblks / 128; /* TODO: find proper mapping */
    if(infop->nalblocks_in_tree < 12)
        infop->nalblocks_in_tree = 12;

    /*
 * NOTE:  For a couple new filesystems I tested, the number of allocation
 *    blocks that were used for "Desktop" was the number in the b-tree
 *    plus one!
 */
    infop->nalblocks_in_desktop = infop->nalblocks_in_tree + 1;

    bufu.vi.drSigWord = 0x4244;
    bufu.vi.drCrDate = infop->time;
    bufu.vi.drLsMod = infop->time;
    bufu.vi.drAtrb = 0x100; /* clean unmount */
    bufu.vi.drNmFls = 1;
    bufu.vi.drVBMSt = 3;
    bufu.vi.drAllocPtr = 2 * infop->nalblocks_in_tree; /* NOTE:
			It is goofy to not count infop->nalblocks_in_desktop,
			but that's the way it goes */
    bufu.vi.drNmAlBlks = nalblks;
    bufu.vi.drAlBlkSiz = infop->nsecs_in_alblock * SECSIZE;
    bufu.vi.drClpSiz = 4 * bufu.vi.drAlBlkSiz;
    bufu.vi.drAlBlSt = bufu.vi.drVBMSt + infop->nsecs_in_map;
    bufu.vi.drNxtCNID = 17;
    bufu.vi.drFreeBks = nalblks - 2 * infop->nalblocks_in_tree - infop->nalblocks_in_desktop;
    bufu.vi.drVN[0] = strlen(infop->volume_name);
    memcpy(bufu.vi.drVN + 1, infop->volume_name,
           (unsigned char)bufu.vi.drVN[0]);
    bufu.vi.drVolBkUp = 0;
    bufu.vi.drVSeqNum = 0;
    bufu.vi.drWrCnt = 0;
    bufu.vi.drXTClpSiz = infop->nalblocks_in_tree * infop->nsecs_in_alblock
                            * SECSIZE;
    bufu.vi.drCTClpSiz = infop->nalblocks_in_tree * infop->nsecs_in_alblock
                            * SECSIZE;
    bufu.vi.drNmRtDirs = 0;
    bufu.vi.drFilCnt = 1;
    bufu.vi.drDirCnt = 0;
    /*  bufu.vi.drFndrInfo = zero */
    bufu.vi.drVCSize = 0;
    bufu.vi.drVCBMSize = 0;
    bufu.vi.drCtlCSize = 0;
    bufu.vi.drXTFlSize = bufu.vi.drXTClpSiz;
    bufu.vi.drXTExtRec[0].blockstart = 0;
    bufu.vi.drXTExtRec[0].blockcount = infop->nalblocks_in_tree;
    bufu.vi.drCTFlSize = bufu.vi.drCTClpSiz;
    bufu.vi.drCTExtRec[0].blockstart = infop->nalblocks_in_tree;
    bufu.vi.drCTExtRec[0].blockcount = infop->nalblocks_in_tree;

    WRITE_AND_RETURN_ERROR(infop, &bufu, sizeof(bufu));
    infop->nsecs_left -= 1;
    return noErr;
}

static int
write_volume_bitmap(info_t *infop)
{
    int32_t nalblocks_in_use;
    unsigned char buf[SECSIZE], *p;
    int32_t i;

    assert(infop->nsecs_left >= infop->nsecs_in_map);
    nalblocks_in_use = infop->nalblocks_in_tree * 2 + infop->nalblocks_in_desktop;
    my_bzero(buf, sizeof(buf));
    for(i = nalblocks_in_use / 8, p = buf; --i >= 0;)
        *p++ = 0xff;
    *p = 0xff << (8 - nalblocks_in_use % 8);
    WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));

    if((i = infop->nsecs_in_map - 1) > 0)
    {
        my_bzero(buf, sizeof(buf));
        while(--i >= 0)
            WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    }
    infop->nsecs_left -= infop->nsecs_in_map;
    return noErr;
}

typedef enum {
    extent,
    catalog
} btree_enum_t;

static int
fill_btblock0(info_t *infop, btree_enum_t btree_enum)
{
    btblock0 bt;

    assert(infop->nsecs_left >= 1);
    assert(sizeof(bt) == SECSIZE);
    my_bzero(&bt, sizeof(bt));
    bt.type = 01;
    bt.dummy = 0;
    bt.hesthreejim = 3;
    bt.btnodesize = 512;
    bt.nnodes = infop->nalblocks_in_tree * infop->nsecs_in_alblock;

    if(bt.nnodes > 2048)
    {
#if defined(MKVOL_PROGRAM)
        fprintf(stderr, "\n"
                        "makehfv is broken and can't handle an HFV this large.\n"
                        "makehfv should be fixed soon (128m max for now).\n");
        exit(-EX_WE_ARE_BROKEN);
#else
        abort();
#endif
    }

    bt.nfreenodes = bt.nnodes - 1;
    bt.unknown2[0] = 0x01f800f8;
    bt.unknown2[1] = 0x0078000e;

    switch(btree_enum)
    {
        case extent:
            bt.flink = 0;
            bt.blink = 0;
            bt.height = 0;
            bt.root = 0;
            bt.numentries = 0;
            bt.firstleaf = 0;
            bt.lastleaf = 0;
            bt.indexkeylen = 7;
            bt.map[0] = 0x80;
            break;
        case catalog:
            bt.flink = 0;
            bt.blink = 0;
            bt.height = 1;
            bt.root = 1;
            bt.numentries = 3;
            bt.firstleaf = 1;
            bt.lastleaf = 1;
            bt.indexkeylen = 37;
            bt.map[0] = 0xC0;
            bt.nfreenodes = bt.nfreenodes - 1;
            break;
        default:
            assert(0);
            break;
    }
    WRITE_AND_RETURN_ERROR(infop, &bt, sizeof(bt));
    return noErr;
}

static int
write_extents(info_t *infop)
{
    char buf[SECSIZE];
    int32_t i, j;

    assert(infop->nsecs_left >= infop->nalblocks_in_tree * infop->nsecs_in_alblock);
    fill_btblock0(infop, extent);
    my_bzero(buf, sizeof(buf));
    for(i = infop->nsecs_in_alblock - 1; --i >= 0;)
        WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    for(i = infop->nalblocks_in_tree - 1; --i >= 0;)
        for(j = infop->nsecs_in_alblock; --j >= 0;)
            WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    infop->nsecs_left -= infop->nalblocks_in_tree * infop->nsecs_in_alblock;
    return noErr;
}

#define DESKTOP "Desktop"

static void
set_key_len(catkey *keyp)
{
    keyp->ckrKeyLen = (1 + sizeof(LONGINT) + 1 + keyp->ckrCName[0]) | 1;
}

#if !defined(EVENUP)
#define EVENUP(x) ((void *)(((long)(x) + 1) & ~1))
#endif /* !defined(EVENUP) */

static int
write_catalog(info_t *infop)
{
    char buf[SECSIZE];
    btnode *btp;
    catkey *catkeyp;
    unsigned short *offsets;
    directoryrec *dirp;
    threadrec *threadp;
    filerec *filep;
    int32_t j, remaining_alblocks_in_tree;

    assert(infop->nsecs_left >= infop->nalblocks_in_tree * infop->nsecs_in_alblock);
    fill_btblock0(infop, catalog);

    /* we use a negative offset */
    offsets = (unsigned short *)(buf + sizeof(buf));

    btp = (btnode *)buf;
    btp->ndFLink = 0;
    btp->ndBLink = 0;
    btp->ndType = leafnode;
    btp->ndLevel = 1;
    btp->ndNRecs = 3;

    catkeyp = (catkey *)(btp + 1);
    catkeyp->ckrResrv1 = 0;
    catkeyp->ckrParID = 1;
    catkeyp->ckrCName[0] = strlen(infop->volume_name);
    memcpy(catkeyp->ckrCName + 1, infop->volume_name,
           (unsigned char)catkeyp->ckrCName[0]);
    set_key_len(catkeyp);

    dirp = (directoryrec *)EVENUP((char *)catkeyp + catkeyp->ckrKeyLen);
    dirp->cdrType = DIRTYPE;
    dirp->cdrResrv2 = 0;
    dirp->dirFlags = 0;
    dirp->dirVal = 1;
    dirp->dirDirID = 2;
    dirp->dirCrDat = infop->time;
    dirp->dirMdDat = infop->time;
    dirp->dirBkDat = 0;
    /*
 * Leave dirUsrInfo, dirFndrInfo and dirResrv zero for now
 */
    offsets[-1] = (char *)catkeyp - buf;

    catkeyp = (catkey *)(dirp + 1);
    catkeyp->ckrResrv1 = 0;
    catkeyp->ckrParID = 2;
    catkeyp->ckrCName[0] = 0;
    set_key_len(catkeyp);

    threadp = (threadrec *)EVENUP((char *)catkeyp + catkeyp->ckrKeyLen);
    threadp->cdrType = THREADTYPE;
    threadp->cdrResrv2 = 0;
    threadp->thdParID = 1;
    threadp->thdCName[0] = strlen(infop->volume_name);
    memcpy(threadp->thdCName + 1, infop->volume_name,
           (unsigned char)threadp->thdCName[0]);

    offsets[-2] = (char *)catkeyp - buf;

    catkeyp = (catkey *)(threadp + 1);
    catkeyp->ckrResrv1 = 0;
    catkeyp->ckrParID = 2;
    catkeyp->ckrCName[0] = strlen(DESKTOP);
    memcpy(catkeyp->ckrCName + 1, DESKTOP,
           (unsigned char)catkeyp->ckrCName[0]);
    set_key_len(catkeyp);

    filep = (filerec *)EVENUP((char *)catkeyp + catkeyp->ckrKeyLen);
    filep->cdrType = FILETYPE;
    filep->cdrResrv2 = 0;
    filep->filFlags = 0;
    filep->filTyp = 0;
    filep->filUsrWds.fdType = "FNDR"_4;
    filep->filUsrWds.fdCreator = "ERIK"_4;
    filep->filUsrWds.fdFlags = fInvisible;
    filep->filFlNum = 16;
    filep->filStBlk = 0;
    filep->filLgLen = 0;
    filep->filPyLen = 0;
    filep->filRStBlk = 0;
    filep->filRLgLen = 321;
    filep->filRPyLen = infop->nalblocks_in_desktop
                           * infop->nsecs_in_alblock * SECSIZE;
    filep->filCrDat = infop->time;
    filep->filMdDat = infop->time;
    filep->filBkDat = 0;
    /*  filep->filFndrInfo is not set up */
    filep->filClpSize = 0;
    filep->filExtRec[0].blockstart = 0;
    filep->filExtRec[0].blockcount = 0;
    filep->filRExtRec[0].blockstart = infop->nalblocks_in_tree * 2;
    filep->filRExtRec[0].blockcount = infop->nalblocks_in_desktop;

    offsets[-3] = (char *)catkeyp - buf;
    offsets[-4] = (char *)(filep + 1) - buf;
    WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));

    my_bzero(buf, sizeof(buf));
    if(infop->nsecs_in_alblock > 1)
    {
        for(j = infop->nsecs_in_alblock - 2; --j >= 0;)
            WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
        remaining_alblocks_in_tree = infop->nalblocks_in_tree - 1;
    }
    else
        remaining_alblocks_in_tree = infop->nalblocks_in_tree - 2;

    while(--remaining_alblocks_in_tree >= 0)
        for(j = infop->nsecs_in_alblock; --j >= 0;)
            WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    infop->nsecs_left -= infop->nalblocks_in_tree * infop->nsecs_in_alblock;
    return noErr;
}

#define STR_ID 0
#define STR_NAME "\012Finder 1.0"

/*
 * NOTE: the "-1" below is because both the byte count and the null zero
 *   are included in the sizeof() value, so we have to decrement to
 *   discount the null.  The null won't actually be in the buffer
 *   because we won't give enough room.
 */

#define DATLEN (sizeof(LONGINT) + sizeof(STR_NAME) - 1)
#define MAPLEN (sizeof(map_t))

static int
write_desktop(info_t *infop)
{
#pragma pack(push, 2)
    typedef struct
    {
        char zeros[22];
        INTEGER fileattrs;
        INTEGER typeoff;
        INTEGER nameoff;
        INTEGER ntypesminus1;
        char restype1[4];
        INTEGER ntype1minus1;
        INTEGER reflistoff1;
        INTEGER resid1;
        INTEGER resnamoff1;
        char resattr1;
        unsigned char resdoff1[3];
        LONGINT reszero1;
    } map_t;

    typedef struct
    {
        LONGINT datoff;
        LONGINT mapoff;
        LONGINT datlen;
        LONGINT maplen;
        char sysuse[112];
        char appluse[128];
        LONGINT datalen1;
        char data1[DATLEN - sizeof(LONGINT)];
        map_t map;
    } res_data_t;

    const static struct
    {
        res_data_t data;
        char filler[SECSIZE - sizeof(res_data_t)];
    } buf = {
        {
            256,
            256 + DATLEN,
            DATLEN,
            MAPLEN,
            {
                0, 0, 0, /* ... */
            },
            {
                0, 0, 0, /* ... */
            },
            DATLEN - sizeof(LONGINT),
            //STR_NAME,
            { 10, 'F', 'i', 'n', 'd', 'e', 'r', ' ', '1', '.', '0' },
            {
                { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
                0x0000,
                22 + 2 + 2 + 2, /* zeros ... nameoff */
                MAPLEN,
                1 - 1,
                { 'S', 'T', 'R', ' ' },
                1 - 1,
                2 + 4 + 2 + 2, /* ntypesminus1 ... reflistoff1 */
                STR_ID,
                -1, /* no name */
#if !defined(resPreload)
#define resPreload 4
#endif
                resPreload,
                { 0, 0, 0 },
                0,
            },
        },
    };
    assert(infop->nsecs_left >= 1);
    WRITE_AND_RETURN_ERROR(infop, &buf, sizeof(buf));
    --infop->nsecs_left;
    return noErr;
#pragma pack(pop)
}

static int
write_rest(info_t *infop)
{
    char buf[SECSIZE];

    assert(infop->nsecs_left >= 2);
    my_bzero(buf, sizeof(buf));

    if(!write_zeros && infop->writefuncp == (write_funcp_t)write)
    {
        int32_t blocks_to_skip;

        blocks_to_skip = infop->nsecs_left - 2;
        if(blocks_to_skip > 0)
            if(lseek(infop->user_arg, blocks_to_skip * sizeof(buf), SEEK_CUR)
               == -1)
                return ioErr;
    }
    else
    {
        while(--infop->nsecs_left >= 2)
            WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    }
    WRITE_AND_RETURN_ERROR(infop, &bufu, sizeof(bufu));
    WRITE_AND_RETURN_ERROR(infop, buf, sizeof(buf));
    return noErr;
}

#if !defined(U70MINUSM04)
#define U70MINUSM04 2082844800L
#endif /* !defined U70MINUSM04 */

#define SECSINMINUTE 60L
#define SECSINHOUR (60L * SECSINMINUTE)
#define SECSINDAY (24L * SECSINHOUR)
#define SECSINYEAR (365L * SECSINDAY)

int format_disk(GUEST<uint32_t> timevar, const char *volumename, int nsecs,
                write_funcp_t writefuncp, int user_arg)
{
    info_t info;
    int retval;

    info.writefuncp = writefuncp;
    info.user_arg = user_arg;
    info.time = timevar;
    info.volume_name = volumename;
    info.nsecs_left = nsecs;

#if !defined(MKVOL_PROGRAM)
#define NOTE(x)
#else

#define NOTE(x)                     \
    do                              \
    {                               \
        fprintf(stderr, "%s", (x)); \
        fflush(stderr);             \
    } while(0)

#endif

#define GOTO_DONE_IF_ERROR(var, val) \
    if(((var) = (val)))              \
    goto DONE

    NOTE("Writing startup");
    GOTO_DONE_IF_ERROR(retval, write_startup(&info));
    NOTE(" info");
    GOTO_DONE_IF_ERROR(retval, write_volume_info(&info));
    NOTE(" bitmap");
    GOTO_DONE_IF_ERROR(retval, write_volume_bitmap(&info));
    NOTE(" extents");
    GOTO_DONE_IF_ERROR(retval, write_extents(&info));
    NOTE(" catalog");
    GOTO_DONE_IF_ERROR(retval, write_catalog(&info));
    NOTE(" desktop");
    GOTO_DONE_IF_ERROR(retval, write_desktop(&info));
    NOTE(" the rest");
    GOTO_DONE_IF_ERROR(retval, write_rest(&info));
    NOTE("\nDone.\n");
DONE:
    return retval;
}

#if defined(MKVOL_PROGRAM)

#include <commandline/parsenum.h>
#include <ctype.h>

//#define DEFAULT_SUFFIX ".hfv"
#define DEFAULT_SUFFIX ".img"

static int
mixed_case_match(const char *str1, const char *str2)
{
    int retval;

    while(*str1 && *str2 && tolower(*str1) == tolower(*str2))
    {
        ++str1;
        ++str2;
    }
    retval = !*str1 && !*str2;
    return retval;
}

static void
adjust_hfv_name(char **namepp)
{
    char *namep = *namepp, *suffixp;
    size_t name_len = strlen(namep);
    FILE *fp;

    if(name_len >= 4)
    {
        suffixp = namep + name_len - 4;
        if(mixed_case_match(suffixp, DEFAULT_SUFFIX))
            memcpy(suffixp, DEFAULT_SUFFIX, strlen(DEFAULT_SUFFIX));
    }
#if !defined(LETGCCWAIL)
    else
        suffixp = 0;
#endif

    if(name_len < 4 || strcmp(suffixp, DEFAULT_SUFFIX) != 0)
    {
        char *new_namep;

        new_namep = (char *)malloc(name_len + 4 + 1);
        sprintf(new_namep, "%s%s", namep, DEFAULT_SUFFIX);
        fprintf(stderr, "Adding \"" DEFAULT_SUFFIX "\" to rename \"%s\" to \"%s\"\n",
                namep, new_namep);
        namep = new_namep;
    }
    fp = fopen(namep, "rb");
    if(fp)
    {
        fclose(fp);
        fprintf(stderr, "File \"%s\" already exists and will not be modified\n",
                namep);
        exit(-EX_EXISTS);
    }
    *namepp = namep;
}

static const char *magic_file_to_delete_because_at_exit_is_flawed;

static void
cleanup(void)
{
    if(magic_file_to_delete_because_at_exit_is_flawed)
        unlink(magic_file_to_delete_because_at_exit_is_flawed);
}

/*
 * Look for a switch.  If found, set a value and remove the switch from
 * the argument list.
 */

static bool
find_and_remove_switch_p(const char *switch_name, int *argcp, char *argv[])
{
    int in, out;
    int start_argc;
    bool present_p;

    present_p = false;
    start_argc = *argcp;
    for(out = in = 1; in < start_argc; in++)
    {
        argv[out] = argv[in];
        if(strcmp(argv[in], switch_name) == 0)
        {
            present_p = true;
            --*argcp;
        }
        else
            ++out;
    }
    argv[out] = argv[in];
    return present_p;
}

static bool
check_hfv_name(const char *hfv_name)
{
    bool success_p;
    if(strlen(hfv_name) == 0)
    {
        fprintf(stderr, "You can't have an empty file name!\n");
        success_p = false;
    }
    else
    {
        success_p = true;
    }
    return success_p;
}

static bool
check_volume_name(const char *volume_name)
{
    size_t len = strlen(volume_name);
    bool success_p;

    if(len == 0)
    {
        fprintf(stderr, "You can't have an empty volume name!\n");
        success_p = false;
    }
    else if(len > 27)
    {
        fprintf(stderr, "Volume names can be no longer than 27 characters.\n");
        success_p = false;
    }
    else if(strchr(volume_name, ':'))
    {
        fprintf(stderr, "Volume names may not contain colons.\n");
        success_p = false;
    }
    else
    {
        success_p = true;
    }

    return success_p;
}

static bool
check_volume_size(const char *volume_size_string)
{
    bool success_p;
    int32_t nbytes;

    if(!parse_number(volume_size_string, &nbytes, SECSIZE))
    {
        fprintf(stderr, "Malformed volume size.\n");
        success_p = false;
    }
    else if(nbytes / SECSIZE < 100) /* need at least 100 sectors */
    {
        fprintf(stderr, "The specified volume is too small.");
        if(volume_size_string[0])
        {
            char last_char;
            last_char = volume_size_string[strlen(volume_size_string) - 1];
            if(last_char != 'k' && last_char != 'K'
               && last_char != 'm' && last_char != 'M')
                fprintf(stderr,
                        "  Remember to add a K or M suffix for\n"
                        "kilobytes or megabytes, respectively.");
        }
        putc('\n', stderr);

        success_p = false;
    }
    else
    {
        success_p = true;
    }

    return success_p;
}

static void
cleanup_string(char *s)
{
    size_t len;

    len = strlen(s);

    /* Nuke leading whitespace */
    while(isspace(s[0]))
    {
        memmove(s, s + 1, len);
        --len;
    }

    /* Nuke trailing whitespace */
    while(len > 0 && isspace(s[len - 1]))
        s[--len] = '\0';
}

static bool
read_parameter(const char *prompt, char **s,
               bool (*verify_func)(const char *))
{
    char buf[2048];
    bool success_p;

    success_p = false;
    do
    {
        fputs(prompt, stdout);
        fflush(stdout);
        if(!fgets(buf, sizeof buf - 1, stdin))
            goto done;
        cleanup_string(buf);
    } while(!verify_func(buf));
    *s = strcpy((char *)malloc(strlen(buf) + 1), buf);
    success_p = true;

done:
    return success_p;
}

#if defined(__MSDOS__)
#define OS_NAME "DOS"
#elif defined(__linux__)
#define OS_NAME "Linux"
#elif defined(__NeXT__)
#define OS_NAME "NEXTSTEP"
#elif defined(MACOSX_)
#define OS_NAME "Mac OS X"
#endif

static bool
prompt_for_parameters(char **hfv_name, char **volume_name, char **volume_size)
{
    bool success_p;

    /* Default to an error string to avoid uninitialized memory bugs. */
    *hfv_name = *volume_name = *volume_size = "???error???";
    success_p = false;

    printf(
        "Makehfv creates \"HFV\" files for use with Executor, the Macintosh emulator.\n"
        "%s will see this HFV file as a single %s file, but Executor treats each\n"
        "HFV file as a separate disk drive that you can use to store Mac files and\n"
        "folders in exactly the same format that Macintosh programs expect.\n\n"
        "Makehfv will prompt you for the %s file name that you want to assign to\n"
        "the new HFV file.  Makehfv will automatically add \".hfv\" to the name that\n"
        "you provide.  Makehfv will also prompt you for the volume name that you\n"
        "want Executor to use for the HFV file.  Finally, makehfv will ask you how\n"
        "many bytes of your hard disk space you want to devote to this HFV file.\n"
        "Usually you'll want to answer with a number followed by \"m\" -- the \"m\"\n"
        "stands for megabyte.\n\n"
        "For example, to create a 20 megabyte HFV named \"test.hfv\" that will be\n"
        "called \"Play\" from within Executor, answer \"test\", \"Play\" and \"20m\".\n\n",
        OS_NAME, OS_NAME, OS_NAME);

    /* Read in the HFV name. */
    if(!read_parameter("Name of the HFV file to create: ",
                       hfv_name, check_hfv_name))
        goto done;

    /* Read in the volume name. */
    if(!read_parameter("Volume name as seen under Executor: ",
                       volume_name, check_volume_name))
        goto done;

    /* Read in the volume size. */
    if(!read_parameter("Volume size: ",
                       volume_size, check_volume_size))
        goto done;

    success_p = true;

done:
    return success_p;
}

int main(int argc, char *argv[])
{
    time_t now;
    struct tm *tmp;
    int32_t years, leaps;
    int32_t nsecs = 0, nbytes = 0;
    long timevar;
    bool force_p, help_p;
    char *hfv_name, *volume_name, *volume_size;

    write_zeros = find_and_remove_switch_p("-zeros", &argc, argv);
    force_p = find_and_remove_switch_p("-force", &argc, argv);
    help_p = find_and_remove_switch_p("-help", &argc, argv);

    if(help_p || (argc != 1 && argc != 4))
    {
        const char *name;

        name = strrchr(argv[0], '/');
        if(name)
            ++name;
        else
            name = argv[0];
        fprintf(stderr,
                "Usage: %s [-help] [-force] [-zeros] [hfvname volumename volumesize]\n"
                "\tThe volume size is specified in bytes, and may contain an \"M\"\n"
                "\tor \"K\" suffix for megabytes or kilobytes.  For example:\n"
                "\t\t%s newvol.hfv NewVolume 4.5M\n"
                "\twill create a 4.5 megabyte HFV named newvol.hfv.\n"
                "\t   The \"-force\" option prevents a \".hfv\" suffix from being "
                "appended\n"
                "\tto the file name.  The \"-zeros\" option causes all unused disk blocks\n"
                "\tto be filled in with zero bytes.  Ordinarily you never need these\n"
                "\toptions, although \"-zeros\" can make the HFV take up less space\n"
                "\ton compressed filesystems.\n",
                name, name);
        exit(-EX_NARGS);
    }

    if(argc == 4)
    {
        hfv_name = argv[1];
        volume_name = argv[2];
        volume_size = argv[3];
    }
    else if(!prompt_for_parameters(&hfv_name, &volume_name, &volume_size))
        exit(-EX_BAD_USER_INPUT);

    if(!force_p)
        adjust_hfv_name(&hfv_name);

    if(!check_hfv_name(hfv_name))
        exit(-EX_BAD_HFV_NAME);

    if(!check_volume_name(volume_name))
        exit(-EX_LONGNAME);

    /* Parse in the disk volume size. */
    if(!check_volume_size(volume_size)
       || !parse_number(volume_size, &nbytes, SECSIZE))
        exit(-EX_BADSIZE);

    nsecs = nbytes / SECSIZE;

    magic_file_to_delete_because_at_exit_is_flawed = hfv_name;
    atexit(cleanup);
    if(freopen(hfv_name, "wb", stdout) == nullptr)
    {
        char errmsg[2048];
        sprintf(errmsg, "Could not open \"%s\"", hfv_name);
        perror(errmsg);
        exit(-EX_CANTOPEN);
    }
    time(&now);
    tmp = localtime(&now);
    years = tmp->tm_year - 70;
    leaps = (years + 1) / 4; /* 1973 counts 1972, 1977 counts '76+'72 */
    timevar = years * SECSINYEAR + leaps * SECSINDAY + tmp->tm_yday * SECSINDAY + tmp->tm_hour * SECSINHOUR + tmp->tm_min * SECSINMINUTE + tmp->tm_sec + U70MINUSM04;
    if(format_disk(timevar, volume_name, nsecs, (write_funcp_t)write, 1)
       != noErr)
    {
        fprintf(stderr, "Error writing HFV.  Perhaps your disk is full.\n");
        exit(-EX_WRITE_ERROR);
    }
    magic_file_to_delete_because_at_exit_is_flawed = 0;
    return 0;
}
#endif /* defined(MKVOL_PROGRAM) */
