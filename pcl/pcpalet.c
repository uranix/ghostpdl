/* Copyright (C) 1996, 1997, 1998 Aladdin Enterprises.  All rights
   reserved.  Unauthorized use, copying, and/or distribution
   prohibited.  */

/* pcpalet.c - PCL 5c palette object implementation */
#include "gx.h"
#include "pldict.h"
#include "pcdraw.h"
#include "pcpage.h"
#include "pcursor.h"
#include "pcpalet.h"
#include "pcfrgrnd.h"

/* GC routines */
private_st_palette_t();
private_st_pstack_entry_t();

/*
 * Free a PCL palette.
 */
  private void
free_palette(
    gs_memory_t *   pmem,
    void *          pvpalet,
    client_name_t   cname
)
{
    pcl_palette_t * ppalet = (pcl_palette_t *)pvpalet;

    if (ppalet->pindexed != 0)
        pcl_cs_indexed_release(ppalet->pindexed);
    if (ppalet->pcrd != 0)
        pcl_crd_release(ppalet->pcrd);
    if (ppalet->pht != 0)
        pcl_ht_release(ppalet->pht);
    gs_free_object(pmem, pvpalet, cname);
}

/*
 * Free procedure for palettes to be used by the dictionary which implements
 * the palette store.
 */
  private void
dict_free_palette(
    gs_memory_t *   pmem,
    void *          pvpalet,
    client_name_t   cname
)
{
    pcl_palette_t * ppalet = (pcl_palette_t *)pvpalet;

    rc_decrement(ppalet, cname);
}

/*
 * Allocat a PCL palette object.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  private int
alloc_palette(
    pcl_state_t *       pcs,
    pcl_palette_t **    pppalet,
    gs_memory_t *       pmem
)
{
    pcl_palette_t *     ppalet = 0;

    rc_alloc_struct_1( ppalet,
                       pcl_palette_t,
                       &st_palette_t,
                       pmem,
                       return e_Memory,
                       "allocate pcl palette object"
                       );
    ppalet->rc.free = free_palette;
    ppalet->id = pcl_next_id(pcs);
    ppalet->pindexed = 0;
    ppalet->pcrd = 0;
    ppalet->pht = 0;
    *pppalet = ppalet;
    return 0;
}

/*
 * Create a unique copy of a PCL palette.
 *
 * The unsharing of PCL palettes is a bit involved due to the nature of the
 * palette store. In normal handling, the current PCL state does not maintain
 * a reference to the current palette. Rather, it keeps the current palette id.,
 * and the definition of this id. in the dictionary that implements the palette
 * store is the only reference to the palette.
 *
 * The structure of XL dictionaries is (properly) opaque, so there is no direct
 * access is available to pointer to a palette. Furthermore, the dictionary
 * get, put, and undef procedures do not recognize object structure, hence they
 * provide no native support for reference counted objects.
 *
 * To work around these problems, a separate pointer to a palette is maintained
 * in the PCL, though one that does not normally counted as a reference. The
 * make-unique operation will modify this pointer, and immediately redefine
 * the current palette id.
 *
 * To simplify the remainder of this code, unshared palettes are given new
 * identifiers, even if the unshare operation does nothing. This operation is
 * specifically overridden when modifying pen widths, since the latter are never
 * cached.
 *
 * Unlike the "unshare" functions for other objects, this function will create
 * the PCL palette object if it does not already exist.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  private int
unshare_palette(
    pcl_state_t *       pcs
)
{
    pcl_palette_t *     ppalet = pcs->ppalet;
    pcl_palette_t *     pnew = 0;
    int                 code = 0;
    pcl_id_t            key;

    /* check if there is anything to do */
    if ((ppalet != 0) && (ppalet->rc.ref_count == 1)) {
        ppalet->id = pcl_next_id(pcs);
        return 0;
    }

    /* allocate a new palette */
    if ((code = alloc_palette(pcs, &pnew, pcs->memory)) < 0)
        return code;
    if (ppalet != 0) {
        pcl_cs_indexed_init_from(pnew->pindexed, ppalet->pindexed);
        pcl_crd_init_from(pnew->pcrd, ppalet->pcrd);
        pcl_ht_init_from(pnew->pht, ppalet->pht);
    }

    /* redefine the current palette id. */
    pcs->ppalet = pnew;
    id_set_value(key, pcs->sel_palette_id);
    code = pl_dict_put(&pcs->palette_store, id_key(key), 2, pnew);
    return (code == -1 ? e_Memory : 0);
}

/*
 * Build a default palette, and define it to be the currently selected
 * palette.
 */
  private int
build_default_palette(
    pcl_state_t *   pcs
)
{
    pcl_id_t        key;
    gs_memory_t *   pmem = pcs->memory;
    pcl_palette_t * ppalet = 0;
    int             code = 0;

    if (pcs->pdflt_palette == 0) {
        code = alloc_palette(pcs, &ppalet, pmem);
        if (code == 0)
            code = pcl_cs_indexed_build_default_cspace( pcs,
							&(ppalet->pindexed),
                                                        pmem
                                                       );
        if ((code == 0) && (pcs->pcl_default_crd == 0))
            code = pcl_crd_build_default_crd(pcs);
        if (code == 0)
            pcl_crd_init_from(ppalet->pcrd, pcs->pcl_default_crd);
        if (code == 0)
            code = pcl_ht_build_default_ht(pcs, &(ppalet->pht), pmem);
        if (code < 0) {
            if (ppalet != 0)
                free_palette(pmem, ppalet, "build default palette");
            return code;
        }
        pcl_palette_init_from(pcs->pdflt_palette, ppalet);
    } else
        pcl_palette_init_from(ppalet, pcs->pdflt_palette);


    /* NB: definitions do NOT record a referece */
    id_set_value(key, pcs->sel_palette_id);
    code = pl_dict_put(&pcs->palette_store, id_key(key), 2, ppalet);
    if (code < 0)
        return e_Memory;

    /* the graphic state pointer does not (yet) amount to a reference */
    pcs->ppalet = ppalet;
    return 0;
}

/*
 * Clear the palette stack.
 */
 void
clear_palette_stack(
    pcl_state_t *       pcs,
    gs_memory_t *       pmem
)
{
    pstack_entry_t *    pentry = pcs->palette_stack;

    while (pentry != 0) {
        pstack_entry_t *    pnext = pentry->pnext;

        pcl_palette_release(pentry->ppalet);
        gs_free_object(pmem, pentry, "clear palette stack");
        pentry = pnext;
    }
    pcs->palette_stack = 0;
}

/*
 * ESC * p # P
 *
 * Push a copy of the current palette onto the palette stack, or pop one off.
 * Note the need to redefine the select palette id. in the event that a palette
 * is popped.
 */
  private int
push_pop_palette(
    pcl_args_t *    pargs,
    pcl_state_t *   pcs
)
{
    int             action = uint_arg(pargs);

    if (action == 0) {
        pstack_entry_t *    pentry;
        int                 code;

        /* if there is not yet a palette, build the default */
        if ((pcs->ppalet == 0) && ((code = build_default_palette(pcs)) < 0))
            return code;

        pentry = gs_alloc_struct( pcs->memory,
                                  pstack_entry_t,
                                  &st_pstack_entry_t,
                                  "push pcl palette"
                                  );
        if (pentry == 0)
            return e_Memory;

        pcl_palette_init_from(pentry->ppalet, pcs->ppalet);
        pentry->pnext = pcs->palette_stack;
        pcs->palette_stack = pentry;

        return 0;

    } else if (action == 1) {
        pstack_entry_t *    pentry = pcs->palette_stack;
        int                 code = 0;

        if (pentry != 0) {
            pcl_id_t    key;

            pcs->palette_stack = pentry->pnext;

            /* NB: just set - pcs->ppalet is not a reference */
            pcs->ppalet = pentry->ppalet;

            /* the dictionary gets the stack reference on the palette */
            id_set_value(key, pcs->sel_palette_id);
            code = pl_dict_put(&pcs->palette_store, id_key(key), 2, pentry->ppalet);
            gs_free_object(pcs->memory, pentry, "pop pcl palette");
        }

        return (code < 0 ? e_Memory : 0);

    } else
        return 0;
}


/*
 * Set the normalization values for an indexed color space. This is needed
 * only for the GL/2 CR command; PCL sets normalization information via the
 * configure image data command, which builds a new indexed color space.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_CR(
    pcl_state_t *   pcs,
    floatp          wht0,
    floatp          wht1,
    floatp          wht2,
    floatp          blk0,
    floatp          blk1,
    floatp          blk2
)
{
    int             code = unshare_palette(pcs);

    /* if the default color space must be built, it is fixed, so don't bother */
    if (pcs->ppalet->pindexed == 0)
        return code;

    if (code == 0)
        code = pcl_cs_indexed_set_norm_and_Decode( &(pcs->ppalet->pindexed),
                                                   wht0,
                                                   wht1,
                                                   wht2,
                                                   blk0,
                                                   blk1,
                                                   blk2
                                                   );
    return code;
}

/*
 * Set the number of entries in a color palette. This is needed only for the
 * GL/2 NP command; PCL sets the number of entries in a palette via the
 * configure image data command, which creates a new indexed color space.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_NP(
    pcl_state_t *   pcs,
    int             num_entries
)
{
    int             code = unshare_palette(pcs);

    /* if the default color space must be built, it is fixed, so don't bother */
    if (pcs->ppalet->pindexed == 0)
        return code;

    if (code == 0)
        code = pcl_cs_indexed_set_num_entries( &(pcs->ppalet->pindexed),
                                               num_entries,
                                               true
                                               );
    return code;
}

/*
 * Set the render method for the palette.
 *
 * Returns 0 on success, < 0 in the event of an error.
 *
 * The value of the render method is not checked against the existing value,
 * since the fact that two are the same is neither a necessary nor sufficient
 * condition for determining that the render algorithm has changed: it is
 * possible that the rendering remap array (see pcht.c) has been modified.
 */
  int
pcl_palette_set_render_method(
    pcl_state_t *   pcs,
    uint            render_method
)
{
    int             code = unshare_palette(pcs);

    if ((code == 0) && (pcs->ppalet->pht == 0))
        code = pcl_ht_build_default_ht(pcs, &(pcs->ppalet->pht), pcs->memory);
    if (code >= 0)
        code = pcl_ht_set_render_method(pcs, &(pcs->ppalet->pht), render_method);
    if (code >= 0)
        pcs->render_mode = render_method;
    return code;
}

/*
 * Set gamma correction information for a palette.
 *
 * Gamma correction and the color lookup table for device specific color spaces
 * perform the same function, but are of different origins. Hence, while a
 * configure image data command will discard all color lookup tables, it inherits
 * the gamma configuration parameter from the existing palette. In addition,
 * while there is an "unset" command for color lookup tables, there is no such
 * command for gamma correction: to override the existing gamma correction,
 * either specify a new one or download a color correction table for a device
 * specific color space.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_set_gamma(
    pcl_state_t *   pcs,
    float           gamma
)
{
    int             code = unshare_palette(pcs);

    if ((code == 0) && (pcs->ppalet->pht == 0))
        code = pcl_ht_build_default_ht(pcs, &(pcs->ppalet->pht), pcs->memory);
    if (code == 0)
        code = pcl_ht_set_gamma(&(pcs->ppalet->pht), gamma);
    return code;
}

/*
 * Set color lookup table information for a palette.
 *
 * Lookup tables for device-specific and device-independent color spaces are
 * implemented in different ways. The former are implemented via transfer
 * functions, and thus affect the halftone component of the current palette.
 * The latter are implemented in the device-independent color spaces themselves,
 * and thus affect the color spaces component of the palette.
 *
 * An anachronism of the PCL is that, while color lookup tables may be set
 * individually for different color spaces, they only be cleared all at once.
 * This is accomplished by calling this routine with a null lookup table pointer.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_set_lookup_tbl(
    pcl_state_t *       pcs,
    pcl_lookup_tbl_t *  plktbl
)
{
    int                 code = unshare_palette(pcs);
    pcl_cspace_type_t   lktype;

    if ((code == 0) && (pcs->ppalet->pindexed == 0))
        code = pcl_cs_indexed_build_default_cspace( pcs,
						    &(pcs->ppalet->pindexed),
                                                    pcs->memory
                                                    );
    if ((code == 0) && (pcs->ppalet->pht == 0))
        code = pcl_ht_build_default_ht(pcs, &(pcs->ppalet->pht), pcs->memory);
    if (code < 0)
        return code;

    /* all look tables are cleared simultaneously */
    if (plktbl != 0)
        lktype = pcl_lookup_tbl_get_cspace(plktbl);
    if ((plktbl == 0) || (lktype <= pcl_cspace_CMY))
        code = pcl_ht_set_lookup_tbl(&(pcs->ppalet->pht), plktbl);
    if ( (code == 0) && ((plktbl == 0) || (lktype >= pcl_cspace_Colorimetric)) )
        code = pcl_cs_indexed_update_lookup_tbl(&(pcs->ppalet->pindexed), plktbl);
    return code;
}

/*
 * Set an entry in a color palette.
 *
 * Returns 0 on success, < 0 in the event of an error. The returned code will
 * normally be ignored.
 */
  int
pcl_palette_set_color(
    pcl_state_t *   pcs,
    int             indx,
    const float     comps[3]
)
{
    int             code = unshare_palette(pcs);

    /* if the default color space must be built, it is fixed, so don't bother */
    if (pcs->ppalet->pindexed == 0)
        return code;

    if (code == 0)
        code = pcl_cs_indexed_set_palette_entry( &(pcs->ppalet->pindexed),
                                                 indx,
                                                 comps
                                                 );
    return code;
}

/*
 * Set a palette entry to its default color.
 *
 * Returns 0 on success, < 0 in the event of an error. The returned code will
 * normally be ignored.
 */
  int
pcl_palette_set_default_color(
    pcl_state_t *   pcs,
    int             indx
)
{
    int             code = unshare_palette(pcs);

    /* if the default color space must be built, it is fixed, so don't bother */
    if (pcs->ppalet->pindexed == 0)
        return code;

    if (code == 0)
        code = pcl_cs_indexed_set_default_palette_entry( &(pcs->ppalet->pindexed),
                                                         indx
                                                         );
    return code;
}

/*
 * Set a pen width. Note that this does NOT change the palette id. This
 * procedure can only be called from GL/2, hence the procedure name.
 *
 * Returns 0 on success, < 0 in the even of an error;
 */
  int
pcl_palette_PW(
    pcl_state_t *   pcs,
    int             pen,
    floatp          width
)
{
    int             code = 0;
    pcl_gsid_t      palette_id;
    pcl_palette_t * ppalet = pcs->ppalet;

    if (ppalet != 0) {
        pcl_cs_indexed_t *  pindexed = ppalet->pindexed;

        if ( (pindexed != 0)                                        &&
             (pen >= 0)                                             &&
             (pen < pcl_cs_indexed_get_num_entries(pindexed))       &&
             (width == pcl_cs_indexed_get_pen_widths(pindexed)[pen])  )
            return 0;

        palette_id = ppalet->id;
        if ((code = unshare_palette(pcs)) < 0)
            return code;
        ppalet = pcs->ppalet;
        ppalet->id = palette_id;

    } else {
        if ((code = unshare_palette(pcs)) < 0)
            return code;
        ppalet = pcs->ppalet;
    }

    return pcl_cs_indexed_set_pen_width(&(ppalet->pindexed), pen, width);
}

/*
 * Set the user-defined dither matrix.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_set_udither(
    pcl_state_t *   pcs,
    pcl_udither_t * pdither
)
{
    int             code = unshare_palette(pcs);

    if ((code == 0) && (pcs->ppalet->pht == 0))
        code = pcl_ht_build_default_ht(pcs, &(pcs->ppalet->pht), pcs->memory);
    if (code == 0)
        code = pcl_ht_set_udither(&(pcs->ppalet->pht), pdither);
    return code;
}

/*
 * Overwrite the current palette with new a new image data configuration.
 * This will rebuild the indexed color space, and discard any currently
 * installed color lookup tables.
 *
 * Tf the operand "fixed" is true, this procedure is being called as part of
 * a "simple color mode" command, and the resulting color palette will have
 * fixed entries.
 *
 * The boolean operand gl2 indicates if this call is being made as the result
 * of an IN command in GL/2. If so, the default set of entries in the color
 * palette is modified.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_set_cid(
    pcl_state_t *       pcs,
    pcl_cid_data_t *    pcid,
    bool                fixed,
    bool                gl2
)
{
    int                 code = unshare_palette(pcs);
    pcl_palette_t *     ppalet = pcs->ppalet;
    pcl_cspace_type_t   cstype_new = pcl_cid_get_cspace(pcid);
    pcl_cspace_type_t   cstype_old;

    if (code < 0)
        return code;

    /* record the old color space type, if it is present */
    if (ppalet->pindexed != 0)
        cstype_old = (pcl_cspace_type_t)ppalet->pindexed->cid.cspace;
    else
        cstype_old = cstype_new;

    /* pcl_cspace_bnuild_indexed_cspace will release the old space */
    code = pcl_cs_indexed_build_cspace( pcs,
					&(ppalet->pindexed),
                                        pcid,
                                        fixed,
                                        gl2,
                                        pcs->memory
                                        );

    /* if a halftone exist, inform it of the update and discard lookup tables */
    if ((code == 0) && (ppalet->pht != 0)) {
        code = pcl_ht_update_cspace(pcs, &(ppalet->pht), cstype_old, cstype_new);
        if (code == 0)
            code = pcl_ht_set_lookup_tbl(&(ppalet->pht), NULL);
    }

    return code;
}

/*
 * Set the view illuminant for a palette.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_set_view_illuminant(
    pcl_state_t *       pcs,
    const gs_vector3 *  pwht_pt
)
{
    int                 code = unshare_palette(pcs);

    if ((code == 0) && (pcs->ppalet->pcrd == 0)) {
        if ((code = pcl_crd_build_default_crd(pcs)) == 0)
            pcl_crd_init_from(pcs->ppalet->pcrd, pcs->pcl_default_crd);
    }
    if (code == 0)
        code = pcl_crd_set_view_illuminant(pcs, &(pcs->ppalet->pcrd), pwht_pt);
    return code;
}

/*
 * Check that all parts of a PCL palette have been built. If not, build the
 * necessary default objects.
 *
 * This procedure should never be required, as the reset operation should build
 * a default palette, but it is kept in case that routine does not succeed.
 *
 * Returns 0 on success, < 0 in the event of an error.
 */
  int
pcl_palette_check_complete(
    pcl_state_t *   pcs
)
{
    pcl_palette_t * ppalet = pcs->ppalet;
    int             code = 0;

    if ( (ppalet != 0)           &&
         (ppalet->pindexed != 0) &&
         (ppalet->pcrd != 0)     &&
         (ppalet->pht != 0)        )
        return 0;

    if ((code = unshare_palette(pcs)) < 0)
        return code;
    ppalet = pcs->ppalet;
    if (ppalet->pindexed == 0)
        code = pcl_cs_indexed_build_default_cspace( pcs,
						    &(ppalet->pindexed),
                                                    pcs->memory
                                                    );
    if ((code == 0) && (ppalet->pcrd == 0)) {
        if ((code = pcl_crd_build_default_crd(pcs)) == 0)
            pcl_crd_init_from(pcs->ppalet->pcrd, pcs->pcl_default_crd);
    }
    if ((code == 0) && (ppalet->pht == 0))
        code = pcl_ht_build_default_ht(pcs, &(ppalet->pht), pcs->memory);
    return code;
}


/*
 * ESC & p # S
 *
 * Change the select palette id. Note that, since the pointer to the palette in
 * the pcl state does not count as a reference, no reference counts are adjusted.
 */
  private int
set_sel_palette_id(
    pcl_args_t *    pargs,
    pcl_state_t *   pcs
)
{
    uint            id = uint_arg(pargs);
    pcl_id_t        key;

    /* ignore attempts to select non-existent palettes */
    id_set_value(key, id);
    if ( pl_dict_lookup( &pcs->palette_store,
                         id_key(key),
                         2, 
                         (void **)&(pcs->ppalet),
                         false,
                         NULL
                         ) )
        pcs->sel_palette_id = id;
    return 0;
}

/*
 * ESC & p # I
 *
 * Set the palette control id.
 */
  private int
set_ctrl_palette_id(
    pcl_args_t *    pargs,
    pcl_state_t *   pcs
)
{
    pcs->ctrl_palette_id = uint_arg(pargs);
    return 0;
}

/*
 * Clear the palette store. This will delete the current palette, but will
 * NOT build the default palette in its place.
 *
 * If the current palette id. already has the default palette assigned, don't
 * bother removing it. This is helpful when working with memory leak detection
 * tools.
 */
  private void
clear_palette_store(
    pcl_state_t *   pcs
)
{
    pl_dict_enum_t  denum;
    void *          pvalue;
    gs_const_string plkey;
    int             sel_id = pcs->sel_palette_id;

    pl_dict_enum_begin(&pcs->palette_store, &denum);
    while (pl_dict_enum_next(&denum, &plkey, &pvalue)) {
        int     id = (((int)plkey.data[0]) << 8) + plkey.data[1];

        if (id == sel_id) {
	    if (pvalue != pcs->pdflt_palette)
                build_default_palette(pcs);     /* will redefine sel_id */
        } else
            pl_dict_undef(&pcs->palette_store, plkey.data, plkey.size);
    }
}

/*
 * ESC & p # C
 *
 * Palette control
 */
  private int
palette_control(
    pcl_args_t *    pargs,
    pcl_state_t *   pcs
)
{
    uint            action = uint_arg(pargs);

    switch (action) {

      case 0:
        clear_palette_store(pcs);
        break;

      case 1:
        clear_palette_stack(pcs, pcs->memory);
        break;

      case 2:
        if (pcs->ctrl_palette_id == pcs->sel_palette_id) {
            if ((pcs->ppalet == 0) || (pcs->ppalet != pcs->pdflt_palette))
                build_default_palette(pcs);
        } else {
            pcl_id_t  key;

            id_set_value(key, pcs->ctrl_palette_id);
            pl_dict_undef(&pcs->palette_store, id_key(key), 2);
        }
        break;

      case 6:
        if (pcs->ctrl_palette_id != pcs->sel_palette_id) {
            pcl_id_t        key;
            int             code = 0;

            /* NB: definitions don't incremente refernece counts */
            id_set_value(key, pcs->ctrl_palette_id);
            code = pl_dict_put(&pcs->palette_store, id_key(key), 2, pcs->ppalet);
            if (code < 0)
                return code;
            rc_increment(pcs->ppalet);
            
        }
        break;

      default:
        return e_Range;
    }

    return 0;
}

/*
 * ESC * t # J
 *
 * Set render method. The rendering method is specifically part of the PCL
 * halftone structure, but the setting command is included here because it
 * must run via the palette mechanism.
 */
  private int
set_render_algorithm(
    pcl_args_t *    pargs,
    pcl_state_t *   pcs
)
{
    return pcl_palette_set_render_method(pcs, uint_arg(pargs));
}

/*
 * ESC & b # M
 *
 * Set monochrome or normal print mode. Note that, for a change in the print
 * mode to take effect, the current rendering mode must be reset.
 *
 * NB: Unlike certain other commands, a monochrome print command always ejects
 *     the current page if it has been marked, even if no change in the print
 *     mode occurs.
 */
  private int
set_print_mode(
    pcl_args_t *    pargs,
    pcl_state_t *   pcs
)
{
    uint            mode = uint_arg(pargs);
    int             code = 0;

    if (mode > 1)
        return 0;

    if ((code = pcl_end_page_if_marked(pcs)) < 0)
        return code;
    pcl_home_cursor(pcs);

    pcs->monochrome_mode = (mode == 1);
    pcl_ht_set_print_mode(pcs, pcs->monochrome_mode);
    return pcl_palette_set_render_method(pcs, pcs->render_mode);
}

/*
 * Initialization routine for palettes.
 */
  private int
palette_do_registration(
    pcl_parser_state_t *pcl_parser_state,
    gs_memory_t *    pmem
)
{
    DEFINE_CLASS('*')
    {
        'p', 'P',
        PCL_COMMAND( "Push/Pop Palette",
                     push_pop_palette,
                     pca_neg_ok | pca_big_ignore | pca_in_rtl
                     )
    },
    {
        't', 'J',
        PCL_COMMAND( "Render Algorithm",
                     set_render_algorithm,
                     pca_neg_ok | pca_big_ignore | pca_in_rtl
                     )
    },
    END_CLASS

    DEFINE_CLASS('&')
    {
        'b', 'M',
        PCL_COMMAND( "Monochrome Printing",
                     set_print_mode,
                     pca_neg_ok | pca_big_ignore | pca_in_rtl
                     )
    },
    {
        'p', 'S',
        PCL_COMMAND( "Select Palette",
                      set_sel_palette_id,
                      pca_neg_ok | pca_big_ignore | pca_in_rtl
                      )
    },
    {
        'p', 'I',
        PCL_COMMAND( "Palette Control ID",
                     set_ctrl_palette_id,
                     pca_neg_ok | pca_big_ignore | pca_in_rtl
                     )
    },
    {
        'p', 'C',
        PCL_COMMAND( "Palette Control",
                     palette_control,
                     pca_neg_ok | pca_big_ignore | pca_in_rtl
                     )
    },
    END_CLASS
    return 0;
}

/*
 * Reset routine for palettes.
 */
  private void
palette_do_reset(
    pcl_state_t *       pcs,
    pcl_reset_type_t    type
)
{
    static const uint   mask = (   pcl_reset_initial
                                 | pcl_reset_cold
                                 | pcl_reset_printer
                                 | pcl_reset_overlay );

    if ((type & mask) == 0)
        return;

    /* for initial reset, set up the palette store */
    if ((type & pcl_reset_initial) != 0) {
        pl_dict_init(&pcs->palette_store, pcs->memory, dict_free_palette);
        pcs->ppalet = 0;
        pcs->pfrgrnd = 0;

        /* set up the built-in render methods and dithers matrices */
        pcl_ht_init_render_methods(pcs, pcs->memory);

        /* handle possible non-initialization of BSS */
        pcs->pdflt_palette = 0;
        pcs->palette_stack = 0;
        pcs->pcl_default_crd = 0;
        pcl_cs_base_init(pcs);
        pcl_cs_indexed_init(pcs);

    } else if ((type & (pcl_reset_cold | pcl_reset_printer)) != 0) {

        /* clear the palette stack and store */
        clear_palette_stack(pcs, pcs->memory);
        clear_palette_store(pcs);
    }

    /* select and control palette ID's must be set back to 0 */
    pcs->sel_palette_id = 0;
    pcs->ctrl_palette_id = 0;

    /* build the default palette and foreground */
    (void)build_default_palette(pcs);
    (void)pcl_frgrnd_set_default_foreground(pcs);

    /* the following is helpful when working with memory leak detectors */
    if ((type & pcl_reset_overlay) == 0)
        pcl_set_drawing_color(pcs, pcl_pattern_solid_frgrnd, 0, false);
}

/*
 * The copy function for palettes.
 *
 * This procedure implements the two most unusual features of the palette
 * object:
 *
 *     The palette pointer becomes a reference on the palette when the
 *     state is saved.
 *
 *     When the state is restored, the palette in the saved state is used
 *     to re-define the select palette id. in the palette store. Palettes are
 *     the only PCL resource object with this behavior.
 *
 * It is not clear HP intended palette to have this behavior. More likely,
 * it was a by-product of the way in which they implemented the save/restore
 * for foregrounds.
 *
 * Note that, in the restore case, the dictionary definition absorbs the
 * reference to the palette held by the saved state, so there is no need to
 * explicitly release this reference (an example of the asymmetric define/
 * undefine properties of the pl_dict_t object).
 */
  private int
palette_do_copy(
    pcl_state_t *           psaved,
    const pcl_state_t *     pcs,
    pcl_copy_operation_t    operation
)
{
    if ((operation & (pcl_copy_before_call | pcl_copy_before_overlay)) != 0)
        pcl_palette_init_from(psaved->ppalet, pcs->ppalet);
    else if ((operation & pcl_copy_after) != 0) {
	pcl_id_t    key;
	/* fix the compiler warning resulting from overuse of const */
	pcl_state_t *pcs2 = (pcl_state_t *)pcs;
        id_set_value(key, psaved->sel_palette_id);
        pl_dict_put(&pcs2->palette_store, id_key(key), 2, psaved->ppalet);
    }
    return 0;
}

const pcl_init_t    pcl_palette_init = {
    palette_do_registration, palette_do_reset, palette_do_copy
};
