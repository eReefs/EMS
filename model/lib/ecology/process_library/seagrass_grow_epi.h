/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/lib/ecology/process_library/seagrass_grow_epi.h
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
 *  $Id: seagrass_grow_epi.h 5846 2018-06-29 04:14:26Z riz008 $
 *
 */

#if !defined(_SEAGRASS_GROW_EPI_H)

void seagrass_grow_epi_init(eprocess* p);
void seagrass_grow_epi_postinit(eprocess* p);
void seagrass_grow_epi_destroy(eprocess* p);
void seagrass_grow_epi_precalc(eprocess* p, void* pp);
void seagrass_grow_epi_calc(eprocess* p, void* pp);
void seagrass_grow_epi_postcalc(eprocess* p, void* pp);

#define _SEAGRASS_GROW_EPI_H
#endif
