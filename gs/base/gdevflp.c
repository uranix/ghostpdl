/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/

/* Device for first/last page handling device */
/* This device is the first 'subclassing' device; the intention of subclassing
 * is to allow us to develop a 'chain' or 'pipeline' of devices, each of which
 * can process some aspect of the graphics methods before passing them on to the
 * next device in the chain.
 *
 * This device's purpose is to implement the 'FirstPage' and 'LastPage' parameters
 * in Ghostscript. Initially only implemented in the PDF interpreter this functionality
 * has been shifted internally so that it can be implemented in all the interpreters.
 * The approach is pretty simple, we modify gdevprn.c and gdevvec.c so that if -dFirstPage
 * or -dLastPage is defined the device in question is subclassed and this device inserted.
 * This device then 'black hole's any graphics operations until we reach 'FirstPage'. We then
 * allow graphics to pass to the device until we reach the end of 'LastPage' at which time we
 * discard operations again until we reach the end, and close the device.
 */

/* This seems to be broadly similar to the 'device filter stack' which is defined in gsdfilt.c
 * and the stack for which is defined in the graphics state (dfilter_stack) but which seems to be
 * completely unused. We should probably remove dfilter_stack from the graphics state and remove
 * gsdfilt.c from the build.
 *
 * It would be nice if we could rewrite the clist handling to use this kind of device class chain
 * instead of the nasty hackery it currently utilises (stores the device procs for the existing
 * device in 'orig_procs' which is in the device structure) and overwrites the procs with its
 * own ones. The bbox forwarding device could also be rewritten this way and would probably then
 * be usable as a real forwarding device (last time I tried to do this for eps2write I was unable
 * to solve the problems with text enumerators).
 */

/* At first sight we should never have a method in a device structure which is NULL
 * because gx_device_fill_in_procs() should replace all the NULLs with default routines.
 * However, obselete routines, and a number of newer routines (especially those involving
 * transparency) don't get filled in. Its not obvious to me if this is deliberate or not,
 * but we'll be careful and check the subclassed device's method before trying to execute
 * it. Same for all the methods. NB the fill_rectangle method is deliberately not filled in
 * because that gets set up by gdev_prn_allocate_memory(). Isn't it great the way we do our
 * initialisation in lots of places?
 */

/* TODO make gx_device_fill_in_procs fill in *all* hte procs, currently it doesn't.
 * this will mean declaring gx_default_ methods for the transparency methods, possibly
 * some others. Like a number of other default methods, these cna simply return an error
 * which hopefuly will avoid us having to check for NULL device methods.
 * We also agreed to set the fill_rectangle method to a default as well (currently it explicitly
 * does not do this) and have gdev_prn_alloc_buffer check to see if the method is the default
 * before overwriting it, rather than the current check for NULL.
 */

/*
 * gsdparam.c line 272 checks for method being NULL, this is bad, we should check for a return error
 * or default method and do initialisation based on that.
 */
#include "math_.h"
#include "memory_.h"
#include "gx.h"
#include "gserrors.h"
#include "gsparam.h"
#include "gxdevice.h"
#include "gsdevice.h"		/* requires gsmatrix.h */
#include "gdevflp.h"
#include "gxdcolor.h"		/* for gx_device_black/white */
#include "gxiparam.h"		/* for image source size */
#include "gxistate.h"
#include "gxpaint.h"
#include "gxpath.h"
#include "gxcpath.h"
#include "gxcmap.h"         /* color mapping procs */
#include "gsstype.h"
#include "gdevprn.h"
#include "gdevflp.h"

/* GC descriptor */
public_st_device_flp();
/* we need text and image enumerators, because of the crazy way that text and images work */
private_st_flp_text_enum();

/* Device procedures, we need to implement all of them */
static dev_proc_open_device(flp_open_device);
static dev_proc_get_initial_matrix(flp_get_initial_matrix);
static dev_proc_sync_output(flp_sync_output);
static dev_proc_output_page(flp_output_page);
static dev_proc_close_device(flp_close_device);
static dev_proc_map_rgb_color(flp_map_rgb_color);
static dev_proc_map_color_rgb(flp_map_color_rgb);
static dev_proc_fill_rectangle(flp_fill_rectangle);
static dev_proc_tile_rectangle(flp_tile_rectangle);
static dev_proc_copy_mono(flp_copy_mono);
static dev_proc_copy_color(flp_copy_color);
static dev_proc_draw_line(flp_draw_line);
static dev_proc_get_bits(flp_get_bits);
static dev_proc_get_params(flp_get_params);
static dev_proc_put_params(flp_put_params);
static dev_proc_map_cmyk_color(flp_map_cmyk_color);
static dev_proc_get_xfont_procs(flp_get_xfont_procs);
static dev_proc_get_xfont_device(flp_get_xfont_device);
static dev_proc_map_rgb_alpha_color(flp_map_rgb_alpha_color);
static dev_proc_get_page_device(flp_get_page_device);
static dev_proc_get_alpha_bits(flp_get_alpha_bits);
static dev_proc_copy_alpha(flp_copy_alpha);
static dev_proc_get_band(flp_get_band);
static dev_proc_copy_rop(flp_copy_rop);
static dev_proc_fill_path(flp_fill_path);
static dev_proc_stroke_path(flp_stroke_path);
static dev_proc_fill_mask(flp_fill_mask);
static dev_proc_fill_trapezoid(flp_fill_trapezoid);
static dev_proc_fill_parallelogram(flp_fill_parallelogram);
static dev_proc_fill_triangle(flp_fill_triangle);
static dev_proc_draw_thin_line(flp_draw_thin_line);
static dev_proc_begin_image(flp_begin_image);
static dev_proc_image_data(flp_image_data);
static dev_proc_end_image(flp_end_image);
static dev_proc_strip_tile_rectangle(flp_strip_tile_rectangle);
static dev_proc_strip_copy_rop(flp_strip_copy_rop);
static dev_proc_get_clipping_box(flp_get_clipping_box);
static dev_proc_begin_typed_image(flp_begin_typed_image);
static dev_proc_get_bits_rectangle(flp_get_bits_rectangle);
static dev_proc_map_color_rgb_alpha(flp_map_color_rgb_alpha);
static dev_proc_create_compositor(flp_create_compositor);
static dev_proc_get_hardware_params(flp_get_hardware_params);
static dev_proc_text_begin(flp_text_begin);
static dev_proc_finish_copydevice(flp_finish_copydevice);
static dev_proc_begin_transparency_group(flp_begin_transparency_group);
static dev_proc_end_transparency_group(flp_end_transparency_group);
static dev_proc_begin_transparency_mask(flp_begin_transparency_mask);
static dev_proc_end_transparency_mask(flp_end_transparency_mask);
static dev_proc_discard_transparency_layer(flp_discard_transparency_layer);
static dev_proc_get_color_mapping_procs(flp_get_color_mapping_procs);
static dev_proc_get_color_comp_index(flp_get_color_comp_index);
static dev_proc_encode_color(flp_encode_color);
static dev_proc_decode_color(flp_decode_color);
static dev_proc_pattern_manage(flp_pattern_manage);
static dev_proc_fill_rectangle_hl_color(flp_fill_rectangle_hl_color);
static dev_proc_include_color_space(flp_include_color_space);
static dev_proc_fill_linear_color_scanline(flp_fill_linear_color_scanline);
static dev_proc_fill_linear_color_trapezoid(flp_fill_linear_color_trapezoid);
static dev_proc_fill_linear_color_triangle(flp_fill_linear_color_triangle);
static dev_proc_update_spot_equivalent_colors(flp_update_spot_equivalent_colors);
static dev_proc_ret_devn_params(flp_ret_devn_params);
static dev_proc_fillpage(flp_fillpage);
static dev_proc_push_transparency_state(flp_push_transparency_state);
static dev_proc_pop_transparency_state(flp_pop_transparency_state);
static dev_proc_put_image(flp_put_image);
static dev_proc_dev_spec_op(flp_dev_spec_op);
static dev_proc_copy_planes(flp_copy_planes);
static dev_proc_get_profile(flp_get_profile);
static dev_proc_set_graphics_type_tag(flp_set_graphics_type_tag);
static dev_proc_strip_copy_rop2(flp_strip_copy_rop2);
static dev_proc_strip_tile_rect_devn(flp_strip_tile_rect_devn);
static dev_proc_copy_alpha_hl_color(flp_copy_alpha_hl_color);
static dev_proc_process_page(flp_process_page);

static void
flp_finalize(gx_device *dev);

/* The device prototype */
#define MAX_COORD (max_int_in_fixed - 1000)
#define MAX_RESOLUTION 4000

#define public_st_flp_device()	/* in gsdevice.c */\
  gs_public_st_complex_only(st_flp_device, gx_device, "first_lastpage",\
    0, flp_enum_ptrs, flp_reloc_ptrs, gx_device_finalize)

static
ENUM_PTRS_WITH(flp_enum_ptrs, gx_device *dev)
{gx_device *dev = (gx_device *)vptr;
vptr = dev->child;
ENUM_PREFIX(*dev->child->stype, 2);}return 0;
case 0:ENUM_RETURN(gx_device_enum_ptr(dev->parent));
case 1:ENUM_RETURN(gx_device_enum_ptr(dev->child));
ENUM_PTRS_END
static RELOC_PTRS_WITH(flp_reloc_ptrs, gx_device *dev)
{
    dev->parent = gx_device_reloc_ptr(dev->parent, gcst);
    dev->child = gx_device_reloc_ptr(dev->child, gcst);
    vptr = dev->child;
    RELOC_PREFIX(*dev->child->stype);
}
RELOC_PTRS_END

public_st_flp_device();

const
gx_device_flp gs_flp_device =
{
    sizeof(gx_device_flp),  /* params _size */\
    0,                      /* obselete static_procs */\
    "first_lastpage",       /* dname */\
    0,                      /* memory */\
    &st_flp_device,              /* stype */\
    0,              /* is_dynamic (always false for a prototype, will be true for an allocated device instance) */\
    flp_finalize,   /* The reason why we can't use the std_device_dci_body macro, we want a finalize */\
    {0},              /* rc */\
    0,              /* retained */\
    0,              /* parent */\
    0,              /* child */\
    0,              /* subclass_data */\
    0,              /* is_open */\
    0,              /* max_fill_band */\
    dci_alpha_values(1, 8, 255, 0, 256, 1, 1, 1),\
    std_device_part2_(MAX_COORD, MAX_COORD, MAX_RESOLUTION, MAX_RESOLUTION),\
    offset_margin_values(0, 0, 0, 0, 0, 0),\
    std_device_part3_(),
#if 0
    /*
     * Define the device as 8-bit gray scale to avoid computing halftones.
     */
    std_device_dci_body(gx_device_flp, 0, "first_lastpage",
                        MAX_COORD, MAX_COORD,
                        MAX_RESOLUTION, MAX_RESOLUTION,
                        1, 8, 255, 0, 256, 1),
#endif
    {flp_open_device,
     flp_get_initial_matrix,
     flp_sync_output,			/* sync_output */
     flp_output_page,
     flp_close_device,
     flp_map_rgb_color,
     flp_map_color_rgb,
     flp_fill_rectangle,
     flp_tile_rectangle,			/* tile_rectangle */
     flp_copy_mono,
     flp_copy_color,
     flp_draw_line,			/* draw_line */
     flp_get_bits,			/* get_bits */
     flp_get_params,
     flp_put_params,
     flp_map_cmyk_color,
     flp_get_xfont_procs,			/* get_xfont_procs */
     flp_get_xfont_device,			/* get_xfont_device */
     flp_map_rgb_alpha_color,
     flp_get_page_device,
     flp_get_alpha_bits,			/* get_alpha_bits */
     flp_copy_alpha,
     flp_get_band,			/* get_band */
     flp_copy_rop,			/* copy_rop */
     flp_fill_path,
     flp_stroke_path,
     flp_fill_mask,
     flp_fill_trapezoid,
     flp_fill_parallelogram,
     flp_fill_triangle,
     flp_draw_thin_line,
     flp_begin_image,
     flp_image_data,			/* image_data */
     flp_end_image,			/* end_image */
     flp_strip_tile_rectangle,
     flp_strip_copy_rop,
     flp_get_clipping_box,			/* get_clipping_box */
     flp_begin_typed_image,
     flp_get_bits_rectangle,			/* get_bits_rectangle */
     flp_map_color_rgb_alpha,
     flp_create_compositor,
     flp_get_hardware_params,			/* get_hardware_params */
     flp_text_begin,
     flp_finish_copydevice,			/* finish_copydevice */
     flp_begin_transparency_group,			/* begin_transparency_group */
     flp_end_transparency_group,			/* end_transparency_group */
     flp_begin_transparency_mask,			/* begin_transparency_mask */
     flp_end_transparency_mask,			/* end_transparency_mask */
     flp_discard_transparency_layer,			/* discard_transparency_layer */
     flp_get_color_mapping_procs,			/* get_color_mapping_procs */
     flp_get_color_comp_index,			/* get_color_comp_index */
     flp_encode_color,			/* encode_color */
     flp_decode_color,			/* decode_color */
     flp_pattern_manage,			/* pattern_manage */
     flp_fill_rectangle_hl_color,			/* fill_rectangle_hl_color */
     flp_include_color_space,			/* include_color_space */
     flp_fill_linear_color_scanline,			/* fill_linear_color_scanline */
     flp_fill_linear_color_trapezoid,			/* fill_linear_color_trapezoid */
     flp_fill_linear_color_triangle,			/* fill_linear_color_triangle */
     flp_update_spot_equivalent_colors,			/* update_spot_equivalent_colors */
     flp_ret_devn_params,			/* ret_devn_params */
     flp_fillpage,		/* fillpage */
     flp_push_transparency_state,                      /* push_transparency_state */
     flp_pop_transparency_state,                      /* pop_transparency_state */
     flp_put_image,                      /* put_image */
     flp_dev_spec_op,                      /* dev_spec_op */
     flp_copy_planes,                      /* copy_planes */
     flp_get_profile,                      /* get_profile */
     flp_set_graphics_type_tag,                      /* set_graphics_type_tag */
     flp_strip_copy_rop2,
     flp_strip_tile_rect_devn,
     flp_copy_alpha_hl_color,
     flp_process_page
    }
};

#undef MAX_COORD
#undef MAX_RESOLUTION

static int copy_procs(gx_device_procs *dest_procs, gx_device_procs *src_procs, gx_device_procs *prototype_procs)
{
    if (src_procs->open_device != NULL)
        dest_procs->open_device = prototype_procs->open_device;
    if (src_procs->get_initial_matrix != NULL)
        dest_procs->get_initial_matrix = prototype_procs->get_initial_matrix;
    if (src_procs->sync_output != NULL)
        dest_procs->sync_output = prototype_procs->sync_output;
    if (src_procs->output_page != NULL)
        dest_procs->output_page = prototype_procs->output_page;
    if (src_procs->close_device != NULL)
        dest_procs->close_device = prototype_procs->close_device;
    if (src_procs->map_rgb_color != NULL)
        dest_procs->map_rgb_color = prototype_procs->map_rgb_color;
    if (src_procs->map_color_rgb != NULL)
        dest_procs->map_color_rgb = prototype_procs->map_color_rgb;
    if (src_procs->fill_rectangle != NULL)
        dest_procs->fill_rectangle = prototype_procs->fill_rectangle;
    if (src_procs->tile_rectangle != NULL)
        dest_procs->tile_rectangle = prototype_procs->tile_rectangle;
    if (src_procs->copy_mono != NULL)
        dest_procs->copy_mono = prototype_procs->copy_mono;
    if (src_procs->copy_color != NULL)
        dest_procs->copy_color = prototype_procs->copy_color;
    if (src_procs->obsolete_draw_line != NULL)
        dest_procs->obsolete_draw_line = prototype_procs->obsolete_draw_line;
    if (src_procs->get_bits != NULL)
        dest_procs->get_bits = prototype_procs->get_bits;
    if (src_procs->get_params != NULL)
        dest_procs->get_params = prototype_procs->get_params;
    if (src_procs->put_params != NULL)
        dest_procs->put_params = prototype_procs->put_params;
    if (src_procs->map_cmyk_color != NULL)
        dest_procs->map_cmyk_color = prototype_procs->map_cmyk_color;
    if (src_procs->get_xfont_procs != NULL)
        dest_procs->get_xfont_procs = prototype_procs->get_xfont_procs;
    if (src_procs->get_xfont_device != NULL)
        dest_procs->get_xfont_device = prototype_procs->get_xfont_device;
    if (src_procs->map_rgb_alpha_color != NULL)
        dest_procs->map_rgb_alpha_color = prototype_procs->map_rgb_alpha_color;
    if (src_procs->get_alpha_bits != NULL)
        dest_procs->get_alpha_bits = prototype_procs->get_alpha_bits;
    if (src_procs->copy_alpha != NULL)
        dest_procs->copy_alpha = prototype_procs->copy_alpha;
    if (src_procs->get_band != NULL)
        dest_procs->get_band = prototype_procs->get_band;
    if (src_procs->copy_rop != NULL)
        dest_procs->copy_rop = prototype_procs->copy_rop;
    if (src_procs->fill_path != NULL)
        dest_procs->fill_path = prototype_procs->fill_path;
    if (src_procs->stroke_path != NULL)
        dest_procs->stroke_path = prototype_procs->stroke_path;
    if (src_procs->fill_mask != NULL)
        dest_procs->fill_mask = prototype_procs->fill_mask;
    if (src_procs->fill_trapezoid != NULL)
        dest_procs->fill_trapezoid = prototype_procs->fill_trapezoid;
    if (src_procs->fill_parallelogram != NULL)
        dest_procs->fill_parallelogram = prototype_procs->fill_parallelogram;
    if (src_procs->fill_triangle != NULL)
        dest_procs->fill_triangle = prototype_procs->fill_triangle;
    if (src_procs->draw_thin_line != NULL)
        dest_procs->draw_thin_line = prototype_procs->draw_thin_line;
    if (src_procs->begin_image != NULL)
        dest_procs->begin_image = prototype_procs->begin_image;
    if (src_procs->image_data != NULL)
        dest_procs->image_data = prototype_procs->image_data;
    if (src_procs->end_image != NULL)
        dest_procs->end_image = prototype_procs->end_image;
    if (src_procs->strip_tile_rectangle != NULL)
        dest_procs->strip_tile_rectangle = prototype_procs->strip_tile_rectangle;
    if (src_procs->strip_copy_rop != NULL)
        dest_procs->strip_copy_rop = prototype_procs->strip_copy_rop;
    if (src_procs->get_clipping_box != NULL)
        dest_procs->get_clipping_box = prototype_procs->get_clipping_box;
    if (src_procs->begin_typed_image != NULL)
        dest_procs->begin_typed_image = prototype_procs->begin_typed_image;
    if (src_procs->get_bits_rectangle != NULL)
        dest_procs->get_bits_rectangle = prototype_procs->get_bits_rectangle;
    if (src_procs->map_color_rgb_alpha != NULL)
        dest_procs->map_color_rgb_alpha = prototype_procs->map_color_rgb_alpha;
    if (src_procs->create_compositor != NULL)
        dest_procs->create_compositor = prototype_procs->create_compositor;
    if (src_procs->get_hardware_params != NULL)
        dest_procs->get_hardware_params = prototype_procs->get_hardware_params;
    if (src_procs->text_begin != NULL)
        dest_procs->text_begin = prototype_procs->text_begin;
    if (src_procs->finish_copydevice != NULL)
        dest_procs->finish_copydevice = prototype_procs->finish_copydevice;
    if (src_procs->begin_transparency_group != NULL)
        dest_procs->begin_transparency_group = prototype_procs->begin_transparency_group;
    if (src_procs->end_transparency_group != NULL)
        dest_procs->end_transparency_group = prototype_procs->end_transparency_group;
    if (src_procs->discard_transparency_layer != NULL)
        dest_procs->discard_transparency_layer = prototype_procs->discard_transparency_layer;
    if (src_procs->get_color_mapping_procs != NULL)
        dest_procs->get_color_mapping_procs = prototype_procs->get_color_mapping_procs;
    if (src_procs->get_color_comp_index != NULL)
        dest_procs->get_color_comp_index = prototype_procs->get_color_comp_index;
    if (src_procs->encode_color != NULL)
        dest_procs->encode_color = prototype_procs->encode_color;
    if (src_procs->decode_color != NULL)
        dest_procs->decode_color = prototype_procs->decode_color;
    if (src_procs->pattern_manage != NULL)
        dest_procs->pattern_manage = prototype_procs->pattern_manage;
    if (src_procs->fill_rectangle_hl_color != NULL)
        dest_procs->fill_rectangle_hl_color = prototype_procs->fill_rectangle_hl_color;
    if (src_procs->include_color_space != NULL)
        dest_procs->include_color_space = prototype_procs->include_color_space;
    if (src_procs->fill_linear_color_scanline != NULL)
        dest_procs->fill_linear_color_scanline = prototype_procs->fill_linear_color_scanline;
    if (src_procs->fill_linear_color_trapezoid != NULL)
        dest_procs->fill_linear_color_trapezoid = prototype_procs->fill_linear_color_trapezoid;
    if (src_procs->fill_linear_color_triangle != NULL)
        dest_procs->fill_linear_color_triangle = prototype_procs->fill_linear_color_triangle;
    if (src_procs->update_spot_equivalent_colors != NULL)
        dest_procs->update_spot_equivalent_colors = prototype_procs->update_spot_equivalent_colors;
    if (src_procs->ret_devn_params != NULL)
        dest_procs->ret_devn_params = prototype_procs->ret_devn_params;
    if (src_procs->fillpage != NULL)
        dest_procs->fillpage = prototype_procs->fillpage;
    if (src_procs->push_transparency_state != NULL)
        dest_procs->push_transparency_state = prototype_procs->push_transparency_state;
    if (src_procs->pop_transparency_state != NULL)
        dest_procs->pop_transparency_state = prototype_procs->pop_transparency_state;
    if (src_procs->put_image != NULL)
        dest_procs->put_image = prototype_procs->put_image;
    if (src_procs->dev_spec_op != NULL)
        dest_procs->dev_spec_op = prototype_procs->dev_spec_op;
    if (src_procs->copy_planes != NULL)
        dest_procs->copy_planes = prototype_procs->copy_planes;
    if (src_procs->get_profile != NULL)
        dest_procs->get_profile = prototype_procs->get_profile;
    if (src_procs->set_graphics_type_tag != NULL)
        dest_procs->set_graphics_type_tag = prototype_procs->set_graphics_type_tag;
    if (src_procs->strip_copy_rop2 != NULL)
        dest_procs->strip_copy_rop2 = prototype_procs->strip_copy_rop2;
    if (src_procs->strip_tile_rect_devn != NULL)
        dest_procs->strip_tile_rect_devn = prototype_procs->strip_tile_rect_devn;
    if (src_procs->copy_alpha_hl_color != NULL)
        dest_procs->copy_alpha_hl_color = prototype_procs->copy_alpha_hl_color;
    if (src_procs->process_page != NULL)
        dest_procs->process_page = prototype_procs->process_page;
    return 0;
}

int gx_device_subclass(gx_device *dev_to_subclass, gx_device *new_prototype, unsigned int private_data_size)
{
    gx_device *child_dev;
    void *psubclass_data;
    gs_memory_struct_type_t **b_std;
    unsigned char *ptr;

    if (!dev_to_subclass->stype)
        return_error(gs_error_VMerror);

    child_dev = (gx_device *)gs_alloc_bytes(dev_to_subclass->memory->stable_memory, dev_to_subclass->stype->ssize, "gx_device_subclass(device)");
    if (child_dev == 0)
        return_error(gs_error_VMerror);

    /* Make sure all methods are filled in, note this won't work for a forwarding device
     * so forwarding devices will have to be filled in before being subclassed. This doesn't fill
     * in the fill_rectangle proc, that gets done in the ultimate device's open proc.
     */
    gx_device_fill_in_procs(dev_to_subclass);
    memcpy(child_dev, dev_to_subclass, dev_to_subclass->stype->ssize);

    psubclass_data = (void *)gs_alloc_bytes(dev_to_subclass->memory->non_gc_memory, private_data_size, "subclass memory for subclassing device");
    if (psubclass_data == 0){
        gs_free(dev_to_subclass->memory, child_dev, 1, dev_to_subclass->stype->ssize, "free subclass memory for subclassing device");
        return_error(gs_error_VMerror);
    }
    memset(psubclass_data, 0x00, private_data_size);

    copy_procs(&dev_to_subclass->procs, &child_dev->procs, &new_prototype->procs);
    dev_to_subclass->procs.fill_rectangle = new_prototype->procs.fill_rectangle;
    dev_to_subclass->finalize = new_prototype->finalize;
    dev_to_subclass->dname = new_prototype->dname;
    dev_to_subclass->stype = new_prototype->stype;

    /* Nasty, nasty hackery. The 'object' (in this case, struct) which wraps
     * the actual body also maintains a pointer to the 'stype' and the garbage
     * collector uses *that* to enumerate pointers and so on, not the one in the
     * actual structure (which begs the qustion of what use the one in the structure
     * is, actually ?). So here we patch the pointer held in the wrapper. We can't
     * simply alter the contents of 'stype' as you might expect, because if its not
     * dynamically allocated we'll be trying to alter the executable, and that's
     * the sort of thing likely to trigger faults these days.
     * And the answer to the question above is that the wrapper only exists in the GC.
     * When we aren't using the GC, then we don't get the wrapper. Obviously in that
     * case we don't care, because we don't need the entries in the stype, so
     * still left wondering what the copy in the structure is for. Anyway, we can't
     * do this patching unless the object has been allocated in GC memory, which
     * is a problem, how can we possibly tell ?
     */
    ptr = (unsigned char *)dev_to_subclass;
    ptr -= 2 * sizeof(void *);
    b_std = (gs_memory_struct_type_t **)ptr;
    *b_std = (gs_memory_struct_type_t *)new_prototype->stype;

    dev_to_subclass->subclass_data = psubclass_data;
    dev_to_subclass->child = child_dev;
    if (child_dev->parent) {
        dev_to_subclass->parent = child_dev->parent;
        child_dev->parent->child = dev_to_subclass;
    }
    child_dev->parent = dev_to_subclass;

    return 0;
}

int gx_unsubclass_device(gx_device *dev)
{
    void *psubclass_data = dev->subclass_data;
    gx_device *parent = dev->parent, *child = dev->child;
    unsigned char *ptr;
    gs_memory_struct_type_t **b_std;

    memcpy(dev, child, child->stype->ssize);
    if (psubclass_data)
        gs_free_object(dev->memory->non_gc_memory, psubclass_data, "subclass memory for first-last page");
    if (child)
        gs_free_object(dev->memory->stable_memory, child, "gx_device_subclass(device)");
    dev->parent = parent;

    ptr = (unsigned char *)dev;
    ptr -= 2 * sizeof(void *);
    b_std = (gs_memory_struct_type_t **)ptr;
    *b_std = (gs_memory_struct_type_t *)dev->stype;

    return 0;
}

int gx_update_from_subclass(gx_device *dev)
{
    if (!dev->child)
        return 0;

    memcpy(&dev->color_info, &dev->child->color_info, sizeof(gx_device_color_info));
    memcpy(&dev->cached_colors, &dev->child->cached_colors, sizeof(gx_device_cached_colors_t));
    dev->max_fill_band = dev->child->max_fill_band;
    dev->width = dev->child->width;
    dev->height = dev->child->height;
    dev->pad = dev->child->pad;
    dev->log2_align_mod = dev->child->log2_align_mod;
    dev->max_fill_band = dev->child->max_fill_band;
    dev->is_planar = dev->child->is_planar;
    dev->LeadingEdge = dev->child->LeadingEdge;
    memcpy(&dev->ImagingBBox, &dev->child->ImagingBBox, 2 * sizeof(float));
    dev->ImagingBBox_set = dev->child->ImagingBBox_set;
    memcpy(&dev->MediaSize, &dev->child->MediaSize, 2 * sizeof(float));
    memcpy(&dev->HWResolution, &dev->child->HWResolution, 2 * sizeof(float));
    memcpy(&dev->Margins, &dev->child->Margins, 2 * sizeof(float));
    memcpy(&dev->HWMargins, &dev->child->HWMargins, 4 * sizeof(float));
    dev->FirstPage = dev->child->FirstPage;
    dev->LastPage = dev->child->LastPage;
    dev->PageCount = dev->child->PageCount;
    dev->ShowpageCount = dev->child->ShowpageCount;
    dev->NumCopies = dev->child->NumCopies;
    dev->NumCopies_set = dev->child->NumCopies_set;
    dev->IgnoreNumCopies = dev->child->IgnoreNumCopies;
    dev->UseCIEColor = dev->child->UseCIEColor;
    dev->LockSafetyParams= dev->child->LockSafetyParams;
    dev->band_offset_x = dev->child->band_offset_y;
    dev->sgr = dev->child->sgr;
    dev->MaxPatternBitmap = dev->child->MaxPatternBitmap;
    dev->page_uses_transparency = dev->child->page_uses_transparency;
    memcpy(&dev->space_params, &dev->child->space_params, sizeof(gdev_space_params));
    dev->icc_struct = dev->child->icc_struct;
    dev->graphics_type_tag = dev->child->graphics_type_tag;
}

static
void flp_finalize(gx_device *dev) {

    gx_unsubclass_device(dev);

    if (dev->finalize)
        dev->finalize(dev);
    return;
}

/* For printing devices the 'open' routine in gdevprn calls gdevprn_allocate_memory
 * which is responsible for creating the page buffer. This *also* fills in some of
 * the device procs, in particular fill_rectangle() so its vitally important that
 * we pass this on.
 */
int flp_open_device(gx_device *dev)
{
    if (dev->child->procs.open_device) {
        dev->child->procs.open_device(dev->child);
        dev->child->is_open = true;
        gx_update_from_subclass(dev);
    }

    return 0;
}

void flp_get_initial_matrix(gx_device *dev, gs_matrix *pmat)
{
    if (dev->child->procs.get_initial_matrix)
        dev->child->procs.get_initial_matrix(dev->child, pmat);
    else
        gx_default_get_initial_matrix(dev, pmat);
    return;
}

int flp_sync_output(gx_device *dev)
{
    if (dev->child->procs.sync_output)
        return dev->child->procs.sync_output(dev->child);
    else
        gx_default_sync_output(dev);

    return 0;
}

int flp_output_page(gx_device *dev, int num_copies, int flush)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.output_page)
                return dev->child->procs.output_page(dev->child, num_copies, flush);
        }
    }
    psubclass_data->PageCount++;

    return 0;
}

int flp_close_device(gx_device *dev)
{
    int code;

    if (dev->child->procs.close_device) {
        code = dev->child->procs.close_device(dev->child);
        dev->is_open = dev->child->is_open = false;
        return code;
    }
    dev->is_open = false;
    return 0;
}

gx_color_index flp_map_rgb_color(gx_device *dev, const gx_color_value cv[])
{
    if (dev->child->procs.map_rgb_color)
        return dev->child->procs.map_rgb_color(dev->child, cv);
    else
        gx_error_encode_color(dev, cv);
    return 0;
}

int flp_map_color_rgb(gx_device *dev, gx_color_index color, gx_color_value rgb[3])
{
    if (dev->child->procs.map_color_rgb)
        return dev->child->procs.map_color_rgb(dev->child, color, rgb);
    else
        gx_default_map_color_rgb(dev, color, rgb);

    return 0;
}

int flp_fill_rectangle(gx_device *dev, int x, int y, int width, int height, gx_color_index color)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_rectangle)
                return dev->child->procs.fill_rectangle(dev->child, x, y, width, height, color);
        }
    }

    return 0;
}

int flp_tile_rectangle(gx_device *dev, const gx_tile_bitmap *tile, int x, int y, int width, int height,
    gx_color_index color0, gx_color_index color1,
    int phase_x, int phase_y)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.tile_rectangle)
                return dev->child->procs.tile_rectangle(dev->child, tile, x, y, width, height, color0, color1, phase_x, phase_y);
        }
    }

    return 0;
}

int flp_copy_mono(gx_device *dev, const byte *data, int data_x, int raster, gx_bitmap_id id,
    int x, int y, int width, int height,
    gx_color_index color0, gx_color_index color1)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.copy_mono)
                return dev->child->procs.copy_mono(dev->child, data, data_x, raster, id, x, y, width, height, color0, color1);
        }
    }

    return 0;
}

int flp_copy_color(gx_device *dev, const byte *data, int data_x, int raster, gx_bitmap_id id,\
    int x, int y, int width, int height)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.copy_color)
                return dev->child->procs.copy_color(dev->child, data, data_x, raster, id, x, y, width, height);
        }
    }

    return 0;
}

int flp_draw_line(gx_device *dev, int x0, int y0, int x1, int y1, gx_color_index color)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.obsolete_draw_line)
                return dev->child->procs.obsolete_draw_line(dev->child, x0, y0, x1, y1, color);
        }
    }
    return 0;
}

int flp_get_bits(gx_device *dev, int y, byte *data, byte **actual_data)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.get_bits)
                return dev->child->procs.get_bits(dev->child, y, data, actual_data);
            else
                return gx_default_get_bits(dev, y, data, actual_data);
        }
        else
            return gx_default_get_bits(dev, y, data, actual_data);
    }
    else
        return gx_default_get_bits(dev, y, data, actual_data);

    return 0;
}

int flp_get_params(gx_device *dev, gs_param_list *plist)
{
    if (dev->child->procs.get_params)
        return dev->child->procs.get_params(dev->child, plist);
    else
        return gx_default_get_params(dev, plist);

    return 0;
}

int flp_put_params(gx_device *dev, gs_param_list *plist)
{
    int code;

    if (dev->child->procs.put_params) {
        code = dev->child->procs.put_params(dev->child, plist);
        /* The child device might have closed itself (yes seriously, this can happen!) */
        dev->is_open = dev->child->is_open;
        gx_update_from_subclass(dev);
        return code;
    }
    else
        return gx_default_put_params(dev, plist);

    return 0;
}

gx_color_index flp_map_cmyk_color(gx_device *dev, const gx_color_value cv[])
{
    if (dev->child->procs.map_cmyk_color)
        return dev->child->procs.map_cmyk_color(dev->child, cv);
    else
        return gx_default_map_cmyk_color(dev, cv);

    return 0;
}

const gx_xfont_procs *flp_get_xfont_procs(gx_device *dev)
{
    if (dev->child->procs.get_xfont_procs)
        return dev->child->procs.get_xfont_procs(dev->child);
    else
        return gx_default_get_xfont_procs(dev);

    return 0;
}

gx_device *flp_get_xfont_device(gx_device *dev)
{
    if (dev->child->procs.get_xfont_device)
        return dev->child->procs.get_xfont_device(dev->child);
    else
        return gx_default_get_xfont_device(dev);

    return 0;
}

gx_color_index flp_map_rgb_alpha_color(gx_device *dev, gx_color_value red, gx_color_value green, gx_color_value blue,
    gx_color_value alpha)
{
    if (dev->child->procs.map_rgb_alpha_color)
        return dev->child->procs.map_rgb_alpha_color(dev->child, red, green, blue, alpha);
    else
        return gx_default_map_rgb_alpha_color(dev->child, red, green, blue, alpha);

    return 0;
}

gx_device *flp_get_page_device(gx_device *dev)
{
    if (dev->child->procs.get_page_device)
        return dev->child->procs.get_page_device(dev->child);
    else
        return gx_default_get_page_device(dev);

    return 0;
}

int flp_get_alpha_bits(gx_device *dev, graphics_object_type type)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.get_alpha_bits)
                return dev->child->procs.get_alpha_bits(dev->child, type);
        }
    }

    return 0;
}

int flp_copy_alpha(gx_device *dev, const byte *data, int data_x,
    int raster, gx_bitmap_id id, int x, int y, int width, int height,
    gx_color_index color, int depth)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.copy_alpha)
                return dev->child->procs.copy_alpha(dev->child, data, data_x, raster, id, x, y, width, height, color, depth);
        }
    }

    return 0;
}

int flp_get_band(gx_device *dev, int y, int *band_start)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.get_band)
                return dev->child->procs.get_band(dev->child, y, band_start);
            else
                return gx_default_get_band(dev, y, band_start);
        }
        else
            return gx_default_get_band(dev, y, band_start);
    }
    else
        return gx_default_get_band(dev, y, band_start);

    return 0;
}

int flp_copy_rop(gx_device *dev, const byte *sdata, int sourcex, uint sraster, gx_bitmap_id id,
    const gx_color_index *scolors,
    const gx_tile_bitmap *texture, const gx_color_index *tcolors,
    int x, int y, int width, int height,
    int phase_x, int phase_y, gs_logical_operation_t lop)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.copy_rop)
                return dev->child->procs.copy_rop(dev->child, sdata, sourcex, sraster, id, scolors, texture, tcolors, x, y, width, height, phase_x, phase_y, lop);
            else
                return gx_default_copy_rop(dev->child, sdata, sourcex, sraster, id, scolors, texture, tcolors, x, y, width, height, phase_x, phase_y, lop);
        } else
            return gx_default_copy_rop(dev->child, sdata, sourcex, sraster, id, scolors, texture, tcolors, x, y, width, height, phase_x, phase_y, lop);
    }
    else
        return gx_default_copy_rop(dev, sdata, sourcex, sraster, id, scolors, texture, tcolors, x, y, width, height, phase_x, phase_y, lop);

    return 0;
}

int flp_fill_path(gx_device *dev, const gs_imager_state *pis, gx_path *ppath,
    const gx_fill_params *params,
    const gx_drawing_color *pdcolor, const gx_clip_path *pcpath)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_path)
                return dev->child->procs.fill_path(dev->child, pis, ppath, params, pdcolor, pcpath);
            else
                return gx_default_fill_path(dev->child, pis, ppath, params, pdcolor, pcpath);
        }
    }

    return 0;
}

int flp_stroke_path(gx_device *dev, const gs_imager_state *pis, gx_path *ppath,
    const gx_stroke_params *params,
    const gx_drawing_color *pdcolor, const gx_clip_path *pcpath)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.stroke_path)
                return dev->child->procs.stroke_path(dev->child, pis, ppath, params, pdcolor, pcpath);
            else
                return gx_default_stroke_path(dev->child, pis, ppath, params, pdcolor, pcpath);
        }
    }

    return 0;
}

int flp_fill_mask(gx_device *dev, const byte *data, int data_x, int raster, gx_bitmap_id id,
    int x, int y, int width, int height,
    const gx_drawing_color *pdcolor, int depth,
    gs_logical_operation_t lop, const gx_clip_path *pcpath)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_mask)
                return dev->child->procs.fill_mask(dev->child, data, data_x, raster, id, x, y, width, height, pdcolor, depth, lop, pcpath);
            else
                return gx_default_fill_mask(dev->child, data, data_x, raster, id, x, y, width, height, pdcolor, depth, lop, pcpath);
        }
    }

    return 0;
}

int flp_fill_trapezoid(gx_device *dev, const gs_fixed_edge *left, const gs_fixed_edge *right,
    fixed ybot, fixed ytop, bool swap_axes,
    const gx_drawing_color *pdcolor, gs_logical_operation_t lop)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_trapezoid)
                return dev->child->procs.fill_trapezoid(dev->child, left, right, ybot, ytop, swap_axes, pdcolor, lop);
            else
                return gx_default_fill_trapezoid(dev->child, left, right, ybot, ytop, swap_axes, pdcolor, lop);
        }
    }

    return 0;
}

int flp_fill_parallelogram(gx_device *dev, fixed px, fixed py, fixed ax, fixed ay, fixed bx, fixed by,
    const gx_drawing_color *pdcolor, gs_logical_operation_t lop)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_parallelogram)
                return dev->child->procs.fill_parallelogram(dev->child, px, py, ax, ay, bx, by, pdcolor, lop);
            else
                return gx_default_fill_parallelogram(dev->child, px, py, ax, ay, bx, by, pdcolor, lop);
        }
    }

    return 0;
}

int flp_fill_triangle(gx_device *dev, fixed px, fixed py, fixed ax, fixed ay, fixed bx, fixed by,
    const gx_drawing_color *pdcolor, gs_logical_operation_t lop)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_triangle)
                return dev->child->procs.fill_triangle(dev->child, px, py, ax, ay, bx, by, pdcolor, lop);
            else
                return gx_default_fill_triangle(dev->child, px, py, ax, ay, bx, by, pdcolor, lop);
        }
    }

    return 0;
}

int flp_draw_thin_line(gx_device *dev, fixed fx0, fixed fy0, fixed fx1, fixed fy1,
    const gx_drawing_color *pdcolor, gs_logical_operation_t lop,
    fixed adjustx, fixed adjusty)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.draw_thin_line)
                return dev->child->procs.draw_thin_line(dev->child, fx0, fy0, fx1, fy1, pdcolor, lop, adjustx, adjusty);
            else
                return gx_default_draw_thin_line(dev->child, fx0, fy0, fx1, fy1, pdcolor, lop, adjustx, adjusty);
        }
    }

    return 0;
}

int flp_begin_image(gx_device *dev, const gs_imager_state *pis, const gs_image_t *pim,
    gs_image_format_t format, const gs_int_rect *prect,
    const gx_drawing_color *pdcolor, const gx_clip_path *pcpath,
    gs_memory_t *memory, gx_image_enum_common_t **pinfo)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.begin_image)
                return dev->child->procs.begin_image(dev->child, pis, pim, format, prect, pdcolor, pcpath, memory, pinfo);
            else
                return gx_default_begin_image(dev->child, pis, pim, format, prect, pdcolor, pcpath, memory, pinfo);
        }
    }

    return 0;
}

int flp_image_data(gx_device *dev, gx_image_enum_common_t *info, const byte **planes, int data_x,
    uint raster, int height)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.image_data)
                return dev->child->procs.image_data(dev->child, info, planes, data_x, raster, height);
        }
    }

    return 0;
}

int flp_end_image(gx_device *dev, gx_image_enum_common_t *info, bool draw_last)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.end_image)
                return dev->child->procs.end_image(dev->child, info, draw_last);
        }
    }

    return 0;
}

int flp_strip_tile_rectangle(gx_device *dev, const gx_strip_bitmap *tiles, int x, int y, int width, int height,
    gx_color_index color0, gx_color_index color1,
    int phase_x, int phase_y)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.strip_tile_rectangle)
                return dev->child->procs.strip_tile_rectangle(dev->child, tiles, x, y, width, height, color0, color1, phase_x, phase_y);
            else
                return gx_default_strip_tile_rectangle(dev->child, tiles, x, y, width, height, color0, color1, phase_x, phase_y);
        }
    }

    return 0;
}

int flp_strip_copy_rop(gx_device *dev, const byte *sdata, int sourcex, uint sraster, gx_bitmap_id id,
    const gx_color_index *scolors,
    const gx_strip_bitmap *textures, const gx_color_index *tcolors,
    int x, int y, int width, int height,
    int phase_x, int phase_y, gs_logical_operation_t lop)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.strip_copy_rop)
                return dev->child->procs.strip_copy_rop(dev->child, sdata, sourcex, sraster, id, scolors, textures, tcolors, x, y, width, height, phase_x, phase_y, lop);
            else
                return gx_default_strip_copy_rop(dev->child, sdata, sourcex, sraster, id, scolors, textures, tcolors, x, y, width, height, phase_x, phase_y, lop);
        }
    }

    return 0;
}

void flp_get_clipping_box(gx_device *dev, gs_fixed_rect *pbox)
{
    if (dev->child->procs.get_clipping_box)
        dev->child->procs.get_clipping_box(dev->child, pbox);
    else
        gx_default_get_clipping_box(dev->child, pbox);

    return;
}

typedef struct flp_image_enum_s {
    gx_image_enum_common;
} flp_image_enum;
gs_private_st_composite(st_flp_image_enum, flp_image_enum, "flp_image_enum",
  flp_image_enum_enum_ptrs, flp_image_enum_reloc_ptrs);

static ENUM_PTRS_WITH(flp_image_enum_enum_ptrs, flp_image_enum *pie)
    return ENUM_USING_PREFIX(st_gx_image_enum_common, 0);
ENUM_PTRS_END
static RELOC_PTRS_WITH(flp_image_enum_reloc_ptrs, flp_image_enum *pie)
{
}
RELOC_PTRS_END

static int
flp_image_plane_data(gx_image_enum_common_t * info,
                     const gx_image_plane_t * planes, int height,
                     int *rows_used)
{
    return 0;
}

static int
flp_image_end_image(gx_image_enum_common_t * info, bool draw_last)
{
    return 0;
}

static const gx_image_enum_procs_t flp_image_enum_procs = {
    flp_image_plane_data,
    flp_image_end_image
};

int flp_begin_typed_image(gx_device *dev, const gs_imager_state *pis, const gs_matrix *pmat,
    const gs_image_common_t *pic, const gs_int_rect *prect,
    const gx_drawing_color *pdcolor, const gx_clip_path *pcpath,
    gs_memory_t *memory, gx_image_enum_common_t **pinfo)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.begin_typed_image)
                return dev->child->procs.begin_typed_image(dev->child, pis, pmat, pic, prect, pdcolor, pcpath, memory, pinfo);
            else
                return gx_default_begin_typed_image(dev->child, pis, pmat, pic, prect, pdcolor, pcpath, memory, pinfo);
        } else
            return gx_default_begin_typed_image(dev->child, pis, pmat, pic, prect, pdcolor, pcpath, memory, pinfo);
    }
    else {
        flp_image_enum *pie;
        const gs_pixel_image_t *pim = (const gs_pixel_image_t *)pic;
        int num_components = gs_color_space_num_components(pim->ColorSpace);

        if (pic->type->index == 1) {
            const gs_image_t *pim1 = (const gs_image_t *)pic;

            if (pim1->ImageMask)
                num_components = 1;
        }

        pie = gs_alloc_struct(memory, flp_image_enum, &st_flp_image_enum,
                            "flp_begin_image");
        if (pie == 0)
            return_error(gs_error_VMerror);
        memset(pie, 0, sizeof(*pie)); /* cleanup entirely for GC to work in all cases. */
        *pinfo = (gx_image_enum_common_t *) pie;
        gx_image_enum_common_init(*pinfo, (const gs_data_image_t *) pim, &flp_image_enum_procs,
                            (gx_device *)dev, num_components, pim->format);
        pie->memory = memory;
    }

    return 0;
}

int flp_get_bits_rectangle(gx_device *dev, const gs_int_rect *prect,
    gs_get_bits_params_t *params, gs_int_rect **unread)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.get_bits_rectangle)
                return dev->child->procs.get_bits_rectangle(dev->child, prect, params, unread);
            else
                return gx_default_get_bits_rectangle(dev->child, prect, params, unread);
        } else
            return gx_default_get_bits_rectangle(dev->child, prect, params, unread);
    }
    else
        return gx_default_get_bits_rectangle(dev->child, prect, params, unread);

    return 0;
}

int flp_map_color_rgb_alpha(gx_device *dev, gx_color_index color, gx_color_value rgba[4])
{
    if (dev->child->procs.map_color_rgb_alpha)
        return dev->child->procs.map_color_rgb_alpha(dev->child, color, rgba);
    else
        return gx_default_map_color_rgb_alpha(dev->child, color, rgba);

    return 0;
}

int flp_create_compositor(gx_device *dev, gx_device **pcdev, const gs_composite_t *pcte,
    gs_imager_state *pis, gs_memory_t *memory, gx_device *cdev)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;
    int code;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.create_compositor) {
                code = dev->child->procs.create_compositor(dev->child, pcdev, pcte, pis, memory, cdev);
                if (*pcdev != dev->child)
                    return code;
                else {
                    *pcdev = dev;
                    return code;
                }
            }
        }
    } else
        gx_default_create_compositor(dev, pcdev, pcte, pis, memory, cdev);

    return 0;
}

int flp_get_hardware_params(gx_device *dev, gs_param_list *plist)
{
    if (dev->child->procs.get_hardware_params)
        return dev->child->procs.get_hardware_params(dev->child, plist);
    else
        return gx_default_get_hardware_params(dev->child, plist);

    return 0;
}

/* Text processing (like images) works differently to other device
 * methods. Instead of the interpreter calling a device method, only
 * the 'begin' method is called, this creates a text enumerator which
 * it fills in (in part with the routines for processing text) and returns
 * to the interpreter. The interpreter then calls the methods defined in
 * the text enumerator to process the text.
 * Mad as a fish.....
 */

/* For our purposes if we are handling the text its because we are not
 * printing the page, so we cna afford to ignore all the text processing.
 * A more complex device might need to define real handlers for these, and
 * pass them on to the subclassed device.
 */
static text_enum_proc_process(flp_text_process);
static int
flp_text_resync(gs_text_enum_t *pte, const gs_text_enum_t *pfrom)
{
    return 0;
}
int
flp_text_process(gs_text_enum_t *pte)
{
    return 0;
}
static bool
flp_text_is_width_only(const gs_text_enum_t *pte)
{
    return false;
}
static int
flp_text_current_width(const gs_text_enum_t *pte, gs_point *pwidth)
{
    return 0;
}
static int
flp_text_set_cache(gs_text_enum_t *pte, const double *pw,
                   gs_text_cache_control_t control)
{
    return 0;
}
flp_text_retry(gs_text_enum_t *pte)
{
    return 0;
}
static void
flp_text_release(gs_text_enum_t *pte, client_name_t cname)
{
    flp_text_enum_t *const penum = (flp_text_enum_t *)pte;

    gx_default_text_release(pte, cname);
}

static const gs_text_enum_procs_t flp_text_procs = {
    flp_text_resync, flp_text_process,
    flp_text_is_width_only, flp_text_current_width,
    flp_text_set_cache, flp_text_retry,
    flp_text_release
};

/* The device method which we do actually need to define. Either we are skipping the page,
 * in which case we create a text enumerator with our dummy procedures, or we are leaving it
 * up to the device, in which case we simply pass on the 'begin' method to the device.
 */
int flp_text_begin(gx_device *dev, gs_imager_state *pis, const gs_text_params_t *text,
    gs_font *font, gx_path *path, const gx_device_color *pdcolor, const gx_clip_path *pcpath,
    gs_memory_t *memory, gs_text_enum_t **ppte)
{
    flp_text_enum_t *penum;
    int code;

    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage && (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage)) {
        if (dev->child->procs.text_begin)
            return dev->child->procs.text_begin(dev->child, pis, text, font, path, pdcolor, pcpath, memory, ppte);
        else
            return gx_default_text_begin(dev->child, pis, text, font, path, pdcolor, pcpath, memory, ppte);
    }
    else {
        rc_alloc_struct_1(penum, flp_text_enum_t, &st_flp_text_enum, memory,
                      return_error(gs_error_VMerror), "gdev_flp_text_begin");
        penum->rc.free = rc_free_text_enum;
        code = gs_text_enum_init((gs_text_enum_t *)penum, &flp_text_procs,
                             dev, pis, text, font, path, pdcolor, pcpath, memory);
        if (code < 0) {
            gs_free_object(memory, penum, "gdev_flp_text_begin");
            return code;
        }
        *ppte = (gs_text_enum_t *)penum;
    }

    return 0;
}

/* This method seems (despite the name) to be intended to allow for
 * devices to initialise data before being invoked. For our subclassed
 * device this should already have been done.
 */
int flp_finish_copydevice(gx_device *dev, const gx_device *from_dev)
{
    return 0;
}

int flp_begin_transparency_group(gx_device *dev, const gs_transparency_group_params_t *ptgp,
    const gs_rect *pbbox, gs_imager_state *pis, gs_memory_t *mem)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.begin_transparency_group)
                return dev->child->procs.begin_transparency_group(dev->child, ptgp, pbbox, pis, mem);
        }
    }

    return 0;
}

int flp_end_transparency_group(gx_device *dev, gs_imager_state *pis)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.end_transparency_group)
                return dev->child->procs.end_transparency_group(dev->child, pis);
        }
    }

    return 0;
}

int flp_begin_transparency_mask(gx_device *dev, const gx_transparency_mask_params_t *ptmp,
    const gs_rect *pbbox, gs_imager_state *pis, gs_memory_t *mem)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.begin_transparency_mask)
                return dev->child->procs.begin_transparency_mask(dev->child, ptmp, pbbox, pis, mem);
        }
    }

    return 0;
}

int flp_end_transparency_mask(gx_device *dev, gs_imager_state *pis)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.end_transparency_mask)
                return dev->child->procs.end_transparency_mask(dev->child, pis);
        }
    }
    return 0;
}

int flp_discard_transparency_layer(gx_device *dev, gs_imager_state *pis)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.discard_transparency_layer)
                return dev->child->procs.discard_transparency_layer(dev->child, pis);
        }
    }

    return 0;
}

const gx_cm_color_map_procs *flp_get_color_mapping_procs(const gx_device *dev)
{
    if (dev->child->procs.get_color_mapping_procs)
        return dev->child->procs.get_color_mapping_procs(dev->child);
    else
        return gx_default_DevGray_get_color_mapping_procs(dev->child);

    return 0;
}

int  flp_get_color_comp_index(gx_device *dev, const char * pname, int name_size, int component_type)
{
    if (dev->child->procs.get_color_comp_index)
        return dev->child->procs.get_color_comp_index(dev->child, pname, name_size, component_type);
    else
        return gx_error_get_color_comp_index(dev->child, pname, name_size, component_type);

    return 0;
}

gx_color_index flp_encode_color(gx_device *dev, const gx_color_value colors[])
{
    if (dev->child->procs.encode_color)
        return dev->child->procs.encode_color(dev->child, colors);
    else
        return gx_error_encode_color(dev->child, colors);

    return 0;
}

flp_decode_color(gx_device *dev, gx_color_index cindex, gx_color_value colors[])
{
    if (dev->child->procs.decode_color)
        return dev->child->procs.decode_color(dev->child, cindex, colors);
    else {
        memset(colors, 0, sizeof(gx_color_value[GX_DEVICE_COLOR_MAX_COMPONENTS]));
    }

    return 0;
}

int flp_pattern_manage(gx_device *dev, gx_bitmap_id id,
                gs_pattern1_instance_t *pinst, pattern_manage_t function)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.pattern_manage)
                return dev->child->procs.pattern_manage(dev->child, id, pinst, function);
        }
    }

    return 0;
}

int flp_fill_rectangle_hl_color(gx_device *dev, const gs_fixed_rect *rect,
        const gs_imager_state *pis, const gx_drawing_color *pdcolor, const gx_clip_path *pcpath)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_rectangle_hl_color)
                return dev->child->procs.fill_rectangle_hl_color(dev->child, rect, pis, pdcolor, pcpath);
            else
                return_error(gs_error_rangecheck);
        }
    }

    return 0;
}

int flp_include_color_space(gx_device *dev, gs_color_space *cspace, const byte *res_name, int name_length)
{
    if (dev->child->procs.include_color_space)
        return dev->child->procs.include_color_space(dev->child, cspace, res_name, name_length);

    return 0;
}

int flp_fill_linear_color_scanline(gx_device *dev, const gs_fill_attributes *fa,
        int i, int j, int w, const frac31 *c0, const int32_t *c0_f, const int32_t *cg_num,
        int32_t cg_den)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_linear_color_scanline)
                return dev->child->procs.fill_linear_color_scanline(dev->child, fa, i, j, w, c0, c0_f, cg_num, cg_den);
            else
                return gx_default_fill_linear_color_scanline(dev->child, fa, i, j, w, c0, c0_f, cg_num, cg_den);
        }
    }

    return 0;
}

int flp_fill_linear_color_trapezoid(gx_device *dev, const gs_fill_attributes *fa,
        const gs_fixed_point *p0, const gs_fixed_point *p1,
        const gs_fixed_point *p2, const gs_fixed_point *p3,
        const frac31 *c0, const frac31 *c1,
        const frac31 *c2, const frac31 *c3)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_linear_color_trapezoid)
                return dev->child->procs.fill_linear_color_trapezoid(dev->child, fa, p0, p1, p2, p3, c0, c1, c2, c3);
            else
                return gx_default_fill_linear_color_trapezoid(dev->child, fa, p0, p1, p2, p3, c0, c1, c2, c3);
        }
    }

    return 0;
}

int flp_fill_linear_color_triangle(gx_device *dev, const gs_fill_attributes *fa,
        const gs_fixed_point *p0, const gs_fixed_point *p1,
        const gs_fixed_point *p2, const frac31 *c0, const frac31 *c1, const frac31 *c2)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fill_linear_color_triangle)
                return dev->child->procs.fill_linear_color_triangle(dev->child, fa, p0, p1, p2, c0, c1, c2);
            else
                return gx_default_fill_linear_color_triangle(dev->child, fa, p0, p1, p2, c0, c1, c2);
        }
    }

    return 0;
}

int flp_update_spot_equivalent_colors(gx_device *dev, const gs_state * pgs)
{
    if (dev->child->procs.update_spot_equivalent_colors)
        return dev->child->procs.update_spot_equivalent_colors(dev->child, pgs);

    return 0;
}

gs_devn_params *flp_ret_devn_params(gx_device *dev)
{
    if (dev->child->procs.ret_devn_params)
        return dev->child->procs.ret_devn_params(dev->child);

    return 0;
}

int flp_fillpage(gx_device *dev, gs_imager_state * pis, gx_device_color *pdevc)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.fillpage)
                return dev->child->procs.fillpage(dev->child, pis, pdevc);
            else
                return gx_default_fillpage(dev->child, pis, pdevc);
        }
    }

    return 0;
}

int flp_push_transparency_state(gx_device *dev, gs_imager_state *pis)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.push_transparency_state)
                return dev->child->procs.push_transparency_state(dev->child, pis);
        }
    }

    return 0;
}

int flp_pop_transparency_state(gx_device *dev, gs_imager_state *pis)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.push_transparency_state)
                return dev->child->procs.push_transparency_state(dev->child, pis);
        }
    }

    return 0;
}

int flp_put_image(gx_device *dev, const byte *buffer, int num_chan, int x, int y,
            int width, int height, int row_stride, int plane_stride,
            int alpha_plane_index, int tag_plane_index)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.put_image)
                return dev->child->procs.put_image(dev->child, buffer, num_chan, x, y, width, height, row_stride, plane_stride, alpha_plane_index, tag_plane_index);
        }
    }

    return 0;
}

int flp_dev_spec_op(gx_device *dev, int op, void *data, int datasize)
{
    if (dev->child->procs.dev_spec_op)
        return dev->child->procs.dev_spec_op(dev->child, op, data, datasize);

    return 0;
}

int flp_copy_planes(gx_device *dev, const byte *data, int data_x, int raster, gx_bitmap_id id,
    int x, int y, int width, int height, int plane_height)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.copy_planes)
                return dev->child->procs.copy_planes(dev->child, data, data_x, raster, id, x, y, width, height, plane_height);
        }
    }

    return 0;
}

int flp_get_profile(gx_device *dev, cmm_dev_profile_t **dev_profile)
{
    if (dev->child->procs.get_profile)
        return dev->child->procs.get_profile(dev->child, dev_profile);
    else
        return gx_default_get_profile(dev->child, dev_profile);

    return 0;
}

void flp_set_graphics_type_tag(gx_device *dev, gs_graphics_type_tag_t tag)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.set_graphics_type_tag)
                dev->child->procs.set_graphics_type_tag(dev->child, tag);
        }
    }

    return;
}

int flp_strip_copy_rop2(gx_device *dev, const byte *sdata, int sourcex, uint sraster, gx_bitmap_id id,
    const gx_color_index *scolors, const gx_strip_bitmap *textures, const gx_color_index *tcolors,
    int x, int y, int width, int height, int phase_x, int phase_y, gs_logical_operation_t lop, uint planar_height)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.strip_copy_rop2)
                return dev->child->procs.strip_copy_rop2(dev->child, sdata, sourcex, sraster, id, scolors, textures, tcolors, x, y, width, height, phase_x, phase_y, lop, planar_height);
            else
                return gx_default_strip_copy_rop2(dev->child, sdata, sourcex, sraster, id, scolors, textures, tcolors, x, y, width, height, phase_x, phase_y, lop, planar_height);
        }
    }

    return 0;
}

int flp_strip_tile_rect_devn(gx_device *dev, const gx_strip_bitmap *tiles, int x, int y, int width, int height,
    const gx_drawing_color *pdcolor0, const gx_drawing_color *pdcolor1, int phase_x, int phase_y)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.strip_tile_rect_devn)
                return dev->child->procs.strip_tile_rect_devn(dev->child, tiles, x, y, width, height, pdcolor0, pdcolor1, phase_x, phase_y);
            else
                return gx_default_strip_tile_rect_devn(dev->child, tiles, x, y, width, height, pdcolor0, pdcolor1, phase_x, phase_y);
        }
    }

    return 0;
}

int flp_copy_alpha_hl_color(gx_device *dev, const byte *data, int data_x,
    int raster, gx_bitmap_id id, int x, int y, int width, int height,
    const gx_drawing_color *pdcolor, int depth)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.copy_alpha_hl_color)
                return dev->child->procs.copy_alpha_hl_color(dev->child, data, data_x, raster, id, x, y, width, height, pdcolor, depth);
            else
                return_error(gs_error_rangecheck);
        }
    }

    return 0;
}

int flp_process_page(gx_device *dev, gx_process_page_options_t *options)
{
    first_last_subclass_data *psubclass_data = dev->subclass_data;

    if (psubclass_data->PageCount >= dev->FirstPage) {
        if (!dev->LastPage || psubclass_data->PageCount <= dev->LastPage) {
            if (dev->child->procs.process_page)
                return dev->child->procs.process_page(dev->child, options);
        }
    }

    return 0;
}