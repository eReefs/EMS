/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/lib/ecology/process_library/tfactor.c
 *  
 *  Description:
 *  Process implementation
 *  
 *  Copyright:
 *  Copyright (c) 2018. Commonwealth Scientific and Industrial
 *  Research Organisation (CSIRO). ABN 41 687 119 230. All rights
 *  reserved. See the license file for disclaimer and full
 *  use/redistribution conditions.
 *  
 *  $Id: tfactor.c 6549 2020-05-10 23:12:28Z bai155 $
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "ecology_internal.h"
#include "eprocess.h"
#include "cell.h"
#include "column.h"
#include "utils.h"
#include "tfactor.h"

typedef struct {
    /*
     * parameters
     */
    double Q10;
    double Tref;

    /*
     * common variables
     */
    int Tfactor_i;

    /*
     * tracers
     */
    int temp_i;
} workspace;

void tfactor_init(eprocess* p)
{
    ecology* e = p->ecology;
    workspace* ws = malloc(sizeof(workspace));

    p->workspace = ws;

    /*
     * parameters
     */

    ws->Q10 = try_parameter_value(e, "Q10");
    if (isnan(ws->Q10)){
      ws->Q10 = 2.0;
      eco_write_setup(e,"Code default of Q10 = %e \n",ws->Q10);
    }

    ws->Tref = try_parameter_value(e, "Tref");
    if (isnan(ws->Tref)){
      ws->Tref = 20.0;
      eco_write_setup(e,"Code default of Tref = %e \n",ws->Tref);
    }

    /*
     * common variables
     */
    ws->Tfactor_i = find_index_or_add(e->cv_cell, "Tfactor", e);

    /*
     * tracers
     */
    ws->temp_i = e->find_index(e->tracers, "temp", e);
}

void tfactor_destroy(eprocess* p)
{
    free(p->workspace);
}

void tfactor_precalc(eprocess* p, void* pp)
{
    workspace* ws = p->workspace;
    cell* c = (cell*) pp;
    double T = c->y[ws->temp_i];

    c->cv[ws->Tfactor_i] = pow(ws->Q10, (T - ws->Tref) / 10.0);
}
