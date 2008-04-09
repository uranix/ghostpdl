/* Copyright (C) 2001-2006 Artifex Software, Inc.
   All Rights Reserved.
  
   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied, modified
   or distributed except as expressly authorized under the terms of that
   license.  Refer to licensing information at http://www.artifex.com/
   or contact Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134,
   San Rafael, CA  94903, U.S.A., +1(415)492-9861, for further information.
*/

/* $Id$ */
/* Type 42 character display operator */
#include "ghost.h"
#include "oper.h"
#include "gsmatrix.h"
#include "gspaint.h"		/* for gs_fill, gs_stroke */
#include "gspath.h"
#include "gxfixed.h"
#include "gxfont.h"
#include "gxfont42.h"
#include "gxistate.h"
#include "gxpath.h"
#include "gxtext.h"
#include "gzstate.h"		/* only for ->path */
#include "dstack.h"		/* only for systemdict */
#include "estack.h"
#include "ichar.h"
#include "icharout.h"
#include "ifont.h"		/* for font_param */
#include "igstate.h"
#include "iname.h"
#include "store.h"
#include "zchar42.h"

/* Get a Type 42 character metrics and set the cache device. */
int
zchar42_set_cache(i_ctx_t *i_ctx_p, gs_font_base *pbfont, ref *cnref, 
	    uint glyph_index, op_proc_t cont, op_proc_t *exec_cont, bool put_lsb)
{   double sbw[4];
    double w[2];
    int present;
    gs_font_type42 *pfont42 = (gs_font_type42 *)pbfont;
    int code = zchar_get_metrics(pbfont, cnref, sbw);
    gs_rect bbox;
    int vertical = gs_rootfont(igs)->WMode;
    float sbw_bbox[8];

    if (code < 0)
	return code;
    present = code;
    if (vertical) { /* for vertically-oriented metrics */
	code = pfont42->data.get_metrics(pfont42, glyph_index, 
		gs_type42_metrics_options_WMODE1_AND_BBOX, sbw_bbox);
	if (code < 0) { 
	    /* No vertical metrics in the font, compose from horizontal. 
	       Still need bbox also. */
	    code = pfont42->data.get_metrics(pfont42, glyph_index, 
		    gs_type42_metrics_options_WMODE0_AND_BBOX, sbw_bbox);
	    if (code < 0)
		return code;
	    if (present == metricsNone) {
		if (pbfont->FontType == ft_CID_TrueType) {
		    sbw[0] = sbw_bbox[2] / 2;
		    sbw[1] = pbfont->FontBBox.q.y;
		    sbw[2] = 0;
		    sbw[3] = pbfont->FontBBox.p.y - pbfont->FontBBox.q.y;
		} else {
		    sbw[0] = sbw_bbox[0];
		    sbw[1] = sbw_bbox[1];
		    sbw[2] = sbw_bbox[2];
		    sbw[3] = sbw_bbox[3];
		}
		present = metricsSideBearingAndWidth;
	    }
	} else {
	    if (present == metricsNone) {
		sbw[0] = sbw_bbox[2] / 2;
		sbw[1] = (pbfont->FontBBox.q.y + pbfont->FontBBox.p.y - sbw_bbox[3]) / 2;
		sbw[2] = sbw_bbox[2];
		sbw[3] = sbw_bbox[3];
		present = metricsSideBearingAndWidth;
	    }
	}
    } else {
	code = pfont42->data.get_metrics(pfont42, glyph_index, 
		    gs_type42_metrics_options_WMODE0_AND_BBOX, sbw_bbox);
	if (code < 0)
	    return code;
	if (present == metricsNone) {
	    sbw[0] = sbw_bbox[0];
	    sbw[1] = sbw_bbox[1];
	    sbw[2] = sbw_bbox[2];
	    sbw[3] = sbw_bbox[3];
	    present = metricsSideBearingAndWidth;
	}
    }
    w[0] = sbw[2];
    w[1] = sbw[3];
    if (!vertical) {
	sbw_bbox[6] = (sbw_bbox[6] - sbw_bbox[4]) + sbw_bbox[0];
	sbw_bbox[4] = sbw_bbox[0];
    }
    /* Note: The glyph bbox usn't useful for Dynalab fonts,
       which stretch subglyphs. Uniting with FontBBox helps.
       In same time, FontBBox with no glyph bbox
       doesn't work for 34_all.PS page 4. */
    bbox.p.x = min(sbw_bbox[4], pbfont->FontBBox.p.y);
    bbox.p.y = min(sbw_bbox[5], pbfont->FontBBox.p.y);
    bbox.q.x = max(sbw_bbox[6], pbfont->FontBBox.q.x);
    bbox.q.y = max(sbw_bbox[7], pbfont->FontBBox.q.y);
    return zchar_set_cache(i_ctx_p, pbfont, cnref,
			   (put_lsb && present == metricsSideBearingAndWidth ?
			    sbw : NULL),
			   w, &bbox,
			   cont, exec_cont,
			   gs_rootfont(igs)->WMode ? sbw : NULL);
}

/* <font> <code|name> <name> <glyph_index> .type42execchar - */
static int type42_fill(i_ctx_t *);
static int type42_stroke(i_ctx_t *);
static int
ztype42execchar(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    gs_font *pfont;
    int code = font_param(op - 3, &pfont);
    gs_font_base *const pbfont = (gs_font_base *) pfont;
    gs_text_enum_t *penum = op_show_find(i_ctx_p);
    op_proc_t cont = (pbfont->PaintType == 0 ? type42_fill : type42_stroke), exec_cont = 0;
    ref *cnref;
    uint glyph_index;

    if (code < 0)
	return code;
    if (penum == 0 ||
	(pfont->FontType != ft_TrueType &&
	 pfont->FontType != ft_CID_TrueType)
	)
	return_error(e_undefined);
    /*
     * Any reasonable implementation would execute something like
     *  1 setmiterlimit 0 setlinejoin 0 setlinecap
     * here, but apparently the Adobe implementations aren't reasonable.
     *
     * If this is a stroked font, set the stroke width.
     */
    if (pfont->PaintType)
	gs_setlinewidth(igs, pfont->StrokeWidth);
    check_estack(3);		/* for continuations */
    /*
     * Execute the definition of the character.
     */
    if (r_is_proc(op))
	return zchar_exec_char_proc(i_ctx_p);
    /*
     * The definition must be a Type 42 glyph index.
     * Note that we do not require read access: this is deliberate.
     */
    check_type(*op, t_integer);
    check_ostack(3);		/* for lsb values */
    /* Establish a current point. */
    code = gs_moveto(igs, 0.0, 0.0);
    if (code < 0)
	return code;
    cnref = op - 1;
    glyph_index = (uint)op->value.intval;
    code = zchar42_set_cache(i_ctx_p, pbfont, cnref, glyph_index, cont, &exec_cont, true);
    if (code >= 0 && exec_cont != 0)
	code = (*exec_cont)(i_ctx_p);
    return code;
}

/* Continue after a CDevProc callout. */
static int type42_finish(i_ctx_t *i_ctx_p,
			  int (*cont)(gs_state *));
static int
type42_fill(i_ctx_t *i_ctx_p)
{
    int code;
    gs_fixed_point fa = i_ctx_p->pgs->fill_adjust;

    i_ctx_p->pgs->fill_adjust.x = i_ctx_p->pgs->fill_adjust.y = -1;
    code = type42_finish(i_ctx_p, gs_fill);
    i_ctx_p->pgs->fill_adjust = fa; /* Not sure whether we need to restore it,
                                       but this isn't harmful. */
    return code;
}
static int
type42_stroke(i_ctx_t *i_ctx_p)
{
    return type42_finish(i_ctx_p, gs_stroke);
}
/* <font> <code|name> <name> <glyph_index> <sbx> <sby> %type42_{fill|stroke} - */
/* <font> <code|name> <name> <glyph_index> %type42_{fill|stroke} - */
static int
type42_finish(i_ctx_t *i_ctx_p, int (*cont) (gs_state *))
{
    os_ptr op = osp;
    gs_font *pfont;
    int code;
    gs_text_enum_t *penum = op_show_find(i_ctx_p);
    double sbxy[2];
    gs_point sbpt;
    gs_point *psbpt = 0;
    os_ptr opc = op;

    if (!r_has_type(op - 3, t_dictionary)) {
	check_op(6);
	code = num_params(op, 2, sbxy);
	if (code < 0)
	    return code;
	sbpt.x = sbxy[0];
	sbpt.y = sbxy[1];
	psbpt = &sbpt;
	opc -= 2;
    }
    check_type(*opc, t_integer);
    code = font_param(opc - 3, &pfont);
    if (code < 0)
	return code;
    if (penum == 0 || (pfont->FontType != ft_TrueType &&
		       pfont->FontType != ft_CID_TrueType)
	)
	return_error(e_undefined);

    if (!i_ctx_p->RenderTTNotdef) {
	if (r_has_type(op - 1, t_name)) {
	    ref gref;

	    name_string_ref(imemory, op - 1, &gref);

	    if (gref.tas.rsize >= 7 && strncmp(gref.value.const_bytes, ".notdef", 7) == 0) {
		pop((psbpt == 0 ? 4 : 6));
		return (*cont)(igs);
	    }
	}
    }
    /*
     * We have to disregard penum->pis and penum->path, and render to
     * the current gstate and path.  This is a design bug that we will
     * have to address someday!
     */
    code = gs_type42_append((uint)opc->value.intval, igs,
			    igs->path, penum, pfont,
			    (penum->text.operation & TEXT_DO_ANY_CHARPATH) != 0);
    if (code < 0)
	return code;
    pop((psbpt == 0 ? 4 : 6));
    return (*cont)(igs);
}

/* ------ Initialization procedure ------ */

const op_def zchar42_op_defs[] =
{
    {"4.type42execchar", ztype42execchar},
    op_def_end(0)
};
