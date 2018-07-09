/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/lib/ecology/process_library/oxygen_exchange_wc.h
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
 *  $Id: oxygen_exchange_sed.h 5846 2018-06-29 04:14:26Z riz008 $
 *
 */

#if !defined(OXYGEN_EXCHANGE_SED_H)

void oxygen_exchange_sed_init(eprocess* p);
void oxygen_exchange_sed_destroy(eprocess* p);
void oxygen_exchange_sed_precalc(eprocess* p, void* pp);
void oxygen_exchange_sed_calc(eprocess* p, void* pp);
void oxygen_exchange_sed_postcalc(eprocess* p, void* pp);



#define OXYGEN_EXCHANGE_SED_H
#endif
