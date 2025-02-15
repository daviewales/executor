/* Copyright 1994, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* color manager */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>

#include <mman/mman.h>
#include <quickdraw/cquick.h>
#include <quickdraw/rgbutil.h>
#include <syn68k_public.h>
#include <vdriver/vdriver.h>
#include <vdriver/dirtyrect.h>
#include <time/syncint.h>
#include <base/cpu.h>

using namespace Executor;

INTEGER ROMlib_qd_error;

struct NativeRGBColor
{
    unsigned short red;
    unsigned short green;
    unsigned short blue;
};
struct NativeColorSpec
{
    INTEGER value;
    NativeRGBColor rgb;
};

INTEGER Executor::C_QDError()
{
    /* #warning "make QDError work for real" */
    return ROMlib_qd_error;
}

LONGINT Executor::C_GetCTSeed()
{
    static LONGINT seed = minSeed;

    return seed++;
}

static uint32_t
itab_base_size(int res)
{
    uint32_t retval;

    retval = offsetof(ITab, iTTable) + ((1 << (3 * res)) * sizeof((ITab *)0)->iTTable[0]);
    return retval;
}

#define ITABLE_HASH_SIZE 512

#if defined(ITABLE_HASH_SIZE)

/* TODO: look for better hash function */

static int
itable_hash(RGBColor *rgbp, int resolution)
{
    uint32_t hash;
    int retval;
    uint16_t red, green, blue;

    red = rgbp->red >> (16 - resolution);
    green = rgbp->green >> (16 - resolution);
    blue = rgbp->blue >> (16 - resolution);

    switch(resolution)
    {
        case 3:
#if ITABLE_HASH_SIZE != 512
#error fix hash funciton
#endif
            hash = (red << 6) | (green << 3) | blue;
            break;
        default:
            warning_unexpected("resolution = %d", resolution);
        /* FALL THROUGH */
        case 4:
            hash = ((blue & 8) << 5) | ((red ^ ((blue >> 1) & 1)) << 4) | (green ^ (blue & 3));
            break;
        case 5:
            hash = ((red & 0xf << 5) | green) ^ ((blue << 2) | ((red & 8) >> 2));
            break;
    }
    retval = hash;
    return retval;
}

/*
 */

static uint32_t
rgb_diff(RGBColor *rgb1p, RGBColor *rgb2p)
{
    uint32_t retval;

    /*
   * NOTE: the surprising "& 0xff" below was determined empirically by
   * using NIH Image to plot histograms of the "Bone Marrow" image that
   * is on the CD-ROM that Chris Russ gave to me.  Chris is the person
   * who pointed out that histograms weren't working, and it took a while
   * to see that not only did we need to fix Color2Index in general, but 
   * that we had to be careful to match like a Mac matches.  Bone Marrow
   * uses a grayscale clut whose low byte of each rgb component is the
   * same as the high byte, instead of zero.  If we don't mask that info
   * off, then we wind up with a different histogram than the Mac produces.
   * From my point of view, the whole way NIH Image and the Mac work
   * together is questionable, since the conversion from the non-standard
   * CLUT that Bone Marrow uses to a standard gray scale clut is dicey.
   * After all, 0x8080 is just as far away from 0x8000 as it is from
   * 0x8100, so counting on it going to a particular value seems risky.
   */

    retval = (std::abs((rgb1p->red & 0xff00) - (rgb2p->red & 0xff00)) + std::abs((rgb1p->green & 0xff00) - (rgb2p->green & 0xff00)) + std::abs((rgb1p->blue & 0xff00) - (rgb2p->blue & 0xff00)));

    return retval;
}
#endif

/* FIXME: may have to round instead of cleave off the low bits */
#define RGB_TO_ITAB_INDEX(rgb, resolution)                           \
    ((((rgb)->red >> (16 - (resolution))) << (2 * (resolution))) \
     | (((rgb)->green >> (16 - (resolution))) << (resolution))   \
     | (((rgb)->blue >> (16 - (resolution)))))

static uint32_t
ROMlib_search_proc(RGBColor *rgb)
{
    GDHandle gd;
    int pixel_size;
    uint32_t retval;

    gd = LM(TheGDevice);
    pixel_size = PIXMAP_PIXEL_SIZE(GD_PMAP(gd));
    if(pixel_size > 8)
    {
        const rgb_spec_t *rgb_spec;

        rgb_spec = pixel_size == 16 ? &mac_16bpp_rgb_spec
                                    : &mac_32bpp_rgb_spec;
        retval = (*rgb_spec->rgbcolor_to_pixel)(rgb_spec, rgb, true);
        if(pixel_size == 16)
            retval = GUEST<uint16_t>::fromRaw(retval);
        else if(pixel_size == 32)
            retval = GUEST<uint32_t>::fromRaw(retval);
        else
            gui_fatal("unknown pixel size `%d'", pixel_size);
    }
    else
    {
        ITabHandle inverse_table;
        int resolution;

        inverse_table = GD_ITABLE(gd);
        resolution = ITAB_RES(inverse_table);

        retval = ITAB_TABLE(inverse_table)[RGB_TO_ITAB_INDEX(rgb, resolution)];
#if defined(ITABLE_HASH_SIZE)
        {
            CTabHandle ctabh;
            ColorSpec *cspecp;

            ctabh = PIXMAP_TABLE(GD_PMAP(gd));
            cspecp = CTAB_TABLE(ctabh);
            if(memcmp(rgb, &cspecp[retval].rgb, sizeof *rgb) != 0)
            {
                int i;
                uint8_t *hash_table;
                uint32_t candidate;
                uint32_t cur_diff, new_diff;
                bool done_zero;

                hash_table = ((uint8_t *)*inverse_table
                              + itab_base_size(resolution));
                i = itable_hash(rgb, resolution);

                cur_diff = rgb_diff(rgb, &cspecp[retval].rgb);
                new_diff = 1;
                done_zero = false;
                while(new_diff && (candidate = hash_table[i]) != 0)
                {
                    if(candidate == 1 && !done_zero)
                    {
                        candidate = 0;
                        --i;
                        done_zero = true;
                    }
                    new_diff = rgb_diff(rgb, &cspecp[candidate].rgb);
                    if(new_diff < cur_diff)
                    {
                        retval = candidate;
                        cur_diff = new_diff;
                    }
                    i = (i + 1) % ITABLE_HASH_SIZE;
                }
            }
        }
#endif
    }
    return retval;
}

LONGINT Executor::C_Color2Index(RGBColor *rgb)
{
    SProcHndl t;
    LONGINT success_p;
    /* default */
    GUEST<LONGINT> position = {};
    GDHandle gdev;
    ITabHandle gd_itab;
    CTabHandle gd_ctab;

    /* rebuild the inverse table for the current graphics device
     if the color table is newer than the inverse table */
    gdev = LM(TheGDevice);
    gd_ctab = PIXMAP_TABLE(GD_PMAP(gdev));
    gd_itab = GD_ITABLE(gdev);
    if(CTAB_SEED(gd_ctab) != ITAB_SEED(gd_itab))
        MakeITable(gd_ctab, gd_itab, GD_RES_PREF(gdev));

    for(t = GD_SEARCH_PROC(LM(TheGDevice)), success_p = false;
        t && !success_p;)
    {
        Boolean(*search_fn)
        ();

        search_fn = (Boolean(*)())(*t)->srchProc;
        /* fetch the next before calling the searchproc,
	 since it can relocate the current `t' */
        t = (*t)->nxtSrch;

        {
            M68kReg save_regs[16];
            memcpy(save_regs, cpu_state.regs, 16 * sizeof cpu_state.regs[0]);
            EM_A7 -= 128;
            PUSHADDR(US_TO_SYN68K(rgb));
            PUSHADDR(US_TO_SYN68K(&position));
            execute68K(US_TO_SYN68K(search_fn));
            /* success_p = EM_D0; */
            success_p = POPUB();
            memcpy(cpu_state.regs, save_regs, 16 * sizeof cpu_state.regs[0]);
        }
    }

    if(!success_p)
        return ROMlib_search_proc(rgb);
    else
        return position; /* They filled this in in big endian order. */
}

void Executor::C_Index2Color(LONGINT index, RGBColor *rgb)
{
    CTabHandle ctab = PIXMAP_TABLE(GD_PMAP(LM(TheGDevice)));
    *rgb = CTAB_TABLE(ctab)[index].rgb;
}

void Executor::C_InvertColor(RGBColor *rgb)
{
    /* #warning "only use default InvertColor complement procedure" */

    /* one's complement */
    rgb->red.raw(~rgb->red.raw());
    rgb->green.raw(~rgb->green.raw());
    rgb->blue.raw(~rgb->blue.raw());
}

Boolean Executor::C_RealColor(RGBColor *rgb)
{
    GDHandle gdev;
    ITabHandle inverse_table;
    CTabHandle table;
    RGBColor *closest;
    int resolution;
    unsigned short mask;
    int index;

    /* rebuild the inverse table for the current graphics device
     if the color table is newer than the inverse table */
    gdev = LM(TheGDevice);
    table = PIXMAP_TABLE(GD_PMAP(gdev));
    inverse_table = GD_ITABLE(gdev);
    if(CTAB_SEED(table) != ITAB_SEED(inverse_table))
        MakeITable(table, inverse_table, GD_RES_PREF(gdev));

    resolution = ITAB_RES(inverse_table);

    index = ITAB_TABLE(inverse_table)[RGB_TO_ITAB_INDEX(rgb, resolution)];

    closest = &(CTAB_TABLE(table)[index].rgb);

    /* high `resolution' bits */
    mask = ((1 << resolution) - 1) << (16 - resolution);

    return !(((rgb->red.raw() ^ closest->red.raw()) & mask)
             || ((rgb->green.raw() ^ closest->green.raw()) & mask)
             || ((rgb->blue.raw() ^ closest->blue.raw()) & mask));
}

void Executor::C_GetSubTable(CTabHandle in_ctab, INTEGER resolution,
                             CTabHandle target_ctab)
{
    /* cached itable from the last transaction */
    static ITabHandle cached_itab = nullptr;

    GDHandle gdev;
    ITabHandle itab = nullptr;
    PixMapHandle gd_pmap;
    int i;

    gdev = LM(TheGDevice);
    gd_pmap = GD_PMAP(gdev);

    if(!target_ctab)
    {
        ITabHandle t = GD_ITABLE(gdev);

        target_ctab = PIXMAP_TABLE(gd_pmap);

        if(resolution == ITAB_RES(t))
        {
            /* rebuild the inverse table for the current graphics device
	     if the color table is newer than the inverse table */
            if(CTAB_SEED(target_ctab) != ITAB_SEED(t))
                MakeITable(target_ctab, t, GD_RES_PREF(gdev));

            itab = t;
        }
    }

    if(!itab)
    {
        if(cached_itab
           && ITAB_SEED(cached_itab) == CTAB_SEED(target_ctab)
           && resolution == ITAB_RES(cached_itab))
            itab = cached_itab;
        else
        {
            /* if we haven't allocated a cached itab; do so now.  make
	     sure to do it out of the system zone, since we will keep
	     this itab around forever */
            if(cached_itab == nullptr)
            {
                TheZoneGuard guard(LM(SysZone));

                cached_itab = (ITabHandle)NewHandle((Size)sizeof(ITab));
            }

            itab = cached_itab;
            MakeITable(target_ctab, itab, resolution);
        }
    }
    HLockGuard guard1(in_ctab), guard2(target_ctab);

    GUEST<CTabHandle> gdev_ctab_save = PIXMAP_TABLE(gd_pmap);
    GUEST<ITabHandle> gdev_itab_save = GD_ITABLE(gdev);

    /* pull tables into locals for easy access */
    ColorSpec *in_ctab_table = CTAB_TABLE(in_ctab);

    /* ColorSpec *target_ctab_table = CTAB_TABLE (target_ctab); */
    /* int gd_ctab_p = CTAB_FLAGS (target_ctab) & CTAB_GDEVICE_BIT; */

    PIXMAP_TABLE(gd_pmap) = target_ctab;
    GD_ITABLE(gdev) = itab;

    for(i = CTAB_SIZE(in_ctab); i >= 0; i--)
    {
        ColorSpec *color = &in_ctab_table[i];
        INTEGER ctab_index;

        ctab_index = Color2Index(&color->rgb);

        /* inverse table maps rgb color space to an index
	      within the target color table, not to a value in the
	      color table */
        /*	   
	   if (gd_ctab_p)
	     color->value = ctab_index;
	   else
	     color->value = target_ctab_table[ctab_index].value;
	   color->rgb   = target_ctab_table[ctab_index].rgb;
*/
        color->value = ctab_index;
    }

    PIXMAP_TABLE(gd_pmap) = gdev_ctab_save;
    GD_ITABLE(gdev) = gdev_itab_save;
}

int Executor::average_color(GDHandle gd,
                            RGBColor *c1, RGBColor *c2, int ratio,
                            RGBColor *out)
{
    RGBColor in_between;
    int c1_index, c2_index, in_between_index;

    PixMapHandle gd_pmap;
    int gd_pixel_size;

    gd_pmap = GD_PMAP(gd);
    gd_pixel_size = PIXMAP_PIXEL_SIZE(gd_pmap);

    in_between.red = ((c1->red * ratio)
                         + (c2->red * (65535 - ratio)))
                        / 65535;
    in_between.green = ((c1->green * ratio)
                           + (c2->green * (65535 - ratio)))
                          / 65535;
    in_between.blue = ((c1->blue * ratio)
                          + (c2->blue * (65535 - ratio)))
                         / 65535;
    if(gd_pixel_size <= 8)
    {
        CTabHandle gd_ctab;
        ITabHandle gd_itab;
        unsigned char *itab_table;
        int itab_res;

        gd_ctab = PIXMAP_TABLE(gd_pmap);
        gd_itab = GD_ITABLE(gd);

        if(CTAB_SEED(gd_ctab) != ITAB_SEED(gd_itab))
            MakeITable(gd_ctab, gd_itab, GD_RES_PREF(gd));

        itab_res = ITAB_RES(gd_itab);
        itab_table = ITAB_TABLE(gd_itab);

        c1_index = itab_table[RGB_TO_ITAB_INDEX(c1, itab_res)];
        c2_index = itab_table[RGB_TO_ITAB_INDEX(c2, itab_res)];
        in_between_index = itab_table[RGB_TO_ITAB_INDEX(&in_between, itab_res)];
    }
    else
    {
        const rgb_spec_t *rgb_spec;

        rgb_spec = pixmap_rgb_spec(*gd_pmap);
        c1_index = (*rgb_spec->rgbcolor_to_pixel)(rgb_spec, c1, true);
        c2_index = (*rgb_spec->rgbcolor_to_pixel)(rgb_spec, c2, true);
        in_between_index = (*rgb_spec->rgbcolor_to_pixel)(rgb_spec, &in_between,
                                                          true);
    }
    if(c1_index != c2_index
       && (in_between_index == c1_index
           || in_between_index == c2_index))
        return false;
    else
    {
        *out = in_between;
        return true;
    }
}

Boolean Executor::C_GetGray(GDHandle gdev, RGBColor *bk, RGBColor *fg)
{
    return average_color(gdev, bk, fg, 0x8000, fg);
}

#define SET_RGB_ERROR(ix, err) (rgb_error[ix] = (err))

/* These are the parameters to rgb_error_for_res_*.  Because they
 * remain fixed during loops, we put them in globals.
 */
static unsigned current_red, current_green, current_blue;

#define RGB_ERROR_FOR_RESOLUTION_FUNC(RES)                                     \
    static ULONGINT                                                            \
        rgb_error_for_res_##RES(unsigned components)                           \
    {                                                                          \
        int rc, bc, gc;                                                   \
        ULONGINT error;                                                        \
        const unsigned mask = (1 << (RES)) - 1;                                \
        const unsigned mid_bit = (0x8000 >> (RES));                            \
                                                                               \
        bc = ((components & mask) << (16 - (RES))) | mid_bit;                  \
        error = std::abs(bc - int(current_blue));                                \
                                                                               \
        gc = ((components & (mask << (RES))) << (16 - (RES)*2)) | mid_bit;     \
        error += std::abs(gc - int(current_green));                              \
                                                                               \
        rc = ((components & (mask << ((RES)*2))) << (16 - (RES)*3)) | mid_bit; \
        error += std::abs(rc - int(current_red));                                \
                                                                               \
        return error;                                                          \
    }

RGB_ERROR_FOR_RESOLUTION_FUNC(3)
RGB_ERROR_FOR_RESOLUTION_FUNC(4)
RGB_ERROR_FOR_RESOLUTION_FUNC(5)

static int offsets[3][26];
static const int basis_offsets[26][3] = {
    { -1, -1, -1 },
    { -1, -1, 0 },
    { -1, -1, 1 },
    { -1, 0, -1 },
    { -1, 0, 0 },
    { -1, 0, 1 },
    { -1, 1, -1 },
    { -1, 1, 0 },
    { -1, 1, 1 },
    { 0, -1, -1 },
    { 0, -1, 0 },
    { 0, -1, 1 },
    { 0, 0, -1 },
#if 0
  { 0, 0, 0 },
#endif
    { 0, 0, 1 },
    { 0, 1, -1 },
    { 0, 1, 0 },
    { 0, 1, 1 },
    { 1, -1, -1 },
    { 1, -1, 0 },
    { 1, -1, 1 },
    { 1, 0, -1 },
    { 1, 0, 0 },
    { 1, 0, 1 },
    { 1, 1, -1 },
    { 1, 1, 0 },
    { 1, 1, 1 },
};

static void
init_offsets(void)
{
    int i, resolution;

    for(resolution = 3; resolution <= 5; resolution++)
    {
        int *offset;

        offset = offsets[resolution - 3];

        for(i = 0; i < 26; i++)
        {
            const int *basis_offset;

            basis_offset = basis_offsets[i];

            offset[i] = (basis_offset[0]
                         + (basis_offset[1] << resolution)
                         + (basis_offset[2] << (2 * resolution)));
        }
    }
}

#if defined(ITABLE_HASH_SIZE)
static void
add_hash_table(CTabHandle color_table, ITabHandle inverse_table,
               int resolution)
{
    int ctab_size;
    uint8_t *hash_tablep;
    int i;

    hash_tablep = ((uint8_t *)*inverse_table
                   + itab_base_size(resolution));
    memset(hash_tablep, 0, ITABLE_HASH_SIZE);
    ctab_size = CTAB_SIZE(color_table);
    for(i = 0; i < ctab_size; ++i)
    {
        RGBColor *ctabcolorp, *itabcolorp;
        int itab_index, index;

        ctabcolorp = &CTAB_TABLE(color_table)[i].rgb;
        itab_index = RGB_TO_ITAB_INDEX(ctabcolorp, resolution);
        index = ITAB_TABLE(inverse_table)[itab_index];
        itabcolorp = &CTAB_TABLE(color_table)[index].rgb;
        if(memcmp(ctabcolorp, itabcolorp, sizeof *ctabcolorp) != 0)
        {
            int j;

            j = itable_hash(ctabcolorp, resolution);
            while(hash_tablep[j])
                j = (j + 1) % ITABLE_HASH_SIZE;
            hash_tablep[j] = i ? i : 1; /* don't ever put in zero */
        }
    }
}
#endif

void Executor::C_MakeITable(CTabHandle color_table, ITabHandle inverse_table,
                            INTEGER resolution)
{
    /* this also counts the number of elements in the queue */
    int itab_elt_count;
    unsigned short *queue;
    char *index_in_queue_p;
    ULONGINT *rgb_error;
    bool queue_starts_empty_p;
    int head, tail;
    int i;
    int ctab_size;
    ULONGINT(*rgb_error_func)
    (unsigned);
    unsigned char *itab_table;
    const int *offset;
    NativeColorSpec color_for_index[256];

    auto ENQUEUE = [&queue, &head, &index_in_queue_p, &itab_elt_count](unsigned _q_elt_) {
        if(!index_in_queue_p[_q_elt_])
        {
            queue[head] = _q_elt_;
            index_in_queue_p[_q_elt_] = true;
            head = (head + 1) & (itab_elt_count - 1);
        }
    };

    auto DEQUEUE = [&queue, &tail, &index_in_queue_p, &itab_elt_count]() {
        const unsigned _q_elt_ = queue[tail];
        tail = (tail + 1) & (itab_elt_count - 1);
        index_in_queue_p[_q_elt_] = false;
        return _q_elt_;
    };

    {
        static bool been_here_p = false;
        if(!been_here_p)
        {
            init_offsets();
            been_here_p = true;
        }
    }

    /* We are supposed to override a resolution of zero with the
   * resolution of LM(TheGDevice) (IMV-142).  */
    if(resolution == 0)
        resolution = GD_RES_PREF(LM(TheGDevice));

    /* We are supposed to override a color_table of nullptr with the color
   * table of LM(TheGDevice) (IMV-142).  */
    if(color_table == nullptr)
        color_table = PIXMAP_TABLE(GD_PMAP(LM(TheGDevice)));

    /* We are supposed to override an inverse_table of nullptr with the
   * inverse table of LM(TheGDevice) (IMV-142).  */
    if(inverse_table == nullptr)
    {
        inverse_table = GD_ITABLE(LM(TheGDevice));
        if(!inverse_table)
        {
            inverse_table = (ITabHandle)NewHandle(0);
            GD_ITABLE(LM(TheGDevice)) = inverse_table;
        }
    }

    gui_assert(resolution >= 3 && resolution <= 5);

    /* Set up the function to compute rgb deltas. */
    if(resolution == 3)
        rgb_error_func = rgb_error_for_res_3;
    else if(resolution == 4)
        rgb_error_func = rgb_error_for_res_4;
    else if(resolution == 5)
        rgb_error_func = rgb_error_for_res_5;
    else
        gui_abort();

    itab_elt_count = 1 << (3 * resolution);
    offset = offsets[resolution - 3];

    /* reallocate `inverse_table' to the appropriate size for the
     current resolution */
    {
        Size new_size;

        new_size = itab_base_size(resolution);
#if defined(ITABLE_HASH_SIZE)
        new_size += ITABLE_HASH_SIZE;
#endif
        SetHandleSize((Handle)inverse_table, new_size);
    }
    ITAB_RES(inverse_table) = resolution;

    itab_table = ITAB_TABLE(inverse_table);

    queue = (unsigned short *)alloca(sizeof(unsigned short) * itab_elt_count);
    head = tail = 0;

    index_in_queue_p = (char *)alloca(sizeof *index_in_queue_p * itab_elt_count);
    memset(index_in_queue_p, '\000', sizeof *index_in_queue_p * itab_elt_count);

    /* Allocate an array for RGB errors and set all entries to the maximum. */
    rgb_error = (ULONGINT *)alloca(sizeof(ULONGINT) * itab_elt_count);
    memset(rgb_error, ~0, sizeof(ULONGINT) * itab_elt_count);

    /* Initialize to a non-0 and non-0xFF value. */
    memset(color_for_index, 0x80, sizeof color_for_index);

    queue_starts_empty_p = true;
    ctab_size = CTAB_SIZE(color_table);
    for(i = ctab_size; i >= 0; i--)
    {
        ColorSpec *color = &CTAB_TABLE(color_table)[i];
        int inverse_table_index;
        ULONGINT error;
        unsigned char new_value;

        /* don't include reserved colors in the inverse table */
        if(color->value & CTAB_RESERVED_BIT)
            continue;

        /* We always want to enqueue this color, even if it isn't the
       * lowest error match for the starting itab entry.  It may become
       * the lowest error color for neighboring itab entries.
       */
        inverse_table_index = RGB_TO_ITAB_INDEX(&(color->rgb), resolution);
        ENQUEUE(inverse_table_index);
        queue_starts_empty_p = false;

        /* Figure out what the color index is. */
        if(CTAB_FLAGS(color_table) & CTAB_GDEVICE_BIT)
            new_value = i;
        else
            new_value = color->value;

        /* Save away the color for this index, in native endian byte order. */
        color_for_index[new_value].rgb.red = color->rgb.red;
        color_for_index[new_value].rgb.green = color->rgb.green;
        color_for_index[new_value].rgb.blue = color->rgb.blue;

        current_red = color_for_index[new_value].rgb.red;
        current_blue = color_for_index[new_value].rgb.blue;
        current_green = color_for_index[new_value].rgb.green;

        /* Compute the error for this match. */
        error = (*rgb_error_func)(inverse_table_index);

        /* If this beats the best error for this square so far, make
       * that be the new error value.
       */
        if(error < rgb_error[inverse_table_index])
        {
            itab_table[inverse_table_index] = new_value;
            SET_RGB_ERROR(inverse_table_index, error);
        }
    }

    if(queue_starts_empty_p)
    {
        /* Some degenerate color tables can cause us to enqueue nothing
       * at all.  It's not clear what to do in the case, but we
       * do _not_ want to start in on the do...while loop below.
       * That can cause mystery smashage deaths, because we'd look
       * at uninitialized queue elements.  This was happening
       * when we were letting FattyBearDemo try a SetDepth to 0 bpp.
       */
        warning_unexpected("Empty queue!");
    }
    else
    {
        do
        {
            int inverse_table_index;
            unsigned char ctab_index;
            const NativeRGBColor *current_color;
            int off;

            inverse_table_index = DEQUEUE();
            ctab_index = itab_table[inverse_table_index];

            /* Stash the current color away in some globals, outside the loop. */
            current_color = &color_for_index[ctab_index].rgb;
            current_red = current_color->red; /* native endian! */
            current_green = current_color->green; /* native endian! */
            current_blue = current_color->blue; /* native endian! */

            /* Find the adjacent rgb coordinates to this pixel.  If we can
	   * beat the error found at each neighbor, then we spread out to
	   * that neighbor.
	   */
            for(off = 25; off >= 0; off--)
            {
                int next_itab_index;
                ULONGINT error;

                /* compute the next adjacent index */
                next_itab_index = inverse_table_index + offset[off];
                if((unsigned)next_itab_index < (unsigned)itab_elt_count)
                {
                    /* Compute the error we'll create by taking this step. */
                    error = (*rgb_error_func)(next_itab_index);

                    /* Did we improve on the best error for that entry? */
                    if(error < rgb_error[next_itab_index])
                    {
                        ENQUEUE(next_itab_index);
                        itab_table[next_itab_index] = ctab_index;
                        rgb_error[next_itab_index] = error;
                    }
                }
            }

            syncint_check_interrupt();
        } while(tail != head);
    }

    /* If the first or last entries in the table are black and white,
   * make sure their entries map to them.  This wouldn't be necessary
   * if we were able to do a more fine-grained resolution; however, some
   * programs (like FattyBearDemo) expect that black will definitely
   * map to the last entry in the color table, even if there's more
   * than one black or near-black color.
   */
    for(i = 0; i <= ctab_size; i += ctab_size ? ctab_size : 1)
    {
        ULONGINT color_sum;

        color_sum = (unsigned(color_for_index[i].rgb.red)
                     + color_for_index[i].rgb.green
                     + color_for_index[i].rgb.blue);

        if(color_sum == 0xFFFF * 3)
            itab_table[itab_elt_count - 1] = i;
        else if(color_sum == 0x0000 * 3)
            itab_table[0] = i;
    }

#if defined(ITABLE_HASH_SIZE)
    add_hash_table(color_table, inverse_table, resolution);
#endif

    ITAB_SEED(inverse_table) = CTAB_SEED(color_table);
}

void Executor::C_ProtectEntry(INTEGER index, Boolean protect)
{
    GDHandle gdev;
    ColorSpec *entry;

    gdev = LM(TheGDevice);
    entry = &CTAB_TABLE(PIXMAP_TABLE(GD_PMAP(gdev)))[index];
    if(protect)
    {
        if(entry->value & CTAB_PROTECTED_BIT)
            /* #warning "set error bit here" */
            return;

        /* mark this entry as protected; and set the
	 low byte of the value field with the current
         device id */
        entry->value = CTAB_PROTECTED_BIT | (GD_ID(gdev) & 0xFF);
    }
    else
    {
        /* clear the protected bit and id fields of the value */
        entry->value = 0;
    }
    /* this does not effect color lookup, so the seed of the
     current graphics device's colortable is unchanged */
}

void Executor::C_ReserveEntry(INTEGER index, Boolean reserve)
{
    GDHandle gdev;
    CTabHandle ctab;
    ColorSpec *entry;
    GUEST<INTEGER> old_value;

    gdev = LM(TheGDevice);
    ctab = PIXMAP_TABLE(GD_PMAP(gdev));
    entry = &CTAB_TABLE(ctab)[index];

    old_value = entry->value;
    if(reserve)
    {
        if(entry->value & CTAB_RESERVED_BIT)
        {
            ROMlib_qd_error = cProtectErr;
            return;
        }

        /* mark this entry as reserved; and set the
	 low byte of the value field with the current
	 device id */
        entry->value = CTAB_RESERVED_BIT | (GD_ID(gdev) & 0xFF);
    }
    else
    {
        /* clear the entry */
        entry->value = entry->value & ~CTAB_RESERVED_BIT;
    }

    /* success */
    ROMlib_qd_error = noErr;

    /* Only change the seed when necessary. */
    if(old_value != entry->value)
        CTAB_SEED(ctab) = GetCTSeed();
}

void Executor::C_SetEntries(INTEGER start, INTEGER count, ColorSpec *atable)
{
    /* ##### figure out exactly what the mac does */
    GDHandle gd;

    CTabHandle ctab;
    ColorSpec *ctab_table;
    int ctab_changed_p, i;
    RGBColor *r1, *r2;
    /* for calling `vdriver->setColors ()' */
    int first_color, num_colors;

    gd = LM(TheGDevice);

    if(GD_TYPE(gd) != clutType)
        /* #### return error code? */
        return;

    ctab = PIXMAP_TABLE(GD_PMAP(gd));
    ctab_table = CTAB_TABLE(ctab);
    ctab_changed_p = false;

    if(start >= 0)
    {
        for(i = 0; i <= count; i++)
        {
            r1 = &ctab_table[start + i].rgb;
            r2 = &atable[i].rgb;
            if(r1->red != r2->red
               || r1->green != r2->green
               || r1->blue != r2->blue)
            {
                *r1 = *r2;
                ctab_changed_p = true;
            }
        }
        first_color = start;
        num_colors = count + 1;
    }
    else
    {
        int min = 10000, max = -10000;

        for(i = 0; i <= count; i++)
        {
            int index = atable[i].value;

            if(index < min)
                min = index;
            if(index > max)
                max = index;

            r1 = &ctab_table[index].rgb;
            r2 = &atable[i].rgb;
            if(r1->red != r2->red
               || r1->green != r2->green
               || r1->blue != r2->blue)
            {
                *r1 = *r2;
                ctab_changed_p = true;
            }
        }

        first_color = min;
        num_colors = max - min + 1;
    }

    /* We need this hack in here so that Wolfenstein 3D will work.
   * If we don't always update the real color table, the colors
   * are simply wrong.
   */
    ctab_changed_p = true;

    if(ctab_changed_p)
    {
        CTAB_SEED(ctab) = GetCTSeed();
        if(gd == LM(MainDevice))
        {
            if(num_colors > 0)
            {
                gd_update_colors();
            }
        }
    }
}

void Executor::C_AddSearch(ProcPtr searchProc)
{
    GDHandle gdev;
    SProcHndl search_list_elt;

    gdev = LM(TheGDevice);

    search_list_elt = (SProcHndl)NewHandle(sizeof(SProcRec));
    (*search_list_elt)->srchProc = searchProc;
    (*search_list_elt)->nxtSrch = GD_SEARCH_PROC(gdev);

    GD_SEARCH_PROC(gdev) = search_list_elt;

    /* Invalidate all color conversion tables. */
    ROMlib_invalidate_conversion_tables();
}

void Executor::C_AddComp(ProcPtr compProc)
{
    warning_unimplemented("");
}

void Executor::C_DelSearch(ProcPtr searchProc)
{
    GDHandle gdev;
    SProcHndl s, prev;

    gdev = LM(TheGDevice);

    prev = nullptr;
    for(s = GD_SEARCH_PROC(gdev); s != nullptr; s = (*s)->nxtSrch)
    {
        if((*s)->srchProc == searchProc)
        {
            if(prev == nullptr)
                GD_SEARCH_PROC(gdev) = (*s)->nxtSrch;
            else
                (*prev)->nxtSrch = (*s)->nxtSrch;
            DisposeHandle((Handle)s);
            /* Invalidate all color conversion tables. */
            ROMlib_invalidate_conversion_tables();
            break;
        }
        else
            prev = s;
    }

    if(s == nullptr)
        warning_unimplemented("");
}

void Executor::C_DelComp(ProcPtr compProc)
{
    warning_unimplemented("");
}

void Executor::C_SetClientID(INTEGER id)
{
    GD_ID(LM(TheGDevice)) = id;
}

void Executor::C_SaveEntries(CTabHandle src, CTabHandle result,
                             ReqListRec *selection)
{
    int req_size, src_ctab_size;
    int req_error_p = false;
    int i;

    if(src == nullptr)
        src = PIXMAP_TABLE(GD_PMAP(LM(TheGDevice)));

    src_ctab_size = CTAB_SIZE(src);
    req_size = selection->reqLSize;

    SetHandleSize((Handle)result,
                  CTAB_STORAGE_FOR_SIZE(req_size));
    CTAB_SIZE(result) = req_size;
    /* #### should this set the color table seed? */
    CTAB_SEED(result) = GetCTSeed();
    CTAB_FLAGS(result) = 0;

    for(i = 0; i <= req_size; i++)
    {
        int req_index = selection->reqLData[i];

        if(req_index >= 0
           && req_index <= src_ctab_size)
            CTAB_TABLE(result)
            [i] = CTAB_TABLE(src)[req_index];
        else
        {
            selection->reqLData[i] = colReqErr;
            req_error_p = true;
        }
    }
    if(req_error_p)
    {
        /* #### does SaveEntries return colReqErr if errors occured */
        ROMlib_qd_error = colReqErr;
    }
}

void Executor::C_RestoreEntries(CTabHandle src, CTabHandle dst,
                                ReqListRec *selection)
{
    int req_size, dst_ctab_size;
    int req_error_p = false;
    int i;

    if(dst == nullptr)
        dst = PIXMAP_TABLE(GD_PMAP(LM(TheGDevice)));

    dst_ctab_size = CTAB_SIZE(dst);
    req_size = selection->reqLSize;

    for(i = 0; i < req_size; i++)
    {
        int req_index = selection->reqLData[i];

        if(req_index >= 0
           && req_index <= dst_ctab_size)
            CTAB_TABLE(dst)
            [req_index] = CTAB_TABLE(src)[i];
        else
        {
            selection->reqLData[i] = colReqErr;
            req_error_p = true;
        }
    }
    if(req_error_p)
    {
        /* #warning does SaveEntries return colReqErr if errors occured */
        ROMlib_qd_error = colReqErr;
    }
}
