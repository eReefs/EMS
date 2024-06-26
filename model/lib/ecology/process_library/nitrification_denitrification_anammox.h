/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/lib/ecology/process_library/nitrification_denitrification_anammox.h
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
 *  $Id: nitrification_denitrification_anammox.h 5846 2018-06-29 04:14:26Z riz008 $
 *
 */

#if !defined(_NITRIFICATION_DENITRIFICATION_ANAMMOX_H)

void nitrification_denitrification_anammox_init(eprocess* p);
void nitrification_denitrification_anammox_postinit(eprocess* p);
void nitrification_denitrification_anammox_destroy(eprocess* p);
void nitrification_denitrification_anammox_precalc(eprocess* p, void* pp);
void nitrification_denitrification_anammox_calc(eprocess* p, void* pp);
void nitrification_denitrification_anammox_postcalc(eprocess* p, void* pp);

#define _NITRIFICATION_DENITRIFICATION_ANAMMOX_H
#endif
