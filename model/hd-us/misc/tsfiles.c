/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/hd-us/misc/tsfiles.c
 *  
 *  Description:
 *  Perform various consistency checks on the sparse
 *  grid before the model starts up
 *  
 *  Copyright:
 *  Copyright (c) 2018. Commonwealth Scientific and Industrial
 *  Research Organisation (CSIRO). ABN 41 687 119 230. All rights
 *  reserved. See the license file for disclaimer and full
 *  use/redistribution conditions.
 *  
 *  $Id: tsfiles.c 7435 2023-10-25 01:30:31Z her127 $
 *
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "hd.h"

#define TS_EPS 0.1
#define START_EPS (0.001)

void cfl_sp(master_t *master);
static timeseries_t *find_tsfile_in_cache(master_t *master, char *fname);
static timeseries_t *find_tsfile_in_cache_us(master_t *master, char *fname,
					     char *i_rule);
static void add_tsfile_to_cache(master_t *master, timeseries_t *ts);
int ts_eval_sparse(timeseries_t *ts, int id, double t, int ns, double *var);
double ts_eval_isparse(timeseries_t *ts, int id, double t, int cc);
double df_eval_isparse(datafile_t *df, df_variable_t *v, double r, int cc);
int dump_choose_multifile(datafile_t *df, double t);
int read_sparse_dims(char *name, int szcS, int szc, int szeS, int sze, int nz);
int read_sparse_dims_struct(char *name, int ns2, int ns3, int nce1, int nce2, int nz);
double median(geometry_t *geom, double *a, int sz, int c);
double s_median(geometry_t *geom, double *a, int sz, int c);
void order (double *a, double *b);
int count_leap(char *d, double t);
void process_ghrsst(master_t *master);
int read_ghrsst(master_t *master, int ntsfiles, timeseries_t **tsfiles,
		cstring * names, double t);
void hd_ts_multifile_eval_ugrid(master_t *master, int ntsfiles, timeseries_t **tsfiles,
				cstring * names, char *varname, double *var,
				double ti, int ns, int thio, int mode);
void hd_ts_multifile_eval_ugrid2D(master_t *master, int ntsfiles, timeseries_t **tsfiles,
				  cstring * names, char *varname, double *var,
				  double ti, int ns, int thio, int mode);

/* check that time step won't violate CFL condition */
void cfl_sp(master_t *master)
{
  int s;
  double vel;

  double mindt = 1e20;
  int mins = -1;
  geometry_t *geom = master->geom;

  for (s = 1; s < geom->sgsizS; ++s) {
    double cfldt = 1.0 / (geom->h1acell[s] * geom->h1acell[s]) +
      1.0 / (geom->h2acell[s] * geom->h2acell[s]);
    cfldt = 1.0 / sqrt(cfldt);
    vel = 2.0 * sqrt(g * (master->topz[s] - geom->botz[s]));
    if (vel > 0) {
      cfldt /= vel;

      if (cfldt < mindt) {
        mins = s;
        mindt = cfldt;
      }
    }
  }

  if (DEBUG("cfl"))
    dlog("cfl", "min dt for CFL is %f at i=%ld j=%ld\n",
         mindt, geom->s2i[mins], geom->s2j[mins]);

  if (mindt <= master->dt2d)
    hd_warn("(i,j) = (%d,%d): 2d time step %f, CFL requires %f\n",
            geom->s2i[mins], geom->s2j[mins], master->dt2d, mindt);
}


timeseries_t *hd_ts_read(master_t *master, char *name, int check)
{

  char buf[MAXSTRLEN];
  timeseries_t *ts = NULL;
  char *fname = fv_get_filename(name, buf);

  if (master->tsfile_caching) {
    ts = find_tsfile_in_cache(master, fname);
  }
  if (ts == NULL) {

    ts = (timeseries_t *)malloc(sizeof(timeseries_t));
    if (ts == NULL)
      hd_quit("hd_ts_read: No memory available.\n");
    memset(ts, 0, sizeof(timeseries_t));
    memset(ts->i_rule, 0, sizeof(ts->i_rule));

    /* Read the time series */
    ts_read(fname, ts);

    /* Check correct time step is used for FFSL transport */
    if (master->tmode & SP_FFSL) {
      double fdt;
      if (nc_get_att_double(ts->df->ncid, NC_GLOBAL, "dt", &fdt) == NC_NOERR)
	if (fdt != master->grid_dt)
	  hd_warn("TRANSPORT FFSL ERROR: file %s sparse file timestep (%5.1f) != prescribed timestep (%5.1f)\n",name, fdt, master->grid_dt);
    }

    /* Convert units to model time units */
    /*
     * Jan '10, FR added the ts->t_units !- NULL check in order to
     * fall through to the calling function in case this is a plain
     * ascii file (i.e. no time)
     */
    if (master->timeunit && 
	ts->t_units != NULL && strcmp(ts->t_units, master->timeunit) != 0) {
      if (DEBUG("conversions"))
        dlog("conversions",
             "hd_ts_read: Converting time series %s units\n  From: %s\n  To: %s\n",
             ts->name, ts->t_units, master->timeunit);

      if (ts->t_mod_type == MOD_NONE)
	ts->df->rec_mod_scale = tm_unit_to_sec(ts->t_units) / 
	  tm_unit_to_sec(master->timeunit);
      
      ts_convert_time_units(ts, master->timeunit);
    }

    if (check)
      hd_ts_check(master, ts);

    if (master->tsfile_caching)
      add_tsfile_to_cache(master, ts);
  }

  return ts;
}

/*-------------------------------------------------------------------*/
/* Initialisation of timeseries structure for using GRID_SPEC        */
/* interpolation. The INTERP_TYPE must be copied into ts->i_rule     */
/* before ts_read is called. Usual bilinear/weighted interpolation   */
/* will be performed if i_rule is NULL.                              */
/*-------------------------------------------------------------------*/
timeseries_t *hd_ts_read_us(master_t *master, char *name, int check, char *i_rule)
{

  char buf[MAXSTRLEN];
  timeseries_t *ts = NULL;
  char *fname = fv_get_filename(name, buf);

  if (master->tsfile_caching) {
    if (i_rule != NULL && strlen(i_rule))
      ts = find_tsfile_in_cache_us(master, fname, i_rule);
    else
      ts = find_tsfile_in_cache(master, fname);
  }

  if (ts == NULL) {
    ts = (timeseries_t *)malloc(sizeof(timeseries_t));
    if (ts == NULL)
      hd_quit("hd_ts_read: No memory available.\n");
    memset(ts, 0, sizeof(timeseries_t));
    memset(ts->i_rule, 0, sizeof(ts->i_rule));
    if (i_rule != NULL) {
      if (strlen(i_rule)) strcpy(ts->i_rule, i_rule);
    }

    /* Read the time series */
    ts_read(fname, ts);

    /* Check correct time step is used for FFSL transport */
    if (master->tmode & SP_FFSL) {
      double fdt;
      if (nc_get_att_double(ts->df->ncid, NC_GLOBAL, "dt", &fdt) == NC_NOERR)
	if (fdt != master->grid_dt)
	  hd_warn("TRANSPORT FFSL ERROR: file %s sparse file timestep (%5.1f) != prescribed timestep (%5.1f)\n",name, fdt, master->grid_dt);
    }

    /* Convert units to model time units */
    /*
     * Jan '10, FR added the ts->t_units !- NULL check in order to
     * fall through to the calling function in case this is a plain
     * ascii file (i.e. no time)
     */
    if (master->timeunit && 
	ts->t_units != NULL && strcmp(ts->t_units, master->timeunit) != 0) {
      if (DEBUG("conversions"))
        dlog("conversions",
             "hd_ts_read: Converting time series %s units\n  From: %s\n  To: %s\n",
             ts->name, ts->t_units, master->timeunit);

      if (ts->t_mod_type == MOD_NONE)
	ts->df->rec_mod_scale = tm_unit_to_sec(ts->t_units) / 
	  tm_unit_to_sec(master->timeunit);
      
      ts_convert_time_units(ts, master->timeunit);
    }

    if (check)
      hd_ts_check(master, ts);

    if (master->tsfile_caching)
      add_tsfile_to_cache(master, ts);
  }

  return ts;
}

/*UR-ADDED destroy function for late free stack */
void hd_ts_free_void(void* ts)
{
  ts_free((timeseries_t*)ts);
  free(ts);
  emstag(LTRACE,"hd:tsfiles:hd_ts_free_void","finished destroy timeseries ");
}

void hd_ts_free(master_t *master, timeseries_t *ts)
{
 if (!master->tsfile_caching) {
    ts_free(ts);
    free(ts);
  }/*UR-TODO * /
  else
   free_stack_add(ts,hd_ts_free_void);
   */
}





void hd_ts_check(master_t *master, timeseries_t *ts)
{
  /* Check extent_t of time series */
  if ((schedule->start_time + TS_EPS) < ts->t[0] ||
      schedule->stop_time > (ts->t[ts->nt - 1] + TS_EPS))
    hd_warn
      ("hd_ts_check: Model run extent (%.3f to %.3f [days]) outside range of time series %s (%.3f to %.3f [days])\n",
       schedule->start_time / 86400.0, schedule->stop_time / 86400.0, ts->name,
       ts->t[0]/86400, ts->t[ts->nt - 1]/86400);
}


/** Check whether the specified variable located in a series
  * of timeseries file. Also check whether the variable spans
  * the range.
  *
  * The time check is still fairly simple minded. It does not
  * look for holes in the data.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param var Name of variable to check.
  * @param tstart Lower end of range to check.
  * @param tstop Upper end of range to check.
  * @return non-zero value if successful.
  */
int hd_ts_multifile_check(int ntsfiles, timeseries_t **tsfiles,
                          cstring * names, char *var, double tstart,
                          double tstop)
{
  double f_tstart = 1e300;
  double f_tstop = -1e300;
  int found_var = 0;
  char buf[MAXLINELEN];

  if (ntsfiles > 0) {
    int i;
    for (i = 0; i < ntsfiles; ++i) {
      timeseries_t *ts = tsfiles[i];
      int varid = ts_get_index(ts, fv_get_varname(names[i], var, buf));
      datafile_t *df = ts->df;

      if(master->lyear) {
	tstart -= count_leap(master->timeunit, tstart) * 86400;
	tstop -= count_leap(master->timeunit, tstop) * 86400;
      }
      if (varid >= 0) {
        found_var = 1;
        if (ts->t_mod_type == MOD_NONE) {
          f_tstart = (ts->t[0] < f_tstart) ? ts->t[0] : f_tstart;
          f_tstop =
            (ts->t[ts->nt - 1] > f_tstop) ? ts->t[ts->nt - 1] : f_tstop;
        } else {
          f_tstart = (tstart < f_tstart) ? tstart : f_tstart;
          f_tstop = (tstop > f_tstop) ? tstop : f_tstop;
        }
      }
    }

    if (!found_var) {
      buf[0] = '\000';
      for (i = 0; i < ntsfiles; i++) {
	strcat(buf,names[i]);
	if(i+1 < ntsfiles)
	  strcat(buf,", ");
      }
      hd_quit
        ("hd_ts_multifile_check: Unable to locate the variable '%s' in the timeseries files (%s).\n",
         var,buf);
      return 0;
    } else {
      if (!((tstart >= f_tstart) && (tstop <= f_tstop))) {
      	buf[0] = '\000';
	for (i = 0; i < ntsfiles; i++) {
	  strcat(buf,names[i]);
	  if(i+1 < ntsfiles)
	    strcat(buf,", ");
	}
	/* fprintf(stderr,"File list %s \n",buf); */

        hd_warn
          ("hd_ts_multifile_check: The variable '%s' does not span the \ntimeseries files for the time range %.3f to %.3f (%.3f to %.3f [days]) - files \n %s.",
           var, tstart / 86400.0, tstop / 86400.0, f_tstart / 86400.0, f_tstop / 86400.0,buf);

      }
    }
  } else {
    hd_quit("hd_ts_multifile_check: No timeseries files specified for %s in %s.\n", var, names[0]);
    return 0;
  }

  return 1;

}

/** Open a set of timeseries files.
  *
  * The time check is still fairly simple minded. It does not
  * look for holes in the data.
  *
  * @param master The master structure.
  * @param nf Number files.
  * @param array of file names.
  */
timeseries_t **hd_ts_multifile_read(master_t *master, int nf,
                                    cstring * files)
{
  timeseries_t **tsfiles = NULL;
  int i;

  if (nf > 0) {
    tsfiles = (timeseries_t **)malloc(sizeof(timeseries_t *) * nf);
    memset(tsfiles, 0, sizeof(timeseries_t *) * nf);
    for (i = 0; i < nf; ++i) {
      if(master->runmode & (AUTO | DUMP) && endswith(files[i],".mpk"))
	hd_quit("Can't initialise file %s using '-g' option; change to .nc file.\n", files[i]);
      else
	tsfiles[i] = hd_ts_read(master, files[i], 0);
    }
  } else
    hd_quit
      ("hd_ts_multifile_read: Must specify at least one timeseries file.\n");

  return tsfiles;
}


timeseries_t **hd_ts_multifile_read_us(master_t *master, int nf,
				       cstring * files,  char *i_rule)
{
  timeseries_t **tsfiles = NULL;
  int i;

  if (nf > 0) {
    tsfiles = (timeseries_t **)malloc(sizeof(timeseries_t *) * nf);
    memset(tsfiles, 0, sizeof(timeseries_t *) * nf);
    for (i = 0; i < nf; ++i) {
      if(master->runmode & (AUTO | DUMP) && endswith(files[i],".mpk"))
	hd_quit("Can't initialise file %s using '-g' option; change to .nc file.\n", files[i]);
      else {
	if (i_rule != NULL && strlen(i_rule))
	  tsfiles[i] = hd_ts_read_us(master, files[i], 0, i_rule);
	else
	  tsfiles[i] = hd_ts_read(master, files[i], 0);
      }
    }
  } else
    hd_quit
      ("hd_ts_multifile_read: Must specify at least one timeseries file.\n");

  return tsfiles;
}


/** Find the file variable id's that match the variable name for
  * supplied set of files.
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param var Name of variable to check.
  * @param varids Array (of length ntsfiles) containing the variable id.
  *  An id value of < 0 means that the variable is not present.
  * @return Number of occurances.
  */
int hd_ts_multifile_get_index(int ntsfiles, timeseries_t **tsfiles,
                                cstring *names, char *var, int *varids) {
    char buf[MAXSTRLEN];
    int i;
    int n = 0;

    for (i = 0; i < ntsfiles; ++i) {
      timeseries_t *ts = tsfiles[i];
      varids[i] = ts_get_index(ts, fv_get_varname(names[i], var, buf));
      if (varids[i] >= 0)
        ++n;
    }

    return n;
}

char *hd_ts_multifile_get_text_att(int ntsfiles, timeseries_t **tsfiles,
				   cstring * names, char *varname, 
				   char *attname)
{
  int varids[MAXNUMTSFILES];
  char *text;
  if (hd_ts_multifile_get_index(ntsfiles, tsfiles, 
				names, varname, varids) == 0)
    hd_quit("hd_ts_multifile_get_att: Unable to evaluate the variable '%s'.\n", varname);

  if (ntsfiles > 0) {
    int i;
    int index = -1;
    timeseries_t *ts;
    datafile_t *df;
    df_variable_t *v;
    df_attribute_t *a;

    for (i = 0; i < ntsfiles; ++i) {
      ts = tsfiles[i];
      if (varids[i] < 0) continue;
      index = i;
    }
    assert(index >= 0);
    ts = tsfiles[index];
    v = df_get_variable(ts->df, varids[index]);
    df = ts->df;
    for (i = 0; i < v->na; ++i) {
      df_attribute_t *a = &v->attributes[i];
      if (strcasecmp(a->name, attname) == 0) {
	text = ATT_TEXT(a);
	return(text);
      }
    }
  }
  return(NULL);
}


/** Evaluate in the first appropriate file the specified variable
  * at the given point and time.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param varids Array of variable ids.
  * @param t time value
  * @param x x value
  * @param y y value
  * @return The interpolated value (NaN if failed).
  */
double hd_ts_multifile_eval_xy(int ntsfiles, timeseries_t **tsfiles,
                                int *varids, double t, double x, double y)
{
  return hd_ts_multifile_eval_xyz(ntsfiles, tsfiles, varids, t, x, y, 0.0);
}

/** Evaluate in the first appropriate file the specified variable
  * at the given point and time.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param varids Array of variable ids.
  * @param t time value
  * @param x x value
  * @param y y value
  * @param z z value
  * @return The interpolated value (NaN if failed).
  */
double hd_ts_multifile_eval_xyz(int ntsfiles, timeseries_t **tsfiles,
                                int *varids, double t,
                                double x, double y, double z)
{

  if (ntsfiles > 0) {
    int i;
    int index = -1;
    double val;

    for (i = 0; i < ntsfiles; ++i) {
      timeseries_t *ts = tsfiles[i];
      if (varids[i] < 0) continue;

      index = i;

      /* If the variable exists in the file and the current time is within
         the bounds of the timeseries files domain, then evaluate the
         point. */
      if (((ts->t_mod_type != MOD_NONE) ||
          ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))))
          break;
    }

    assert(index >= 0);
    val = ts_eval_xyz(tsfiles[index], varids[index], t, x, y, z);
    if (ts_eval_runcode(tsfiles[index])) master->regf = RS_OBCSET;

    return val;

  } else
    hd_quit("hd_ts_multifile_eval_xyz: No timeseries files specified.\n");

  return NaN;
}

/** Evaluate in the first appropriate file the specified variable
  * at the given point and time.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param var Name of variable to check.
  * @param t time value
  * @param x x value
  * @param y y value
  * @return The interpolated value (NaN if failed).
  */
double hd_ts_multifile_eval_xy_by_name(int ntsfiles, timeseries_t **tsfiles,
                                cstring * names, char *var, double t,
                                double x, double y)
{
  return hd_ts_multifile_eval_xyz_by_name(ntsfiles, tsfiles, names, var, t, x, y, 0.0);
}

/** Evaluate in the first appropriate file the specified variable
  * at the given point and time.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param var Name of variable to check.
  * @param t time value
  * @param x x value
  * @param y y value
  * @param z z value
  * @return The interpolated value (NaN if failed).
  */
double hd_ts_multifile_eval_xyz_by_name(int ntsfiles, timeseries_t **tsfiles,
                                cstring * names, char *var, double t,
                                double x, double y, double z)
{
    int varids[MAXNUMTSFILES];
    if (hd_ts_multifile_get_index(ntsfiles, tsfiles, names, var, varids) == 0)
       hd_quit("hd_ts_multifile_eval_xyz: Unable to evaluate the variable '%s'.\n", var);

    return hd_ts_multifile_eval_xyz(ntsfiles, tsfiles, varids,
                          t, x, y, z);
}



/** Locate a timeseries file in the cache that matchs that provided.
  *
  * @param master C grid data struture.
  * @param fname The filename to search for.
  * @return A pointer to the timeseries_t file, or NULL if none located.
  */
static timeseries_t *find_tsfile_in_cache(master_t *master, char *fname)
{

  int i;

  if (!master->tsfile_caching || master->ntscached == 0)
    return NULL;

  for (i = 0; i < master->ntscached; ++i) {
    if (master->tscache[i] == NULL)
      continue;
    if (strcmp(fname, master->tscache[i]->name) == 0)
      return master->tscache[i];
  }

  return NULL;
}


static timeseries_t *find_tsfile_in_cache_us(master_t *master, char *fname,
					     char *i_rule)
{

  int i;

  if (!master->tsfile_caching || master->ntscached == 0)
    return NULL;

  for (i = 0; i < master->ntscached; ++i) {
    if (master->tscache[i] == NULL)
      continue;
    if (strcmp(fname, master->tscache[i]->name) == 0 &&
	strcmp(i_rule, master->tscache[i]->i_rule) == 0)
      return master->tscache[i];
  }

  return NULL;
}

/** Add a timeseries file to the cache.
  */
static void add_tsfile_to_cache(master_t *master, timeseries_t *ts)
{

  int nts, i;
  timeseries_t **tss = NULL;

  if (!master->tsfile_caching)
    return;

  nts = master->ntscached + 1;
  tss = (timeseries_t **)malloc(nts * sizeof(timeseries_t *));
  for (i = 0; i < master->ntscached; ++i)
    tss[i] = master->tscache[i];
  tss[nts - 1] = ts;

  if (master->tscache != NULL)
    free(master->tscache);

  master->ntscached = nts;
  master->tscache = tss;
}


/** Evaluate in the first appropriate file the specified variable
  * at the given point and time.
  *
  * @param master  Master data structure
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param var Name of variable to check.
  * @param v Variable to update
  * @param t time value
  * @param vec Cells to process vector
  * @param nvec Size of vec
  * @param mode File read mode
  */
void hd_ts_multifile_eval(master_t *master, 
			  int ntsfiles, timeseries_t **tsfiles,
			  cstring * names, char *var, double *v, double t,
			  int *vec, int nvec, int mode)
{
  geometry_t *geom = master->geom;
  dump_data_t *dumpdata = master->dumpdata;

  if(master->lyear)
    t -= count_leap(master->timeunit, t) * 86400;

  if (mode & SP_EXACT) {
    hd_ts_multifile_eval_sparse(ntsfiles, tsfiles, names, var,
				v, t, nvec, master->thIO);
  }
  else if (mode & SP_TINT) {
    hd_ts_multifile_eval_isparse(ntsfiles, tsfiles, names, var,
				 v, t, nvec, master->thIO);
  } else if (mode & XYZ_TINT) {
    int cc, c, cs;
    if (mode & VEL2D) {
      if (mode & GHRSST) {
	double x, kelvin = 273.0;

	for (cc = 1; cc <= nvec; cc++) {
	  c = vec[cc];
	  x = (v == master->ghrsst && geom->cellx[c] > 180.0) ? geom->cellx[c] - 360.0 : geom->cellx[c];
	  v[c] = hd_ts_multifile_eval_xy_by_name(ntsfiles, tsfiles,
						 names, var, t,
						 x, geom->celly[c]) - kelvin;
	}
	/*read_ghrsst(master, ntsfiles, tsfiles, names, t);*/
	process_ghrsst(master);

      } else {
	for (cc = 1; cc <= nvec; cc++) {
	  c = vec[cc];
	  v[c] = hd_ts_multifile_eval_xy_by_name(ntsfiles, tsfiles,
						 names, var, t,
						 geom->cellx[c], 
						 geom->celly[c]);
	}
      }
    } else {
      for (cc = 1; cc <= nvec; cc++) {
	c = vec[cc];
	cs = geom->m2d[c];

	v[c] = hd_ts_multifile_eval_xyz_by_name(ntsfiles, tsfiles,
						names, var, t,
						geom->cellx[cs], 
						geom->celly[cs],
						geom->cellz[c] * master->Ds[cs]);
      }
    }
  }
}

/* Same as hd_ts_multifile_eval(), but with polygon inclusion / exclusion zones. */
void hd_ts_multifile_evalp(master_t *master, 
			   int ntsfiles, timeseries_t **tsfiles,
			   cstring * names, char *var, double *v, double t,
			   int *vec, int nvec, int mode, poly_t *pl)
{
  geometry_t *geom = master->geom;
  dump_data_t *dumpdata = master->dumpdata;

  if(master->lyear)
    t -= count_leap(master->timeunit, t) * 86400;

  if (mode & SP_EXACT) {
    hd_ts_multifile_eval_sparse(ntsfiles, tsfiles, names, var,
				v, t, nvec, master->thIO);
  }
  else if (mode & SP_TINT) {
    hd_ts_multifile_eval_isparse(ntsfiles, tsfiles, names, var,
				 v, t, nvec, master->thIO);
  } else if (mode & XYZ_TINT) {
    int cc, c, cs;
    if (mode & VEL2D) {
      if (mode & GHRSST) {
	double x, kelvin = 273.0;
	for (cc = 1; cc <= nvec; cc++) {
	  c = vec[cc];
	  x = (v == master->ghrsst && geom->cellx[c] > 180.0) ? geom->cellx[c] - 360.0 : geom->cellx[c];
	  v[c] = hd_ts_multifile_eval_xy_by_name(ntsfiles, tsfiles,
						 names, var, t,
						 x, geom->celly[c]) - kelvin;
	}
	/*read_ghrsst(master, ntsfiles, tsfiles, names, t);*/
	process_ghrsst(master);

      } else {

	for (cc = 1; cc <= nvec; cc++) {
	  c = vec[cc];

	  if (mode & P_IN) {
	    if (!poly_contains_point(pl, geom->cellx[c], geom->celly[c])) {
	      v[c] = 0.0;
	      continue;
	    }
	  }
	  if (mode & P_EX) {
	    if (poly_contains_point(pl, geom->cellx[c], geom->celly[c])) {
	      v[c] = 0.0;
	      continue;
	    }
	  }

	  v[c] = hd_ts_multifile_eval_xy_by_name(ntsfiles, tsfiles,
						 names, var, t,
						 geom->cellx[c], 
						 geom->celly[c]);
	}
      }
    } else {
      for (cc = 1; cc <= nvec; cc++) {
	c = vec[cc];
	cs = geom->m2d[c];

	if (mode & P_IN) {
	  if (!poly_contains_point(pl, geom->cellx[cs], geom->celly[cs])) {
	    v[c] = 0.0;
	    continue;
	  }
	}
	if (mode & P_EX) {
	  if (poly_contains_point(pl, geom->cellx[cs], geom->celly[cs])) {
	    v[c] = 0.0;
	    continue;
	  }
	}

	v[c] = hd_ts_multifile_eval_xyz_by_name(ntsfiles, tsfiles,
						names, var, t,
						geom->cellx[cs], 
						geom->celly[cs],
						geom->cellz[c] * master->Ds[cs]);
      }
    }
  }
}


/** Evaluate in the first appropriate file the specified variable
  * at the given point and time.
  *
  * @param master  Master data structure
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param var Name of variable to check.
  * @param v Variable to update
  * @param t time value
  * @param vec Cells to process vector
  * @param nvec Size of vec
  * @param mode File read mode
  */
void hd_trans_multifile_eval(master_t *master, 
			     int ntsfiles, timeseries_t **tsfiles,
			     cstring * names, char *var, double *v, double t,
			     int *vec, int nvec, int mode)
{
  geometry_t *geom = master->geom;
  dump_data_t *dumpdata = master->dumpdata;
  int oset = (dumpdata->start_index) ? 0 : 1;
  int n, cc;

  if(master->lyear)
    t -= count_leap(master->timeunit, t) * 86400;

  if (mode & SP_EXACT && mode & SP_UGRID) {
    if (mode & VEL2D) {
      hd_ts_multifile_eval_ugrid2D(master, ntsfiles, tsfiles, names, var,
				   v, t, nvec, master->thIO, mode);
    } else {
      hd_ts_multifile_eval_ugrid(master, ntsfiles, tsfiles, names, var,
				 v, t, nvec, master->thIO, mode);
    }
  }
  else if (mode & SP_EXACT) {
    hd_ts_multifile_eval_sparse(ntsfiles, tsfiles, names, var,
				master->d3, t, nvec, master->thIO);
    if (mode & (U1STRUCT|U2STRUCT)) {
      unpack_sparse3(vec, nvec, master->d3, dumpdata->w1, oset);
      for (cc = 1; cc < geom->b3_t; cc++) {
	int c1, c = geom->w3_t[cc];
	int e;
	if (mode & U1STRUCT) e = geom->c2e[1][c];
	if (mode & U2STRUCT) e = geom->c2e[4][c];
	v[e] = dumpdata->w1[c];
      }
      /* Normal open boundary cells */
      for (n = 0; n < geom->nobc; n++) {
	open_bdrys_t *open = geom->open[n];
	for (cc = 1; cc <= open->no3_e1; cc++) {
	  int c = open->ogc_t[cc];
	  int e;
	  if (mode & U1STRUCT) e = geom->c2e[1][c];
	  if (mode & U2STRUCT) e = geom->c2e[4][c];
	  v[e] = dumpdata->w1[c];
	}
      }
    } else
      unpack_sparse3(vec, nvec, master->d3, v, oset);
  }
  else if (mode & SP_TINT) {
    hd_ts_multifile_eval_isparse(ntsfiles, tsfiles, names, var,
				 master->d3, t, nvec, master->thIO);
    unpack_sparse3(vec, nvec, master->d3, v, oset);
  } else if (mode & XYZ_TINT) {
    int cc, c, cs;
    if (mode & VEL2D) {
      for (cc = 1; cc <= nvec; cc++) {
	c = vec[cc];
	v[c] = hd_ts_multifile_eval_xy_by_name(ntsfiles, tsfiles,
					       names, var, t,
					       geom->cellx[c], 
					       geom->celly[c]);
      }
    } else {
      /* Assume u and v are oriented in east and north directions,
	 and rotate onto the grid. */
      if (mode & (U1GEN|U2GEN)) {
	double uvel, vvel;
	double *ax, *ay, sign;
	if (mode & U1GEN) {
	  ax = geom->costhu1;
	  ay = geom->sinthu1;
	  sign = 1.0;
	} else {
	  ax = geom->sinthu2;
	  ay = geom->costhu2;
	  sign = -1.0;
	}
        for (cc = 1; cc <= nvec; cc++) {
	  c = vec[cc];
	  cs = geom->m2d[c];
	  uvel = hd_ts_multifile_eval_xyz_by_name(ntsfiles, tsfiles,
						  names, "u", t,
						  geom->cellx[cs], 
						  geom->celly[cs],
						  geom->cellz[c]); 
	  vvel = hd_ts_multifile_eval_xyz_by_name(ntsfiles, tsfiles,
						  names, "v", t,
						  geom->cellx[cs], 
						  geom->celly[cs],
						  geom->cellz[c]);
	  v[c] = sign * uvel * ax[cs] + vvel * ay[cs];
	}	  
      } else {
        for (cc = 1; cc <= nvec; cc++) {
	  c = vec[cc];
	  cs = geom->m2d[c];
	  v[c] = hd_ts_multifile_eval_xyz_by_name(ntsfiles, tsfiles,
		  				  names, var, t,
						  geom->cellx[cs], 
						  geom->celly[cs],
						  geom->cellz[c]);
	}
      }
    }
  }
}

/** Evaluate in the first appropriate file the specified variable
  * at the given time for the whole sparse array. The time is
  * assumed to correspond to an exact dump time in the file.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param varname Name of variable to check.
  * @param var Array to store values.
  * @param t time value
  * @param ns Sparse array size
  * @return The interpolated value (NaN if failed).
  */
void hd_ts_multifile_eval_sparse(int ntsfiles, timeseries_t **tsfiles,
				 cstring * names, char *varname, double *var,
				 double ti, int ns, int thio)
{
  int varids[MAXNUMTSFILES];

  if (hd_ts_multifile_get_index(ntsfiles, tsfiles, 
				names, varname, varids) == 0)
    hd_quit("hd_ts_multifile_eval_sparse: Unable to evaluate the variable '%s'.\n", varname);

  if (ntsfiles > 0) {
    int i;
    int index = -1;
    int timeindex;
    double t = ti;
    size_t start[2];
    size_t count[2];
    timeseries_t *ts;
    datafile_t *df;
    df_variable_t *v;
    int r0, r1;
    double rfrac, rOrfrac;

    start[1] = 0;
    count[0] = 1L;

    /*---------------------------------------------------------------*/
    /* If there is a list of files, find the file in the list that   */
    /* contains the time ti.                                         */
    for (i = 0; i < ntsfiles; ++i) {
      ts = tsfiles[i];
      if (varids[i] < 0) continue;
      index = i;
      t = ti;

      if (ts->t_mod_type != MOD_NONE)
	t = get_file_time(ts, t);
      if ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))
	break;
    }
    assert(index >= 0);
    ts = tsfiles[index];
    v = df_get_variable(ts->df, varids[index]);
    df = ts->df;

    /*---------------------------------------------------------------*/
    /* Get the correct dump                                          */
    if (df->ncid == -1) {
      /* Find the correct file in the multi-netcdf file list         */
      if ((timeindex = dump_choose_multifile(df, t)) == -1)
	hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
    } else {
      /* Find the record in the file; this is either the correct     */
      /* in the multi-file list located in dump_choose_multifile()   */
      /* above, or tsfiles[index] located above.                     */
      df_find_record(df, t, &r0, &r1, &rfrac);
      /*
       * This might seem like a bit of a hack but its here to get
       * around extremely minute numerical issues arising from Trike
       * running with TIMEUNIT like "seconds since 2014-01-01 +10" and
       * then START and STOP of 0 and 1 respectively and the
       * OUTPUT_TIMEUNIT of "days since 1990-01-01 +10" that end up in
       * the output trans files. So we end up with numbers like:
       * 8978.00000000015 -> which is close enough to 8978. The
       * df_find_record call above will therefore have a non-zero frac
       * value.
       */

      rOrfrac = round(rfrac);
      if (rfrac > 0.0 && fabs(rOrfrac-rfrac) < START_EPS) {
	/* Snap to the nearest value */
	r0 += rOrfrac;
      } // no else, we'll assume rfrac will always be positive

      timeindex = r0;
      /* When the time ti is greater than the last time in the file, */
      /* a new file in the multi-file list is located. If this isn't */
      /* located, the program terminates.                            */
      if (fabs(t - df->records[r0]) > START_EPS) {
	if ((timeindex = dump_choose_multifile(df, t)) == -1) {
	  hd_quit("hd_ts_multifile_eval_sparse: The multi-dump file '%s' does not contain the time %.2f.\n", names[index], t);
	}
      }
    }

    /* Old code - see below */
    /*
    if (df->ncid == -1) {
      if ((timeindex = dump_choose_multifile_o(df, t)) == -1)
	hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
    } else if ((timeindex = dump_choose_by_time_s(df->ncid, t)) == -1) {
      if ((timeindex = dump_choose_multifile_o(df, t)) == -1)
	hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
    }
    */

    /*---------------------------------------------------------------*/
    /* Read in the variable data                                     */
    start[0] = timeindex;
    count[1] = ns;
    /*
     * The buffered function reads ahead in parallel - see also the
     * associated cleanup function in dump_choose_multifile
     */
    if (thio)
      nc_buffered_get_vara_double(df->ncid, v, varids[index], start, count, var);
    else
      nc_get_vara_double(df->ncid, varids[index], start, count, var);

  } else
    hd_quit("hd_ts_multifile_eval_sparse: No timeseries files specified.\n");
}


/** Evaluate in the first appropriate file the specified variable
  * at the given time for the whole sparse array. The time is
  * assumed to correspond to an exact dump time in the file.
  *
  * @param ntsfiles Number of timeseries files in array.
  * @param tsfiles Array of timeseries files.
  * @param names Array of timeseries file names.
  * @param varname Name of variable to check.
  * @param var Array to store values.
  * @param t time value
  * @param ns Sparse array size
  * @return The interpolated value (NaN if failed).
  */
void hd_ts_multifile_eval_ugrid(master_t *master, int ntsfiles, timeseries_t **tsfiles,
				cstring * names, char *varname, double *var,
				double ti, int ns, int thio, int mode)
{
  int varids[MAXNUMTSFILES];
  geometry_t *geom = master->geom;

  if (hd_ts_multifile_get_index(ntsfiles, tsfiles, 
				names, varname, varids) == 0)
    hd_quit("hd_ts_multifile_eval_sparse: Unable to evaluate the variable '%s'.\n", varname);

  if (ntsfiles > 0) {
    int i, cc, c, k;
    int index = -1;
    int timeindex;
    int nz = geom->nz;
    double t = ti;
    size_t start[4];
    size_t count[4];
    timeseries_t *ts;
    datafile_t *df;
    df_variable_t *v;
    int r0, r1;
    double rfrac, rOrfrac;
    double **in = d_alloc_2d(ns, nz);
    int **rmap = (mode & U1GEN) ? master->k2e : master->k2c;
    int istart = (master->tmode & SP_START1) ? 1 : 0;

    start[1] = 0;
    start[2] = 0;
    start[3] = 0;
    count[0] = 1L;
    count[3] = 0;

    /*---------------------------------------------------------------*/
    /* If there is a list of files, find the file in the list that   */
    /* contains the time ti.                                         */
    for (i = 0; i < ntsfiles; ++i) {
      ts = tsfiles[i];
      if (varids[i] < 0) continue;
      index = i;
      t = ti;

      if (ts->t_mod_type != MOD_NONE)
	t = get_file_time(ts, t);
      if ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))
	break;
    }
    assert(index >= 0);
    ts = tsfiles[index];
    v = df_get_variable(ts->df, varids[index]);
    df = ts->df;

    /*---------------------------------------------------------------*/
    /* Get the correct dump                                          */
    if (df->ncid == -1) {
      /* Find the correct file in the multi-netcdf file list         */
      if ((timeindex = dump_choose_multifile(df, t)) == -1)
	hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
    } else {
      /* Find the record in the file; this is either the correct     */
      /* in the multi-file list located in dump_choose_multifile()   */
      /* above, or tsfiles[index] located above.                     */
      df_find_record(df, t, &r0, &r1, &rfrac);
      /*
       * This might seem like a bit of a hack but its here to get
       * around extremely minute numerical issues arising from Trike
       * running with TIMEUNIT like "seconds since 2014-01-01 +10" and
       * then START and STOP of 0 and 1 respectively and the
       * OUTPUT_TIMEUNIT of "days since 1990-01-01 +10" that end up in
       * the output trans files. So we end up with numbers like:
       * 8978.00000000015 -> which is close enough to 8978. The
       * df_find_record call above will therefore have a non-zero frac
       * value.
       */
      rOrfrac = round(rfrac);
      if (rfrac > 0.0 && fabs(rOrfrac-rfrac) < START_EPS) {
	/* Snap to the nearest value */
	r0 += rOrfrac;
      } // no else, we'll assume rfrac will always be positive

      timeindex = r0;
      /* When the time ti is greater than the last time in the file, */
      /* a new file in the multi-file list is located. If this isn't */
      /* located, the program terminates.                            */
      if (fabs(t - df->records[r0]) > START_EPS) {
	if ((timeindex = dump_choose_multifile(df, t)) == -1)
	  hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
      }
    }

    /*---------------------------------------------------------------*/
    /* Read in the variable data                                     */
    start[0] = timeindex;
    count[1] = geom->nz;
    count[2] = ns;

    /*
     * The buffered function reads ahead in parallel - see also the
     * associated cleanup function in dump_choose_multifile
     */
    if (thio)
      nc_buffered_get_vara_double(df->ncid, v, varids[index], start, count, in[0]);
    else
      nc_get_vara_double(df->ncid, varids[index], start, count, in[0]);

    /* Map to the mesh indexing                                      */
    for (k = 0; k < nz; k++) {
      for (cc = istart; cc < ns; cc++) {
	c = rmap[k][cc];
	if (c) var[c] = in[k][cc];
      }
    }
    d_free_2d(in);
  } else
    hd_quit("hd_ts_multifile_eval_sparse: No timeseries files specified.\n");
}


/*-------------------------------------------------------------------*/
/* Read a UGRID layered topology file directly into a 2D variable.   */
/*-------------------------------------------------------------------*/
void hd_ts_multifile_eval_ugrid2D(master_t *master, int ntsfiles, timeseries_t **tsfiles,
				  cstring * names, char *varname, double *var,
				  double ti, int ns, int thio, int mode)
{
  int varids[MAXNUMTSFILES];
  geometry_t *geom = master->geom;

  if (hd_ts_multifile_get_index(ntsfiles, tsfiles, 
				names, varname, varids) == 0)
    hd_quit("hd_ts_multifile_eval_sparse: Unable to evaluate the variable '%s'.\n", varname);

  if (ntsfiles > 0) {
    int i, cc, c;
    int index = -1;
    int timeindex;
    double t = ti;
    size_t start[4];
    size_t count[4];
    timeseries_t *ts;
    datafile_t *df;
    df_variable_t *v;
    int r0, r1;
    double rfrac, rOrfrac;
    double *in = d_alloc_1d(ns);
    int istart = (master->tmode & SP_START1) ? 1 : 0;
    int oset = (istart) ? 0 : 1;

    start[1] = 0;
    start[2] = 0;
    start[3] = 0;
    count[0] = 1L;
    count[2] = 0;
    count[3] = 0;

    /*---------------------------------------------------------------*/
    /* If there is a list of files, find the file in the list that   */
    /* contains the time ti.                                         */
    for (i = 0; i < ntsfiles; ++i) {
      ts = tsfiles[i];
      if (varids[i] < 0) continue;
      index = i;
      t = ti;

      if (ts->t_mod_type != MOD_NONE)
	t = get_file_time(ts, t);
      if ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))
	break;
    }
    assert(index >= 0);
    ts = tsfiles[index];
    v = df_get_variable(ts->df, varids[index]);
    df = ts->df;

    /*---------------------------------------------------------------*/
    /* Get the correct dump                                          */
    if (df->ncid == -1) {
      /* Find the correct file in the multi-netcdf file list         */
      if ((timeindex = dump_choose_multifile(df, t)) == -1)
	hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
    } else {
      /* Find the record in the file; this is either the correct     */
      /* in the multi-file list located in dump_choose_multifile()   */
      /* above, or tsfiles[index] located above.                     */
      df_find_record(df, t, &r0, &r1, &rfrac);
      /*
       * This might seem like a bit of a hack but its here to get
       * around extremely minute numerical issues arising from Trike
       * running with TIMEUNIT like "seconds since 2014-01-01 +10" and
       * then START and STOP of 0 and 1 respectively and the
       * OUTPUT_TIMEUNIT of "days since 1990-01-01 +10" that end up in
       * the output trans files. So we end up with numbers like:
       * 8978.00000000015 -> which is close enough to 8978. The
       * df_find_record call above will therefore have a non-zero frac
       * value.
       */
      rOrfrac = round(rfrac);
      if (rfrac > 0.0 && fabs(rOrfrac-rfrac) < START_EPS) {
	/* Snap to the nearest value */
	r0 += rOrfrac;
      } // no else, we'll assume rfrac will always be positive

      timeindex = r0;
      /* When the time ti is greater than the last time in the file, */
      /* a new file in the multi-file list is located. If this isn't */
      /* located, the program terminates.                            */
      if (fabs(t - df->records[r0]) > START_EPS) {
	if ((timeindex = dump_choose_multifile(df, t)) == -1)
	  hd_quit("hd_ts_multifile_eval_sparse: The dump file '%s' does not contain the time %.2f.\n", names[index], t);
      }
    }

    /*---------------------------------------------------------------*/
    /* Read in the variable data                                     */
    start[0] = timeindex;
    count[1] = ns;

    /*
     * The buffered function reads ahead in parallel - see also the
     * associated cleanup function in dump_choose_multifile
     */
    if (thio)
      nc_buffered_get_vara_double(df->ncid, v, varids[index], start, count, in);
    else
      nc_get_vara_double(df->ncid, varids[index], start, count, in);

    for (cc = istart; cc < ns; cc++) {
      c = cc + oset;
      var[c] = in[cc];
    }
    d_free_1d(in);
  } else
    hd_quit("hd_ts_multifile_eval_sparse: No timeseries files specified.\n");
}

/* END hd_ts_multifile_eval_ugrid2D()                                */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Read a UGRID layered topology file directly into a 2D variable.   */
/* This routine is the same as hd_ts_multifile_eval_ugrid2D() except */
/* the variable ids are supplied rather than the variable name.      */ 
/*-------------------------------------------------------------------*/
void frc_multifile_eval_ugrid2D(master_t *master, int ntsfiles, 
				timeseries_t **tsfiles, int *varids, 
				double *var, double ti, int ns, int thio)
{
  geometry_t *geom = master->geom;

  if (ntsfiles > 0) {
    int i, cc, c;
    int index = -1;
    int timeindex;
    double t = ti;
    size_t start[4];
    size_t count[4];
    timeseries_t *ts;
    datafile_t *df;
    df_variable_t *v;
    int r0, r1;
    double rfrac, rOrfrac;
    double *in = d_alloc_1d(ns);
    int istart = (master->tmode & SP_START1) ? 1 : 0;
    int oset = (istart) ? 0 : 1;

    start[1] = 0;
    start[2] = 0;
    start[3] = 0;
    count[0] = 1L;
    count[2] = 0;
    count[3] = 0;

    /*---------------------------------------------------------------*/
    /* If there is a list of files, find the file in the list that   */
    /* contains the time ti.                                         */
    for (i = 0; i < ntsfiles; ++i) {
      ts = tsfiles[i];
      if (varids[i] < 0) continue;
      index = i;
      t = ti;

      if (ts->t_mod_type != MOD_NONE)
	t = get_file_time(ts, t);
      if ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))
	break;
    }
    assert(index >= 0);
    ts = tsfiles[index];
    v = df_get_variable(ts->df, varids[index]);
    df = ts->df;

    /*---------------------------------------------------------------*/
    /* Get the correct dump                                          */
    if (df->ncid == -1) {
      /* Find the correct file in the multi-netcdf file list         */
      if ((timeindex = dump_choose_multifile(df, t)) == -1)
	hd_quit("frc_multifile_eval_ugrid2D: The UGRID forcing file does not contain the time %.2f.\n", t);
    } else {
      /* Find the record in the file; this is either the correct     */
      /* in the multi-file list located in dump_choose_multifile()   */
      /* above, or tsfiles[index] located above.                     */
      df_find_record(df, t, &r0, &r1, &rfrac);
      /*
       * This might seem like a bit of a hack but its here to get
       * around extremely minute numerical issues arising from Trike
       * running with TIMEUNIT like "seconds since 2014-01-01 +10" and
       * then START and STOP of 0 and 1 respectively and the
       * OUTPUT_TIMEUNIT of "days since 1990-01-01 +10" that end up in
       * the output trans files. So we end up with numbers like:
       * 8978.00000000015 -> which is close enough to 8978. The
       * df_find_record call above will therefore have a non-zero frac
       * value.
       */
      rOrfrac = round(rfrac);
      if (rfrac > 0.0 && fabs(rOrfrac-rfrac) < START_EPS) {
	/* Snap to the nearest value */
	r0 += rOrfrac;
      } // no else, we'll assume rfrac will always be positive

      timeindex = r0;
      /* When the time ti is greater than the last time in the file, */
      /* a new file in the multi-file list is located. If this isn't */
      /* located, the program terminates.                            */
      if (fabs(t - df->records[r0]) > START_EPS) {
	if ((timeindex = dump_choose_multifile(df, t)) == -1)
	  hd_quit("frc_multifile_eval_ugrid2D: The UGRID forcing file does not contain the time %.2f.\n", t);
      }
    }

    /*---------------------------------------------------------------*/
    /* Read in the variable data                                     */
    start[0] = timeindex;
    count[1] = ns;

    /*
     * The buffered function reads ahead in parallel - see also the
     * associated cleanup function in dump_choose_multifile
     */
    if (thio)
      nc_buffered_get_vara_double(df->ncid, v, varids[index], start, count, in);
    else
      nc_get_vara_double(df->ncid, varids[index], start, count, in);

    for (cc = istart; cc < ns; cc++) {
      c = cc + oset;
      var[c] = in[cc];
    }
    d_free_1d(in);
  } else
    hd_quit("hd_ts_multifile_eval_sparse: No timeseries files specified.\n");
}

/* END frc_multifile_eval_ugrid2D()                                  */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Finds a record corresponding to a given time in a multi-netCDF    */
/* file.                                                             */
/*-------------------------------------------------------------------*/
int dump_choose_multifile(datafile_t *df, double t)
{
  df_multi_t *fd = (df_multi_t *)df->private_data;
  int i, r0, r1, n;
  double rfrac;
  char timeunits[MAXSTRLEN];

  if (fd != NULL) {
    /* Loop through the files in the multi-file list                 */
    for (i=fd->last_fidx; i<fd->nfiles; ++i) {
      df_multi_file_t *f = &fd->files[i];
      /* Open the netcdf file                                        */
      if (nc_open(f->filename, NC_NOWRITE, &df->ncid) == NC_NOERR) {
	/* Update index */
	fd->last_fidx = i;
	/* Copy the records to the df structure (adjusting for the   */
	/* timeunit and reset df->nrecords = f->nrecords.            */
	memset(timeunits, 0, MAXSTRLEN);
	nc_get_att_text(df->ncid, ncw_var_id(df->ncid, "t"), "units", timeunits);
	df->nrecords = f->nrecords;
	for (n = 0; n < f->nrecords; n++) {
	  df->records[n] = f->records[n];
	  tm_change_time_units(timeunits, schedule->units, &df->records[n], 1);
	}
	/* Find the record closest to t                              */
	df_find_record(df, t, &r0, &r1, &rfrac);
	if (r0 == r1 && fabs(t - df->records[r0]) > START_EPS) {
	  /* Reached the end of this file, go to the next */
	  nc_buffered_clean_up_all_vars(df);
	  nc_close(df->ncid);
	  df->ncid = -1;
	} else if (r0 < r1 && fabs(t - df->records[r0]) > START_EPS) {
	  /* Not an exact match, bail out */
	  return(-1);
	} else {
	  return(r0);
	}
      }
    }
  }
  return(-1);
}

/* END dump_choose_multifile()                                       */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Old code : dump_choose_by_time_s is inefficient since it reads    */
/* the file one record at a time.                                    */
/*-------------------------------------------------------------------*/
int dump_choose_multifile_o(datafile_t *df, double t)
{
  df_multi_t *fd = (df_multi_t *)df->private_data;
  int timeindex, i;

  if (fd != NULL) {
    for (i=0; i<fd->nfiles; ++i) {
      df_multi_file_t *f = &fd->files[i];
      if (nc_open(f->filename, NC_NOWRITE, &df->ncid) == NC_NOERR) {
	if ((timeindex = dump_choose_by_time_s(df->ncid, t)) == -1) {
	  nc_close(df->ncid);
	  df->ncid = -1;
	} else
	  return(timeindex);
      }
    }
  }
  return(-1);
}

int dump_choose_by_time_s(int fid, double t)
{
  unsigned int i;
  size_t n;
  size_t start;
  size_t count;
  double tvals;
  char timeunits[MAXSTRLEN];

/* This is a little naughty. The times should have been mapped
 * to a common base, and then compared.
 */
  nc_inq_dimlen(fid, ncw_dim_id(fid, "record"), &n);

  if (n < 1)
    return(-1);

  memset(timeunits, 0, MAXSTRLEN);
  nc_get_att_text(fid, ncw_var_id(fid, "t"), "units", timeunits);
  for (i = 0; i < n; ++i) {
    start = i;
    count = 1;
    nc_get_vara_double(fid, ncw_var_id(fid, "t"), &start, &count, &tvals);
    tm_change_time_units(timeunits, schedule->units, &tvals, 1);
    if (fabs(tvals - t) < 0.001) {
      return i;
    }
  }

  return -1;
}

/* END Old code                                                      */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Evaluate in the first appropriate file the specified variable     */
/* at the given time for the whole sparse array. The variable value  */
/* is interpolated in time.                                          */
/* @param ntsfiles Number of timeseries files in array.              */
/* @param tsfiles Array of timeseries files.                         */
/* @param names Array of timeseries file names.                      */
/* @param varname Name of variable to check.                         */
/* @param var Array to store values.                                 */
/* @param t time value                                               */
/* @param ns Sparse array size                                       */
/* @return The interpolated value (NaN if failed).                   */
/*-------------------------------------------------------------------*/
void hd_ts_multifile_eval_isparse(int ntsfiles, timeseries_t **tsfiles,
				 cstring * names, char *varname, double *var,
				  double ti, int ns, int thIO)
{
  int varids[MAXNUMTSFILES];

  if (hd_ts_multifile_get_index(ntsfiles, tsfiles, 
				names, varname, varids) == 0)
    hd_quit("hd_ts_multifile_eval_sparse: Unable to evaluate the variable '%s'.\n", varname);

  if (ntsfiles > 0) {
    int i, cc;
    int index = -1;
    double t = ti;
    size_t start[2];
    size_t count[2];

    start[1] = 0;
    count[0] = 1L;

    for (i = 0; i < ntsfiles; ++i) {
      timeseries_t *ts = tsfiles[i];
      if (varids[i] < 0) continue;

      index = i;
      t = ti;
      /* If the variable exists in the file and the current time is within
         the bounds of the timeseries files domain, then evaluate the
        point. */
      if (ts->t_mod_type != MOD_NONE)
	t = get_file_time(ts, t);
      if ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))
	break;
    }

    assert(index >= 0);

    ts_eval_sparse(tsfiles[index], varids[index], t, ns, var);

    /* Interpolate the sparse array for the given time */
    /*
    for (cc = 0; cc < ns; cc++)
      var[cc] = ts_eval_isparse(tsfiles[index], varids[index], t, cc);
    */
  } else
    hd_quit("hd_ts_multifile_eval_sparse: No timeseries files specified.\n");
}

/* END hd_ts_multifile_eval_isparse()                                */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Evaluate a time series at a specified time.                       */
/* This function interpolates in time but not space.                 */
/* @param ts pointer to time series structure                        */
/* @param id index of variable to evaluate                           */
/* @param t time value                                               */
/* Calls quit() if something goes wrong.                             */
/*-------------------------------------------------------------------*/
double ts_eval_isparse(timeseries_t *ts, int id, double t, int cc)
{
  datafile_t *df = ts->df;
  df_variable_t *v = df_get_variable(df, id);

  if (v == NULL)
    quit("ts_eval_isparse: Invalid variable id specified (%d).\n", id);

  /* If 0 dimension, then the best we can do is give the interpolated time 
     value */
  if (v->nd == 0)
    return df_eval(df, v, t);

  return df_eval_isparse(df, v, t, cc);
}

/* END ts_eval_isparse()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Interpolate the variable at known coordinates and record.         */
/* @param df pointer to datafile structure.                          */
/* @param v pointer to variable structure.                           */
/* @param r record value.                                            */
/* @param coords coordinates values.                                 */
/* @return Interpolated value.                                       */
/* @see quit() If anything goes wrong.                               */
/*-------------------------------------------------------------------*/
double df_eval_isparse(datafile_t *df, df_variable_t *v, double r, int cc)
                      
{
  double val = 0.0;

  /* Sanity checks */
  if (df == NULL)
    quit("df_eval_coords: NULL Datafile pointer\n");
  if (v == NULL)
    quit("df_eval_coords: NULL Variable pointer\n");

  if (v->nd == 0) {
    val = df_eval(df, v, r);
  } else {
    int i;
    int r0, r1;
    double rfrac;
    double recvals[2];
    int is[1];

    recvals[0] = 0.0;
    recvals[1] = 0.0;

    /* Find nearest records in table */
    if ((v->dim_as_record) && (df->records != NULL))
      df_find_record(df, r, &r0, &r1, &rfrac);
    else {
      r0 = r1 = 0;
      rfrac = 0.0;
    }
    if (isnan(rfrac)) rfrac = 0.0;

    if ((df->rec_modulus) && (r0 > r1))
      r1 += df->nrecords;

    /* Read in the data for each record and interpolate the coordinates
       for each record. Store the interpolated value for each record in
       the recvals array. */
    df_read_records(df, v, r0, r1 - r0 + 1);
    for (i = r0; i <= r1; ++i) {
      int j = i - r0;
      recvals[j] = 0.0;

      is[0] = cc;
      recvals[j] = df_get_data_value(df, v, i, is);
    }

    /* Interpolate the record */
    val = recvals[0] * (1.0 - rfrac) + recvals[1] * rfrac;
  }

  return val;
}

/* END df_eval_isparse()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Evaluate a time series at a specified time.                       */
/* This function interpolates in time but not space.                 */
/* If the input time corresponds to a record in the file (rfrac=0)   */
/* then the variable at that record is returned, otherwise an        */
/* interpolation in time is performed.                               */
/* interpolation in time or space.                                   */
/* @param ts pointer to time series structure                        */
/* @param id index of variable to evaluate                           */
/* @param t time value                                               */
/* Returns 0 if something goes wrong.                                */
/*-------------------------------------------------------------------*/
int ts_eval_sparse(timeseries_t *ts, int id, double t, int ns, double *var)
{

  datafile_t *df = ts->df;
  df_variable_t *v = df_get_variable(df, id);
  int i, cc, r0, r1;
  double rfrac;
  double recvals[2];
  int is[1];
  
  /* Sanity checks */
  if (df == NULL) return(0);
  if (v == NULL) return(0);

  /* If 0 dimension, then the best we can do is give the interpolated time 
     value */
  if (v->nd == 0) {
    for (cc = 0; cc < ns; cc++)
      var[cc] = df_eval(df, v, t);
    return(1);
  }

  recvals[0] = 0.0;
  recvals[1] = 0.0;

  /* Find nearest records in table */
  if ((v->dim_as_record) && (df->records != NULL))
    df_find_record(df, t, &r0, &r1, &rfrac);
  else {
    r0 = r1 = 0;
    rfrac = 0.0;
  }
  if (isnan(rfrac)) rfrac = 0.0;

  if ((df->rec_modulus) && (r0 > r1))
    r1 += df->nrecords;

  /* Read in the data for each record and interpolate the coordinates
     for each record. Store the interpolated value for each record in
     the recvals array. */
  if (!((v->data != NULL) && (v->start_record == r0)
        && (v->nrecords == r1 - r0 + 1)))
    df_read_records(df, v, r0, r1 - r0 + 1);

  if (rfrac == 0.0) {
    r0 = r0 - v->start_record;
    memcpy(var, VAR_1D(v)[r0], ns * sizeof(double));
    /*
    for (cc = 0; cc < ns; cc++) {
      is[0] = cc;
      var[cc] = df_get_data_value(df, v, r0, is);
    }
    */
  } else {
    for (cc = 0; cc < ns; cc++) {
      for (i = r0; i <= r1; ++i) {
	int j = i - r0;
	recvals[j] = 0.0;
	is[0] = cc;
	recvals[j] = df_get_data_value(df, v, i, is);
      }

      /* Interpolate the record */
      var[cc] = recvals[0] * (1.0 - rfrac) + recvals[1] * rfrac;
    }
  }
  return(1);
}

/* ts_eval_sparse()                                                  */
/*-------------------------------------------------------------------*/


/** Interpolate the variable at known coordinates and record.
  *
  * @param df pointer to datafile structure.
  * @param v pointer to variable structure.
  * @param r record value.
  * @param coords coordinates values.
  * @return Interpolated value.
  *
  * @see dfGetNumCoordinates
  * @see dfGetCoordIds
  * @see quit() If anything goes wrong.
  */
double df_eval_sparse_coords(datafile_t *df, df_variable_t *v, double r,
			     double coords[])
{
  double val = 0.0;

  /* Sanity checks */
  if (df == NULL)
    quit("df_eval_coords: NULL Datafile pointer\n");
  if (v == NULL)
    quit("df_eval_coords: NULL Variable pointer\n");

  if (v->nd == 0) {
    val = df_eval(df, v, r);
  } else {
    int i;
    int r0, r1;
    double rfrac;
    double recvals[2];
    df_coord_system_t *cs = v->csystem;
    int nc = 0;
    int nd = 0;
    int in[2];

    recvals[0] = 0.0;
    recvals[1] = 0.0;
    in[0] = 0;
    in[1] = 0;

    if (cs == NULL)
      quit
        ("df_eval_coords: No coordinate system specified for variable '%s'\n",
         v->name);

    nc = df_get_num_coords(df, v);
    nd = df_get_num_dims(df, v);
    /* Find nearest records in table */
    if ((v->dim_as_record) && (df->records != NULL))
      df_find_record(df, r, &r0, &r1, &rfrac);
    else {
      r0 = r1 = 0;
      rfrac = 0.0;
    }

    if ((df->rec_modulus) && (r0 > r1))
      r1 += df->nrecords;

    /* Read in the data for each record and interpolate the coordinates
       for each record. Store the interpolated value for each record in
       the recvals array. */
    df_read_records(df, v, r0, r1 - r0 + 1);

    for (i = r0; i <= r1; ++i) {
      int j = i - r0;
      recvals[j] = 0.0;

      /* Are there the same number of coordinates as dimensions. We shall
         assume a regular grid. */
      if (nc == nd)
        recvals[j] = v->interp(df, v, i, coords);

      /*recvals[j] = interp_linear(df, v, i, coords);*/

      /* Are there more coordinate than dimensions, and is there only one
         dimension. If so, then we can assume than we shall interpolate
         using an inverse weighting scheme. */
      else if (nc > nd) {
        if (nd == 1)
          recvals[j] = interp_1d_inv_weight(df, v, i, coords);
        else if (nd == 2)
          recvals[j] = interp_2d_inv_weight(df, v, i, coords);
        else
          quit
            ("df_eval_coords: Unable to interpolate data with %d coordinate and %d dimensions.\n",
             nc, nd);
      } else
        quit("df_eval_coords: Less coordinates than dimensions.\n");
    }

    /* Interpolate the record */
    val = recvals[0] * (1.0 - rfrac) + recvals[1] * rfrac;
  }

  return val;
}


int check_sparse_dumpfile(geometry_t *geom, int ntsfiles, 
			  timeseries_t **tsfiles,
			  cstring * names)
{
  int i, j, n;
  int szcS, szc;
  int szeS, sze, nz;
  int nfiles;
  FILE *fp;
  char buf[MAXSTRLEN];
  double ver;
  int isnc;

  szcS = geom->b2_t; szc = geom->b3_t;
  szeS = geom->n2_e1; sze = geom->n3_e1;
  nz = geom->nz;

  if (ntsfiles > 0) {
    for (i = 0; i < ntsfiles; ++i) {
      char *fname = fv_get_filename(names[i], buf);
      if (master->tmode & SP_STRUCT)
	n = read_sparse_dims_struct(fname, szcS, szc, geom->nce1, geom->nce2, nz);
      else
	n = read_sparse_dims(fname, szcS, szc, szeS, sze, nz);
      if (n) {
	if (n == 2) return(1);
	if ((fp = fopen(fname, "r")) != NULL) {
	  prm_set_errfn(quiet);
	  if (prm_skip_to_end_of_key(fp, "multi-netcdf-version")) {
	    prm_flush_line(fp);
	    prm_set_errfn(quit);
	    prm_read_double(fp, "multi-netcdf-version", &ver);
	    prm_read_int(fp, "nfiles", &nfiles);
	    if (nfiles > 0) {
	      for (j = 0; j < nfiles; ++j) {
		sprintf(buf, "file%d.filename", j);
		prm_read_char(fp, buf, fname);
		if (master->tmode & SP_STRUCT)
		  n = read_sparse_dims_struct(fname, szcS, szc, geom->nce1, geom->nce2, nz);
		else
		  n = read_sparse_dims(fname, szcS, szc, szeS, sze, nz);
		if (n) {
		  fclose(fp);
		  return(1);
		}
	      }
	    }
	  } else
	    return(1);
	  fclose(fp);
	}
      }
    }
  } else {
    return(1);
  }
  return(0);
}


/*-------------------------------------------------------------------*/
/* Reads the dimensions from a transport file and compares them with */
/* those of an input file to check for compatibility.                */
/*-------------------------------------------------------------------*/
int read_sparse_dims(char *name, int szcS, int szc, int szeS, int sze, int nz)
		      
{
  size_t mc2 = -1, mc3 = -1;
  size_t me2 = -1, me3 = -1, mz = -1;
  int mc3i, me3i;
  int ncid;

  if (nc_open(name, NC_NOWRITE, &ncid) == NC_NOERR) {
    /* Look for ugrid3 dimensions                                    */
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "nMesh3_face"), &mc2);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "nMesh3_vol"), &mc3);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "nMesh3_edge"), &me2);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "nMesh3_vol_edge"), &me3);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "Mesh3_layerfaces"), &mz);
    /*
    if (szcS != mc2 || szc != mc3 || szeS != me2 || sze != me3 || nz != mz-1) {
      hd_warn("Input file: face2D=%d, face3D=%d edge2D=%d, edge3D=%d, nz=%d\n",szcS, szc, szeS, sze, nz);
      hd_warn("Trans file: face2D=%d, face3D=%d edge2D=%d, edge3D=%d, nz=%d\n",mc2, mc3, me2, me3, mz-1);
      return(2);
    }
    */
    if (szcS != mc2 || szc != mc3 || nz != mz-1) {
      hd_warn("Input file: face2D=%d, face3D=%d nz=%d\n",szcS, szc, nz);
      hd_warn("Trans file (UGRID3): face2D=%d, face3D=%d nz=%d\n",mc2, mc3, mz-1);

      /* Look for ugrid2 dimensions                                  */
      nc_inq_dimlen(ncid, ncw_dim_id(ncid, "nMesh2_face"), &mc2);
      nc_get_att_int(ncid, NC_GLOBAL, "nface3", &mc3i);
      nc_inq_dimlen(ncid, ncw_dim_id(ncid, "nMesh2_edge"), &me2);
      nc_get_att_int(ncid, NC_GLOBAL, "nedge3", &me3i);
      nc_inq_dimlen(ncid, ncw_dim_id(ncid, "Mesh2_layerfaces"), &mz);
      nc_close(ncid);
      if (szcS != mc2 || szc != mc3i || nz != mz-1) {
	hd_warn("Input file: face2D=%d, face3D=%d nz=%d\n",szcS, szc, nz);
	hd_warn("Trans file (UGRID2): face2D=%d, face3D=%d nz=%d\n",mc2, mc3, mz-1);
	return(2);
      }
    }
    nc_close(ncid);
    return(0);
  }
  return(1);
}


int read_sparse_dims_struct(char *name, int ns2, int ns3, int nce1, int nce2, int nz)
		      
{
  size_t ms2 = -1, ms3 = -1;
  size_t mce1 = -1, mce2 = -1, mz = -1;
  int mc1, mc2;
  int ncid; 
  int cc, c, cs, i, j, k, n;
  size_t start[4];
  size_t count[4];
  int *s2i, *s2j, *s2k, i1f, i2f, i3f;
  double **x, **y;

  start[0] = 0;
  start[1] = 0;
  start[2] = 0;
  start[3] = 0;
  /* time independent variables */
  count[0] = 0;
  count[1] = 0;
  count[2] = 0;
  count[3] = 0;

  /* Get the cell centre coordinates */
  if (nc_open(name, NC_NOWRITE, &ncid) == NC_NOERR) {
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "ns2"), &ms2);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "ns3"), &ms3);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "i_centre"), &mce1);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "j_centre"), &mce2);
    nc_inq_dimlen(ncid, ncw_dim_id(ncid, "k_grid"), &mz);

    count[0] = ms3;
    s2i = i_alloc_1d(ms3+1);
    s2j = i_alloc_1d(ms3+1);
    s2k = i_alloc_1d(ms3+1);
    x = d_alloc_2d(mce1, mce2);
    y = d_alloc_2d(mce1, mce2);

    if ((i1f = ncw_var_id(ncid, "s2i")) >= 0) {
      nc_get_vara_int(ncid, ncw_var_id(ncid, "s2i"), start, count, s2i);  
    }
    else {
      hd_warn("Can't find s2i in %s : is this a structured sparse file?\n", name);
      return(2);
    }
    if ((i2f = ncw_var_id(ncid, "s2j")) >= 0) {
      nc_get_vara_int(ncid, ncw_var_id(ncid, "s2j"), start, count, s2j);
    } else {
      hd_warn("Can't find s2i in %s : is this a structured sparse file?\n", name);
      return(2);
    }
    if ((i3f = ncw_var_id(ncid, "s2k")) >= 0)
      nc_get_vara_int(ncid, ncw_var_id(ncid, "s2k"), start, count, s2k);
    else {
      hd_warn("Can't find s2i in %s : is this a structured sparse file?\n", name);
      return(2);
    }
    count[0] = mce2;
    count[1] = mce1;

    nc_get_vara_double(ncid, ncw_var_id(ncid, "x_centre"), start, count, x[0]);
    nc_get_vara_double(ncid, ncw_var_id(ncid, "y_centre"), start, count, y[0]);  
    /*
    nc_get_att_int(ncid, NC_GLOBAL, "NCE1", &mce1);
    nc_get_att_int(ncid, NC_GLOBAL, "NCE2", &mce2);
    */
    nc_close(ncid);
    if (nce1 != mce1 || nce2 != mce2 || nz != mz-1) {
      hd_warn("Different dimensions in %s to the input file\n", name);
      return(2);
    }

    /* Get the mapping via s2i and s2j arrays                        */
    geom->s2c = i_alloc_1d(ms3 + 1);
    geom->ns2 = ms2;
    geom->ns3 = ms3;
    for (cc = 0; cc <= geom->b3_t; cc++) {
      c = geom->w3_t[cc];
      for (n = 0; n < ms3; n++) {
	if (geom->s2i[c] == s2i[n] && geom->s2j[c] == s2j[n] && geom->s2k[c] == s2k[n]) {
	  geom->s2c[n+1] = c;
	  break;
	}
      }
    }
    /* Normal open boundary cells */
    for (i = 0; i < geom->nobc; i++) {
      open_bdrys_t *open = geom->open[i];
      for (cc = 1; cc <= open->no3_e1; cc++) {
	c = open->ogc_t[cc];
	cs = geom->m2d[c];
	for (n = 0; n < ms3; n++) {
	  if (geom->s2i[cs] == s2i[n] && geom->s2j[cs] == s2j[n] && geom->s2k[c] == s2k[n]) {
	    geom->s2c[n+1] = c;
	    break;
	  }
	}
      }
    }
    i_free_1d(s2i);
    i_free_1d(s2j);
    i_free_1d(s2k);
    return(0);
  }
  return(1);
}

/*-------------------------------------------------------------------*/
/* Routine to perform a global fill and smooth the GHRSST SST data.  */
/*-------------------------------------------------------------------*/
void process_ghrsst(master_t *master  /* Master data                 */
		    )
{
  geometry_t *geom = master->geom;
  double sfact = 100.0;              /* Scaing for std_dev threshold */
  double tolmn = 0.0;                /* Error tolerance              */
  double tolmx = 1.0;                /* Error tolerance              */
  int c, c1, cc, n, sn;
  double *sst = master->ghrsst;
  double *sste = master->ghrsste;
  double mean_sst, std_dev, ms;
  GRID_SPECS *gs = NULL;
  double *x, *y, *z;
  char *i_rule = "nn_sibson";

  /*if (master->ghrsst_type & G_HIM) return;*/

  /*-----------------------------------------------------------------*/
  /* Get the mean sst                                                */
  if (master->ghrsst_type & (G_BOM|G_HIM)) {
    tolmn = 5.0;
    tolmx = 5.0;
  }
  mean_sst = 0.0;
  std_dev = 0.0;
  n = 0;
  for(cc = 1; cc <= geom->b2_t; cc++) {
    c = geom->w2_t[cc];    
    if (fabs(sste[c]) >= tolmn && fabs(sste[c]) <= tolmx) {
      mean_sst += sst[c];
      n += 1;
    }
  }
  if (n) {
    mean_sst /= (double)n;
    for(cc = 1; cc <= geom->b2_t; cc++) {
      c = geom->w2_t[cc];    
      if (fabs(sste[c]) >= tolmn && fabs(sste[c]) <= tolmx) {
	std_dev += (sst[c] - mean_sst) * (sst[c] - mean_sst);
      }
    }
    std_dev = sqrt(std_dev / (double)(n - 1));
  }
  else {
    hd_warn("No valid SST values in GHRSST file, time %f\n", master->t/86400);
    return;
  }

  /*-----------------------------------------------------------------*/
  /* Perform a global fill on the data at land locations             */
  n = 0;
  x = d_alloc_1d(geom->szcS);
  y = d_alloc_1d(geom->szcS);
  z = d_alloc_1d(geom->szcS);
  for(cc = 1; cc <= geom->b2_t; cc++) {
    c = geom->w2_t[cc];
    if (fabs(sste[c]) >= tolmn && fabs(sste[c]) <= tolmx) {
      x[n] = geom->cellx[c];
      y[n] = geom->celly[c];
      z[n] = sst[c];
      n++;
    }
  }
  /* Initiliase the grid specs struct                                */
  gs = grid_interp_init(x, y, z, n, i_rule);

  /* Do the interpolation                                            */
  for (cc = 1; cc <= geom->b2_t; cc++) {
    c = geom->w2_t[cc];
    if (fabs(sste[c]) < tolmn || fabs(sste[c]) > tolmx)
      sst[c] = grid_interp_on_point(gs, geom->cellx[c], geom->celly[c]);

    /* Check for nan's                                               */
    if (isnan(sst[c])) sst[c] = mean_sst;
    if (fabs(sst[c] - mean_sst) > sfact * std_dev) sst[c] = mean_sst;
  }
	  
  /* Cleanup                                                         */
  grid_specs_destroy(gs);
  d_free_1d(x);
  d_free_1d(y);
  d_free_1d(z);
}

/* END process_ghrsst()                                              */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Reads structured 2D tracer values from a netCDF file, packs into  */
/* a vector and interpolates onto an unstructured mesh.              */
/*-------------------------------------------------------------------*/
int read_ghrsst(master_t *master,       /* Master data               */
		int ntsfiles,           /* Number of ts files        */
		timeseries_t **tsfiles, /* Timeseries structures     */
		cstring * names,        /* File names                */
		double t                /* Time                      */
		)
{
  geometry_t *geom = master->geom;
  char buf[MAXSTRLEN];
  GRID_SPECS *gs = NULL;
  int nvar;
  double *x, *y, *v, **cellx, **celly, **sst, **sste;
  double vmean, mv;
  int fid;
  int ncerr;
  size_t start[4];
  size_t count[4];
  size_t nce1;
  size_t nce2;
  int ti, index;
  int n, i, j, c, cc;
  int bverbose = 0;
  char *i_rule = "nn_sibson";
  char *vname = "analysed_sst";
  char *ename = "analysis_error";
  double tol = 1.0;                  /* Error tolerance              */

  /*-----------------------------------------------------------------*/
  /* Open the dump file for reading                                  */
  if (ntsfiles > 0) {
    double t = ti;
    start[1] = 0;
    count[0] = 1L;

    for (i = 0; i < ntsfiles; ++i) {
      timeseries_t *ts = tsfiles[i];
      if ((t >= ts->t[0]) && (t <= ts->t[ts->nt - 1]))
	break;
    }
    index = i;
  }

  /*-----------------------------------------------------------------*/
  /* Open the dump file for reading                                  */
  if ((ncerr = nc_open(names[index], NC_NOWRITE, &fid)) != NC_NOERR) {
    hd_warn("read_ghrsst: Can't find input file %s\n", names[index]);
    return(1);
  }

  /* Get dimensions                                                  */
  nc_inq_dimlen(fid, ncw_dim_id(fid, "lat"), &nce2);
  nc_inq_dimlen(fid, ncw_dim_id(fid, "lon"), &nce1);

  /* Get the time index                                              */
  ti = dump_choose_by_time_m(master, fid, t);
  if (ti == -1) return(1);

  /* Allocate and read                                               */
  cellx = d_alloc_2d(nce1, nce2);
  celly = d_alloc_2d(nce1, nce2);
  sst = d_alloc_2d(nce1, nce2);
  sste = d_alloc_2d(nce1, nce2);
  start[0] = 0L;
  start[1] = 0L;
  start[2] = 0L;
  start[3] = 0L;
  count[0] = nce2;
  count[1] = nce1;
  count[2] = 0;
  count[3] = 0;
  nc_get_vara_double(fid, ncw_var_id(fid, "lon"), start, count, cellx[0]);
  nc_get_vara_double(fid, ncw_var_id(fid, "lat"), start, count, celly[0]);
  start[0] = ti;
  start[1] = 0L;
  start[2] = 0L;
  start[3] = 0L;
  count[0] = 1L;
  count[1] = nce2;
  count[2] = nce1;
  count[3] = 0;
  nc_get_vara_double(fid, ncw_var_id(fid, vname), start, count, sst[0]);
  nc_get_vara_double(fid, ncw_var_id(fid, ename), start, count, sste[0]);

  /*-----------------------------------------------------------------*/
  /* Set the wet GHRSST vector (to interpolate from)                 */
  nvar = n = 0;
  for (j = 0; j < nce2; j++) {
    for (i = 0; i < nce1; i++) {
      if (fabs(sste[j][i]) <= tol) nvar++;
    }
  }
  if (nvar) {
    x = d_alloc_1d(nvar);
    y = d_alloc_1d(nvar);
    v = d_alloc_1d(nvar);
  } else
    hd_quit("read_ghrsst: Can't find valid %s values in file %s.\n", vname, names[index]);

  vmean = 0.0;
  for (j = 0; j < nce2; j++) {
    for (i = 0; i < nce1; i++) {
      if (fabs(sste[j][i]) <= tol) {
	x[n] = cellx[j][i];
	y[n] = celly[j][i]; 
	v[n] = sst[j][i];
	vmean += v[n];
	n++;
      }
    }
  }

  if (n) vmean /= (double)n;
  d_free_2d(cellx);
  d_free_2d(celly);
  d_free_2d(sst);
  d_free_2d(sste);
  nc_close(fid);

  /*-----------------------------------------------------------------*/
  /* Interpolate the tracer value                                    */
  gs = grid_interp_init(x, y, v, nvar, i_rule);
  for (cc = 1; cc <= geom->b2_t; cc++) {
    c = geom->w2_t[cc];
    master->ghrsst[c] = grid_interp_on_point(gs, geom->cellx[c], geom->celly[c]);
    if (isnan(master->ghrsst[c])) master->ghrsst[c] = vmean;
    if (bverbose) printf("%d %f : %f %f\n",c, master->ghrsst[c], geom->cellx[c], geom->celly[c]);
  }
  grid_specs_destroy(gs);
  return(0);
}

/* END read_ghrsst()                                                 */
/*-------------------------------------------------------------------*/


double s_median(geometry_t *geom,       /* Window geometry         */
		double *a,              /* Array                   */
		int sz,                 /* Filter size             */
		int c)                  /* Sparse coodinate        */
{
  int i, j;
  double v[sz];
  int *st;
  int ns = sqrt(sz);

  st = stencil(geom, c, &ns, ST_SIZED, 0);

  for (i = 0; i < ns; i++)
    v[i] = a[st[i]];

  for (i = 0; i < sz - 1; i++)
    for (j = sz - 1; i < j; --j)
      order(&v[j-1], &v[j]);

  ns = floor(sz / 2);
  i_free_1d(st);
  return(v[ns]);
}

double median(geometry_t *geom,       /* Window geometry         */
	      double *a,              /* Array                   */
	      int sz,                 /* Filter size             */
	      int c)                  /* Sparse coodinate        */
{
  int c1, i, j, ii, jj, n;
  double v[sz];
  int ns = sqrt(sz);

  n = floor(ns / 2);
  ii = max(geom->s2i[c] - n, 0);
  jj = max(geom->s2j[c] - n, 0);

  n = 0;
  for (j = jj; j < min(jj + ns, geom->nce2); j++)
    for (i = ii; i < min(ii + ns, geom->nce1); i++) {
      c1 = geom->map[geom->nz - 1][j][i];
      if (c1) {
	v[n++] = a[c1];
      }
    }
  n--;
  for (i = 0; i < n - 1; i++)
    for (j = n - 1; i < j; --j)
      order(&v[j-1], &v[j]);

  ns = floor(n / 2);
  return(v[ns]);
}


int count_leap(char *d, double t)
{
  int sy, y, mo, day;
  int h, mi, s;
  double j;
  char *p;

  /* Strip "units since", if present */
  if ((p = strstr(d, "since")) != NULL)
    p += 5;
  else
    p = d;

  /* Read year, month and day */
  if (sscanf(p, "%d-%d-%d", &sy, &mo, &day) != 3)
    quit("count_leap: Can't understand %s\n", p);
  p = tm_time_to_datestr(t, d);
  if (sscanf(p, "%d-%d-%d", &y, &mo, &day) != 3)
    quit("count_leap: Can't understand %s\n", p);

  h = 0;
  for (s = sy; s < y; s++)
    if (s % 4 == 0 && (s % 100 != 0 || s % 400 == 0))
      h++;
  return(h);
}

/*
 * Wrapper to initialise the grid specs structure from the given file
 */
void hd_ts_grid_interp_multifile(master_t *master, timeseries_t **ts, int nts, int *varids,
				 double *tr, double t, int *vec, int nvec, char *method)
{
  int i, vi;
  int index = -1;

  if (t < ts[0]->t[0])
    index = 0;
  else if ( t > ts[nts-1]->t[ts[nts-1]->nt-1])
    index = nts - 1;
  else {
    /* Find the correct file index */
    for (i=0; i<nts; i++) {
      if ( (t >= ts[i]->t[0]) && (t <= ts[i]->t[ts[i]->nt - 1]) ) {
	index = i;
	break;
      }
    }
  }

  if (index < 0) 
    hd_error("hd_ts_grid_multifile_interp: Multi-file index not found\n");
  else
    hd_ts_grid_interp(master, ts[index], (char*)ts_get_varname(ts[index], varids[index]),
		      tr, t, vec, nvec, method);
}

/*
 * Wrapper to initialise the grid specs structure from the given file
 */
void hd_ts_grid_interp(master_t *master, timeseries_t *ts, char *varname,
		       double *tr, double t, int *vec, int nvec, char *method)
{
  int i,j, nc, *cids = NULL, *coordtypes = NULL;
  int c, cc;
  df_variable_t *var;
  GRID_SPECS *gs = NULL;
  double *vals, *xcoord, *ycoord;
  df_variable_t *xid, *yid;
  int r0, r1;
  double rfrac;
  int  dimid0=0,  dimid1=0;
  int ndimid0=0, ndimid1=0;
  int count = 0;
  int is[2];
  datafile_t *df = ts->df;

  /* Get variable */
  var = df_get_variable_by_name(df, varname);

  /* Figure out the coordinate system */
  if (var->csystem == NULL)
    if (!df_infer_coord_system(ts->df, var))
      hd_quit("Unable to infer the coordinate system for %s\n", varname);

  /* Bail out if not XY */
  nc = df_get_num_coords(ts->df, var);
  cids = df_get_coord_ids(df, var);
  coordtypes = df_get_coord_types(df, var);
  if ( (nc != 2) || !(coordtypes[0] & (VT_X | VT_LONGITUDE)) ||
       !(coordtypes[1] & (VT_Y | VT_LATITUDE)) )
    hd_quit("hd_ts_grid_interp: Expecting 2 XY coordinate types for %s, found %d\n", varname, nc);

  /* Find time */
  df_find_record(df, t, &r0, &r1, &rfrac);

  xid = &df->variables[cids[0]];
  yid = &df->variables[cids[1]];

  /* Check dimensionality */
  if ( (var->dimids[0] != xid->dimids[0]) &&
       (var->dimids[0] != yid->dimids[0]) )
    hd_quit("hd_ts_grid_interp: Mismatched dimension 0\n");
  if ( (var->dimids[1] != xid->dimids[1]) &&
       (var->dimids[1] != yid->dimids[1]) )
    hd_quit("hd_ts_grid_interp: Mismatched dimension 0\n");

  /* Grab dimensions and dimension sizes */
  dimid0 = var->dimids[0]; ndimid0 = df->dimensions[dimid0].size;
  dimid1 = var->dimids[1]; ndimid1 = df->dimensions[dimid1].size;

  /* Allocate the larget possible buffers */
  vals   = d_alloc_1d(ndimid0 * ndimid1);
  xcoord = d_alloc_1d(ndimid0 * ndimid1);
  ycoord = d_alloc_1d(ndimid0 * ndimid1);

  /* Loop over and assign non-fill values */
  for (i=0; i<ndimid0; i++) {
    for (j=0; j<ndimid1; j++) {
      double x,y, recvals[2] = {0.0, 0.0};
      is[0] = i;
      is[1] = j;
      /* Get lat and lon values */
      x = df_get_data_value(df, xid, 0, is);
      y = df_get_data_value(df, yid, 0, is);
      /* Read data */
      df_read_records(df, var, r0, r1 - r0 + 1);
      /* Get the bracketing z values */
      recvals[0] = df_get_data_value(df, var, r0, is);
      recvals[1] = df_get_data_value(df, var, r1, is);
      /* 
       * Skip bogus values 
       */
      //      if (isnan(recvals[0]) || isnan(recvals[1]))
      if (recvals[0] == var->fillvalue || 
	  recvals[1] == var->fillvalue ||
	  isnan(recvals[0]) || 
	  isnan(recvals[1]) )
	continue;
      
      /* Interpolate over time */
      vals[count]   = recvals[0] * (1.0 - rfrac) + recvals[1] * rfrac;
      xcoord[count] = x;
      ycoord[count] = y;
      count++;
    }
  }

  /* All good, init gs */
  gs = grid_interp_init(xcoord, ycoord, vals, count, method);

  /* Interpolate the given points */
  for (cc = 1; cc <= nvec; cc++) {
    c = vec[cc];
    tr[c] = grid_interp_on_point(gs, geom->cellx[c], geom->celly[c]);
  }

  // Cleanup
  d_free_1d(vals);
  d_free_1d(xcoord);
  d_free_1d(ycoord);
  grid_specs_destroy(gs);
}
