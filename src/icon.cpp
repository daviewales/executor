/* Copyright 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

// FIXME: #warning the icon suite representation is our own brew -- tests should
// FIXME: #warning be written and we should do what the Mac does

#include <base/common.h>

#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <Iconutil.h>

#include <quickdraw/cquick.h>
#include <res/resource.h>
#include <mman/mman.h>
#include <rsys/icon.h>

using namespace Executor;

#define ICON_RETURN_ERROR(error)                                     \
    do{                                                               \
        OSErr _error_ = (error);                                     \
                                                                     \
        if(_error_ != noErr)                                         \
            warning_unexpected("error `%s', `%d'", #error, _error_); \
        return _error_;                                              \
    }while(false)

OSErr Executor::C_PlotIconID(const Rect *rect, IconAlignmentType align,
                             IconTransformType transform, short res_id)
{
    Handle icon_suite;
    OSErr err;

    err = GetIconSuite(out(icon_suite), res_id, svAllAvailableData);
    if(err != noErr)
        ICON_RETURN_ERROR(err);

    err = PlotIconSuite(rect, align, transform, icon_suite);
    if(err != noErr)
        ICON_RETURN_ERROR(err);

    DisposeIconSuite(icon_suite, false);

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_PlotIconMethod(const Rect *rect, IconAlignmentType align,
                                 IconTransformType transform,
                                 IconGetterUPP method, void *data)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

void Executor::C_PlotIcon(const Rect *rect, Handle icon)
{
    if(icon == nullptr)
        return;

    if(!*icon)
        LoadResource(icon);

    HLockGuard guard(icon);
    BitMap bm;

    bm.baseAddr = *icon;
    bm.rowBytes = 4;
    bm.bounds.left = bm.bounds.top = 0;
    if(GetHandleSize(icon) == 2 * 16)
    {
        bm.rowBytes = 2;
        bm.bounds.bottom = 16;
    }
    else
    {
        bm.rowBytes = 4;
        bm.bounds.bottom = 32;
    }
    bm.bounds.right = bm.bounds.bottom;
    CopyBits(&bm, PORT_BITS_FOR_COPY(qdGlobals().thePort), &bm.bounds, rect,
             srcCopy, nullptr);
}

OSErr Executor::C_PlotIconHandle(const Rect *rect, IconAlignmentType align,
                                 IconTransformType transform, Handle icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

void Executor::C_PlotCIcon(const Rect *rect, CIconHandle icon)
{
    /* when plotting, `ignore' the current fg/bk colors */
    GrafPtr current_port;
    RGBColor bk_rgb, fg_rgb;
    GUEST<int32_t> bk_color, fg_color;
    int cgrafport_p;

    if(!icon)
        return;

    current_port = qdGlobals().thePort;

    cgrafport_p = CGrafPort_p(current_port);
    if(cgrafport_p)
    {
        fg_rgb = CPORT_RGB_FG_COLOR(current_port);
        bk_rgb = CPORT_RGB_BK_COLOR(current_port);
    }
    fg_color = PORT_FG_COLOR(current_port);
    bk_color = PORT_BK_COLOR(current_port);

    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);

    HLockGuard guard(icon);
    PixMapHandle gd_pixmap;

    BitMap *mask_bm;
    BitMap *icon_bm;

    icon_bm = &CICON_BMAP(icon);

    mask_bm = &CICON_MASK(icon);
    BITMAP_BASEADDR(mask_bm) = (Ptr)CICON_MASK_DATA(icon);

    gd_pixmap = GD_PMAP(LM(MainDevice));

    if((PORT_BASEADDR(current_port) == PIXMAP_BASEADDR(gd_pixmap)
        && PIXMAP_PIXEL_SIZE(gd_pixmap) > 2)
       || (CGrafPort_p(current_port)
           && PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(current_port)) > 2)
       || !BITMAP_ROWBYTES(icon_bm))
    {
        Handle icon_data;

        icon_data = CICON_DATA(icon);
        HLockGuard guard(icon_data);

        PixMap *icon_pm;

        icon_pm = &CICON_PMAP(icon);
        BITMAP_BASEADDR(icon_pm) = *icon_data;

        CopyMask((BitMap *)icon_pm,
                 mask_bm,
                 PORT_BITS_FOR_COPY(current_port),
                 &BITMAP_BOUNDS(icon_pm),
                 &BITMAP_BOUNDS(mask_bm),
                 /* #### fix up the need for this cast */
                 (Rect *)rect);
    }
    else
    {
        Rect *icon_bm_bounds;
        Ptr bm_baseaddr;
        int height;
        int mask_data_size;

        icon_bm_bounds = &BITMAP_BOUNDS(icon_bm);

        height = RECT_HEIGHT(icon_bm_bounds);
        mask_data_size = BITMAP_ROWBYTES(icon_bm) * height;
        bm_baseaddr = (Ptr)((char *)CICON_MASK_DATA(icon)
                            + mask_data_size);

        BITMAP_BASEADDR(icon_bm) = bm_baseaddr;
        CopyMask(icon_bm,
                 mask_bm,
                 PORT_BITS_FOR_COPY(current_port),
                 icon_bm_bounds,
                 &BITMAP_BOUNDS(mask_bm),
                 /* #### fix up the need for this cast */
                 (Rect *)rect);
    }

    if(cgrafport_p)
    {
        CPORT_RGB_FG_COLOR(current_port) = fg_rgb;
        CPORT_RGB_BK_COLOR(current_port) = bk_rgb;
    }
    PORT_FG_COLOR(current_port) = fg_color;
    PORT_BK_COLOR(current_port) = bk_color;
}

OSErr Executor::C_PlotCIconHandle(const Rect *rect, IconAlignmentType align,
                                  IconTransformType transform, CIconHandle icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotCIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_PlotSICNHandle(const Rect *rect, IconAlignmentType align,
                                 IconTransformType transform, Handle icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

Handle Executor::C_GetIcon(short icon_id)
{
    return ROMlib_getrestid("ICON"_4, icon_id);
}

CIconHandle Executor::C_GetCIcon(short icon_id)
{
    CIconHandle cicon_handle;
    CIconHandle cicon_res_handle;
    CIconPtr cicon_res;
    int height;
    int mask_data_size;
    int bmap_data_size;
    int new_size;

    cicon_res_handle = (CIconHandle)ROMlib_getrestid("cicn"_4, icon_id);
    if(cicon_res_handle == nullptr)
        return nullptr;

    cicon_res = *cicon_res_handle;
    height = RECT_HEIGHT(&cicon_res->iconPMap.bounds);
    mask_data_size = cicon_res->iconMask.rowBytes * height;
    bmap_data_size = cicon_res->iconBMap.rowBytes * height;
    new_size = sizeof(CIcon) - sizeof(INTEGER) + mask_data_size + bmap_data_size;

    cicon_handle = (CIconHandle)NewHandle(new_size);
    HLockGuard guard1(cicon_handle), guard2(cicon_res_handle);

    CTabPtr tmp_ctab;
    int mask_data_offset;
    int bmap_data_offset;
    int pmap_ctab_offset;
    int pmap_ctab_size;
    int pmap_data_offset;
    int pmap_data_size;
    CIconPtr cicon;

    cicon = *cicon_handle;
    cicon_res = *cicon_res_handle;

    BlockMoveData((Ptr)cicon_res, (Ptr)cicon, new_size);

    mask_data_offset = 0;

    bmap_data_offset = mask_data_size;

    pmap_ctab_offset = bmap_data_offset + bmap_data_size;
    tmp_ctab = (CTabPtr)((char *)&cicon_res->iconMaskData + pmap_ctab_offset);
    pmap_ctab_size = sizeof(ColorTable) + (tmp_ctab->ctSize
                                           * sizeof(ColorSpec));

    pmap_data_offset = pmap_ctab_offset + pmap_ctab_size;
    pmap_data_size = (cicon->iconPMap.rowBytes
                      & ROWBYTES_VALUE_BITS)
        * height;

    cicon->iconMask.baseAddr = nullptr;

    cicon->iconBMap.baseAddr = nullptr;

    {
        CTabHandle color_table;

        color_table
            = (CTabHandle)NewHandle(pmap_ctab_size);
        BlockMoveData((Ptr)&cicon_res->iconMaskData + pmap_ctab_offset,
                      (Ptr)*color_table,
                      pmap_ctab_size);
        CTAB_SEED(color_table) = GetCTSeed();
        cicon->iconPMap.pmTable = color_table;

        cicon->iconPMap.baseAddr = nullptr;
        cicon->iconData = NewHandle(pmap_data_size);
        BlockMoveData((Ptr)&cicon_res->iconMaskData + pmap_data_offset,
                      (Ptr)(*cicon->iconData),
                      pmap_data_size);
    }

    return cicon_handle;
}

void Executor::C_DisposeCIcon(CIconHandle icon)
{
    DisposeHandle(CICON_DATA(icon));
    DisposeHandle((Handle)CICON_PMAP(icon).pmTable);
    DisposeHandle((Handle)icon);
}

#define large_bw_icon 0
#define small_bw_icon 3

static int icon_for_log2_bpp[] = {
    0, 0, 1, 2, 2, 2,
};

static int bpp_for_icon[] = {
    1, 4, 8,
};

static int restype_for_icon[] = {
    large1BitMask,
    large4BitData,
    large8BitData,
    small1BitMask,
    small4BitData,
    small8BitData,
};

static int mask_for_icon[] = {
    svLarge1Bit,
    svLarge4Bit,
    svLarge8Bit,
    svSmall1Bit,
    svSmall4Bit,
    svSmall8Bit,
};

static int
restype_to_index(ResType type)
{
    int i;

    for(i = 0; i < N_SUITE_ICONS; i++)
    {
        if(type == restype_for_icon[i])
            return i;
    }

    gui_fatal("unknown icon restype `%d'");
}

OSErr Executor::C_GetIconSuite(GUEST<Handle> *icon_suite_return, short res_id,
                               IconSelectorValue selector)
{
    Handle icon_suite, *icons;
    int i;

    icon_suite = NewHandleClear(sizeof(cotton_suite_layout_t));
    if(LM(MemErr) != noErr)
        ICON_RETURN_ERROR(memFullErr);

    HLockGuard guard(icon_suite);

    icons = (Handle *)*icon_suite;

    for(i = 0; i < N_SUITE_ICONS; i++)
    {
        if(selector & mask_for_icon[i])
        {
            Handle icon;

            icon = GetResource(restype_for_icon[i], res_id);
            if(icon != nullptr)
                icons[i] = icon;
        }
    }

    *icon_suite_return = icon_suite;

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_NewIconSuite(GUEST<Handle> *icon_suite_return)
{
    Handle icon_suite;

    icon_suite = NewHandleClear(sizeof(cotton_suite_layout_t));
    if(LM(MemErr) != noErr)
        ICON_RETURN_ERROR(memFullErr);

    *icon_suite_return = icon_suite;

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_AddIconToSuite(Handle icon_data, Handle icon_suite,
                                 ResType type)
{
    Handle *icons;

    icons = (Handle *)*icon_suite;
    icons[restype_to_index(type)] = icon_data;

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_GetIconFromSuite(GUEST<Handle> *icon_data_return,
                                   Handle icon_suite, ResType type)
{
    Handle *icons, icon_data;

    icons = (Handle *)*icon_suite;
    icon_data = icons[restype_to_index(type)];

    if(icon_data == nullptr)
        ICON_RETURN_ERROR(paramErr);

    *icon_data_return = icon_data;
    ICON_RETURN_ERROR(noErr);
}

static OSErr
find_best_icon(bool small_p, int bpp,
               Handle icon_suite_h,
               Handle *icon_data_return, Handle *icon_mask_return,
               bool *small_return_p, int *icon_bpp_return)
{
    Handle *icons, *sized_icons;
    Handle icon_data, icon_mask;
    int best_icon;

    icons = (Handle *)*icon_suite_h;

    sized_icons = (small_p
                       ? &icons[small_bw_icon]
                       : &icons[large_bw_icon]);
    icon_mask = *sized_icons;
    if(icon_mask == nullptr)
    {
        small_p = !small_p;

        sized_icons = (small_p
                           ? &icons[small_bw_icon]
                           : &icons[large_bw_icon]);
        icon_mask = *sized_icons;
        if(icon_mask == nullptr)
            ICON_RETURN_ERROR(noMaskFoundErr);
    }

    best_icon = icon_for_log2_bpp[ROMlib_log2[bpp]];

#if !defined(LETGCCWAIL)
    icon_data = nullptr;
#endif

    for(; best_icon > -1; best_icon--)
    {
        icon_data = sized_icons[best_icon];
        if(icon_data != nullptr)
            break;
    }

    gui_assert(best_icon > -1);

    *small_return_p = small_p;
    *icon_bpp_return = bpp_for_icon[best_icon];

    *icon_mask_return = icon_mask;
    *icon_data_return = icon_data;

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_PlotIconSuite(const Rect *rect, IconAlignmentType align,
                                IconTransformType transform, Handle icon_suite)
{
    GrafPtr current_port;
    int port_bpp, icon_bpp;
    bool little_rect_p, little_icon_p;
    Handle icon_data, icon_mask;
    OSErr err;

    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    current_port = qdGlobals().thePort;
    little_rect_p = (RECT_WIDTH(rect) < 32
                     && RECT_HEIGHT(rect) < 32);
    port_bpp = (CGrafPort_p(current_port)
                    ? toHost(PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(current_port)))
                    : 1);

    err = find_best_icon(little_rect_p, port_bpp, icon_suite,
                         &icon_data, &icon_mask,
                         &little_icon_p, &icon_bpp);
    if(err != noErr)
        ICON_RETURN_ERROR(err);

    /* plot our icon */

    HLockGuard guard1(icon_data), guard2(icon_mask);

    PixMap icon_pm;
    BitMap mask_bm;
    CTabHandle color_table;
    Rect icon_rect;
    int icon_size;

    color_table = GetCTable(icon_bpp);

    memset(&icon_pm, '\000', sizeof icon_pm);
    memset(&icon_rect, '\000', sizeof icon_rect);

    icon_size = (little_icon_p ? 16 : 32);
    icon_rect.bottom = icon_rect.right = icon_size;

    icon_pm.baseAddr = *icon_data;
    icon_pm.rowBytes = (icon_size * icon_bpp / 8)
                          | PIXMAP_DEFAULT_ROW_BYTES;
    icon_pm.bounds = icon_rect;
    icon_pm.pixelSize = icon_pm.cmpSize = icon_bpp;
    icon_pm.cmpCount = 1;
    icon_pm.pmTable = color_table;

    mask_bm.baseAddr = (Ptr)(char *)*icon_mask
                          + icon_size * icon_size / 8;
    mask_bm.rowBytes = icon_size / 8;
    mask_bm.bounds = icon_rect;

    CopyMask((BitMap *)&icon_pm, &mask_bm,
             PORT_BITS_FOR_COPY(current_port),
             &icon_pm.bounds, &mask_bm.bounds,
             /* #### fix up the need for this cast */
             (Rect *)rect);

    DisposeCTable(color_table);

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_ForEachIconDo(Handle suite, IconSelectorValue selector,
                                IconActionUPP action, void *data)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

short Executor::C_GetSuiteLabel(Handle suite)
{
    short retval;
    cotton_suite_layout_t *suitep;

    suitep = (cotton_suite_layout_t *)*suite;
    retval = suitep->label;
    return retval;
}

OSErr Executor::C_SetSuiteLabel(Handle suite, short label)
{
    OSErr retval;
    cotton_suite_layout_t *suitep;

    suitep = (cotton_suite_layout_t *)*suite;
    suitep->label = label;
    retval = noErr;

    return retval;
}

typedef struct
{
    RGBColor rgb_color;
    Str255 string;
} label_info_t;

static label_info_t labels[7] = {
    {
        {
            0, 0, 0,
        },
        "\011Essential",
    },
    {
        {
            0, 0, 0,
        },
        "\03Hot",
    },
    {
        {
            0, 0, 0,
        },
        "\013In Progress",
    },
    {
        {
            0, 0, 0,
        },
        "\04Cool",
    },
    {
        {
            0, 0, 0,
        },
        "\010Personal",
    },
    {
        {
            0, 0, 0,
        },
        "\011Project 1",
    },
    {
        {
            0, 0, 0,
        },
        "\011Project 2",
    },
};

OSErr Executor::C_GetLabel(short label, RGBColor *label_color,
                           Str255 label_string)
{
    unsigned int index;
    OSErr retval;
    static bool been_here = false;

    if(!been_here)
    {
        /* icky */
        labels[0].rgb_color = ROMlib_QDColors[1].rgb; /* orange->yellow */
        labels[1].rgb_color = ROMlib_QDColors[3].rgb; /* red */
        labels[2].rgb_color = ROMlib_QDColors[2].rgb; /* magenta */
        labels[3].rgb_color = ROMlib_QDColors[4].rgb; /* cyan */
        labels[4].rgb_color = ROMlib_QDColors[6].rgb; /* blue */
        labels[5].rgb_color = ROMlib_QDColors[5].rgb; /* green */
        labels[6].rgb_color = ROMlib_QDColors[0].rgb; /* brown->black */
        been_here = true;
    }

    index = label - 1;
    if(index > 6)
        retval = paramErr;
    else
    {
        *label_color = labels[index].rgb_color;
        str255assign((StringPtr)label_string,
                     (StringPtr)labels[index].string);
        retval = noErr;
    }

    ICON_RETURN_ERROR(retval);
}

OSErr Executor::C_DisposeIconSuite(Handle suite, Boolean dispose_data_p)
{
    if(dispose_data_p)
    {
        HLockGuard guard(suite);

        Handle *icons;
        int i;

        icons = (Handle *)*suite;
        for(i = 0; i < N_SUITE_ICONS; i++)
        {
            Handle icon;
            SignedByte icon_state;

            icon = icons[i];
            if(icon)
            {
                icon_state = HGetState(icon);
                if(icon_state & RSRCBIT)
                    ;
                else
                    DisposeHandle(icons[i]);
            }
        }
    }

    DisposeHandle(suite);

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_IconSuiteToRgn(RgnHandle rgn, const Rect *rect,
                                 IconAlignmentType align, Handle suite)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_IconIDToRgn(RgnHandle rgn, const Rect *rect,
                              IconAlignmentType align, short icon_id)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_IconMethodToRgn(RgnHandle rgn, const Rect *rect,
                                  IconAlignmentType align,
                                  IconGetterUPP method, void *data)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

Boolean Executor::C_PtInIconSuite(Point test_pt, const Rect *rect,
                                  IconAlignmentType align, Handle suite)
{
    warning_unimplemented("");
    return false;
}

Boolean Executor::C_PtInIconID(Point test_pt, const Rect *rect,
                               IconAlignmentType align, short icon_id)
{
    Boolean retval;

    warning_unimplemented("poorly implemented");
    retval = PtInRect(test_pt, (Rect *)rect);
    return retval;
}

Boolean Executor::C_PtInIconMethod(Point test_pt, const Rect *rect,
                                   IconAlignmentType align,
                                   IconGetterUPP method, void *data)
{
    warning_unimplemented("");
    return false;
}

Boolean Executor::C_RectInIconSuite(const Rect *test_rect, const Rect *rect,
                                    IconAlignmentType align, Handle suite)
{
    warning_unimplemented("");
    return false;
}

Boolean Executor::C_RectInIconID(const Rect *test_rect, const Rect *rect,
                                 IconAlignmentType align, short icon_id)
{
    warning_unimplemented("");
    return false;
}

Boolean Executor::C_RectInIconMethod(const Rect *test_rect, const Rect *rect,
                                     IconAlignmentType align,
                                     IconGetterUPP method, void *data)
{
    warning_unimplemented("");
    return false;
}

OSErr Executor::C_MakeIconCache(GUEST<Handle> *cache, IconGetterUPP make_icon,
                                void *data)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_LoadIconCache(const Rect *rect, IconAlignmentType align,
                                IconTransformType transform, Handle cache)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_GetIconCacheData(Handle cache, GUEST<void *>*data)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_SetIconCacheData(Handle cache, void *data)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_GetIconCacheProc(Handle cache, GUEST<IconGetterUPP> *proc)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}

OSErr Executor::C_SetIconCacheProc(Handle cache, IconGetterUPP proc)
{
    warning_unimplemented("");
    ICON_RETURN_ERROR(paramErr);
}
