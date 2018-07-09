/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/hd-us/ecology/ecology.c
 *  
 *  Description:
 *  Interfaces the ecology library to SHOC
 *  
 *  Copyright:
 *  Copyright (c) 2018. Commonwealth Scientific and Industrial
 *  Research Organisation (CSIRO). ABN 41 687 119 230. All rights
 *  reserved. See the license file for disclaimer and full
 *  use/redistribution conditions.
 *  
 *  $Id: ecology.c 5873 2018-07-06 07:23:48Z riz008 $
 *
 */

#include <stdlib.h>
#include "hd.h"


#if defined(HAVE_ECOLOGY_MODULE)

#include <assert.h>
#include "einterface.h"
#include "ecology_tracer_defaults.h"

#define ECO_MAXNUMARGS 300

double sinterface_get_svel(void* model,char* name);

/*
 * FR: These should be moved into the ecology library
 */

/* Ecology 3D tracers */
const char *ECONAME3D[][2] = {
  {"Age",         "Tracer age"},
  {"source",      "Ageing zone"},
  {"PhyL_N",      "Large Phytoplankton N"},
  {"PhyL_NR",      "Large Phytoplankton N reserve"},
  {"PhyL_PR",      "Large Phytoplankton P reserve"},
  {"PhyL_I",      "Large Phytoplankton I reserve"},
  {"PhyL_Chl",      "Large Phytoplankton chlorophyll"},
  {"PhyS_N",      "Small Phytoplankton N"},
  {"PhyS_NR",      "Small Phytoplankton N reserve"},
  {"PhyS_PR",      "Small Phytoplankton P reserve"},
  {"PhyS_I",      "Small Phytoplankton I reserve"},
  {"PhyS_Chl",      "Small Phytoplankton chlorophyll"},
  {"PhyL_sv",      "diatoms settling velocity"},
  {"MPB_N",       "Microphytobenthos N"},
  {"MPB_NR",       "Microphytobenthos N reserve"},
  {"MPB_PR",       "Microphytobenthos P reserve"},
  {"MPB_I",       "Microphytobenthos light reserve"},
  {"MPB_Chl",       "Microphytobenthos chlorophyll"},
  {"Zenith",       "Solar zenith"},
  {"ZooL_N",      "Large Zooplankton N"},
  {"ZooS_N",      "Small Zooplankton N"},
  {"PhyD_N",      "Dinoflagellate N"},
  {"PhyD_C",      "Dinoflagellate C"},
  {"Tricho_N",       "Trichodesmium Nitrogen"},
  {"Tricho_NR",       "Trichodesmium N reserve"},
  {"Tricho_PR",       "Trichodesmium P reserve"},
  {"Tricho_I",       "Trichodesmium I reserve"},
  {"Tricho_Chl",       "Trichodesmium chlorophyll"},
  {"Tricho_sv",       "Trichodesmium settling velocity"},
  {"ZooL_sv",       "Large Zooplankton settling velocity"},
  {"Phy_L_N2",    "Lyngbya"},
  {"NH4",         "Ammonia"},
  {"NO3",         "Nitrate"},
  {"DIP",         "Dissolved Inorganic Phosphorus"},
  {"DIC",         "Dissolved Inorganic Carbon"},
  {"DOR_C",       "Dissolved Organic Carbon"},
  {"DOR_N",       "Dissolved Organic Nitrogen"},
  {"DOR_P",       "Dissolved Organic Phosphorus"},
  {"PIP",         "Particulate Inorganic Phosphorus"},
  {"PIPI",        "Immobilised Particulate Inorganic Phosphorus"},
  {"PIPF",        "Flocculated Particulate Inorganic Phosphorus"},
  {"DetR_C",      "Refractory Detrital Carbon"},
  {"DetR_N",      "Refractory Detrital Nitrogen"},
  {"DetR_P",      "Refractory Detrital Phosphorus"},
  {"DetPL_N",     "Labile Detrital Nitrogen Plank"},
  {"DetBL_N",     "Labile Detrital Nitrogen Benthic"},
  {"Oxygen",      "Dissolved Oxygen"},
  {"Oxy_sat",     "Oxygen saturation percent"},
  {"Light",       "Av. light in layer"},
  {"PAR",         "Av. PAR in layer"},
  {"K_heat",      "Vertical attenuation of heat"},
  {"Kd",          "Attenuation coefficient in layer"},
  {"TN",          "Total N"},
  {"TP",          "Total P"},
  {"TC",          "Total C"},
  {"EFI",         "Ecology Fine Inorganics"},
  {"DIN",         "Dissolved Inorganic Nitrogen"},
  {"Chl_a",       "Total Chlorophyll"},
  {"Chl_a_sum",   "Total Chlorophyll"},
  {"PhyL_N_pr",   "Large Phytoplankton net production"},
  {"PhyS_N_pr",   "Small Phytoplankton net production"},
  {"ZooL_N_rm",   "Large Zooplankton removal rate from Large Phytoplankton"},
  {"ZooS_N_rm",   "Small Zooplankton removal from Small Phytoplankton"},
  {"Den_fl",      "Denitrfication flux"},
  {"Den_eff",     "Denitrification effectiveness"},
  {"NH4_pr",      "Ammonia production"},
  {"MPB_N_pr",    "Microphytobenthos net production"},
  {"Tricho_N_pr", "Trichodesmium net production"},
  {"PhyL_N_gr",   "Large Phytoplankton growth rate"},
  {"PhyS_N_gr",   "Small Phytoplankton growth rate"},
  {"BOD",         "Biochemical Oxygen Demand"},
  {"COD",         "Chemical Oxygen Demand"},
  {"MPB_N_gr",    "Microphytobenthos growth rate"},
  {"ZooL_N_gr",   "Large Zooplankton growth rate"},
  {"ZooS_N_gr",   "Small Zooplankton growth rate"},
  {"PhyD_N_gr",   "Dinoflagellate growth rate"},
  {"Tricho_N_gr", "Trichodesmium growth rate"},
  {"Oxy_pr",      "Oxygen production"},
  {"PhyD_N_pr",   "Dinoflagellate net production"},
  {"dens",        "Density"},
  {"alk",         "Total alkalinity"},
  {"Nfix",        "N2 fixation"},
  {"PH",          "PH"},
  {"CO32",        "Carbonate"},
  {"HCO3",        "Bicarbonate"},
  {"CO2_starair", "Atm CO2 (xco2*ff*atmpres)"}, 
  {"CO2_star",    "Ocean surface aquaeous CO2 concentration"},
  {"dco2star",    "Sea-air Delta CO2"},
  {"pco2surf",    "Oceanic pCO2"},
  {"dpCO2",       "Sea-air Delta pCO2"},
  {"omega_ca",    "Calcite saturation state"},
  {"omega_ar",    "Aragonite saturation state"},
  {"CO2_flux",    "Sea-air CO2 flux"},
  {"O2_flux",     "Sea-air O2 flux"},
  {"at_440",      "Absorption at 440 nm"},
  {"bt_550",      "Scattering at 550 nm"},  
  {"Kd_490",      "Vertical attenuation at 490 nm"},
  {"Phy_L_N2_fix","N2 fix rate Lyngbya"}
};
const int NUM_ECO_VARS_3D = ((int)(sizeof(ECONAME3D)/(2*sizeof(char*))));

/* Ecology 2D tracers */
const char *ECONAME2D[][2] = {
  {"Epilight",    "Light intensity above epibenthos"},
  {"Epilightatt", "Light attenuation in epibenthos"}, 
  {"EpiPAR",      "PAR light below epibenthos"},
  {"EpiPAR_sg",   "PAR light above seagrass"},
  {"EpiBOD",      "Total biochemical oxygen demand in epibenthos"},
  {"SG_N",        "Seagrass N"},
  {"SGH_N",       "Halophila N"},
  {"SGROOT_N",    "Seagrass root N"},
  {"SGHROOT_N",   "Halophila root N"},
  {"CS_N",        "Coral symbiont N"},
  {"CS_Chl",      "Coral symbiont Chl"},
  {"CH_N",        "Coral host N"},
  {"MA_N",        "Macroalgae N"},
  {"EpiTN",       "Total N in epibenthos"},
  {"EpiTP",       "Total P in epibenthos"},
  {"EpiTC",       "Total C in epibenthos"},
  {"SG_N_pr",     "Seagrass net production"},
  {"SGH_N_pr",    "Halophila net production"},
  {"MA_N_pr",     "Macroalgae net production"},
  {"SG_N_gr",     "Seagrass growth rate"},
  {"SGH_N_gr",    "Halophila growth rate"},
  {"MA_N_gr",     "Macroalgae growth rate"},
  {"EpiOxy_pr",   "Net Oxygen Production in epibenthos"},
  {"Coral_IN_up", "Coral inorganic uptake"},
  {"Coral_ON_up", "Coral organic uptake"},
  {"Gnet",        "Coral net calcification"},
  {"mucus",       "Coral mucus production"},
  {"CS_N_pr",     "Coral symbiont net production"},
  {"CH_N_pr",     "Coral host net production"}

};
const int NUM_ECO_VARS_2D = ((int)(sizeof(ECONAME2D)/(2*sizeof(char*))));

static void *private_data_copy_eco(void *src);

static int e_ntr;
static int tr_map[MAXNUMVARS];
static int sed_map[MAXNUMVARS];
static int e_nepi;
static int epi_map[MAXNUMVARS];

extern int i_tracername_exists(void* model, char*name);
extern int i_tracername_exists_2d(void* model, char*name);
extern int i_tracername_exists_sed(void* model, char*name);

static void einterface_tracermap(void* model, ecology *e, int ntr)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  char trname[MAXSTRLEN];
  int tn,n,m;

  n = m = 0;
  for(tn=0; tn<ntr; tn++) {
    strcpy(trname,ecology_gettracername(e, tn));
    /* Water column */
    if((tr_map[n] = tracer_find_index(trname, wincon->ntr,
              wincon->trinfo_3d)) >= 0) {
      n++;
    }
    else
      hd_quit("ecology interface: Can't find type WATER tracer '%s' in parameter file.\n", trname);
    /* Sediments */
    if((sed_map[m] = tracer_find_index(trname, wincon->nsed,
              wincon->trinfo_sed)) >= 0) {
      m++;
    }
    else
      hd_quit("ecology interface: Can't find type SEDIMENT tracer '%s' in parameter file.\n", trname);
  }
}

static void einterface_epimap(void* model, ecology *e, int ntr)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  char trname[MAXSTRLEN];
  int tn,n;

  n = 0;
  for(tn=0; tn<ntr; tn++) {
    strcpy(trname,ecology_getepiname(e, tn));
    if((epi_map[n] = tracer_find_index(trname, wincon->ntrS,
               wincon->trinfo_2d)) >= 0) {
      n++;
    }
    else
      hd_warn("ecology : Can't find type BENTHIC tracer '%s' in parameter file.\n", trname);
  }
}

int einterface_get_num_rsr_tracers(void* model)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  int tn,n;

  n = 0;
  for(tn=0; tn<wincon->ntrS; tn++) {
    char *trname = wincon->trinfo_2d[tn].name;
    if (strncmp(trname, "R_", 2) == 0)
      n++;
  }
  return(n);
}

void einterface_get_rsr_tracers(void* model, int *rtns)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  int tn,n;

  n = 0;
  for(tn=0; tn<wincon->ntrS; tn++) {
    char *trname = wincon->trinfo_2d[tn].name;
    if (strncmp(trname, "R_", 2) == 0)
      rtns[n++] = tn;
  }
}

int einterface_getntracers(void* model)
{
    return 0;
}

int einterface_get_eco_flag(void* model, char* name)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  tracer_info_t *tr = i_get_tracer(model, name);
  trinfo_priv_eco_t *data = tr->private_data[TR_PRV_DATA_ECO];
  int flag = ECO_NONE;
    
  if (data)
    flag = data->flag;

  return flag;
}

int einterface_gettracerdiagnflag(void* model, char* name)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  int index = tracer_find_index(name, wincon->ntr, wincon->trinfo_3d);
  return wincon->trinfo_3d[index].diagn;
}

int einterface_gettracerparticflag(void* model, char* name)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  int index = tracer_find_index(name, wincon->ntr, wincon->trinfo_3d);
  return wincon->trinfo_3d[index].partic;
}

double einterface_gettracersvel(void* model, char* name)
{
  return(sinterface_get_svel(model,name));

}

char* einterface_gettracername(void* model, int i)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  if(i >= 0 && i < wincon->ntr)
    return wincon->trinfo_3d[i].name;

  return NULL;
}

char* einterface_get2Dtracername(void* model, int i)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  if(i >= 0 && i < wincon->ntrS)
    return wincon->trinfo_2d[i].name;

  return NULL;
}


int einterface_tracername_exists(void* model, char*name)
{
	/* use the generic function in ginterface */
	return i_tracername_exists(model, name);
}


int einterface_tracername_exists_epi(void* model, char*name)
{
	/* use the generic function in ginterface */
	return i_tracername_exists_2d(model, name);
}

void einterface_get_ij(void* model, int col, int *ij)
{
  geometry_t *window = (geometry_t *)model;
  /* Convert column number to 2D sparse coord in host */
  int c  = window->wincon->s2[col+1];
  int cs = window->m2d[c];

  ij[0] = window->s2i[cs];
  ij[1] = window->s2j[cs];

}


int einterface_getnepis(void* model)
{
    return 0;
}


char* einterface_getepiname(void* model, int i)
{
  return einterface_gettracername(model,i);
  /*  return NULL; */
}


int einterface_getepidiagnflag(void* model, char* name)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  int index = tracer_find_index(name, wincon->ntrS, wincon->trinfo_2d);
  return wincon->trinfo_2d[index].diagn;
}


tracer_info_t* einterface_getepiinfo(void* model, int* n)
{
    return NULL;
}


double einterface_getmodeltime(void* model)
{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  return windat->t;
}


int  einterface_getnumberwclayers(void* model)
{
  return ((geometry_t*) model)->nz;
}


int  einterface_getnumbersedlayers(void* model)
{
  return ((geometry_t*) model)->sednz;
}

/*
 * Beware this has to be called after setup is complete
 * Use the max_columns function if you need to pre-allocate stuff
 */
int  einterface_getnumbercolumns(void* model)
{
  geometry_t *window = (geometry_t *)model;
  process_cell_mask(window, window->cbgc, window->ncbgc);
  return window->wincon->vca2;
}

int  einterface_get_max_numbercolumns(void* model)
{
  geometry_t *window = (geometry_t *)model;
  return window->b2_t;
}

int einterface_getwctopk(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  int c  = window->wincon->s2[b+1];
  int c2 = window->m2d[c];
  int cc = window->c2cc[c2];
  return window->s2k[window->nsur_t[cc]];
}

int einterface_getwcbotk(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  int c  = window->wincon->s2[b+1];
  int c2 = window->m2d[c];
  int cc = window->c2cc[c2];
  return window->s2k[window->bot_t[cc]];
}

/* Returns cell centred depth */
double einterface_getcellz(void *model, int b, int k)
{
  geometry_t *window = (geometry_t *)model;
  int c  = window->wincon->s2[b+1];
  int c2 = window->m2d[c];
  int cc = window->c2cc[c2];
  int cs = window->nsur_t[cc];
  int cb = window->bot_t[cc];
  int kk = window->s2k[cb];
  
  /* Search for the correct level */
  for (c = cb; c != cs; c = window->zp1[c])
    if (k == kk++)
      break;
  
  return(window->cellz[c]);
}


int einterface_isboundarycolumn(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  int c = window->wincon->s2[b+1];
  
  /* Exclude process points if required */
  if (window->wincon->c2[window->m2d[c]]) return 1;

  /* Do ecology on boundary cells */
  // FR (11/09) : By definition now that we've changed over to vca2,
  //              we wont ever have boundary or dry columns
  return 0;
  /* Don't do ecology on boundary cells */
  /*return b + 1 > window->v2_t ? 1 : 0;*/
}


int einterface_getsedtopk(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  if (window != NULL)
    return window->sednz - 1;
  else {
    // This happens for ecology_pre_build
    return 0;
  }
}


int einterface_getsedbotk(void *model, int b)
{
  return 0;
}


double *einterface_getwccellthicknesses(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  int cs2  = window->wincon->s2[b+1];
  int c2 = window->m2d[cs2];
  int cc = window->c2cc[c2];
  int cs = window->nsur_t[cc];
  int cb = window->bot_t[cc];
  double *dz = calloc(window->nz, sizeof(double));
  int c, k = window->s2k[cb];
  assert(window->nz > 0);

  for (c = cb; c != cs; c = window->zp1[c]) {
    dz[k] = wincon->dz[c];
    k++;
  }
  dz[k] = wincon->dz[c];
  return dz;
}


double *einterface_getsedcellthicknesses(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  int nz = window->sednz;
  double *dz = calloc(nz, sizeof(double));
  int c = window->wincon->s2[b + 1];
  int c2 = window->m2d[c];
  int k;
  assert(nz > 0);

  for (k = 0; k < nz; ++k) {
    dz[k] = window->gridz_sed[k + 1][c2] - window->gridz_sed[k][c2];
  }
  return dz;
}

double einterface_botstress(void *model, int b)

/* Added for use by benthic plants */

{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  int c  = window->wincon->s2[b + 1];
  int c2 = window->m2d[c];

  if (windat->tau_bm != NULL) 
    return windat->tau_bm[c2];
  else
    return 0.0;
}

double einterface_get_windspeed(void *model, int b)

/* Added to calculate gas exchange */

{
   geometry_t *window = (geometry_t *)model;
   int c  = window->wincon->s2[b+1];
   int c2 = window->m2d[c];
   return window->windat->windspeed[c2];
}

double einterface_getlighttop(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  int c  = window->wincon->s2[b + 1];
  int c2 = window->m2d[c];

  if (windat->light != NULL) 
    return windat->light[c2];
  else
    return 0.0;
}

double *einterface_getporosity(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  window_t *windat = window->windat;
  double por_def = 0.4;
  double *por = wincon->sd1;
  int nz = window->sednz;
  int ntr = windat->nsed;
  int c = wincon->s2[b+1];
  int c2=window->m2d[c];
  int n, k;
  for(k = 0; k < nz; k++)
    por[k] = por_def;

#if defined(HAVE_SEDIMENT_MODULE)
  if (wincon->do_sed) {
    for(n = 0; n < ntr; n++) {
      // FR : This is to make sure we are backwards compatible
      if( (strcmp(wincon->trinfo_sed[n].name,"por_sed") == 0) ||
	  (strcmp(wincon->trinfo_sed[n].name,"porosity") == 0)) {
        for(k = 0; k < nz; k++)
          por[k] = windat->tr_sed[n][k][c2];
	break; // end for loop
      }
    }
  }
#endif

  return por;
}


double einterface_geterosionrate(void *model, int b);

double einterface_getustrcw(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  int c = window->wincon->s2[b + 1];
  int c2 = window->m2d[c];
  return windat->ustrcw[c2];
}


double**  einterface_getwctracers(void* model, int b)
{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  int nz = window->nz;
  int ntr = e_ntr;
  double **wctr = (double **)calloc(ntr * nz, sizeof(double*));
  int cs2 = window->wincon->s2[b+1];
  int c2 = window->m2d[cs2];
  int cc = window->c2cc[c2];
  int cs = window->nsur_t[cc];
  int cb = window->bot_t[cc];
  int c, k = window->s2k[cb], n;

  /*
   * Loop from the bottom cell up
   */
  for (c = cb; c != cs; c = window->zp1[c]) {
    for (n = 0; n < ntr; ++n) {
      wctr[k * ntr + n] = &windat->tr_wc[tr_map[n]][c];
    }
    k++;
  }
  // The surface cell
  for (n = 0; n < ntr; ++n) {
    wctr[k * ntr + n] = &windat->tr_wc[tr_map[n]][c];
  }

  return wctr;
}


double** einterface_getsedtracers(void* model, int b)
{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  int nz = window->sednz;
  int ntr = e_ntr;
  double **sedtr = (double **)calloc(ntr * nz, sizeof(double*));
  int c = window->wincon->s2[b + 1];
  int c2=window->m2d[c];
  int k, n;

  for (k = 0; k < nz; ++k)
    for (n = 0; n < ntr; ++n) {
      sedtr[k * ntr + n] = &windat->tr_sed[sed_map[n]][k][c2];
    }
  return sedtr;
}


double **einterface_getepivars(void *model, int b)
{
  geometry_t *window = (geometry_t *)model;
  window_t *windat = window->windat;
  int nepi = e_nepi;
  double **epivar = (double **)calloc(nepi, sizeof(double*));
  int c = window->wincon->s2[b + 1];
  int c2 = window->m2d[c];
  int n;

  for (n = 0; n < nepi; ++n) {
    epivar[n] = &windat->tr_wcS[epi_map[n]][c2];
  }
  return epivar;
}


quitfntype einterface_getquitfn(void)
{
  return (quitfntype) hd_quit_and_dump;
}


/*UR added 2/2006 */
/**
 * retrieve the cell area for this column
 *
 * @parma hmodel - the hydrodynamic host model
 * @param b - the column index
 * @return the cell area in m2
 */
double einterface_cellarea(void* hmodel, int b)
{
    geometry_t* window = (geometry_t*) hmodel;
    int c  = window->wincon->s2[b+1];
    int c2 = window->m2d[c];
    double v = window->cellarea[c2];
    return v;
}


void einterface_ecologyinit(void *model, void *_e)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  window_t *windat = window->windat;
  ecology* e = (ecology*) _e;

  /*
   * move to ecology_step
   */
  /*
    if (wincon->ecodt < windat->dt) {
    hd_warn
    ("Ecomodel timestep = %f < physical timestep = %f;\nsetting ecomodel timestep = physical timestep.\n",
    wincon->ecodt, windat->dt);
    wincon->ecodt = windat->dt;
    }
    
    wincon->eco_timestep_ratio = wincon->ecodt / windat->dt;
    if (wincon->ecodt != windat->dt * wincon->eco_timestep_ratio) {
    wincon->ecodt = windat->dt * wincon->eco_timestep_ratio;
    hd_warn("Set ecomodel timestep to %f seconds.\n", wincon->ecodt);
    }
  */

  /* Ecology requires water column tracers to be both of type WATER */
  /* and SEDIMENT and requires a 1:1 corresponance between the */
  /* indicies for the water column and sediment components. This */
  /* requires an index map to be established. */
  /* Get the number of water column tracers used by ecology. */
  e_ntr = ecology_getntracers(e);
  /* Get the water column tracers required by ecology and the tracer */
  /* index map. */
  einterface_tracermap(window, e, e_ntr);

  /* Get the number of water column tracers used by ecology */
  e_nepi = ecology_getnepis(e);
  /* Get the water column tracers required by ecology and the tracer */
  /* index map. */
  einterface_epimap(window, e, e_nepi);
}


int einterface_getverbosity(void *model)
{
  extern int debug;

  return debug;
}


char *einterface_gettimeunits(void *model)
{
  geometry_t *window = (geometry_t *)model;
  win_priv_t *wincon = window->wincon;
  return wincon->timeunit;
}

int einterface_transport_mode(void)
{
  return(master->runmode & TRANS);
}

/*
 * Calculates the Zenith using the library function
 */
double einterface_calc_zenith(void *model, double t, int b)
{
  double lat;
  double elev;
  geometry_t *window = (geometry_t *)model;
  int c  = window->wincon->s2[b+1];
  int c2 = window->m2d[c];
  double ang = 7.29e-5;  /* Earth's angular velocity (s-1) */
  char *tunit = window->wincon->timeunit;
  char *ounit = master->params->output_tunit;

  /* the master coriolis index was wrong in the earlier version */
  lat = asin(window->wincon->coriolis[c2] / (2.0 * ang));

  /* Call the library function to calculate the solar elevation */
  elev = calc_solar_elevation(ounit, tunit, t, lat, NULL);

  /* zenith */
  return ( (PI/2.0) - elev);
}

/*
 * Returns the window number
 */
int einterface_get_win_num(void *model)
{
  return(((geometry_t *)model)->wn);
}

/*
 * Logs the ecology error and optionally quits if error function is
 * set to none
 */
void einterface_log_error(ecology *e, void *model, int b)
{
  geometry_t *window = (geometry_t*)model;
  window_t   *windat = window->windat;
  win_priv_t *wincon = window->wincon;
  int c  = wincon->s2[b+1];
  int c2 = window->m2d[c];
  int eco_nstep = eco_get_nstep(e);

  hd_warn("Ecology library error @ (%d %d) : %5.2f days\n", 
	  window->s2i[c2], window->s2j[c2], windat->days);
  windat->ecoerr[c2] = 100.0 * 
    (eco_nstep * 0.01 * windat->ecoerr[c2] + 1.0) / (eco_nstep+1);
  
  if (wincon->gint_errfcn == LWARN)
    hd_warn(wincon->gint_error[b]);
  if (wincon->gint_errfcn == LFATAL)
    hd_quit(wincon->gint_error[b]);
}

/*
 * Sanity check (called from ecology_pre_build) to check for
 * duplicates in the tracer default list. The stringtable_add will
 * error out when the same name is added twice
 */
void einterface_check_default_tracers(void)
{
  stringtable *tbl;
  int i;

  tbl = stringtable_create("Tracer-defaults-ECONAME3D");
  for (i = 0; i < NUM_ECO_VARS_3D; i++)
    stringtable_add(tbl, (char*)ECONAME3D[i][0], i);
  stringtable_destroy(tbl);

  tbl = stringtable_create("Tracer-defaults-ECONAME2D");
  for (i = 0; i < NUM_ECO_VARS_2D; i++)
    stringtable_add(tbl, (char*)ECONAME2D[i][0], i);
  stringtable_destroy(tbl);
}

/*-------------------------------------------------------------------*/
/* Ecology step                                                      */
/*-------------------------------------------------------------------*/
void eco_step(geometry_t *window)
{
  win_priv_t *wincon = window->wincon;
  window_t   *windat = window->windat;

  /*-----------------------------------------------------------------*/
  /* Do the ecology if required */
#if defined(HAVE_ECOLOGY_MODULE)
  if (wincon->do_eco) {
    /*
     * Redo eco_timestep_ratio calculation as tratio may have altered
     * windat->dt in hd_step[_trans]
     */
    if (wincon->ecodt < windat->dt) {
      hd_warn
	("Ecomodel timestep = %f < physical timestep = %f;\nsetting ecomodel timestep = physical timestep.\n",
	 wincon->ecodt, windat->dt);
      wincon->ecodt = windat->dt;
    }
    
    wincon->eco_timestep_ratio = wincon->ecodt / windat->dt;
    /* Trike introduces minor rounding errors due to timeunit conversions */
    if (fabs(wincon->ecodt - (windat->dt * wincon->eco_timestep_ratio)) > 1e-3)
      hd_quit("Inconsistency in setting ecology time step as a multiple of physical timestep: eco_dt=%f, phys_dt=%f\n", wincon->ecodt, windat->dt);

    /* Run ecology if its the right time */
    if (!(windat->nstep % wincon->eco_timestep_ratio)) {
      TIMING_SET;
      ecology_step(wincon->e, wincon->ecodt);
      TIMING_DUMP_WIN(3, "   eco_step", window->wn);
    }
  }
#endif

}

/* END eco_step()                                                    */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Sets the ecology tracers standard default attributes              */
/*-------------------------------------------------------------------*/
void eco_set_tracer_defaults(tracer_info_t *tracer, char *trname,
			     char *defname, ecology *e)
{
  int i;
  struct {
    char *name;
    char *description;
    void (*init) (tracer_info_t *tracer, char *trname, ecology *e);
  } def_list[] = {
    {"standard","Standard ecology values",  eco_defaults_std},
    {"estuary", "Estuarine ecology values", eco_defaults_est},
    {NULL, NULL, NULL}
  };

  void (*init) (tracer_info_t *tracer, char *trname, ecology *e)= NULL;

  if (defname != NULL) {
    /* Search for the eco defaults scheme name in the above table */
    for (i = 0; (init == NULL) && def_list[i].name; ++i) {
      if (strcasecmp(defname, def_list[i].name) == 0) {
        init = def_list[i].init;
      }
    }
  }
  if (init != NULL) {
    init(tracer, trname, e);
  }
}

/* END set_eco_defaults()                                            */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Initialised ecology private data                                  */
/*-------------------------------------------------------------------*/
trinfo_priv_eco_t *get_private_data_eco(tracer_info_t *tr)
{
  trinfo_priv_eco_t *data = tr->private_data[TR_PRV_DATA_ECO];

  /* Allocate memory, if needed */
  if (data == NULL) {
    data = (trinfo_priv_eco_t *)malloc(sizeof(trinfo_priv_eco_t));
    /*
     * Useful for debugging in case the wrong pointer happens 
     * to be passed in 
     */
    data->type = PTR_BGC;
    
    /* Set data and copy function */
    tr->private_data[TR_PRV_DATA_ECO]      = data;
    tr->private_data_copy[TR_PRV_DATA_ECO] = private_data_copy_eco;
  
    sprintf(data->name, "%c", '\0');
    data->obc  = NOGRAD;
    data->flag = 0;
  }
  
  return(data);
}


/*-------------------------------------------------------------------*/
/* Copies ecology private data                                       */
/*-------------------------------------------------------------------*/
static void *private_data_copy_eco(void *src)
{
  void *dest = NULL;

  if (src) {
    dest = (trinfo_priv_eco_t *)malloc(sizeof(trinfo_priv_eco_t));
    memcpy(dest, src, sizeof(trinfo_priv_eco_t));
  }
  return(dest);
}

/* END private_data_copy_eco()                                       */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Initialised ecology private data                                  */
/*-------------------------------------------------------------------*/
void init_eco_private_data(trinfo_priv_eco_t *data)
{
  data->flag = 0;
}

/* END init_eco_private_data()                                       */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Searches through the tracer list and returns the tracer type      */
/*-------------------------------------------------------------------*/
int get_eco_var_type(char *trname, char *eco_defs)
{
  tracer_info_t tr;
  memset(&tr, 0, sizeof(tracer_info_t));
  eco_set_tracer_defaults(&tr, trname, eco_defs, NULL);
  if (!tr.type)
    hd_quit("ecology:get_eco_var_type: Tracer '%s' is not listed in '%s'\n",
	    trname, eco_defs);

  return(tr.type);
}

/*-------------------------------------------------------------------*/
/* Checks if a trcer is a valid ecology tracer                       */
/*-------------------------------------------------------------------*/
int is_eco_var(char *trname)
{
  int i;

  for (i = 0; i < NUM_ECO_VARS_3D; i++)
    if (strcmp(trname, ECONAME3D[i][0]) == 0) {
      return(1);
    }
  for (i = 0; i < NUM_ECO_VARS_2D; i++)
    if (strcmp(trname, ECONAME2D[i][0]) == 0) {
      return(1);
    }
  // Return false
  return (0);
}

/* END is_eco_tracer()                                               */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Reads the ecology related attributes                              */
/*-------------------------------------------------------------------*/
void set_eco_atts(tracer_info_t *tr, FILE *fp, char *keyname)
{

}

/* END set_eco_atts()                                                */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Perform any custom adjustments to the ecology tracer info         */
/*-------------------------------------------------------------------*/
void eco_tracer_custom(master_t *master)
{
  tracer_info_t *tr2d =  master->trinfo_2d;
  tracer_info_t *tr3d =  master->trinfo_3d;
  tracer_info_t *trsed =  master->trinfo_sed;
  char buf[MAXSTRLEN];
  int n, m;

}

/* END eco_tracer_custom()                                           */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Reads the ecology related attributes                              */
/*-------------------------------------------------------------------*/
void eco_read_tr_atts(tracer_info_t *tr, FILE *fp, char *keyname)
{
  trinfo_priv_eco_t *data;
  char buf[MAXSTRLEN], key[MAXSTRLEN];
  int i_val;

  /* Check for a ecology variable */
  if ( !is_eco_var(tr->name) ) return;

  // Allocate memory, if needed
  data = get_private_data_eco(tr);

  sprintf(key, "%s.eco_flag", keyname);
  if (prm_read_int(fp, key, &i_val))
    data->flag = i_val;
}


/*-------------------------------------------------------------------*/
/* Writes the attributes for ecology tracers                         */
/*-------------------------------------------------------------------*/
void eco_write_tr_atts(tracer_info_t *tr, FILE *fp, int n)
{
  trinfo_priv_eco_t *data = tr->private_data[TR_PRV_DATA_ECO];

  if (data != NULL && data->type == PTR_BGC) {
    fprintf(fp, "TRACER%1.1d.eco_flag            %d\n", n, data->flag);
  }
}
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Counts the number of valid ecology classes                        */
/*-------------------------------------------------------------------*/
int count_eco_3d(char *eco_vars)
{
  char *vars[ECO_MAXNUMARGS], buf[MAXSTRLEN];
  int m, n, i, nvars = 0;

  if (eco_vars == NULL || strlen(eco_vars) == 0)
    return(0);

  strcpy(buf, eco_vars);
  m = parseline(buf, vars, ECO_MAXNUMARGS);
  for (n = 0; n < m; n++) {
    for (i = 0; i < NUM_ECO_VARS_3D; i++)
      if (strcmp(vars[n], ECONAME3D[i][0]) == 0) {
	nvars++;
      }
  }
  if (nvars == 0) {
    hd_quit("No valid ecology 3D tracers found.\n");
  }
  return(nvars);
}

int count_eco_2d(char *eco_vars)
{
  char *vars[ECO_MAXNUMARGS], buf[MAXSTRLEN];
  int m, n, i, nvars = 0;

  if (eco_vars == NULL || strlen(eco_vars) == 0)
    return(0);

  strcpy(buf, eco_vars);
  m = parseline(buf, vars, ECO_MAXNUMARGS);
  for (n = 0; n < m; n++) {
    for (i = 0; i < NUM_ECO_VARS_2D; i++)
      if (strcmp(vars[n], ECONAME2D[i][0]) == 0) {
	nvars++;
      }
  }
  if (nvars == 0) {
    //  hd_quit("No valid ecology 2D tracers found.\n");
  }
  return(nvars);
}

/* END count_eco()                                                   */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Routine to initialise the 2D tracers in the master                */
/*-------------------------------------------------------------------*/
static int eco_set_autotracer(FILE *fp, 
			      int   do_eco, 
			      char *eco_vars,  /* list of vars */ 
			      char *eco_defs,  /* name of class */
			      void *e,         /* pre_build eco struct */
			      tracer_info_t *trinfo, 
			      int ntr, int tn,
			      int         necoclass,
			      const char *ecoclass[][2],
			      int trinfo_type)
{
  char *vars[ECO_MAXNUMARGS], buf[MAXSTRLEN];
  int m, n, i;

  /* Set up the auto-tracers */
  if (do_eco && strlen(eco_vars)) {
    strcpy(buf, eco_vars);
    m = parseline(buf, vars, ECO_MAXNUMARGS);
    for (n = 0; n < m; n++) {
      for (i = 0; i < necoclass; i++) {
	if (strcmp(vars[n], ecoclass[i][0]) == 0) {
	  if ((tracer_find_index(vars[n], ntr, trinfo)) < 0) {
	    /* 
	     * Set the default value 
	     */
	    eco_set_tracer_defaults(&trinfo[tn], vars[n], eco_defs,
				    (ecology*)e);
	    /*
	     * Overwrite any att from prm-file
	     */
	    sed_read_tr_atts(&trinfo[tn], fp, vars[n]); // includes data
	    eco_read_tr_atts(&trinfo[tn], fp, vars[n]);
	    trinfo[tn].m = -1; // does not exist in the prm-file
	    tracer_re_read(&trinfo[tn], fp, trinfo_type);
	    /*
	     * Fill in the tracer number
	     */
	    trinfo[tn].n = tn;
	    tn++;
	  }
	}
      }
    }
  }
  return(tn);
}

/*-------------------------------------------------------------------*/
/* Routine to initialise the 2D tracers in the master                */
/*-------------------------------------------------------------------*/
int ecology_autotracer_3d(FILE *fp, int do_eco, char *eco_vars, char *eco_defs,
			  void *e, tracer_info_t *trinfo, int ntr, int tn)
{
  /* ECO 3D */
  tn = eco_set_autotracer(fp,  do_eco, eco_vars, eco_defs, e, trinfo, ntr, tn,
			  NUM_ECO_VARS_3D, ECONAME3D, WATER);
  return(tn);
}

/* END ecology_autotracer_3d()                                       */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Routine to initialise the 2D tracers in the master                */
/*-------------------------------------------------------------------*/
int ecology_autotracer_2d(FILE *fp, int do_eco, char *eco_vars, char *eco_defs,
			  void *e, tracer_info_t *trinfo, int ntr, int tn)
{
  /* ECO 2D */
  tn = eco_set_autotracer(fp,  do_eco, eco_vars, eco_defs, e, trinfo, ntr, tn,
			  NUM_ECO_VARS_2D, ECONAME2D, INTER);
  
  return(tn);
}

/* END ecolocy_autotracer_2d()                                       */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Routine to initialise the 2D tracers in the master                */
/*-------------------------------------------------------------------*/
int ecology_autotracer_sed(FILE *fp, int do_eco, char *eco_vars, char *eco_defs,
			   void *e, tracer_info_t *trinfo, int ntr, int tn)
{
  /* ECO SED */
  tn = eco_set_autotracer(fp,  do_eco, eco_vars, eco_defs, e, trinfo, ntr, tn,
			  NUM_ECO_VARS_3D, ECONAME3D, SEDIM);
  return(tn);
}

/* END ecology_autotracer_sed()                                      */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Set the open boundary condition as specified in the ecology       */
/* private data for sediment autotracers.                            */
/*-------------------------------------------------------------------*/
int eco_get_obc(tracer_info_t *tr)
{
  trinfo_priv_eco_t *data = tr->private_data[TR_PRV_DATA_ECO];
  int ret = NOGRAD;

  if (data != NULL) {
    if (data->type == PTR_BGC && data->obc > 0)
      ret = data->obc;
  }
  return(ret);
}

/* END eco_get_obc()                                                 */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Writes the sediment autotracers to the setup file                 */
/*-------------------------------------------------------------------*/
int ecology_autotracer_write(master_t *master, FILE *op, int tn)
{
  parameters_t *params = master->params;
  char key[MAXSTRLEN];
  int i, n, m, trn, sn;

  if (!params->do_eco || strlen(params->eco_vars) == 0) return(0);

  fprintf(op, "#\n");
  fprintf(op, "# Ecology tracers - 3D\n");
  fprintf(op, "#\n");
  /* 3D water column and sediment autotracers                        */
  n = tn;
  for (m = 0; m < params->atr; m++) {
    tracer_info_t *tracer = &master->trinfo_3d[m];

    trn = -1;
    for (i = 0; i < NUM_ECO_VARS_3D; i++) {
      if (strcmp(tracer->name, ECONAME3D[i][0]) == 0) {
	trn = i;
	break;
      }
    }
    if (trn < 0) continue;

    /* Call the 3d helper function */
    tracer_write_3d(master, op, tracer, n);

    n++;
  }

  fprintf(op, "#\n");
  fprintf(op, "# Ecology tracers - BENTHIC\n");
  fprintf(op, "#\n");
  /* 2D sediment autotracers                                         */
  for (m = 0; m < master->ntrS; m++) {
    tracer_info_t *tracer = &master->trinfo_2d[m];

    trn = -1;
    for (i = 0; i < NUM_ECO_VARS_2D; i++)
      if (strcmp(tracer->name, ECONAME2D[i][0]) == 0) {
	trn = i;
	break;
      }
    if (trn < 0) continue;

    /* Call the 2d helper function */
    tracer_write_2d(master, op, tracer, n);

    n++;
  }
  return(n);
}

/* END ecology_autotracer_write()                                   */
/*-------------------------------------------------------------------*/

#endif /* HAVE_ECOLOGY */

// EOF
