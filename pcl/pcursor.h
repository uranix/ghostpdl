/* Copyright (C) 1996, 1997, 1998 Aladdin Enterprises.  All rights
   reserved.  Unauthorized use, copying, and/or distribution
   prohibited.  */

/* pcursor.h - interface to the PCL cursor positioning code */

#ifndef pcursor_INCLUDED
#define pcursor_INCLUDED

#include "gx.h"
#include "pcstate.h"
#include "pcommand.h"

/*
 * Default values for HMI and VMI. The use of -1 for HMI indicates "not set".
 */
#define HMI_DEFAULT -1L
#define VMI_DEFAULT inch2coord(8.0 / 48)

/*
 * Horizontal and vertical cursor movement routines. x and y are in
 * centipoints.
 *
 * NB: absolute vertical positions passed to pcl_set_cap_y are in full
 *     page direction space, not the "pseudo" page directions space in
 *     which the pcs->cap is maintained. If passing coordinates in the
 *     latter space, BE SURE TO SUBTRACT THE CURRENT TOP MARGIN.
 */
void pcl_set_cap_x(P4(
    pcl_state_t *   pcs,
    coord           x,              /* position or distance */
    bool            relative,       /* x is distance (else position) */
    bool            use_margins     /* apply text margins */
));

int pcl_set_cap_y(P5(
    pcl_state_t *   pcs,
    coord           y,                  /* position or distance */
    bool            relative,           /* y is distance (else position) */
    bool            use_margins,        /* apply text margins */
    bool            by_row              /* LF, half LF, or by row */
));

void    pcl_do_CR(P1(pcl_state_t * pcs));
int     pcl_do_FF(P1(pcl_state_t * pcs));
int     pcl_do_LF(P1(pcl_state_t * pcs));
void    pcl_home_cursor(P1(pcl_state_t * pcs));

/* Get the HMI.  This may require recomputing it from the font. */
coord   pcl_updated_hmi(P1(pcl_state_t * pcs));

#define pcl_hmi(pcs)                                                    \
    ((pcs)->hmi_cp == HMI_DEFAULT ? pcl_updated_hmi(pcs) : (pcs)->hmi_cp)

extern  const pcl_init_t    pcursor_init;

#endif			/* pcursor_INCLUDED */
