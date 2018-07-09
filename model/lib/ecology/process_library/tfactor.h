/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/lib/ecology/process_library/tfactor.h
 *  
 *  Description:
 *  Process header
 *  
 *  Copyright:
 *  Copyright (c) 2018. Commonwealth Scientific and Industrial
 *  Research Organisation (CSIRO). ABN 41 687 119 230. All rights
 *  reserved. See the license file for disclaimer and full
 *  use/redistribution conditions.
 *  
 *  $Id: tfactor.h 5846 2018-06-29 04:14:26Z riz008 $
 *
 */

#if !defined(_TFACTOR_H)

void tfactor_init(eprocess* p);
void tfactor_destroy(eprocess* p);
void tfactor_precalc(eprocess* p, void* pp);

#define _TFACTOR_H
#endif
