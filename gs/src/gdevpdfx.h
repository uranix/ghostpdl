/* Copyright (C) 1996, 2000, 2001 Aladdin Enterprises.  All rights reserved.
  
  This software is provided AS-IS with no warranty, either express or
  implied.
  
  This software is distributed under license and may not be copied,
  modified or distributed except as expressly authorized under the terms
  of the license contained in the file LICENSE in this distribution.
  
  For more information about licensing, please refer to
  http://www.ghostscript.com/licensing/. For information on
  commercial licensing, go to http://www.artifex.com/licensing/ or
  contact Artifex Software, Inc., 101 Lucas Valley Road #110,
  San Rafael, CA  94903, U.S.A., +1(415)492-9861.
*/

/* $Id$ */
/* Internal definitions for PDF-writing driver. */

#ifndef gdevpdfx_INCLUDED
#  define gdevpdfx_INCLUDED

#include "gsparam.h"
#include "gsuid.h"
#include "gxdevice.h"
#include "gxfont.h"
#include "gxline.h"
#include "stream.h"
#include "spprint.h"
#include "gdevpsdf.h"

/* ---------------- Acrobat limitations ---------------- */

/*
 * The PDF reference manual, 2nd ed., claims that the limit for coordinates
 * is +/- 32767. However, testing indicates that Acrobat Reader 4 for
 * Windows and Linux fail with coordinates outside +/- 16383. Hence, we
 * limit coordinates to 16k, with a little slop.
 */
#define MAX_USER_COORD 16300

/* ---------------- Statically allocated sizes ---------------- */
/* These should really be dynamic.... */

/* Define the maximum depth of an outline tree. */
/* Note that there is no limit on the breadth of the tree. */
#define MAX_OUTLINE_DEPTH 32

/* Define the maximum size of a destination array string. */
#define MAX_DEST_STRING 80

/* ================ Types and structures ================ */

/* Define the possible contexts for the output stream. */
typedef enum {
    PDF_IN_NONE,
    PDF_IN_STREAM,
    PDF_IN_TEXT,
    PDF_IN_STRING
} pdf_context_t;

/* ---------------- Cos objects ---------------- */

/*
 * These are abstract types for cos objects.  The concrete types are in
 * gdevpdfo.h.
 */
typedef struct cos_object_s cos_object_t;
typedef struct cos_stream_s cos_stream_t;
typedef struct cos_dict_s cos_dict_t;
typedef struct cos_array_s cos_array_t;
typedef struct cos_value_s cos_value_t;
typedef struct cos_object_procs_s cos_object_procs_t;
typedef const cos_object_procs_t *cos_type_t;
#define cos_types_DEFINED

#ifndef pdf_text_state_DEFINED
#  define pdf_text_state_DEFINED
typedef struct pdf_text_state_s pdf_text_state_t;
#endif

/* ---------------- Resources ---------------- */

typedef enum {
    /*
     * Standard PDF resources.  Font must be last, because resources
     * up to but not including Font are written page-by-page.
     */
    resourceColorSpace,
    resourceExtGState,
    resourcePattern,
    resourceShading,
    resourceXObject,
    resourceFont,
    /*
     * Internally used (pseudo-)resources.
     */
    resourceCharProc,
    resourceCIDFont,
    resourceCMap,
    resourceFontDescriptor,
    resourceFunction,
    NUM_RESOURCE_TYPES
} pdf_resource_type_t;

#define PDF_RESOURCE_TYPE_NAMES\
  "/ColorSpace", "/ExtGState", "/Pattern", "/Shading", "/XObject", "/Font",\
  0, "/Font", "/CMap", "/FontDescriptor", 0
#define PDF_RESOURCE_TYPE_STRUCTS\
  &st_pdf_color_space,		/* gdevpdfg.h / gdevpdfc.c */\
  &st_pdf_resource,		/* see below */\
  &st_pdf_resource,\
  &st_pdf_resource,\
  &st_pdf_x_object,		/* see below */\
  &st_pdf_font_resource,	/* gdevpdff.h / gdevpdff.c */\
  &st_pdf_char_proc,		/* gdevpdff.h / gdevpdff.c */\
  &st_pdf_font_resource,	/* gdevpdff.h / gdevpdff.c */\
  &st_pdf_resource,\
  &st_pdf_font_descriptor,	/* gdevpdff.h / gdevpdff.c */\
  &st_pdf_resource

/*
 * rname is currently R<id> for all resources other than synthesized fonts;
 * for synthesized fonts, rname is A, B, ....  The string is null-terminated.
 */

#define pdf_resource_common(typ)\
    typ *next;			/* next resource of this type */\
    pdf_resource_t *prev;	/* previously allocated resource */\
    gs_id rid;			/* optional ID key */\
    bool named;\
    char rname[1/*R*/ + (sizeof(long) * 8 / 3 + 1) + 1/*\0*/];\
    ulong where_used;		/* 1 bit per level of content stream */\
    cos_object_t *object
typedef struct pdf_resource_s pdf_resource_t;
struct pdf_resource_s {
    pdf_resource_common(pdf_resource_t);
};

/* The descriptor is public for subclassing. */
extern_st(st_pdf_resource);
#define public_st_pdf_resource()  /* in gdevpdfu.c */\
  gs_public_st_ptrs3(st_pdf_resource, pdf_resource_t, "pdf_resource_t",\
    pdf_resource_enum_ptrs, pdf_resource_reloc_ptrs, next, prev, object)

/*
 * We define XObject resources here because they are used for Image,
 * Form, and PS XObjects, which are implemented in different files.
 */
typedef struct pdf_x_object_s pdf_x_object_t;
struct pdf_x_object_s {
    pdf_resource_common(pdf_x_object_t);
    int width, height;		/* specified width and height for images */
    int data_height;		/* actual data height for images */
};
#define private_st_pdf_x_object()  /* in gdevpdfu.c */\
  gs_private_st_suffix_add0(st_pdf_x_object, pdf_x_object_t,\
    "pdf_x_object_t", pdf_x_object_enum_ptrs, pdf_x_object_reloc_ptrs,\
    st_pdf_resource)

/* Define the mask for which procsets have been used on a page. */
typedef enum {
    NoMarks = 0,
    ImageB = 1,
    ImageC = 2,
    ImageI = 4,
    Text = 8
} pdf_procset_t;

/* ------ Fonts ------ */

/* Define abstract types. */
typedef struct pdf_char_proc_s pdf_char_proc_t;	/* gdevpdff.h */
typedef struct pdf_font_s pdf_font_t;  /* gdevpdff.h */
typedef struct pdf_text_data_s pdf_text_data_t;  /* gdevpdft.h */

/* ---------------- Other auxiliary structures ---------------- */

/* Outline nodes and levels */
typedef struct pdf_outline_node_s {
    long id, parent_id, prev_id, first_id, last_id;
    int count;
    cos_dict_t *action;
} pdf_outline_node_t;
typedef struct pdf_outline_level_s {
    pdf_outline_node_t first;
    pdf_outline_node_t last;
    int left;
} pdf_outline_level_t;
/*
 * The GC descriptor is implicit, since outline levels occur only in an
 * embedded array in the gx_device_pdf structure.
 */

/* Articles */
typedef struct pdf_bead_s {
    long id, article_id, prev_id, next_id, page_id;
    gs_rect rect;
} pdf_bead_t;
typedef struct pdf_article_s pdf_article_t;
struct pdf_article_s {
    pdf_article_t *next;
    cos_dict_t *contents;
    pdf_bead_t first;
    pdf_bead_t last;
};

#define private_st_pdf_article()\
  gs_private_st_ptrs2(st_pdf_article, pdf_article_t,\
    "pdf_article_t", pdf_article_enum_ptrs, pdf_article_reloc_ptrs,\
    next, contents)

/* ---------------- The device structure ---------------- */

/* Resource lists */
#define NUM_RESOURCE_CHAINS 16
typedef struct pdf_resource_list_s {
    pdf_resource_t *chains[NUM_RESOURCE_CHAINS];
} pdf_resource_list_t;

/* Define the hash function for gs_ids. */
#define gs_id_hash(rid) ((rid) + ((rid) / NUM_RESOURCE_CHAINS))
/* Define the accessor for the proper hash chain. */
#define PDF_RESOURCE_CHAIN(pdev, type, rid)\
  (&(pdev)->resources[type].chains[gs_id_hash(rid) % NUM_RESOURCE_CHAINS])

/* Define the bookkeeping for an open stream. */
typedef struct pdf_stream_position_s {
    long length_id;
    long start_pos;
} pdf_stream_position_t;

/*
 * Define the structure for keeping track of text rotation.
 * There is one for the current page (for AutoRotate /PageByPage)
 * and one for the whole document (for AutoRotate /All).
 */
typedef struct pdf_text_rotation_s {
    long counts[5];		/* 0, 90, 180, 270, other */
    int Rotate;			/* computed rotation, -1 means none */
} pdf_text_rotation_t;
#define pdf_text_rotation_angle_values 0, 90, 180, 270, -1

/*
 * Define document and page information derived from DSC comments.
 */
typedef struct pdf_page_dsc_info_s {
    int orientation;		/* -1 .. 3 */
    int viewing_orientation;	/* -1 .. 3 */
    gs_rect bounding_box;
} pdf_page_dsc_info_t;

/*
 * Define the stored information for a page.  Because pdfmarks may add
 * information to any page anywhere in the document, we have to wait
 * until the end to write the page dictionaries.
 */
typedef struct pdf_page_s {
    cos_dict_t *Page;
    gs_point MediaBox;
    pdf_procset_t procsets;
    long contents_id;
    long resource_ids[resourceFont + 1]; /* resources thru Font, see above */
    cos_array_t *Annots;
    pdf_text_rotation_t text_rotation;
    pdf_page_dsc_info_t dsc_info;
} pdf_page_t;
#define private_st_pdf_page()	/* in gdevpdf.c */\
  gs_private_st_ptrs2(st_pdf_page, pdf_page_t, "pdf_page_t",\
    pdf_page_enum_ptrs, pdf_page_reloc_ptrs, Page, Annots)

/*
 * Define the structure for the temporary files used while writing.
 * There are 4 of these, described below.
 */
typedef struct pdf_temp_file_s {
    char file_name[gp_file_name_sizeof];
    FILE *file;
    stream *strm;
    byte *strm_buf;
    stream *save_strm;		/* save pdev->strm while writing here */
} pdf_temp_file_t;

#ifndef gx_device_pdf_DEFINED
#  define gx_device_pdf_DEFINED
typedef struct gx_device_pdf_s gx_device_pdf;
#endif

/*
 * Define the structure for PDF font cache element.
 */
typedef struct pdf_font_cache_elem_s pdf_font_cache_elem_t;
struct pdf_font_cache_elem_s {
    pdf_font_cache_elem_t *next;
    gs_id font_id;
    int num_chars;		/* safety purpose only */
    int num_widths;		/* safety purpose only */
    struct pdf_font_resource_s *pdfont;
    byte *glyph_usage;
    double *real_widths;	/* [count] (not used for Type 0) */
    gx_device_pdf *pdev;	/* For pdf_remove_font_cache_elem */
};

#define private_st_pdf_font_cache_elem()\
    gs_private_st_ptrs5(st_pdf_font_cache_elem, pdf_font_cache_elem_t,\
	"pdf_font_cache_elem_t", pdf_font_cache_elem_enum,\
	pdf_font_cache_elem_reloc, next, pdfont,\
	glyph_usage, real_widths, pdev);\

/*
 * pdf_viewer_state tracks the graphic state of a viewer,
 * which would interpret the generated PDF file
 * immediately when it is generated.
 */
typedef struct pdf_viewer_state_s {
    int transfer_not_identity;	/* bitmask */
    gs_id transfer_ids[4];
    float opacity_alpha; /* state.opacity.alpha */
    float shape_alpha; /* state.shape.alpha */
    gs_blend_mode_t blend_mode; /* state.blend_mode */
    gs_id halftone_id;
    gs_id black_generation_id;
    gs_id undercolor_removal_id;
    int overprint_mode;
    float smoothness; /* state.smoothness */
    bool text_knockout; /* state.text_knockout */
    bool fill_overprint;
    bool stroke_overprint;
    bool stroke_adjust; /* state.stroke_adjust */
    gx_hl_saved_color saved_fill_color;
    gx_hl_saved_color saved_stroke_color;
    gx_line_params line_params;
    float dash_pattern[max_dash];
} pdf_viewer_state;

/*
 * Define a structure for saving context when entering
 * a contents stream accumulation mode (charproc, Type 1 pattern).
 */
typedef struct pdf_substream_save_s {
    pdf_context_t	context;
    pdf_text_state_t	*text_state;
    gx_path		*clip_path;
    gs_id		clip_path_id;
    int			vgstack_bottom;
    stream		*strm;
    cos_dict_t		*substream_Resources;
    pdf_procset_t	procsets;
    bool		skip_colors;
    pdf_resource_t      *font3;
} pdf_substream_save;

#define private_st_pdf_substream_save()\
    gs_private_st_ptrs5(st_pdf_substream_save, pdf_substream_save,\
	"pdf_substream_save", pdf_substream_save_enum,\
	pdf_substream_save_reloc, text_state, clip_path, strm, \
	substream_Resources, font3);
#define private_st_pdf_substream_save_element()\
  gs_private_st_element(st_pdf_substream_save_element, pdf_substream_save,\
    "pdf_substream_save[]", pdf_substream_save_elt_enum_ptrs,\
    pdf_substream_save_elt_reloc_ptrs, st_pdf_substream_save)

/* Define the device structure. */
struct gx_device_pdf_s {
    gx_device_psdf_common;
    /* PDF-specific distiller parameters */
    double CompatibilityLevel;
    int EndPage;
    int StartPage;
    bool Optimize;
    bool ParseDSCCommentsForDocInfo;
    bool ParseDSCComments;
    bool EmitDSCWarnings;
    bool CreateJobTicket;
    bool PreserveEPSInfo;
    bool AutoPositionEPSFiles;
    bool PreserveCopyPage;
    bool UsePrologue;
    int OffOptimizations;
    /* End of distiller parameters */
    /* Other parameters */
    bool ReAssignCharacters;
    bool ReEncodeCharacters;
    long FirstObjectNumber;
    bool CompressFonts;
    long MaxInlineImageSize;
    /* End of parameters */
    /* Values derived from DSC comments */
    bool is_EPS;
    pdf_page_dsc_info_t doc_dsc_info; /* document default */
    pdf_page_dsc_info_t page_dsc_info; /* current page */
    /* Additional graphics state */
    bool fill_overprint, stroke_overprint;
    int overprint_mode;
    gs_id halftone_id;
    gs_id transfer_ids[4];
    int transfer_not_identity;	/* bitmask */
    gs_id black_generation_id, undercolor_removal_id;
    /* Following are set when device is opened. */
    enum {
	pdf_compress_none,
	pdf_compress_LZW,	/* not currently used, thanks to Unisys */
	pdf_compress_Flate
    } compression;
#define pdf_memory v_memory
    /*
     * The xref temporary file is logically an array of longs.
     * xref[id - FirstObjectNumber] is the position in the output file
     * of the object with the given id.
     *
     * Note that xref, unlike the other temporary files, does not have
     * an associated stream or stream buffer.
     */
    pdf_temp_file_t xref;
    /*
     * asides holds resources and other "aside" objects.  It is
     * copied verbatim to the output file at the end of the document.
     */
    pdf_temp_file_t asides;
    /*
     * streams holds data for stream-type Cos objects.  The data is
     * copied to the output file at the end of the document.
     *
     * Note that streams.save_strm is not used, since we don't interrupt
     * normal output when saving stream data.
     */
    pdf_temp_file_t streams;
    /*
     * pictures holds graphic objects being accumulated between BP and EP.
     * The object is moved to streams when the EP is reached: since BP and
     * EP nest, we delete the object from the pictures file at that time.
     */
    pdf_temp_file_t pictures;
    /* ................ */
    long next_id;
    /* The following 3 objects, and only these, are allocated */
    /* when the file is opened. */
    cos_dict_t *Catalog;
    cos_dict_t *Info;
    cos_dict_t *Pages;
#define pdf_num_initial_ids 3
    long outlines_id;
    int next_page;
    long contents_id;
    pdf_context_t context;
    long contents_length_id;
    long contents_pos;
    pdf_procset_t procsets;	/* used on this page */
    pdf_text_data_t *text;
    pdf_text_rotation_t text_rotation;
#define initial_num_pages 50
    pdf_page_t *pages;
    int num_pages;
    ulong used_mask;		/* for where_used: page level = 1 */
    pdf_resource_list_t resources[NUM_RESOURCE_TYPES];
    /* cs_Patterns[0] is colored; 1,3,4 are uncolored + Gray,RGB,CMYK */
    pdf_resource_t *cs_Patterns[5];
    pdf_resource_t *Identity_ToUnicode_CMaps[2]; /* WMode = 0,1 */
    pdf_resource_t *last_resource;
    pdf_outline_level_t outline_levels[MAX_OUTLINE_DEPTH];
    int outline_depth;
    int closed_outline_depth;
    int outlines_open;
    pdf_article_t *articles;
    cos_dict_t *Dests;
    byte fileID[16];
    /*
     * global_named_objects holds named objects that are independent of
     * the current namespace: {Catalog}, {DocInfo}, {Page#}, {ThisPage},
     * {PrevPage}, {NextPage}.
     */
    cos_dict_t *global_named_objects;
    /*
     * local_named_objects holds named objects in the current namespace.
     */
    cos_dict_t *local_named_objects;
    /*
     * NI_stack is a last-in, first-out stack in which each element is a
     * (named) cos_stream_t object that eventually becomes the object of an
     * image XObject resource.
     */
    cos_array_t *NI_stack;
    /*
     * Namespace_stack is a last-in, first-out stack in which each pair of
     * elements is, respectively, a saved value of local_named_objects and
     * a saved value of NI_stack.  (The latter is not documented by Adobe,
     * but it was confirmed by them.)
     */
    cos_array_t *Namespace_stack;
    pdf_font_cache_elem_t *font_cache;
    /* 
     * char_width is used by pdf_text_set_cache to communicate 
     * with assign_char_code around gdev_pdf_fill_mask. 
     */
    gs_point char_width; 
    /*
     * We need a stable copy of clipping path to prevent writing
     * redundant clipping paths when PS document generates such ones.
     */
    gx_path *clip_path;
    /*
     * Page labels.
     */
    cos_array_t *PageLabels;
    int PageLabels_current_page;
    cos_dict_t *PageLabels_current_label;
    /*
     * The following is a dangerous pointer, which pdf_text_process
     * uses to communicate with assign_char_code.
     * It is a pointer from global memory to local memory.
     * The garbager must not proceess this pointer, and it must
     * not be listed in st_device_pdfwrite.
     * It's life time terminates on garbager invocation.
     */
    gs_text_enum_t *pte;
    /* 
     * The viewer's graphic state stack.
     * We restrict its length with the strongest PDF spec limitation.
     * Usually 5 levels is enough, but patterns and charprocs may be nested recursively.
     */
    pdf_viewer_state vgstack[11];
    int vgstack_depth;
    int vgstack_bottom;		 /* Stack bottom for the current substream. */
    pdf_viewer_state vg_initial; /* Initial values for viewer's graphic state */
    bool vg_initial_set;

    /* The substream context stack. */
    int sbstack_size;
    int sbstack_depth;
    pdf_substream_save *sbstack;

    /* Accessories */
    cos_dict_t *substream_Resources;     /* Substream resources */
    int pcm_color_info_index;    /* Index to pcm_color_info. */
    bool skip_colors; /* Skip colors while a pattern/charproc accumulation. */
    bool AR4_save_bug; /* See pdf_put_uncolored_pattern */
    pdf_resource_t *font3; /* The owner of the accumulated charstring. */
};

#define is_in_page(pdev)\
  ((pdev)->contents_id != 0)
#define is_in_document(pdev)\
  (is_in_page(pdev) || (pdev)->last_resource != 0)

/* Enumerate the individual pointers in a gx_device_pdf. */
#define gx_device_pdf_do_ptrs(m)\
 m(0,asides.strm) m(1,asides.strm_buf) m(2,asides.save_strm)\
 m(3,streams.strm) m(4,streams.strm_buf)\
 m(5,pictures.strm) m(6,pictures.strm_buf) m(7,pictures.save_strm)\
 m(8,Catalog) m(9,Info) m(10,Pages)\
 m(11,text) m(12,pages)\
 m(13,cs_Patterns[0])\
 m(14,cs_Patterns[1]) m(15,cs_Patterns[3]) m(16,cs_Patterns[4])\
 m(17,last_resource)\
 m(18,articles) m(19,Dests) m(20,global_named_objects)\
 m(21, local_named_objects) m(22,NI_stack) m(23,Namespace_stack)\
 m(24,font_cache) m(25,clip_path)\
 m(26,PageLabels) m(27,PageLabels_current_label)\
 m(28,sbstack) m(29,substream_Resources) m(30,font3)
#define gx_device_pdf_num_ptrs 31
#define gx_device_pdf_do_strings(m) /* do nothing */
#define gx_device_pdf_num_strings 0
#define st_device_pdf_max_ptrs\
  (st_device_psdf_max_ptrs + gx_device_pdf_num_ptrs +\
   gx_device_pdf_num_strings +\
   NUM_RESOURCE_TYPES * NUM_RESOURCE_CHAINS /* resources[].chains[] */ +\
   MAX_OUTLINE_DEPTH * 2 /* outline_levels[].{first,last}.action */

#define private_st_device_pdfwrite()	/* in gdevpdf.c */\
  gs_private_st_composite_final(st_device_pdfwrite, gx_device_pdf,\
    "gx_device_pdf", device_pdfwrite_enum_ptrs, device_pdfwrite_reloc_ptrs,\
    device_pdfwrite_finalize)

/* ================ Driver procedures ================ */

    /* In gdevpdfb.c */
dev_proc_copy_mono(gdev_pdf_copy_mono);
dev_proc_copy_color(gdev_pdf_copy_color);
dev_proc_fill_mask(gdev_pdf_fill_mask);
dev_proc_strip_tile_rectangle(gdev_pdf_strip_tile_rectangle);
    /* In gdevpdfd.c */
extern const gx_device_vector_procs pdf_vector_procs;
dev_proc_fill_rectangle(gdev_pdf_fill_rectangle);
dev_proc_fill_path(gdev_pdf_fill_path);
dev_proc_stroke_path(gdev_pdf_stroke_path);
    /* In gdevpdfi.c */
dev_proc_begin_typed_image(gdev_pdf_begin_typed_image);
    /* In gdevpdfp.c */
dev_proc_get_params(gdev_pdf_get_params);
dev_proc_put_params(gdev_pdf_put_params);
    /* In gdevpdft.c */
dev_proc_text_begin(gdev_pdf_text_begin);
dev_proc_pattern_manage(gdev_pdf_pattern_manage);
dev_proc_fill_rectangle_hl_color(gdev_pdf_fill_rectangle_hl_color);
    /* In gdevpdfv.c */
dev_proc_include_color_space(gdev_pdf_include_color_space);

/* ================ Utility procedures ================ */

/* ---------------- Exported by gdevpdf.c ---------------- */

/* Initialize the IDs allocated at startup. */
void pdf_initialize_ids(gx_device_pdf * pdev);

/* Update the color mapping procedures after setting ProcessColorModel. */
void pdf_set_process_color_model(gx_device_pdf * pdev, int index);

/* Reset the text state parameters to initial values. */
void pdf_reset_text(gx_device_pdf *pdev);

/* ---------------- Exported by gdevpdfu.c ---------------- */

/* ------ Document ------ */

/* Open the document if necessary. */
void pdf_open_document(gx_device_pdf * pdev);

/* ------ Objects ------ */

/* Allocate an ID for a future object. */
long pdf_obj_ref(gx_device_pdf * pdev);

/* Read the current position in the output stream. */
long pdf_stell(gx_device_pdf * pdev);

/* Begin an object, optionally allocating an ID. */
long pdf_open_obj(gx_device_pdf * pdev, long id);
long pdf_begin_obj(gx_device_pdf * pdev);

/* End an object. */
int pdf_end_obj(gx_device_pdf * pdev);

/* ------ Page contents ------ */

/* Open a page contents part. */
/* Return an error if the page has too many contents parts. */
int pdf_open_contents(gx_device_pdf * pdev, pdf_context_t context);

/* Close the current contents part if we are in one. */
int pdf_close_contents(gx_device_pdf * pdev, bool last);

/* ------ Resources et al ------ */

extern const char *const pdf_resource_type_names[];
extern const gs_memory_struct_type_t *const pdf_resource_type_structs[];

/*
 * Define the offset that indicates that a file position is in the
 * asides file rather than the main (contents) file.
 * Must be a power of 2, and larger than the largest possible output file.
 */
#define ASIDES_BASE_POSITION min_long

/* Begin an object logically separate from the contents. */
/* (I.e., an object in the resource file.) */
long pdf_open_separate(gx_device_pdf * pdev, long id);
long pdf_begin_separate(gx_device_pdf * pdev);

/* Reserve object id. */
void pdf_reserve_object_id(gx_device_pdf * pdev, pdf_resource_t *ppres, long id);

/* Begin an aside (resource, annotation, ...). */
int pdf_alloc_aside(gx_device_pdf * pdev, pdf_resource_t ** plist,
		const gs_memory_struct_type_t * pst, pdf_resource_t **ppres,
		long id);
/* Begin an aside (resource, annotation, ...). */
int pdf_begin_aside(gx_device_pdf * pdev, pdf_resource_t **plist,
		    const gs_memory_struct_type_t * pst,
		    pdf_resource_t **ppres);

/* Begin a resource of a given type. */
int pdf_begin_resource(gx_device_pdf * pdev, pdf_resource_type_t rtype,
		       gs_id rid, pdf_resource_t **ppres);

/* Begin a resource body of a given type. */
int pdf_begin_resource_body(gx_device_pdf * pdev, pdf_resource_type_t rtype,
			    gs_id rid, pdf_resource_t **ppres);

/* Allocate a resource, but don't open the stream. */
int pdf_alloc_resource(gx_device_pdf * pdev, pdf_resource_type_t rtype,
		       gs_id rid, pdf_resource_t **ppres, long id);

/* Find same resource. */
int pdf_find_same_resource(gx_device_pdf * pdev, pdf_resource_type_t rtype, 
	pdf_resource_t **ppres);

/* Find a resource of a given type by gs_id. */
pdf_resource_t *pdf_find_resource_by_gs_id(gx_device_pdf * pdev,
					   pdf_resource_type_t rtype,
					   gs_id rid);

/* Cancel a resource (do not write it into PDF). */
int pdf_cancel_resource(gx_device_pdf * pdev, pdf_resource_t *pres, 
	pdf_resource_type_t rtype);

/* Get the object id of a resource. */
long pdf_resource_id(const pdf_resource_t *pres);

/* End a separate object. */
int pdf_end_separate(gx_device_pdf * pdev);

/* End an aside. */
int pdf_end_aside(gx_device_pdf * pdev);

/* End a resource. */
int pdf_end_resource(gx_device_pdf * pdev);

/*
 * Write the Cos objects for resources local to a content stream.
 */
int pdf_write_resource_objects(gx_device_pdf *pdev, pdf_resource_type_t rtype);

/*
 * Free unnamed Cos objects for resources local to a content stream.
 */
int pdf_free_resource_objects(gx_device_pdf *pdev, pdf_resource_type_t rtype);

/*
 * Store the resource sets for a content stream (page or XObject).
 * Sets page->{procsets, resource_ids[], fonts_id}.
 */
int pdf_store_page_resources(gx_device_pdf *pdev, pdf_page_t *page);

/* Copy data from a temporary file to a stream. */
void pdf_copy_data(stream *s, FILE *file, long count);
void pdf_copy_data_safe(stream *s, FILE *file, long position, long count);

/* ------ Pages ------ */

/* Get or assign the ID for a page. */
/* Returns 0 if the page number is out of range. */
long pdf_page_id(gx_device_pdf * pdev, int page_num);

/* Get the page structure for the current page. */
pdf_page_t *pdf_current_page(gx_device_pdf *pdev);

/* Get the dictionary object for the current page. */
cos_dict_t *pdf_current_page_dict(gx_device_pdf *pdev);

/* Open a page for writing. */
int pdf_open_page(gx_device_pdf * pdev, pdf_context_t context);

/*  Go to the unclipped stream context. */
int pdf_unclip(gx_device_pdf * pdev);

/* Write saved page- or document-level information. */
int pdf_write_saved_string(gx_device_pdf * pdev, gs_string * pstr);

/* ------ Path drawing ------ */

/* Store a copy of clipping path. */
int pdf_remember_clip_path(gx_device_pdf * pdev, const gx_clip_path * pcpath);

/* Test whether the clip path needs updating. */
bool pdf_must_put_clip_path(gx_device_pdf * pdev, const gx_clip_path * pcpath);

/* Write and update the clip path. */
int pdf_put_clip_path(gx_device_pdf * pdev, const gx_clip_path * pcpath);

/* ------ Miscellaneous output ------ */

#define PDF_MAX_PRODUCER 200	/* adhoc */
/* Generate the default Producer string. */
void pdf_store_default_Producer(char buf[PDF_MAX_PRODUCER]);

/* Define the strings for filter names and parameters. */
typedef struct pdf_filter_names_s {
    const char *ASCII85Decode;
    const char *ASCIIHexDecode;
    const char *CCITTFaxDecode;
    const char *DCTDecode;
    const char *DecodeParms;
    const char *Filter;
    const char *FlateDecode;
    const char *LZWDecode;
    const char *RunLengthDecode;
} pdf_filter_names_t;
#define PDF_FILTER_NAMES\
  "/ASCII85Decode", "/ASCIIHexDecode", "/CCITTFaxDecode",\
  "/DCTDecode",  "/DecodeParms", "/Filter", "/FlateDecode",\
  "/LZWDecode", "/RunLengthDecode"
#define PDF_FILTER_NAMES_SHORT\
  "/A85", "/AHx", "/CCF", "/DCT", "/DP", "/F", "/Fl", "/LZW", "/RL"

/* Write matrix values. */
void pdf_put_matrix(gx_device_pdf *pdev, const char *before,
		    const gs_matrix *pmat, const char *after);

/* Write a name, with escapes for unusual characters. */
typedef int (*pdf_put_name_chars_proc_t)(stream *, const byte *, uint);
pdf_put_name_chars_proc_t
    pdf_put_name_chars_proc(const gx_device_pdf *pdev);
void pdf_put_name_chars(const gx_device_pdf *pdev, const byte *nstr,
			uint size);
void pdf_put_name(const gx_device_pdf *pdev, const byte *nstr, uint size);

/* Write a string in its shortest form ( () or <> ). */
void pdf_put_string(const gx_device_pdf *pdev, const byte *str, uint size);

/* Write a value, treating names specially. */
void pdf_write_value(const gx_device_pdf *pdev, const byte *vstr, uint size);

/* Store filters for a stream. */
int pdf_put_filters(cos_dict_t *pcd, gx_device_pdf *pdev, stream *s,
		    const pdf_filter_names_t *pfn);

/* Define a possibly encoded and compressed data stream. */
typedef struct pdf_data_writer_s {
    psdf_binary_writer binary;
    long start;
    long length_id;
} pdf_data_writer_t;
/*
 * Begin a data stream.  The client has opened the object and written
 * the << and any desired dictionary keys.
 */
#define DATA_STREAM_NOT_BINARY 0  /* data are text, not binary */
#define DATA_STREAM_BINARY 1	/* data are binary */
#define DATA_STREAM_COMPRESS 2	/* OK to compress data */
#define DATA_STREAM_NOLENGTH 4	/* Skip the length reference and filter names writing. */
int pdf_begin_data_stream(gx_device_pdf *pdev, pdf_data_writer_t *pdw,
			  int options);
/* begin_data = begin_data_binary with both options = true. */
int pdf_begin_data(gx_device_pdf *pdev, pdf_data_writer_t *pdw);

/* End a data stream. */
int pdf_end_data(pdf_data_writer_t *pdw);

/* ------ Functions ------ */

/* Define the maximum size of a Function reference. */
#define MAX_REF_CHARS ((sizeof(long) * 8 + 2) / 3)

/*
 * Create a Function object with or without range scaling.  Scaling means
 * that if x[i] is the i'th output value from the original Function,
 * the i'th output value from the Function object will be (x[i] -
 * ranges[i].rmin) / (ranges[i].rmax - ranges[i].rmin).  Note that this is
 * the inverse of the scaling convention for Functions per se.
 */
#ifndef gs_function_DEFINED
typedef struct gs_function_s gs_function_t;
#  define gs_function_DEFINED
#endif
int pdf_function(gx_device_pdf *pdev, const gs_function_t *pfn,
		 cos_value_t *pvalue);
int pdf_function_scaled(gx_device_pdf *pdev, const gs_function_t *pfn,
			const gs_range_t *pranges, cos_value_t *pvalue);

/* Write a Function object, returning its object ID. */
int pdf_write_function(gx_device_pdf *pdev, const gs_function_t *pfn,
		       long *pid);

/* ------ Fonts ------ */

/* Write a FontBBox dictionary element. */
int pdf_write_font_bbox(gx_device_pdf *pdev, const gs_int_rect *pbox);

/* ---------------- Exported by gdevpdfm.c ---------------- */

/*
 * Define the type for a pdfmark-processing procedure.
 * If nameable is false, the objname argument is always NULL.
 */
#define pdfmark_proc(proc)\
  int proc(gx_device_pdf *pdev, gs_param_string *pairs, uint count,\
	   const gs_matrix *pctm, const gs_param_string *objname)

/* Compare a C string and a gs_param_string. */
bool pdf_key_eq(const gs_param_string * pcs, const char *str);

/* Scan an integer out of a parameter string. */
int pdfmark_scan_int(const gs_param_string * pstr, int *pvalue);

/* Process a pdfmark (called from pdf_put_params). */
int pdfmark_process(gx_device_pdf * pdev, const gs_param_string_array * pma);

/* Close the current level of the outline tree. */
int pdfmark_close_outline(gx_device_pdf * pdev);

/* Close the pagelabel numtree. */
int pdfmark_end_pagelabels(gx_device_pdf * pdev);

/* Finish writing an article. */
int pdfmark_write_article(gx_device_pdf * pdev, const pdf_article_t * part);

/* ---------------- Exported by gdevpdfr.c ---------------- */

/* Test whether an object name has valid syntax, {name}. */
bool pdf_objname_is_valid(const byte *data, uint size);

/*
 * Look up a named object.  Return e_rangecheck if the syntax is invalid,
 * e_undefined if no object by that name exists.
 */
int pdf_find_named(gx_device_pdf * pdev, const gs_param_string * pname,
		   cos_object_t **ppco);

/*
 * Create a named object.  id = -1L means do not assign an id.  pname = 0
 * means just create the object, do not name it.
 */
int pdf_create_named(gx_device_pdf *pdev, const gs_param_string *pname,
		     cos_type_t cotype, cos_object_t **ppco, long id);
int pdf_create_named_dict(gx_device_pdf *pdev, const gs_param_string *pname,
			  cos_dict_t **ppcd, long id);

/*
 * Look up a named object as for pdf_find_named.  If the object does not
 * exist, create it (as a dictionary if it is one of the predefined names
 * {ThisPage}, {NextPage}, {PrevPage}, or {Page<#>}, otherwise as a
 * generic object) and return 1.
 */
int pdf_refer_named(gx_device_pdf *pdev, const gs_param_string *pname,
		    cos_object_t **ppco);

/*
 * Look up a named object as for pdf_refer_named.  If the object already
 * exists and is not simply a forward reference, return e_rangecheck;
 * if it exists as a forward reference, set its type and return 0;
 * otherwise, create the object with the given type and return 1.
 * pname = 0 is allowed: in this case, simply create the object.
 */
int pdf_make_named(gx_device_pdf * pdev, const gs_param_string * pname,
		   cos_type_t cotype, cos_object_t **ppco, bool assign_id);
int pdf_make_named_dict(gx_device_pdf * pdev, const gs_param_string * pname,
			cos_dict_t **ppcd, bool assign_id);

/*
 * Look up a named object as for pdf_refer_named.  If the object does not
 * exist, or is a forward reference, return e_undefined; if the object
 * exists has the wrong type, return e_typecheck.
 */
int pdf_get_named(gx_device_pdf * pdev, const gs_param_string * pname,
		  cos_type_t cotype, cos_object_t **ppco);

/*
 * Push the current local namespace onto the namespace stack, and reset it
 * to an empty namespace.
 */
int pdf_push_namespace(gx_device_pdf *pdev);

/*
 * Pop the top local namespace from the namespace stack.  Return an error if
 * the stack is empty.
 */
int pdf_pop_namespace(gx_device_pdf *pdev);

/*
 * Scan a string for a token.  <<, >>, [, and ] are treated as tokens.
 * Return 1 if a token was scanned, 0 if we reached the end of the string,
 * or an error.  On a successful return, the token extends from *ptoken up
 * to but not including *pscan.
 */
int pdf_scan_token(const byte **pscan, const byte * end, const byte **ptoken);

/*
 * Scan a possibly composite token: arrays and dictionaries are treated as
 * single tokens.
 */
int pdf_scan_token_composite(const byte **pscan, const byte * end,
			     const byte **ptoken);

/* Replace object names with object references in a (parameter) string. */
int pdf_replace_names(gx_device_pdf *pdev, const gs_param_string *from,
		      gs_param_string *to);

/* ================ Text module procedures ================ */

/* ---------------- Exported by gdevpdfw.c ---------------- */

/* For gdevpdf.c */

/*
 * Close the text-related parts of a document, including writing out font
 * and related resources.
 */
int pdf_close_text_document(gx_device_pdf *pdev);

/* ---------------- Exported by gdevpdft.c ---------------- */

/* For gdevpdf.c */

pdf_text_data_t *pdf_text_data_alloc(gs_memory_t *mem);
void pdf_set_text_state_default(pdf_text_state_t *pts);
void pdf_text_state_copy(pdf_text_state_t *pts_to, pdf_text_state_t *pts_from);
void pdf_reset_text_page(pdf_text_data_t *ptd);
void pdf_reset_text_state(pdf_text_data_t *ptd);
void pdf_close_text_page(gx_device_pdf *pdev);

/* For gdevpdfb.c */

int pdf_char_image_y_offset(const gx_device_pdf *pdev, int x, int y, int h);

/* Begin a CharProc for an embedded (bitmap) font. */
int pdf_begin_char_proc(gx_device_pdf * pdev, int w, int h, int x_width,
			int y_offset, gs_id id, pdf_char_proc_t **ppcp,
			pdf_stream_position_t * ppos);

/* End a CharProc. */
int pdf_end_char_proc(gx_device_pdf * pdev, pdf_stream_position_t * ppos);

/* Put out a reference to an image as a character in an embedded font. */
int pdf_do_char_image(gx_device_pdf * pdev, const pdf_char_proc_t * pcp,
		      const gs_matrix * pimat);

/* Install charproc accumulator for a Type 3 font. */
int pdf_install_charproc_accum(gx_device_pdf *pdev, gs_font *font, const double *pw, 
		gs_text_cache_control_t control, gs_char ch, gs_const_string *gnstr);
/* Complete charproc accumulation for aType 3 font. */
int pdf_end_charproc_accum(gx_device_pdf *pdev);

/* Enter the substream accumulation mode. */
int pdf_enter_substream(gx_device_pdf *pdev, pdf_resource_type_t rtype, 
			gs_id id, pdf_resource_t **ppres);

/* Exit the substream accumulation mode. */
int pdf_exit_substream(gx_device_pdf *pdev);
/* Add procsets to substream Resources. */
int pdf_add_procsets(cos_dict_t *pcd, pdf_procset_t procsets);
/* Add a resource to substream Resources. */
int pdf_add_resource(gx_device_pdf *pdev, cos_dict_t *pcd, const char *key, pdf_resource_t *pres);


/* For gdevpdfu.c */

int pdf_from_stream_to_text(gx_device_pdf *pdev);
int pdf_from_string_to_text(gx_device_pdf *pdev);
void pdf_close_text_contents(gx_device_pdf *pdev);

#endif /* gdevpdfx_INCLUDED */
