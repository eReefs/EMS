/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/hd-us/inputs/meshes.c
 *  
 *  Description:
 *  Routine to create grid meshes.
 *  from a file.
 *  
 *  Copyright:
 *  Copyright (c) 2018. Commonwealth Scientific and Industrial
 *  Research Organisation (CSIRO). ABN 41 687 119 230. All rights
 *  reserved. See the license file for disclaimer and full
 *  use/redistribution conditions.
 *  
 *  $Id: meshes.c 5873 2018-07-06 07:23:48Z riz008 $
 *
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hd.h"

#define DEG2RAD(d) ((d)*M_PI/180.0)
#define RAD2DEG(r) ((r)*180.0/M_PI)

#define RADIUS 6370997.0
#define ECC 0.0

#define GM_W    0x0001
#define GM_E    0x0002
#define GM_N    0x0004
#define GM_S    0x0008
#define GM_NW   0x0010
#define GM_NE   0x0020
#define GM_B    0x0040
#define GM_F    0x0080
#define GM_L    0x0100
#define GM_R    0x0200
#define GM_U    0x0400
#define GM_D    0x0800

/*------------------------------------------------------------------*/
/* Valid bathymetry netCDF dimension names                          */
static char *bathy_dims[3][6] = {
  {"botz", "i_centre", "j_centre", "x_centre", "y_centre", "standard"},
  {"height", "lon", "lat", "lon", "lat", "nc_bathy"},
  {NULL, NULL, NULL, NULL, NULL, NULL}
};

int iswetc(unsigned long flag);
int isdryc(unsigned long flag);
void addpoint(parameters_t *params, int i, int j, int *n, point *pin, int **mask);
double triarea(double ax, double ay, double bx, double by, double cx, double cy);
void reorder_pin(int np, point *pin, int ns, int *sin, int nh, int *hin);
int prm_skip_to_end_of_tok(FILE * fp, char *key, char *token, char *ret);
int SortCornersClockwise(double x0, double y0, double x1, double y1, double rx, double ry);
int find_mesh_vertex(int c, double x, double y, double **xloc, double **yloc, int *mask, int ns2, int *npe);
void order_c(double *a, double *b, int *c1, int *c2);
void sort_circle(delaunay *d, int *vedge, int nedge, int dir);
int find_mindex(double slon, double slat, double *lon, double *lat, int nsl, double *d);
void mesh_init_OBC(parameters_t *params, mesh_t *mesh);
void tria_ball_2d(double *bb, double *p1, double *p2, double *p3);
void tria_com(double *bb, double *p1, double *p2, double *p3);
void remove_duplicates(int ns2, double **x, double **y, mesh_t *mesh);
void mesh_expand(parameters_t *params, double *bathy, double **xc, double **yc);
void xy_to_d(delaunay *d, int np, double *x, double *y);
void circen(double *p1, double *p2, double *p3);
double is_obtuse(double *p0, double *p1, double *p2);
void init_J_jig(jigsaw_jig_t *J_jig);
double coast_dist(msh_t *msh, double xloc, double yloc);
double bathyset(double b, double bmin, double bmax, double hmin,
		double hmax, double expf);


/*-------------------------------------------------------------------*/
/* Compute the geographic metrics on the sphere using a false pole   */
/*-------------------------------------------------------------------*/
void geog_false_pole_coord(double **x,  /* where to store grid x values */
                           double **y,  /* where to store grid y values */
                           double **h1, /* where to store h1 metric values
                                         */
                           double **h2, /* where to store h2 metric values
                                         */
                           double **a1, /* where to store a1 angle values */
                           double **a2, /* where to store a2 angle values */
                           long int nce1, /* number of cells in e1
                                             direction */
                           long int nce2, /* number of cells in e2
                                             direction */
                           double x00,  /* x origin offset */
                           double y00,  /* y origin offset */
                           double flon, /* False longitude */
                           double flat, /* False latitude */
                           double xinc, /* cell size in x direction */
                           double yinc  /* cell size in y direction */
  )
{
  long i, j;
  double fx00, fy00, xval, yval;
  double rflon = DEG2RAD(flon);
  double rflat = DEG2RAD(flat);

  geod_fwd_spherical_rot(DEG2RAD(x00), DEG2RAD(y00), rflon, rflat, &fx00,
                         &fy00);
  xinc = DEG2RAD(xinc);
  yinc = DEG2RAD(yinc);

  for (j = 0; j < nce2 + 1; j++) {
    yval = j * yinc;
    for (i = 0; i < nce1 + 1; i++) {
      xval = i * xinc;
      geod_inv_spherical_rot(fx00 + xval, fy00 + yval, rflon, rflat,
                             &x[j][i], &y[j][i]);
      x[j][i] = RAD2DEG(x[j][i]);
      y[j][i] = RAD2DEG(y[j][i]);
    }
  }

  /* Calculate h1 and h2 numerically */
  grid_get_geog_metrics(x, y, nce1, nce2, h1, h2);

  /* calculate a1, a2 numerically */
  grid_get_geog_angle(x, y, nce1, nce2, a1, a2);

  /* Check ranges; must be 0 to 360 */
  for (j = 0; j < nce2 + 1; j++) {
    for (i = 0; i < nce1 + 1; i++) {
      while (x[j][i] < 0.0 || x[j][i] > 360.0) {
	if (x[j][i] < 0.0) x[j][i] += 360.0;
	if (x[j][i] > 360.0) x[j][i] -= 360.0;
      }
    }
  }
}

/* END geog_false_pole_coord()                                       */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Compute the geographic metrics on the sphere by computing the     */
/* latitude and longitude by dead reckoning. We use the Sodano's     */
/* direct formulation to compute the latitude/longitude given a      */
/* range and bearing.                                                */
/*-------------------------------------------------------------------*/
void geog_dreckon_coord(double **x, /* where to store grid x values */
                        double **y, /* where to store grid y values */
                        double **h1,  /* where to store h1 metric values */
                        double **h2,  /* where to store h2 metric values */
                        double **a1,  /* where to store a1 angle values */
                        double **a2,  /* where to store a2 angle values */
                        long int nce1,  /* number of cells in e1 direction
                                         */
                        long int nce2,  /* number of cells in e2 direction
                                         */
                        double x00, /* x origin offset */
                        double y00, /* y origin offset */
                        double rotn,  /* Rotation */
                        double xinc,  /* cell size in x direction */
                        double yinc /* cell size in y direction */
  )
{
  long i, j;
  double rx00 = DEG2RAD(x00);
  double ry00 = DEG2RAD(y00);
  double xaz = M_PI / 2 - DEG2RAD(rotn);
  double yaz = xaz - M_PI / 2;

#if 0
  for (j = 0; j < nce2 + 1; j++) {
    double slat;                /* Start latitude of line */
    double slon;                /* Start longitude of line */
    geod_fwd_sodanos(rx00, ry00, yaz, j * yinc, RADIUS, ECC, &slon, &slat);
    for (i = 0; i < nce1 + 1; i++) {
      geod_fwd_sodanos(slon, slat, xaz, i * xinc, RADIUS, ECC, &x[j][i],
                       &y[j][i]);
      x[j][i] = RAD2DEG(x[j][i]);
      y[j][i] = RAD2DEG(y[j][i]);
    }
  }
#else
  x[0][0] = rx00;
  y[0][0] = ry00;
  for (j = 0; j < nce2 + 1; j++) {
    if (j > 0)
      geod_fwd_sodanos(x[j - 1][0], y[j - 1][0], yaz, yinc, RADIUS, ECC,
                       &x[j][0], &y[j][0]);
    for (i = 1; i < nce1 + 1; i++)
      geod_fwd_sodanos(x[j][i - 1], y[j][i - 1], xaz, xinc, RADIUS, ECC,
                       &x[j][i], &y[j][i]);
  }

  for (j = 0; j < nce2 + 1; j++) {
    for (i = 0; i < nce1 + 1; i++) {
      x[j][i] = RAD2DEG(x[j][i]);
      y[j][i] = RAD2DEG(y[j][i]);
    }
  }
#endif

  /* Calculate h1 and h2 numerically */
  grid_get_geog_metrics(x, y, nce1, nce2, h1, h2);

  /* calculate a1, a2 numerically */
  grid_get_geog_angle(x, y, nce1, nce2, a1, a2);
}

/* END geog_dreckon_coord()                                          */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Compute the metrics on a plane for a delaunay grid.               */
/*-------------------------------------------------------------------*/
int delaunay_rect_coord(double *x,   /* where to store grid x values */
			double *y,   /* where to store grid y values */
			long int nce1,  /* number of cells in e1 direction
					 */
			long int nce2,  /* number of cells in e2 direction
					 */
			double x00,  /* x origin offset */
			double y00,  /* y origin offset */
			double rotn, /* Rotation */
			double xinc  /* cell size in x direction */
			)
{
  long i, j, n;
  double xval, yval;
  double sinth;
  double costh;
  double **ax, **ay;
  double yinc = (0.5 * xinc) * tan(DEG2RAD(60.0));
  sinth = sin(DEG2RAD(rotn));
  costh = cos(DEG2RAD(rotn));
  ax = d_alloc_2d(nce1+1, nce2+1);
  ay = d_alloc_2d(nce1+1, nce2+1);
  int iend;

  ax[0][0] = x00;
  ay[0][0] = y00;
  for (j = 0; j < nce2 + 1; j++) {
    double oset;
    if (j%2 == 1) {
      iend = nce1;
      oset = 0.5 * xinc;
    } else {
      iend = nce1 + 1;
      oset = 0.0;
    }
    yval = j * yinc;
    for (i = 0; i < iend; i++) {
      xval = i * xinc;
      ax[j][i] = x00 + oset + xval * costh - yval * sinth;
      ay[j][i] = y00 + xval * sinth + yval * costh;
    }
  }

  /* Count the cells */
  n = 0;
  for (j = 0; j < nce2 + 1; j++) {
    for (i = 0; i < nce1 + 1; i++) {
      if (j%2 == 0) {
	n++;	
      } else {
	if (i < nce1) {
	  n++;	
	}
      }
    }
  }
  n = 0;
  for (j = 0; j < nce2 + 1; j++) {
    for (i = 0; i < nce1 + 1; i++) {
      if (j%2 == 0) {
	x[n] = ax[j][i];
	y[n] = ay[j][i];
	n++;	
      } else {
	if (i < nce1) {
	  x[n] = ax[j][i];
	  y[n] = ay[j][i];
	  n++;	
	}
      }
    }
  }
  d_free_2d(ax);
  d_free_2d(ay);
  return(n);
}

/* END delaunay_rect_coord()                                         */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Compute the geographic metrics on the sphere for a delaunay grid  */
/* by computing the latitude and longitude by dead reckoning. We use */
/* the Sodano's direct formulation to compute the latitude/longitude */
/* given a range and bearing.                                        */
/*-------------------------------------------------------------------*/
int delaunay_dreckon_coord(double *x,   /* where to store grid x values */
			   double *y,   /* where to store grid y values */
			   long int nce1,  /* number of cells in e1 direction
					    */
			   long int nce2,  /* number of cells in e2 direction
					    */
			   double x00,  /* x origin offset */
			   double y00,  /* y origin offset */
			   double rotn, /* Rotation */
			   double xinc  /* cell size in x direction */
			   )
{
  long i, j, n;
  double rx00 = DEG2RAD(x00);
  double ry00 = DEG2RAD(y00);
  double xaz = M_PI / 2 - DEG2RAD(rotn);
  double yaz = xaz - M_PI / 2;
  double yinc = (0.5 * xinc) * tan(DEG2RAD(60.0));
  int iend;
  double **ax, **ay;
  int filef = 1;

  ax = d_alloc_2d(nce1+1, nce2+1);
  ay = d_alloc_2d(nce1+1, nce2+1);

  ax[0][0] = rx00;
  ay[0][0] = ry00;
  for (j = 0; j < nce2 + 1; j++) {
    double oset;
    if (j == 0) {
      iend = nce1 + 1;
      oset = 0;
    } else if (j%2 == 1) {
      iend = nce1;
      oset = 0.5 * (ax[j - 1][1] - ax[j - 1][0]);
    } else {
      iend = nce1 + 1;
      oset = -oset;
    }
    if (j > 0) {
      geod_fwd_sodanos(oset + ax[j - 1][0], ay[j - 1][0], yaz, yinc, RADIUS, ECC,
                       &ax[j][0], &ay[j][0]);
    }
    for (i = 1; i < iend; i++)
      geod_fwd_sodanos(ax[j][i - 1], ay[j][i - 1], xaz, xinc, RADIUS, ECC,
                       &ax[j][i], &ay[j][i]);
  }

  /* Count the cells */
  n = 0;
  for (j = 0; j < nce2 + 1; j++) {
    for (i = 0; i < nce1 + 1; i++) {
      if (j%2 == 0) {
	n++;	
      } else {
	if (i < nce1) {
	  n++;	
	}
      }
    }
  }

  n = 0;
  for (j = 0; j < nce2 + 1; j++) {
    for (i = 0; i < nce1 + 1; i++) {
      if (j%2 == 0) {
	x[n] = RAD2DEG(ax[j][i]);
	y[n] = RAD2DEG(ay[j][i]);
	n++;	
      } else {
	if (i < nce1) {
	  x[n] = RAD2DEG(ax[j][i]);
	  y[n] = RAD2DEG(ay[j][i]);
	  n++;	
	}
      }
    }
  }

  if (filef) {
    FILE *op;
    op = fopen("ddg.txt", "w");
    for (i = 0; i < n; i++)
      fprintf(op, "%f %f\n", x[i], y[i]);
    fclose(op);
  }

  d_free_2d(ax);
  d_free_2d(ay);

  return(n);
}

/* END void delaunay_dreckon_coord()                                 */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Finds a false pole                                                */
/*-------------------------------------------------------------------*/
void f_pole(double ilat, double ilon, double ang, double *olat,
            double *olon)
{
  double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0, az = 0.0;
  double qdist = (2.0 * M_PI * RADIUS) / 4.0;

  x1 = DEG2RAD(ilon);
  y1 = DEG2RAD(ilat);
  az = DEG2RAD(ang);

  geod_fwd_sodanos(x1, y1, az, qdist, RADIUS, ECC, &x2, &y2);
  *olat = RAD2DEG(x2);
  *olon = RAD2DEG(y2);
}

/* END f_pole()                                                      */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Converts a curvilinear mesh to a triangulation using grid corners */
/* for even j and the u2 location for odd j.                         */
/*-------------------------------------------------------------------*/
void convert_quad_mesh(parameters_t *params)
{
  FILE *fp;
  int c, i, j, n, m;
  int **mask, **map;
  double **bathy;
  int nce1 = params->nce1;
  int nce2 = params->nce2;
  int nfe1 = params->nfe1;
  int nfe2 = params->nfe2;
  int nz = params->nz;
  unsigned long ***flg, **flag;
  int nbath;
  double *x, *y, *b;
  double **xin, **yin;
  GRID_SPECS *gs = NULL;
  char i_rule[MAXSTRLEN];
  point *pin;
  int *sin = NULL;
  int np, ns = 0;
  int verbose = 0;  /* Print grid info                               */
  int bverbose = 0; /* Print bathymetry info                         */
  int doseg = 1;    /* Create segments around the grid perimeter for the triangulation */
  int landf = 0;    /* Include land cells in the conversion          */
  int filef = 1;    /* Print grid to file                            */
  int limf = 1;     /* Limit bathymetry to bmin and bmax             */
  int dopoints = 1;
  int ***tri;
  double area, na;
  int ip, im, jp, jm;
  double bmean;

  /*-----------------------------------------------------------------*/
  /* Interpolation method. Options:                                  */
  /* linear, cubic, nn_sibson, nn_non_sibson, average                */
  if (params->runmode & ROAM) {
    strcpy(i_rule, "linear");
    landf = 1;
  } else 
    strcpy(i_rule, "linear");
  /*strcpy(i_rule, "nn_non_sibson");*/

  /*-----------------------------------------------------------------*/
  /* Set the (i,j) indexed bathymetry array                          */
  bathy = d_alloc_2d(nce1, nce2);
  n = 0;
  for (j = 0; j < nce2; j++)
    for (i = 0; i < nce1; i++) {
      bathy[j][i] = -params->bathy[n++];
    } 
  if (params->runmode & DUMP) {
    for (n = 1; n <= params->nland; n++) {
      i = params->lande1[n];
      j = params->lande2[n];
      bathy[j][i] = LANDCELL;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Make the flags array                                            */
  flg = (unsigned long ***)l_alloc_3d(nce1 + 1, nce2 + 1, nz);
  params->flag = (unsigned long **)l_alloc_2d(nce1 + 1, nce2 + 1);
  make_flags(params, flg, bathy, params->layers, nce1, nce2, nz);	     
  u1_flags(flg, nce1, nce2, nz);
  u2_flags(flg, nce1, nce2, nz);
  if (params->sigma)
    sigma_flags(nfe1, nfe2, nz-1, flg);
  for (j = 0; j < nfe2; j++)
    for (i = 0; i < nfe1; i++) {
      params->flag[j][i] = flg[nz-1][j][i];
      if (landf && params->flag[j][i] & SOLID) params->flag[j][i] &= ~SOLID;
    }
  for (i = 0; i < nfe1; i++) params->flag[nce2][i] |= OUTSIDE;
  for (j = 0; j < nfe2; j++) params->flag[j][nce1] |= OUTSIDE;
  l_free_3d((long ***)flg);
  flag = params->flag;

  /*-----------------------------------------------------------------*/
  /* Set the wet bathymetry vector (to interpolate from)             */
  nbath = n = 0;
  for (j = 0; j < nce2; j++) {
    for (i = 0; i < nce1; i++) {
      if (iswetc(params->flag[j][i])) nbath++;
    }
  }
  x = d_alloc_1d(nbath);
  y = d_alloc_1d(nbath);
  b = d_alloc_1d(nbath);
  bmean = 0.0;
  for (j = 0; j < nce2; j++) {
    for (i = 0; i < nce1; i++) {
      if (iswetc(params->flag[j][i])) {
      x[n] = params->x[j*2][i*2];
      y[n] = params->y[j*2][i*2];
      b[n] = bathy[j][i];
      bmean += b[n];
      n++;
      }
    }
  }
  if (n) bmean /= (double)n;

  /*-----------------------------------------------------------------*/
  /* Double the resolution for hex conversions                       */
  if (params->uscf & US_HEX) {
    nce1 *= 2;
    nce2 *= 2;
    nfe1 = nce1 + 1;
    nfe2 = nce2 + 1;
    xin = d_alloc_2d(2*nce1+1, 2*nce2+1);
    yin = d_alloc_2d(2*nce1+1, 2*nce2+1);

    /* Create a flag array at double the resolution                  */
    flg = (unsigned long ***)l_alloc_3d(nfe1, nfe2, 1);
    for (j = 0; j < nfe2; j++)
      for (i = 0; i < nfe1; i++) {
	flg[0][j][i] = params->flag[j/2][i/2];
      }
    for (i = 0; i < nce1; i++) flg[0][nce2][i] |= OUTSIDE;
    for (j = 0; j < nce2; j++) flg[0][j][nce1] |= OUTSIDE;
    flag = flg[0];

    /* Create grid metric arrays at double the resolution            */
    for (j = 0; j < 2*nce2+1; j++) {
      for (i = 0; i < 2*nce1+1; i++) {
	xin[j][i] = NOTVALID;
	yin[j][i] = NOTVALID;
      }
    }
    for (j = 0; j < nfe2; j++) {
      for (i = 0; i < nfe1; i++) {
	xin[2*j][2*i] = params->x[j][i];
	yin[2*j][2*i] = params->y[j][i];
	xin[2*j][2*i] = params->x[j][i];
	yin[2*j][2*i] = params->y[j][i];

      }
    }
    for (j = 0; j < 2*nce2+1; j++) {
      for (i = 0; i < 2*nce1+1; i++) {
	if (j%2==0) {
	  if (xin[j][i] == NOTVALID) xin[j][i] = 0.5 * (xin[j][i-1] + xin[j][i+1]);
	  if (yin[j][i] == NOTVALID) yin[j][i] = 0.5 * (yin[j][i-1] + yin[j][i+1]);
	} else {
	  if (xin[j][i] == NOTVALID) xin[j][i] = 0.5 * (xin[j-1][i] + xin[j+1][i]);
	  if (yin[j][i] == NOTVALID) yin[j][i] = 0.5 * (yin[j-1][i] + yin[j+1][i]);
	}
      }
    }
  } else {
    xin = params->x;
    yin = params->y;
  }

  /*-----------------------------------------------------------------*/
  /* Set the vertices to the boundaries of the grid.                 */ 
  mask = i_alloc_2d(nfe1, nfe2);
  map = i_alloc_2d(nfe1, nfe2);
  for (j = 0; j < nfe2; j++)
    for (i = 0; i < nfe1; i++) {
      mask[j][i] = 0;
    }

  /* Count the boundary vertices.                                    */
  np = 0;
  for (j = 0; j < nce2; j++) {
    for (i = 0; i < nce1; i++) {
      if (iswetc(flag[j][i])) {
	if (j%2 == 0) {
	  if (!(mask[j][i] & GM_W)) {
	    mask[j][i] |= GM_W;
	    np++;
	  }
	  if (!(mask[j][i+1] & GM_W)) {
	    mask[j][i+1] |= GM_W;
	    mask[j][i] |= GM_E;
	    np++;
	  }
	  if (!(mask[j+1][i] & GM_S)) {
	    mask[j+1][i] |= GM_S;
	    mask[j][i] |= GM_N;
	    np++;
	  }
	} else {
	  if (!(mask[j][i] & GM_S)) {
	    mask[j][i] |= GM_S;
	    np++;
	  }
	  if (!(mask[j+1][i] & GM_W)) {
	    mask[j+1][i] |= GM_W;
	    mask[j][i] |= GM_W;
	    np++;
	  }
	  if (!(mask[j+1][i+1] & GM_W)) {
	    mask[j+1][i+1] |= GM_W;
	    mask[j+1][i] |= GM_E;
	    np++;
	  }
	}
      }
    }
  }
  for (j = 0; j < nfe2; j++)
    for (i = 0; i < nfe1; i++) {
      mask[j][i] = 0;
      map[j][i] = 0;
    }

  /*-----------------------------------------------------------------*/
  /* Allocate the points                                             */
  n = m = 0;
  pin = malloc(np * sizeof(point));
  for (j = 0; j < nce2; j++) {
    for (i = 0; i < nce1; i++) {
      if (iswetc(flag[j][i])) {
	if (j%2 == 0) {
	  if (!(mask[j][i] & GM_W)) {
	    mask[j][i] |= GM_W;
	    pin[n].x = xin[j*2][i*2];
	    pin[n].y = yin[j*2][i*2];
	    if (verbose) printf("%d e(%d %d) : %f %f\n",n, i, j, pin[n].x, pin[n].y);
	    map[j][i] = n;
	    n++;
	  }
	  if (!(mask[j][i+1] & GM_W)) {
	    mask[j][i+1] |= GM_W;
	    mask[j][i] |= GM_E;
	    pin[n].x = xin[j*2][(i+1)*2];
	    pin[n].y = yin[j*2][(i+1)*2];
	    if (verbose) printf("%d e+(%d %d): %f %f\n",n, i+1, j, pin[n].x, pin[n].y);
	    map[j][i+1] = n;
	    n++;
	  }
	  if (!(mask[j+1][i] & GM_S)) {
	    mask[j+1][i] |= GM_S;
	    mask[j][i] |= GM_N;
	    pin[n].x = xin[(j+1)*2][i*2+1];
	    pin[n].y = yin[(j+1)*2][i*2+1];
	    if (verbose) printf("%d o+(%d %d): %f %f\n",n, i, j+1, pin[n].x, pin[n].y);
	    map[j+1][i] = n;
	    n++;
	  }
	} else {
	  if (!(mask[j][i] & GM_S)) {
	    mask[j][i] |= GM_S;
	    pin[n].x = xin[j*2][i*2+1];
	    pin[n].y = yin[j*2][i*2+1];
	    if (verbose) printf("%d o(%d %d) : %f %f\n",n, i, j, pin[n].x, pin[n].y);
	    map[j][i] = n;
	    n++;
	  }
	  if (!(mask[j+1][i] & GM_W)) {
	    mask[j+1][i] |= GM_W;
	    mask[j][i] |= GM_NW;
	    pin[n].x = xin[(j+1)*2][i*2];
	    pin[n].y = yin[(j+1)*2][i*2];
	    if (verbose) printf("%d e+(%d %d): %f %f\n",n, i, j+1, pin[n].x, pin[n].y);
	    map[j+1][i] = n;
	    n++;
	  }
	  if (!(mask[j+1][i+1] & GM_W)) {
	    mask[j+1][i+1] |= GM_W;
	    mask[j+1][i] |= GM_E;
	    mask[j][i] |= GM_NE;
	    pin[n].x = xin[(j+1)*2][(i+1)*2];
	    pin[n].y = yin[(j+1)*2][(i+1)*2];
	    if (verbose) printf("%d e++(%d %d): %f %f\n",n, i+1, j+1, pin[n].x, pin[n].y);
	    map[j+1][i+1] = n;
	    n++;
	  }
	}
      }
    }
  }

  /* Check for duplicates                                            */
  for (i = 0; i < np; i++) {
    double x = pin[i].x;
    double y = pin[i].y;
    for (n = 0; n < np; n++) {
      if (verbose && n != i && x == pin[n].x && y == pin[n].y)
	printf("Duplicate n=%d, i=%d\n", n, i);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Count the segments                                              */
  if (doseg) {
    ns = 0;
    area = na = 0.0;
    tri = i_alloc_3d(nce1, nce2, 3);
    for (j = 0; j < nce2; j++) {
      for (i = 0; i < nce1; i++) {
	if (iswetc(flag[j][i])) {
	  jp = j + 1;
	  jm = (j == 0) ? nce2 : j - 1;
	  ip = i + 1;
	  im = (i == 0) ? nce1 : i - 1;
	  if (j%2 == 0) {
	    tri[0][j][i] = map[j][i];
	    tri[1][j][i] = map[j][ip];
	    tri[2][j][i] = map[jp][i];
	    area += triarea(pin[map[j][i]].x, pin[map[j][i]].y,
			    pin[map[j][ip]].x, pin[map[j][ip]].y,
			    pin[map[jp][i]].x, pin[map[jp][i]].y);
	    na += 1.0;
	    if (isdryc(flag[jm][i])) {
	      mask[j][i] |= (GM_B|GM_U);
	      ns++;
	    }
	    if (isdryc(flag[jp][i]) && iswetc(flag[j][ip])) {
	      mask[j][i] |= (GM_F|GM_U);
	      ns++;
	    }
	  } else {
	    tri[2][j][i] = map[j][i];
	    tri[0][j][i] = map[jp][i];
	    tri[1][j][i] = map[jp][ip];
	    area += triarea(pin[map[jp][i]].x, pin[map[jp][i]].y,
			    pin[map[jp][ip]].x, pin[map[jp][ip]].y,
			    pin[map[j][i]].x, pin[map[j][i]].y);
	    na += 1.0;
	    if (isdryc(flag[jp][i])) {
	      mask[j][i] |= (GM_F|GM_D);
	      ns++;
	    }
	    if (isdryc(flag[jm][i]) && iswetc(flag[j][ip])) {
	      mask[j][i] |= (GM_B|GM_D);
	      ns++;
	    }
	  }
	  if (isdryc(flag[j][im])) {
	    mask[j][i] |= GM_L;
	    if (j%2 == 0)
	      mask[j][i] |= GM_U;
	    else
	      mask[j][i] |= GM_D;
	    ns++;
	  } 
	  if (isdryc(flag[j][ip])) {
	    mask[j][i] |= GM_R;
	    if (j%2 == 0) 
	      mask[j][i] |= GM_U;
	    else
	      mask[j][i] |= GM_D;
	    ns++;
	  }
	}
      }
    }
    if (verbose) {
      printf("Segments = %d\n",ns);
      for (j = 0; j < nce2; j++) {
	for (i = 0; i < nce1; i++) {
	  if (mask[j][i]) {
	    printf("(%d %d) %x : ", i,j,mask[j][i]);
	    if (mask[j][i] & GM_U) printf("up ");
	    if (mask[j][i] & GM_D) printf("down ");
	    if (mask[j][i] & GM_L) printf("left ");
	    if (mask[j][i] & GM_R) printf("right ");
	    if (mask[j][i] & GM_F) printf("front ");
	    if (mask[j][i] & GM_B) printf("back ");
	    printf("\n");
	    /*printf(" : (%d %d %d)\n",tri[0][j][i], tri[1][j][i], tri[2][j][i]);*/
	  }
	}
      }
    }
    sin = i_alloc_1d(2*ns);
    n = 0;
    for (j = 0; j < nce2; j++) {
      for (i = 0; i < nce1; i++) {
	if (iswetc(flag[j][i])) {
	  if (mask[j][i] & GM_F) {
	    if (mask[j][i] & GM_D) {
	      sin[n++] = tri[0][j][i];
	      sin[n++] = tri[1][j][i];
	      if (verbose) printf("segment %d = %d %d (FD %d,%d)\n",n-2, sin[n-2], sin[n-1], i, j);
	    }
	    if (mask[j][i] & GM_U) {
	      sin[n++] = tri[2][j][i];
	      sin[n++] = tri[2][j][i+1];
	      if (verbose) printf("segment %d = %d %d (FU %d,%d)\n",n-2, sin[n-2], sin[n-1], i, j);
	    }
	  }
	  if (mask[j][i] & GM_B) {
	    if (mask[j][i] & GM_U) {
	      sin[n++] = tri[0][j][i];
	      sin[n++] = tri[1][j][i];
	      if (verbose) printf("segment %d = %d %d (BU %d,%d)\n",n-2, sin[n-2], sin[n-1], i, j);
	    }
	    if (mask[j][i] & GM_D) {
	      sin[n++] = tri[2][j][i];
	      sin[n++] = tri[2][j][i+1];
	      if (verbose) printf("segment %d = %d %d (BD %d,%d)\n",n-2, sin[n-2], sin[n-1], i, j);
	    }
	  }
	  if (mask[j][i] & GM_L) {
	    sin[n++] = tri[0][j][i];
	    sin[n++] = tri[2][j][i];
	    if (verbose) printf("segment %d = %d %d (LUD %d,%d)\n",n-2, sin[n-2], sin[n-1], i, j);
	  }
	  if (mask[j][i] & GM_R) {
	    sin[n++] = tri[1][j][i];
	    sin[n++] = tri[2][j][i];
	    if (verbose) printf("segment %d = %d %d (RUD %d,%d)\n",n-2, sin[n-2], sin[n-1], i, j);
	  }
	}
      }
    }

    if (n != 2*ns) hd_quit("quad_mesh: inconsistent segment numbers (%d != %d)\n",n, ns);
    area /= na;
  }

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    if ((fp = fopen("points.txt", "w")) != NULL) {
      for (i = 0; i < np; i++)
	fprintf(fp, "%f %f\n",pin[i].x, pin[i].y);
      fclose(fp);
    }
    if (doseg) {
      if ((fp = fopen("segment.txt", "w")) != NULL) {
	for (n = 0; n < 2*ns; n+=2) {
	  fprintf(fp,"%f %f\n",pin[sin[n]].x, pin[sin[n]].y); 
	  fprintf(fp,"%f %f\n",pin[sin[n+1]].x, pin[sin[n+1]].y); 
	  fprintf(fp, "NaN NaN\n");
	}
	fclose(fp);
      }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Create the triangulation                                        */
  reorder_pin(np, pin, ns, sin, 0, NULL);
  if (dopoints) 
    area = 0.0;
  else
    np = ns;
  params->d = delaunay_voronoi_build(np, pin, ns, sin, 0, NULL, area, NULL);
  
  if (params->uscf & US_TRI)
    convert_tri_mesh(params, params->d);
  if (params->uscf & US_HEX)
    convert_hex_mesh(params, params->d, 1);

  /*-----------------------------------------------------------------*/
  /* Reset flags                                                     */
  strcpy(params->gridtype, "UNSTRUCTURED");
  params->gridcode = UNSTRUCTURED;
  params->us_type |= US_G;
  if (params->us_type & US_IJ)
    params->us_type &= ~US_IJ;
  if (params->us_type & US_RS)
    params->us_type &= ~US_RS;
  if (params->us_type & US_WS)
    params->us_type &= ~US_RS;
  params->us_type |= (US_RUS|US_WUS);

  /*-----------------------------------------------------------------*/
  /* Interpolate the bathymetry                                      */
  gs = grid_interp_init(x, y, b, nbath, i_rule);
  d_free_1d(params->bathy);
  params->bathy = d_alloc_1d(params->ns2+1);
  for (c = 1; c <= params->ns2; c++) {
    params->bathy[c] = grid_interp_on_point(gs, params->x[c][0], params->y[c][0]);
    if (limf) {
      if (params->bmax && fabs(params->bathy[c]) > params->bmax) 
	params->bathy[c] = -params->bmax;
      if (params->bmin && fabs(params->bathy[c]) < params->bmin) 
	params->bathy[c] = -params->bmin;
      if (params->bmin && params->bathy[c] > params->bmin)
	params->bathy[c] = LANDCELL;
    }
    if (isnan(params->bathy[c])) params->bathy[c] = bmean;
    if (bverbose) printf("%d %f : %f %f\n",c, params->bathy[c], params->x[c][0], params->y[c][0]);
  }
  grid_specs_destroy(gs);
  params->nvals = params->ns2-1;

  d_free_1d(x);
  d_free_1d(y);
  d_free_1d(b);
  free((point *)pin);
  d_free_2d(bathy);
  i_free_2d(mask);
  i_free_2d(map);
  if (doseg) {
    i_free_1d(sin);
    i_free_3d(tri);
  }
}

/* END convert_quad_mesh()                                           */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Re-orders the points list so that the segments occupy the first   */
/* ns points, followed by nh holes points, followed by points in the */
/* interior of the domain. This allows the segments and holes to be  */
/* retained when refining the mesh points in the interior (i.e.      */
/* chaning the points list from (ns + nh) onwards).                  */
/*-------------------------------------------------------------------*/
void reorder_pin(int np, point *pin, int ns, int *sin, int nh, int *hin)
{
  int i, n, m, mp;
  point *npin= malloc(np * sizeof(point));
  int *mask = i_alloc_1d(np);
  int *map = i_alloc_1d(np);
  int verbose = 0;

  /* Reorder the segments and make a map of old to new points.       */
  mp = 0;
  memset(mask, 0, np * sizeof(int));
  if (ns) {
    for (n = 0; n < 2*ns; n++) {
      m = sin[n];
      if (!mask[m]) {
	map[m] = mp;
	npin[mp].x = pin[m].x;
	npin[mp++].y = pin[m].y;
	for (i = n; i < 2*ns; i++) {	
	  if (sin[i] == sin[n]) {
	    mask[sin[i]] = 1;
	  }
	}
      }
    }

    /* Reorder the segments using the map                            */
    for (n = 0; n < 2*ns; n++) {
      sin[n] = map[sin[n]];
    }
  }

  /* Reorder the holes and make a map of old to new points.          */
  if (nh) {
    for (n = 0; n < 2*nh; n++) {
      m = hin[n];
      if (!mask[m]) {
	npin[mp].x = pin[m].x;
	npin[mp++].y = pin[m].y;
	for (i = n; i < 2*nh; i++) {
	  if (hin[i] == hin[n]) {
	    mask[hin[i]] = 1;
	  }
	}
      }
    }
    for (n = 0; n < 2*nh; n++) {
      hin[n] = map[hin[n]];
    }
  }

  /* Fill the remaining points                                       */
  for (n = 0; n < np; n++) {
    if (!mask[n]) {
      npin[mp].x = pin[n].x;
      npin[mp++].y = pin[n].y;
    }
  }
  for (n = 0; n < np; n++) {
    pin[n].x = npin[n].x;
    pin[n].y = npin[n].y;
  }

  /* Print if required                                               */
  if (verbose) {
    for (n = 0; n < np; n++) 
      printf("point %d %f %f\n",n, npin[n].x, npin[n].y);
    for (n = 0; n < 2*ns; n+=2) 
      printf("segment %d : %d %d\n",n,sin[n],sin[n+1]);
    for (n = 0; n < 2*nh; n+=2) 
      printf("hole %d : %d %d\n",n,hin[n],hin[n+1]);
  }
  free((point *)npin);
  i_free_1d(mask);
  i_free_1d(map);
}

/* END reorder_pin()                                                 */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Routine to create a Delaunay and Voronoi mesh given a set of      */
/* points.                                                           */
/*-------------------------------------------------------------------*/
delaunay *create_tri_mesh(int np, point *pin, int ns, int *sin, int nh, double *hin, char *code)
{
  FILE *fp;
  delaunay *d;
  int i, j, n, m;
  point *npin;
  double area, areaf = 0.5;
  int verbose = 0;
  int iterations = 0;
  int filef = 0;

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    if ((fp = fopen("trigrid.txt", "w")) == NULL) filef = 0;
    for (i = 0; i < np; i++) {
      /*
      n = (int)(pin[i].x * 1e4);
      pin[i].x = (double)n / 1e4;
      m = (int)(pin[i].y * 1e4);
      pin[i].y = (double)m / 1e4;
      */
      fprintf(fp, "%f %f\n",pin[i].x, pin[i].y);
    }
    fclose(fp);
  }

  /*-----------------------------------------------------------------*/
  /* Do the triangulation                                            */
  if (sin == NULL)
    d = delaunay_voronoi_build(np, pin, 0, NULL, 0, NULL, 0, code);
  else
    d = delaunay_voronoi_build(np, pin, ns, sin, nh, hin, 0, code);

  if (verbose) {
    printf("Triangulation has %d points\n", d->npoints);
    printf("Voronoi has %d points\n", d->nvoints);
  }

  /*-----------------------------------------------------------------*/
  /* Iterate to improve the triangulation                            */
  for (m = 0; m < iterations; m++) {
    /* Get the new set of points                                     */
    npin = malloc(d->npoints * sizeof(point));
    for (i = 0; i < d->npoints; ++i) {
      point* p = &d->points[i];
      npin[i].x = p->x;
      npin[i].y = p->y;
      if (verbose) printf("Delaunay points %d: %f %f\n", i, p->x, p->y);
    }

    /* Get the mean triangle area. A fraction of this is used to     */
    /* refine the next iteration of the mesh.                        */
    area = 0;
    for (n = 0; n < d->ntriangles; n++) {
      triangle* t = &d->triangles[n];
      triangle_neighbours* nb = &d->neighbours[n];
      circle* c = &d->circles[n];
      area += triarea(d->points[t->vids[0]].x, d->points[t->vids[0]].y,
		      d->points[t->vids[1]].x, d->points[t->vids[1]].y,
		      d->points[t->vids[2]].x, d->points[t->vids[2]].y);
      if (verbose) printf("triangle %d %d %d %d\n",n, t->vids[0], t->vids[1], t->vids[2]);
    }
    area /= (double)d->ntriangles;
    for (n = 0, j = 0; n < d->nedges-1; n++) {
      if (verbose) printf("edge %d %d %d\n",n, d->edges[j], d->edges[j+1]);
      j += 2;
    }
    area *= areaf;
    if (verbose) printf("Triangle area = %f\n",area);

    /* Print Voronoi points and edges if required                    */
    for (i = 0; i < d->nvoints; ++i) {
      point* p = &d->voints[i];
      if (verbose) printf("Voronoi points %d: %f %f\n", i, p->x, p->y);
    }
    for (n = 0, j = 0; n < d->nvdges; n++) {
      if (verbose) printf("Voronoi edge %d %d %d\n",n, d->vdges[j], d->vdges[j+1]);
      j += 2;
    }

    /* Create the next iteration of the mesh                         */
    free((delaunay *)d);

    if (sin == NULL)
      d = delaunay_voronoi_build(np, pin, 0, NULL, 0, NULL, area, code);
    else 
      d = delaunay_voronoi_build(np, pin, ns, sin, nh, hin, area, code);
    if (verbose) {
      printf("Triangulation has %d points\n", d->npoints);
      printf("Voronoi has %d points\n\n", d->nvoints);
    }
  }
  return(d);
}

/* END create_tri_mesh()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Routine to create a Delaunay and Voronoi mesh with ordered        */
/* indexing using a delaunay datastructure as input. The output mesh */
/* is stored in params->x and params->y.                             */
/*-------------------------------------------------------------------*/
void convert_hex_mesh(parameters_t *params, delaunay *d, int mode)
{
  FILE *fp, *cf, *ef, *vf, *bf;
  char oname[MAXSTRLEN], key[MAXSTRLEN], buf[MAXSTRLEN];
  int **mask;
  int *vmask, **vedge, *nvedge, *bmask;
  point *pin;
  int npe;            /* Voronoi tesselation */
  int i, j, n, m, ne, nc;
  int donpe = 0;     /* 1 = only include cells with six sides */
  int dodel = 0;
  int verbose = 0;
  int filef = 1;
  int nnh = 0;
  double dist, dmin;
  double xn, yn, xs, ys;
  double mlon, mlat;
  int nskip = 0;
  int sortdir = 1; /* Sort verices; 1=clockwise, -1=anticlockwise    */
  int debug = -1;  /* Debugging information for points index         */
  int isdedge = 0; /* Debugging location is an edge (not a point)    */
  int edqu = 0;    /* 1 = exit if closed cells can't be made         */
  int nn;
  int vd;

  /*-----------------------------------------------------------------*/
  /* Allocate                                                        */
  nvedge = i_alloc_1d(d->npoints);
  bmask = i_alloc_1d(d->npoints);
  memset(bmask, 0, d->npoints * sizeof(int));
  /*vmask = i_alloc_1d(d->npoints);*/
  if (params->gridcode & (GTRI_DXY_ROT|TRI_DXY_ROT)) donpe = 0;
  donpe = 1;
  if(mode==0) {
    donpe = 0;
    mode = 1;
  }

  /*-----------------------------------------------------------------*/
  /* Get the centre and edges of each Voronoi cell. The centre is a  */
  /* point in the list of points. First store all the number of all  */
  /* d->edges emanting from points in vedge[]. The corresponding     */
  /* edge in d->vdges is the edge of the Voronoi cell whose centre   */
  /* is the point.                                                   */

  for (n = 0; n < d->npoints; n++) {
    nvedge[n] = 0;
    for (m = 0, j = 0;  m < d->nedges; m++) {
      if (n == d->edges[j] || n == d->edges[j+1]) {
	nvedge[n]++;
      }
      j += 2;
    }
    if (n == debug) printf("centre %d[%f %f] = %d vedges\n",n,
			   d->points[n].x, d->points[n].y, nvedge[n]);
  }
  npe = 6;

  if (donpe) {
    npe = 0;
    for (n = 0; n < d->npoints; n++) {
      if (nvedge[n] > npe) npe = nvedge[n];
    }

    /* If the maximum npe lies on a boundary that requires an extra  */
    /* edge, then nvedge[] is incremented by 1, so add 1 to npe for  */
    /* safety to account for this case.                              */
    npe++;
  }

  /* Convert debug edges to a point                                  */
  if (debug >= 0 && isdedge) {
    debug = d->edges[2*debug];
    printf("DEBUG = %d[%f %f]\n", debug, d->points[debug].x, d->points[debug].y);
  }

  /* Save the edges i (0:npe) emanating from point n in vedge[n][i]  */
  vedge = i_alloc_2d(npe, d->npoints);
  for (n = 0; n < d->npoints; n++) {
    nvedge[n] = 0;
    for (m = 0, j = 0;  m < d->nedges; m++) {
      if (n == d->edges[j] || n == d->edges[j+1]) {
	vedge[n][nvedge[n]] = m;
	if (n == debug) {
	  printf("vedge%d of edge%d = %d[%f %f] to %d[%f %f]\n", nvedge[n], m, d->vdges[2*m],
		 d->voints[d->vdges[2*m]].x, 
		 d->voints[d->vdges[2*m]].y, d->vdges[2*m+1],
		 d->voints[d->vdges[2*m+1]].x, 
		 d->voints[d->vdges[2*m+1]].y);
	}
	nvedge[n]++;
      }
      j += 2;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Rearrange the edges to be continuous. Store the Voronoi edges   */
  /* in mask[].                                                      */
  mask = i_alloc_2d(npe+1, d->npoints);
  for (n = 0; n < d->npoints; n++) {
    int nn, ce, cs, c1, c2;
    int is_closed = 0;
    int cep, csp, cap[npe], cap2[npe], ip, ip2;
    char p1[MAXSTRLEN], p2[MAXSTRLEN];

    if (!nvedge[n]) continue;
    vmask = i_alloc_1d(nvedge[n]+1);
    memset(vmask, 0, (nvedge[n]+1) * sizeof(int));

    /* First edge                                                    */
    i = 0;
    vmask[i] = 1;
    j = 2 * vedge[n][0];
    mask[n][i++] = cs = d->vdges[j];  /* Save the start index        */
    j += 1;
    mask[n][i++] = ce = d->vdges[j];  /* Index to carry forward      */
    if (n == debug) {
      printf("Start index0 = %d [%f %f]\n", cs, d->voints[cs].x, d->voints[cs].y);
      printf("Carry index1 = %d [%f %f]\n", ce, d->voints[ce].x, d->voints[ce].y);
    }
    if (edqu) {
      csp = cs;
      cep = ce;
    }
    /* Remaining edges                                               */
    for (nn = 0; nn < nvedge[n]; nn++) {
      int found = 0;
      /* Loop over all edges and find an index equal to the one      */
      /* carried forward. If found, make this index the new one to   */
      /* carry forward. This may reside on either end of the         */
      /* Voronoi edge n.                                             */
      for (m = 0; m < nvedge[n]; m++) {
	if (found || vmask[m]) continue;
	j = 2 * vedge[n][m];
	if(n==debug)printf("%d %d %d %d\n",nn,m,d->vdges[j],d->vdges[j+1]);
	if (ce == d->vdges[j]) {
	  if (edqu) {
	    strcpy(p1, "Pass1(a)");
	    ip = i;
	  }
	  vmask[m] = 1;
	  mask[n][i++] = ce  = d->vdges[j + 1];
	  if (n == debug) printf("Pass1(a) index%d = %d[%f %f]\n",
				 i-1, ce, d->voints[ce].x, d->voints[ce].y);
	  found = 1;
	} else if (ce == d->vdges[j + 1]) {
	  if (edqu) {
	    strcpy(p1, "Pass1(b)");
	    ip = i;
	  }
	  vmask[m] = 1;
	  mask[n][i++] = ce = d->vdges[j];
	  if (n == debug) printf("Pass1(b) index%d = %d[%f %f]\n",
				 i-1, ce, d->voints[ce].x, d->voints[ce].y);
	  found = 1;
	}

	/* If the edge to carry forward is equal to the start index, */
	/* (i.e. a dead end is encountered) then a closed cell has   */
	/* been formed. Note that last value of mask contains the    */
	/* same index as that one before the last value (i.e.        */
	/* redundant information).                                   */
	if (ce == cs) is_closed = 1;
      }
      /*if (i == nvedge[n]) break;*/
    }
    if (n == debug && is_closed) printf("Cell %d is closed\n", n);

    /* If the loop is not closed we most likely have a boundary cell */
    /* that we require to close. Increment the number of points in   */
    /* the Voronoi cell (nvedge[n]) by one, and add indices in the   */
    /* opposite direction to that above, using the start index as    */
    /* that to carry forward, until a dead end is found. Note that   */
    /* the indexing in this direction starts from nvedge[n] and      */
    /* decrements.                                                   */
    /* The Voronoi edge to close the loop is that joining the two    */
    /* dead ends.                                                    */
    if (!is_closed) {
      int nve = nvedge[n] - 1;
      int ii = nvedge[n];
      int ni = i;
      nvedge[n]++;
      c1 = ce;
      ce = cs;
      /*
      printf("Cell not closed at %d: (nedges=%d) : %f %f\n", 
	     n, ii, d->points[n].x, d->points[n].y);
      */
      /* Do not include triangular boundary cells                    */
      if (nvedge[n] == 3) nvedge[n] = 0;
      /* Do not include quadrilateral boundary cells                 */
      /*if (nvedge[n] == 4) nvedge[n] = 0;*/

      /*
      if(nvedge[n])printf("Cell not closed at %d: (nedges=%d) : %f %f\n", 
			 n, ii, d->points[n].x, d->points[n].y);
      */

      /* Search for edges in the reverse direction */
      for (nn = nvedge[n]-1; nn >= 0 ; nn--) {
	int found = 0;
	for (m = nve; m >= 0; m--) {
	  if (found || vmask[m]) continue;
	  j = 2 * vedge[n][m];
	  if (ce == d->vdges[j]) {
	    if (edqu) {
	      strcpy(p2, "Pass2(a)");
	      ip2 = ii;
	    }
	    vmask[m] = 1;
	    mask[n][ii--] = ce  = d->vdges[j + 1];
	    if (n == debug) printf("Pass2(a) index%d = %d[%f %f]\n",
				   ni, ce, d->voints[ce].x, d->voints[ce].y);
	    ni++;
	    found = 1;
	  } else if (ce == d->vdges[j + 1]) {
	    if (edqu) {
	      strcpy(p2, "Pass2(b)");
	      ip2 = ii;
	    }
	    vmask[m] = 1;
	    mask[n][ii--] = ce = d->vdges[j];
	    if (n == debug) printf("Pass2(b) index%d = %d[%f %f]\n",
				   ni, ce, d->voints[ce].x, d->voints[ce].y);
	    ni++;
	    found = 1;
	  }
	}
      }
      c2 = ce;
      if(nvedge[n] && nvedge[n] != ni) {
	hd_warn("convert_hex_mesh: Can't create closed cell at %d [%f %f]. Removing cell.\n",n, d->points[n].x, d->points[n].y);
	if (edqu && nskip == 1) {
	  printf("nvedge[%d] = %d\n",n, nvedge[n]);
	  printf("ni = %d\n", ni);
	  for (nn = 0; nn < nvedge[n]; nn++) {
	    j = 2 * vedge[n][nn];
	    ce = d->vdges[j];
	    printf("vertex%d = %d[%f %f]\n",nn, ce, d->voints[ce].x, d->voints[ce].y);
	  }
	  cs = mask[n][0];
	  ce = mask[n][1];
	  printf("Start index0 = %d [%f %f]\n", cs, d->voints[cs].x, d->voints[cs].y);
	  printf("Carry index1 = %d [%f %f]\n", ce, d->voints[ce].x, d->voints[ce].y);
	  printf("Forward #points = 0:%d\n", ip);
	  for (nn = 0; nn < ip; nn++) {
	    ce = mask[n][nn];
	    printf("%s index%d = %d[%f %f]\n",p1, nn, ce, d->voints[ce].x, d->voints[ce].y);
	  }
	  printf("Backward #points = %d:%d\n", nve, ip2);
	  for (nn = nve; nn >= ip2; nn--) {
	    ce = mask[n][nn];
	    printf("%s index%d = %d[%f %f]\n",p2, nn, ce, d->voints[ce].x, d->voints[ce].y);
	  }
	  hd_quit("convert_hex_mesh: Can't create closed cell at %d [%f %f]. Removing cell.\n",n, d->points[n].x, d->points[n].y);
	}
	nvedge[n] = 0;
	nskip++;
      }
      bmask[n] = 1;
    }
    i_free_1d(vmask);
  }

  /*-----------------------------------------------------------------*/
  /* Remove edges to infinity if required                            */
  if (mode) {
    for (n = 0; n < d->npoints; n++) {
      /* 
      if (nvedge[n] != 6) {
	nvedge[n] = 0;
	break;
      }
      */
      for (m = 0; m < nvedge[n]; m++) {

	if (mask[n][m] == -1) {
	  nvedge[n] = 0;
	  break;
	}

	if (params->gridcode & (GTRI_DXY_ROT|GTRI_DLATLON_FP)) {
	  if (fabs(d->voints[mask[n][m]].x) > 360.0 || 
	      fabs(d->voints[mask[n][m]].y) > 360.0) {
	    nvedge[n] = 0;
	    break;
	  }
	  if (isnan(d->voints[mask[n][m]].x) || 
	      isnan(d->voints[mask[n][m]].y)) {
	    nvedge[n] = 0;
	    break;
	  }
	}
      }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Reorder clockwise starting from most SW vertex. The vector      */
  /* containing the Voronoi edges (vedge[]) is overwritten with the  */
  /* edge numbers stored in mask[].                                  */
  if (mode) {
  for (n = 0; n < d->npoints; n++) {
    int nn, dir;
    double xsw = 1e10;
    double ysw = 1e10;
    double xn;
    double eps, epsm;
    double xr, yr;
    double x0, y0, x1, y1;
    int dof = 0;
    if (!nvedge[n]) continue;

    vmask = i_alloc_1d(nvedge[n]);
    memset(vmask, 0, nvedge[n] * sizeof(int));

    /* Sort in clockwise (or anti-clockwise) order. NOTE: All cells  */
    /* must have vertices ordered in the same sense (clockwise or    */
    /* anti-clockwise) or the preprocessor will fail (e2v will be in */
    /* the wrong direction if one cell adacent to an edge is in one  */
    /* sense and the other cell is in the opposite sense).           */
    sort_circle(d, mask[n], nvedge[n], sortdir);

    /* Get the most westerly                                         */
    /* Get the tolerance based on grid size                          */
    eps = 1e-10;
    epsm = HUGE;
    for (m = 0; m < nvedge[n]; m++) {
      double xn;
      xn = d->voints[mask[n][m]].x;
      for (nn = 0; nn < nvedge[n]; nn++) {
	double x;
	x = d->voints[mask[n][nn]].x;
	if (fabs(x-xn) > eps) eps = fabs(x-xn);
	if (fabs(x-xn) < epsm) epsm = fabs(x-xn);
      }
    }
    eps = max(0.25 * eps, epsm);

    /* Mask all vertices with distance differences > tolerance       */
    for (m = 0; m < nvedge[n]; m++) {
      double xn;
      /*if (mask[n][m] == -1) continue;*/
      xn = d->voints[mask[n][m]].x;
      for (nn = 0; nn < nvedge[n]; nn++) {
	double x;
	/*if (mask[n][nn] == -1) continue;*/
	x = d->voints[mask[n][nn]].x;
	if (x < xn && fabs(x-xn) > eps) vmask[m] = 1;
      }
    }

    /* Get the most southerly of un-masked vertices                  */
    for (m = 0; m < nvedge[n]; m++) {
      double y;
      /*if (mask[n][m] == -1) continue;*/
      y = d->voints[mask[n][m]].y;
      if (!vmask[m] && y < ysw) {
	ysw = y;
	i = m;
      }
    }

    /* Re-order                                                      */
    for (m = 0; m < nvedge[n]; m++) {
      vedge[n][m] = mask[n][i];
      if (n == debug) printf("Re-ordered: %d(%d)=>%d(%d)\n",i, mask[n][i], m, vedge[n][m]);
      i = (i == nvedge[n] - 1) ? 0 : i + 1;
    }

    if (dof) {
    xr = yr = 0.0;
    for (m = 0; m < nvedge[n]; m++) {
      xr += d->voints[vedge[n][m]].x;
      yr += d->voints[vedge[n][m]].y;
    }
    xr /= (double)nvedge[n];
    yr /= (double)nvedge[n];
    x0 = d->voints[vedge[n][0]].x;
    y0 = d->voints[vedge[n][0]].y;
    x1 = d->voints[vedge[n][1]].x;
    y1 = d->voints[vedge[n][1]].y;

    /* Set clockwise (in order of increasing latitude from vertex 0  */
    /* to 1). If latitudes are equal, then vertex 1 must be to the   */
    /* east of vertex 0 (vertex 0 is the SW corner) and order should */
    /* be reversed.                                                  */
    dir = SortCornersClockwise(x0, y0, x1, y1, xr, yr);

    if (dir == -1) {
      for (m = 0; m < nvedge[n]; m++)
	mask[n][m] = vedge[n][m];
      i = nvedge[n] - 1;
      for (m = 1; m < nvedge[n]; m++) {
	vedge[n][m] = mask[n][i--];
      }
    }
    }
    i_free_1d(vmask);
  }
  } else {
    /* Ideally truncate the point to the nearest boundary */
    for (n = 0; n < d->npoints; n++) {
      for (m = 0; m < nvedge[n]; m++) {
	if (mask[n][m] == -1) {
	  i = m;
	  while (mask[n][i] == -1)
	    i = (i == nvedge[n] - 1) ? 0 : i + 1;
	  vedge[n][m] = mask[n][i];
	} else
	  vedge[n][m] = mask[n][m];
      }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_us.us", key);
    if ((fp = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_c.txt", key);
    if ((cf = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_v.txt", key);
    if ((vf = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_b.txt", key);
    if ((bf = fopen(buf, "w")) == NULL) filef = 0;
  }

  m = 0;
  for (n = 0; n < d->npoints; n++)
    if (nvedge[n]) m++;
  if (filef) {
    fprintf(fp, "Mesh2 unstructured v1.0\n");
    fprintf(fp, "nMaxMesh2_face_nodes %d\n", npe);
    fprintf(fp, "nMesh2_face          %d\n", m);
    fprintf(fp, "Mesh2_topology\n");
  }
  params->npe = npe;
  params->ns2 = 0;
  if (!donpe) {
    for (n = 0, i = 0; n < d->npoints; n++)
      if (nvedge[n] == npe)
	params->ns2++;
  } else {
    for (n = 0, i = 0; n < d->npoints; n++)
      if (nvedge[n])
	params->ns2++;
  }
  if (params->x != NULL) d_free_2d(params->x);
  if (params->y != NULL) d_free_2d(params->y);
  if (params->npe2 != NULL) i_free_1d(params->npe2);
  params->npe2 = i_alloc_1d(params->ns2+1);
  params->x = d_alloc_2d(params->npe+1, params->ns2+1);
  params->y = d_alloc_2d(params->npe+1, params->ns2+1);
  nc = max(d->npoints, d->nvoints);

  mask =  i_alloc_2d(nc, 2);
  memset(mask[0], 0, nc * sizeof(int));
  memset(mask[1], 0, nc * sizeof(int));
  nc = ne = 0;
  for (n = 0, i = 1; n < d->npoints; n++) {
    if (nvedge[n]) {
      double xm = 0.0, ym = 0.0;
      if (nvedge[n] != 6) nnh++;
      if (!donpe && nvedge[n] != npe) continue;
      params->npe2[i] = nvedge[n];
      if (verbose) printf("Voronoi cell %d %f %f\n", i, d->points[n].x, d->points[n].y);
      if (filef) {
	fprintf(fp, "%d %f %f\n", i, d->points[n].x, d->points[n].y);
      }
      if (debug == n) {
	printf("centre Voronoi cell %d[%f %f]\n", i, d->points[n].x, d->points[n].y);
	vd = i;
      }
      params->x[i][0] = d->points[n].x;
      params->y[i][0] = d->points[n].y;

      mask[0][n] = 1;
      nc++;
      for (m = 0; m < nvedge[n]; m++) {
	if (filef) {
	  fprintf(fp, "%f %f\n", d->voints[vedge[n][m]].x, d->voints[vedge[n][m]].y);
	  fprintf(ef, "%f %f\n", d->voints[vedge[n][m]].x, d->voints[vedge[n][m]].y);
	}
	if (n == debug) printf("vedge=%d[%f %f]\n", vedge[n][m], d->voints[vedge[n][m]].x, d->voints[vedge[n][m]].y);
	if (verbose) printf("  vertex %d=%d : %f %f\n", m, vedge[n][m],
			    d->voints[vedge[n][m]].x, d->voints[vedge[n][m]].y);
	params->x[i][m+1] = d->voints[vedge[n][m]].x;
	params->y[i][m+1] = d->voints[vedge[n][m]].y;
	xm += params->x[i][m+1];
	ym += params->y[i][m+1];
	if (mask[1][vedge[n][m]] == 0) {
	  mask[1][vedge[n][m]] = 1;
	  ne++;
	}
      }
      /* Replace the centre location with the centre of mass for     */
      /* perimeter cells.                                            */
      if (bmask[n]) {
	params->x[i][0] = xm / (double)nvedge[n];
	params->y[i][0] = ym / (double)nvedge[n];
      }

      if (filef) {
	fprintf(cf, "%f %f\n", params->x[i][0], params->y[i][0]);
	fprintf(ef, "%f %f\n", d->voints[vedge[n][0]].x, d->voints[vedge[n][0]].y);
	fprintf(ef, "NaN NaN\n");
	if (bmask[n])
	  fprintf(bf, "%f %f\n", d->points[n].x, d->points[n].y);
      }
      if (n == debug) {
	printf("vedge=%d[%f %f]\n", vedge[n][0], d->voints[vedge[n][0]].x, d->voints[vedge[n][0]].y);
      }
      i++;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Set up a delaunay triangulation for xytoi interpolation using   */
  /* delaunay_xytoi().                                               */
  if (dodel) {
    pin = malloc((nc + ne) * sizeof(point));
  
    for (i = 0, j = 0; i < d->npoints; ++i) {
      if (mask[0][i]) {
	pin[j].x = d->points[i].x;
	pin[j++].y = d->points[i].y;
      }
    }
    for (i = 0; i < d->nvoints; ++i) {
      if (mask[1][i]) {
	pin[j].x = d->voints[i].x;
	pin[j++].y = d->voints[i].y;
      }
    }
    i_free_2d(mask);
    free((delaunay *)d);
    /*d = delaunay_voronoi_build(j, pin, 0, NULL, 0, NULL, 0.0, NULL);*/
    d = delaunay_build(j, pin, 0, NULL, 0, NULL);
    if (filef) {
      for (n = 0; n < d->ntriangles; n++) {
	triangle* t = &d->triangles[n];
	fprintf(vf, "%f %f\n", d->points[t->vids[0]].x, d->points[t->vids[0]].y);
	fprintf(vf, "%f %f\n", d->points[t->vids[1]].x, d->points[t->vids[1]].y);
	fprintf(vf, "%f %f\n", d->points[t->vids[2]].x, d->points[t->vids[2]].y);
	fprintf(vf, "%f %f\n", d->points[t->vids[0]].x, d->points[t->vids[0]].y);
	fprintf(vf, "NaN NaN\n");
      }
    }
  }
  params->us_type |= US_CUS;

  if (filef) {
    fclose(fp);
    fclose(cf);
    fclose(ef);
    fclose(vf);
    fclose(bf);
  }
  i_free_2d(mask);
  i_free_1d(nvedge);
  i_free_2d(vedge);
  i_free_1d(bmask);
}

/* END convert_hex_mesh()                                            */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Orders a list of coordinates in a clockwise direction, starting   */
/* from the coordinate with the smallest angle to the x-plane for    */
/* anti clockwise sorting (dir=-1) and largest angle for clockwise   */
/* sorting (dir=1).                                                  */
/*-------------------------------------------------------------------*/
void sort_circle(delaunay *d, int *vedge, int nedge, int dir)
{
  double x[nedge];
  double y[nedge];
  double a[nedge];
  double xr = 0.0, yr = 0.0;
  int m, i, j;

  /* Save the locations and index and find the central reference     */
  for(m = 0; m < nedge; m++) {
    x[m] = d->voints[vedge[m]].x;
    y[m] = d->voints[vedge[m]].y;
    xr += x[m];
    yr += y[m];
  }
  xr /= (double)nedge;
  yr /= (double)nedge;

  /* Get the atan2 values                                            */
  for(m = 0; m < nedge; m++) {
    a[m] = atan2(y[m] - yr, x[m] - xr);
    if (a[m] < 0.0) a[m] += 2.0 * PI;
  }

  /* Order the angles                                                */
  for (i = 0; i < nedge; i++)
    for (j = nedge-1; i < j; --j)
      order_c(&a[j-1], &a[j], &vedge[j-1], &vedge[j]);

  if (dir = 1) {
    int index[nedge];
    memcpy(index, vedge, nedge * sizeof(int));
    i = 0;
    for(m = nedge-1; m >= 0; m--) {
      vedge[i++] = index[m];
    }
  }
}

/* END sort_circle()                                                 */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Orders a list of double values and thier associated index.        */
/*-------------------------------------------------------------------*/
void order_c(double *a, double *b, int *c1, int *c2)
{
  double val;
  int v;
  if (*a > *b) {
    val = *a;
    *a = *b;
    *b = val;
    v = *c1;
    *c1 = *c2;
    *c2 = v;
  }
}

/* END order_c()                                                     */
/*-------------------------------------------------------------------*/

int SortCornersClockwise(double x0, double y0, double x1, double y1, double rx, double ry)
{
  double aTanA, aTanB;

  if (x0 == x1 && y0 < y1) return 1;
  if (x0 == x1 && y0 > y1) return -1;
  if (y0 == y1 && x0 < x1) return -1;
  if (y0 == y1 && x0 > x1) return 1;

  aTanA = atan2(y0 - ry, x0 - rx);
  if (aTanA < 0.0) aTanA += 2.0 * PI;
  aTanB = atan2(y1 - ry, x1 - rx);
  if (aTanB < 0.0) aTanB += 2.0 * PI;

  if (aTanA < aTanB) return -1;
  else if (aTanB < aTanA) return 1;
  return 0;
}

/*-------------------------------------------------------------------*/
/* Routine to create a Delaunay and Voronoi mesh given a set of      */
/* points.                                                           */
/*-------------------------------------------------------------------*/
void convert_tri_mesh(parameters_t *params, delaunay *d)
{
  FILE *fp, *cf, *ef;
  char oname[MAXSTRLEN], key[MAXSTRLEN], buf[MAXSTRLEN];
  point *pin;
  int npe = 3;            /* Delaunay tesselation */
  int i, j, n, m;
  int verbose = 0;
  int filef = 1;

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_us.us", key);
    if ((fp = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_c.txt", key);
    if ((cf = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
  }
  if (filef) {
    fprintf(fp, "Mesh2 unstructured v1.0\n");
    fprintf(fp, "nMaxMesh2_face_nodes %d\n", npe);
    fprintf(fp, "nMesh2_face          %d\n", d->ntriangles);
    fprintf(fp, "Mesh2_topology\n");
  }
  params->npe = npe;
  params->ns2 = d->ntriangles;
  if (params->x != NULL) d_free_2d(params->x);
  if (params->y != NULL) d_free_2d(params->y);
  if (params->npe2 != NULL) i_free_1d(params->npe2);
  params->npe2 = i_alloc_1d(params->ns2+1);
  for (n = 1; n <= params->ns2; n++)
    params->npe2[n] = npe;
  params->x = d_alloc_2d(params->npe+1, params->ns2+1);
  params->y = d_alloc_2d(params->npe+1, params->ns2+1);

  for (n = 0, i = 1; n < d->ntriangles; n++) {
    triangle* t = &d->triangles[n];
    double x[3], y[3], c1, c2, area, ec;
    x[0] = d->points[t->vids[0]].x;
    y[0] = d->points[t->vids[0]].y;
    x[1] = d->points[t->vids[1]].x;
    y[1] = d->points[t->vids[1]].y;
    x[2] = d->points[t->vids[2]].x;
    y[2] = d->points[t->vids[2]].y;
    c1 = (x[0] + x[1] + x[2]) / 3.0;
    c2 = (y[0] + y[1] + y[2]) / 3.0;

    /* Find the eastern-most coordinate                              */
    m = 0;
    for (j = 0; j < 3; j++) {
      if (x[j] < x[m]) m = j;
    }
    /* Reset coordinates clockwise from easternmost (note: triangle  */
    /* coordinates are listed in counter-clockwise sense.            */
    for (j = 0; j < 3; j++) {
      x[j] = d->points[t->vids[m]].x;
      y[j] = d->points[t->vids[m]].y;
      m = (m == 0) ? 2 : m - 1;
    }

    area = triarea(x[0], y[0], x[1], y[1], x[2], y[2]);

    if (verbose) printf("Delaunay cell %d %f %f\n", i, c1, c2);
    if (filef) {
      fprintf(fp, "%d %f %f\n", i, c1, c2);
      fprintf(cf, "%f %f\n", c1, c2);
    }
    params->x[i][0] = c1;
    params->y[i][0] = c2;
    for (m = 0; m < 3; m++) {
      if (filef) {
	fprintf(fp, "%f %f\n", x[m], y[m]);
	fprintf(ef, "%f %f\n", x[m], y[m]);
      }
      if (verbose) printf("  vertex %d : %f %f\n", m, x[m], y[m]);
      params->x[i][m+1] = x[m];
      params->y[i][m+1] = y[m];
    }
    if (filef) {
      fprintf(ef, "%f %f\n", x[0], y[0]);
      fprintf(ef, "NaN NaN\n");
    }
    i++;
  }

  params->us_type |= US_CUS;

  if (filef) {
    fclose(fp);
    fclose(cf);
    fclose(ef);
  }
}

/* END convert_tri_mesh()                                            */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Area of a triangle given vertex coordinates using Heron's formula */
/* and Pythagoras.                                                   */
/*-------------------------------------------------------------------*/
double triarea(double ax, double ay, double bx, double by, double cx, double cy)
{
  double a = sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
  double b = sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy));
  double c = sqrt((bx - cx) * (bx - cx) + (by - cy) * (by - cy));
  double s = 0.5 * (a + b + c);
  /*double area = 0.5*ax*(by - cy) + bx*(cy - ay) + cx*(ay - by);*/
  double area = sqrt(s * (s-a) * (s-b) * (s-c));
  return(area);
}

/* END triarea()                                                     */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Converts a mesh given params->x[cc][1:npe] to a list of vertex    */
/* and centre locations (double) and a list of integers pointing to  */
/* these locations to define the polygon edges and centres.          */
/*-------------------------------------------------------------------*/
void convert_mesh_input(parameters_t *params,
			mesh_t *mesh,  /* Mesh structure             */
			double **xc,   /* Cell x coordinates         */
			double **yc,   /* Cell y coordinates         */
			double *bathy, /* Bathymetry                 */
			int wf         /* 1 = write to file          */
			)
{
  FILE *op;
  char key[MAXSTRLEN], buf[MAXSTRLEN];
  int ns2;               /* Number of cells                          */
  int npe;               /* Maximum number of vertices               */
  int *maskv;            /* Vertex mask                              */
  int cc, c, cci, j, jj, i, n, cco;
  double x0, y0, x1, y1, x2, y2;
  double *xv, *yv;
  int iv;
  int oldcode = 0;

  if (DEBUG("init_m"))
    dlog("init_m", "\nStart mesh conversion\n");

  ns2 = mesh->ns2;
  npe = mesh->mnpe;

  /*-----------------------------------------------------------------*/
  /* Save all unique locations of vertices and make the mapping from */
  /* indices to coordinates.                                         */
  remove_duplicates(ns2, xc, yc, mesh);
  if (DEBUG("init_m"))
    dlog("init_m", "\nIndex - coordinate mappings created OK\n");

  if (oldcode) {
  maskv = i_alloc_1d(ns2 + 1);
  memset(maskv, 0, (ns2 + 1) * sizeof(int));
  mesh->ns = 1;

  for (cc = 1; cc <= ns2; cc++) {
    for (j = 1; j <= mesh->npe[cc]; j++) {
      x1 = xc[cc][j];
      y1 = yc[cc][j];
      i = find_mesh_vertex(cc, x1, y1, xc, yc, maskv, ns2, mesh->npe);
      if (!i) {
	mesh->xloc[mesh->ns] = x1;
	mesh->yloc[mesh->ns++] = y1;
      }
    }
    maskv[cc] = 1;
  }
  i_free_1d(maskv);
  if (DEBUG("init_m"))
    dlog("init_m", "\nUnique vertices found OK\n");

  /*-----------------------------------------------------------------*/
  /* Save the location of the cell centres                           */
  for (cc = 1; cc <= ns2; cc++) {
    x1 = xc[cc][0];
    y1 = yc[cc][0];
    mesh->xloc[mesh->ns] = x1;
    mesh->yloc[mesh->ns++] = y1;
  }
  mesh->ns--;
  if (DEBUG("init_m"))
    dlog("init_m", "\nCell centres found OK\n");

  /*-----------------------------------------------------------------*/
  /* Map the edges for each cell to the vertex location numbers, eg. */
  /* cell cc, edge j has the x vertices xloc[eloc[0][cc][j]] and     */
  /* xloc[eloc[1][cc][j]].                                           */
  for (cc = 1; cc <= ns2; cc++) {
    for (j = 1; j <= mesh->npe[cc]; j++) {
      x1 = xc[cc][j];
      y1 = yc[cc][j];
      jj = (j == mesh->npe[cc]) ? 1 : j + 1;
      x2 = xc[cc][jj];
      y2 = yc[cc][jj];
      for (n = 1; n <= mesh->ns; n++) {
	if (x1 == mesh->xloc[n] && y1 == mesh->yloc[n])
	  mesh->eloc[0][cc][j] = n;
	if (x2 == mesh->xloc[n] && y2 == mesh->yloc[n])
	  mesh->eloc[1][cc][j] = n;
      }
    }
    for (n = 1; n <= mesh->ns; n++) {
      x1 = xc[cc][0];
      y1 = yc[cc][0];
      if (x1 == mesh->xloc[n] && y1 == mesh->yloc[n])
	mesh->eloc[0][cc][0] = mesh->eloc[1][cc][0] = n;
    }
  }
  if (DEBUG("init_m"))
    dlog("init_m", "\nCell vertices mapped OK\n");
  }

  /*-----------------------------------------------------------------*/
  /* Get the neighbour mappings                                      */
  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh neighbours\n");
  neighbour_finder(mesh);

  /*-----------------------------------------------------------------*/
  /* Expand the mesh if required                                     */
  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh expansion\n");
  mesh_expand(params, params->bathy, xc, yc);

  /*-----------------------------------------------------------------*/
  /* Open boundaries. Remap the obc edge number to the vertex        */
  /* location indices.                                               */
  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh OBCs\n");
  if (mesh->nobc) {
    for (n = 0; n < mesh->nobc; n++) {
      /* If mesh indices were read from the parameter file and       */
      /* copied directly to the mesh structure in meshstruct_us()    */
      /* then continue.                                              */ 
      if (mesh->loc[n][0] == NOTVALID) continue;
      for (cc = 1; cc <= mesh->npts[n]; cc++) {
	cco = mesh->loc[n][cc];
	x0 = xc[cco][0];
	y0 = yc[cco][0];
	j = mesh->obc[n][cc][0];
	x1 = xc[cco][j];
	y1 = yc[cco][j];
	j = mesh->obc[n][cc][1];
	x2 = xc[cco][j];
	y2 = yc[cco][j];

	for (cci = 1; cci <= mesh->ns2; cci++) {
	  if (x0 == mesh->xloc[mesh->eloc[0][cci][0]] && y0 == mesh->yloc[mesh->eloc[0][cci][0]])
	    mesh->loc[n][cc] = cci;
	}
	for (cci = 1; cci <= mesh->ns; cci++) {
	  if (x1 == mesh->xloc[cci] && y1 == mesh->yloc[cci])
	    mesh->obc[n][cc][0] = cci;
	  if (x2 == mesh->xloc[cci] && y2 == mesh->yloc[cci])
	    mesh->obc[n][cc][1] = cci;
	}
      }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Write to file if required. Output file is <INPUT_FILE>_us.txt   */
  if (wf) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_us.txt", key);
    if ((op = fopen(buf, "w")) == NULL)
      return;
    fprintf(op, "Mesh2 unstructured   v1.0\n");
    fprintf(op, "nMaxMesh2_face_nodes %d\n", mesh->mnpe);
    fprintf(op, "nMesh2_face_indices  %d\n", mesh->ns);
    fprintf(op, "nMesh2_face          %d\n",mesh->ns2);
    if (mesh->nce1)
      fprintf(op, "NCE1                 %d\n",mesh->nce1);
    if (mesh->nce2)
      fprintf(op, "NCE2                 %d\n",mesh->nce2);
    fprintf(op, "Mesh2_topology\n");

    write_mesh_us(params, bathy, op, 1);

    fclose(op);
  }

  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh parameters\n");
  set_params_mesh(params, mesh, xc, yc);

  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh conversion complete\n");
}

/* END convert_mesh_input()                                          */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Initializes the params flags based on information in the mesh     */
/* structure.                                                        */
/*-------------------------------------------------------------------*/
void set_params_mesh(parameters_t *params, mesh_t *mesh, double **x, double **y)
{
  int cc;

  /* Set the grid code                                               */
  if (mesh->mnpe == 3) 
    params->us_type |= US_TRI;
  else if (mesh->mnpe == 4) 
    params->us_type |= US_QUAD;
  else if (mesh->mnpe == 6) 
    params->us_type |= US_HEX;
  else
    params->us_type |= US_MIX;

  /* Get the grid metrics                                            */
  if (params->h1 != NULL) d_free_2d(params->h1);
  if (params->h2 != NULL) d_free_2d(params->h2);
  if (params->a1 != NULL) d_free_2d(params->a1);
  if (params->a2 != NULL) d_free_2d(params->a2);
  params->h1 = d_alloc_2d(mesh->mnpe+1, mesh->ns2+1);
  params->h2 = d_alloc_2d(mesh->mnpe+1, mesh->ns2+1);
  params->a1 = d_alloc_2d(mesh->mnpe+1, mesh->ns2+1);
  params->a2 = d_alloc_2d(mesh->mnpe+1, mesh->ns2+1);

  grid_get_metrics_us(mesh->npe, x, y, mesh->ns2,
		      params->h1, params->h2);
  grid_get_angle_us(mesh->npe, x, y, mesh->ns2,
		    params->a1, params->a2);
  /*params->gridcode = NUMERICAL;*/

  /* Set the time-series file projection                             */
  if (strlen(params->projection) > 0) {
    strcpy(projection, params->projection);
    ts_set_default_proj_type(projection);
  }

  /* If the the projection is specified and is geographic, then      */
  /* compute the metrics and angles for the sphere.                  */
  if (strncasecmp(params->projection,
                  GEOGRAPHIC_TAG, strlen(GEOGRAPHIC_TAG)) == 0) {
    grid_get_geog_metrics_us(mesh->npe, x, y, mesh->ns2,
			     params->h1, params->h2);
    grid_get_geog_angle_us(mesh->npe, x, y, mesh->ns2,
			   params->a1, params->a2);
  }
}

/* END set_params_mesh()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Build a mesh struture for structured orthogonal curvilinear grids */
/*-------------------------------------------------------------------*/
void meshstruct_s(parameters_t *params, geometry_t *geom) 
{
  int cc, c, i, j, cco, n;
  mesh_t *m;
  double **x, **y;
  int mo2;              /* Max. # of cells in each open boundary */
  int filef = 1;

  /* Allocate                                                        */
  free_mesh(params->mesh);
  params->mesh = mesh_init(params, geom->b2_t, 4);
  m = params->mesh;
  strcpy(params->gridtype, "STRUCTURED");

  /*-----------------------------------------------------------------*/
  /* Cell centre and vertex locations                                */
  x = d_alloc_2d(m->mnpe+1, m->ns2+1);
  y = d_alloc_2d(m->mnpe+1, m->ns2+1);
  m->nce1 = geom->nce1;
  m->nce2 = geom->nce2;
  for (cc = 1; cc <= geom->b2_t; cc++) {
    c = geom->w2_t[cc];
    i = geom->s2i[c];
    j = geom->s2j[c];
    x[cc][0] = params->x[j*2+1][i*2+1];
    y[cc][0] = params->y[j*2+1][i*2+1];
    if (params->us_type & US_IJ) {
      m->iloc[cc] = i;
      m->jloc[cc] = j;
    }
    x[cc][1] = params->x[j*2][i*2];
    y[cc][1] = params->y[j*2][i*2];
    x[cc][2] = params->x[(j+1)*2][i*2];
    y[cc][2] = params->y[(j+1)*2][i*2];
    x[cc][3] = params->x[(j+1)*2][(i+1)*2];
    y[cc][3] = params->y[(j+1)*2][(i+1)*2];
    x[cc][4] = params->x[j*2][(i+1)*2];
    y[cc][4] = params->y[j*2][(i+1)*2];
  }

  /*-----------------------------------------------------------------*/
  /* Open boundaries                                                 */
  if (params->nobc) {
    for (n = 0; n < params->nobc; n++) {
      open_bdrys_t *open = geom->open[n];
      convert_obc_list(params, open, n, geom, NULL);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Bathymetry                                                      */
  if (params->bathy) d_free_1d(params->bathy);
  params->bathy = d_alloc_1d(m->ns2+1);
  for (cc = 1; cc <= geom->b2_t; cc++) {
    c = geom->w2_t[cc];
    params->bathy[cc] = geom->botz[c];
  }

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  /*-----------------------------------------------------------------*/
  /*
  if (filef) {
    FILE *ef;
    char key[MAXSTRLEN], buf[MAXSTRLEN];
    if (endswith(params->oname, ".nc")) {
      n = strlen(params->oname);
      for (i = 0; i < n-3; i++)
	key[i] = params->oname[i];
      key[i] = '\0';
    } else
      strcpy(key, params->oname);
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= geom->b2_t; cc++) {
	for (n = 1; n <= 4; n++) {
	  fprintf(ef, "%f %f\n",x[cc][n],y[cc][n]);
	}
	fprintf(ef, "%f %f\n",x[cc][1],y[cc][1]);
	fprintf(ef, "NaN Nan\n");
      }
    }
  }
  */

  /*-----------------------------------------------------------------*/
  /* Convert the coordinate information to mesh indices              */
  convert_mesh_input(params, m, x, y, params->bathy, 1);

  /* Write to file from the mesh structure                           */
  if (filef) {
    FILE *ef;
    char key[MAXSTRLEN], buf[MAXSTRLEN];
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= m->ns2; cc++) {
	for (n = 1; n <= m->npe[cc]; n++) {
	  fprintf(ef, "%f %f\n",m->xloc[m->eloc[0][cc][n]], m->yloc[m->eloc[0][cc][n]]);
	}
	fprintf(ef, "%f %f\n",m->xloc[m->eloc[0][cc][1]], m->yloc[m->eloc[0][cc][1]]);
	fprintf(ef, "NaN Nan\n");
      }
      fclose(ef);
    }
    sprintf(buf,"%s_c.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= m->ns2; cc++)
	fprintf(ef, "%f %f\n",m->xloc[m->eloc[0][cc][0]], m->yloc[m->eloc[0][cc][0]]);
      fclose(ef);
    }
    if (m->nobc) {
      sprintf(buf,"%s_b.txt", key);
      if ((ef = fopen(buf, "w")) == NULL) filef = 0;
      if (filef) {
	for (n = 0; n < m->nobc; n++) {
	  for (cc = 1; cc <= m->npts[n]; cc++) {
	    fprintf(ef, "%f %f\n", m->xloc[m->obc[n][cc][0]], m->yloc[m->obc[n][cc][0]]);
	    fprintf(ef, "%f %f\n", m->xloc[m->obc[n][cc][1]], m->yloc[m->obc[n][cc][1]]);
	    fprintf(ef, "NaN NaN\n");
	  }
	}
	fclose(ef);
      }
    }
  }

  d_free_2d(x);
  d_free_2d(y);
}

/* END meshstruct_s()                                                */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Build an input mesh struture for unstructured grids               */
/*-------------------------------------------------------------------*/
void meshstruct_us(parameters_t *params)
{
  char key[MAXSTRLEN], buf[MAXSTRLEN];
  int cc, c, i, j, jj, cco, n;
  mesh_t *m;
  int mo2;              /* Max. # of cells in each open boundary */
  double bw, bo;
  double **x, **y;
  int *cmap;
  int verbose = 0;      /* Verbose output                            */
  int filef = 2;        /* Output mesh (1 from params, 2 from mesh)  */
  int testp = 0;        /* Test a closed perimeter exists            */
  int debug = -1;       /* Debug for params->x[debug][n]             */

  if (DEBUG("init_m"))
    dlog("init_m", "\nBegin building mesh structure\n");

  /*-----------------------------------------------------------------*/
  /* If an unstructured mesh configuration is input from file, then  */
  /* the mesh structure is populated in read_mesh_us(), so return.   */
  if (params->us_type & US_IUS) {
    if (params->runmode & (AUTO | DUMP)) set_bathy_us(params);
    mesh_init_OBC(params, params->mesh);
    cmap = i_alloc_1d(params->ns2+1);
    for (cc = 1; cc <= params->ns2; cc++) cmap[cc] = cc;
    for (n = 0; n < params->nobc; n++) {
      convert_obc_list(params, params->open[n], n, NULL, cmap);
    }
    i_free_1d(cmap);
    return;
  }

  /*-----------------------------------------------------------------*/
  /* Strip out any land cells                                        */
  c = 1;
  cmap = i_alloc_1d(params->ns2+1);
  x = d_alloc_2d(params->npe+1, params->ns2+1);
  y = d_alloc_2d(params->npe+1, params->ns2+1);
  for (cc = 1; cc <= params->ns2; cc++) {
    i = (params->npe2[cc]) ? params->npe2[cc] : params->npe;
    /* Make a copy of the original mesh to get the OBCs below        */
    for (n = 0; n <= i; n++) {
      x[cc][n] = params->x[cc][n];
      y[cc][n] = params->y[cc][n];
    }
    if (params->bathyfill & B_REMOVE) {
      if (params->bathy[cc] != LANDCELL && params->bathy[cc] != NOTVALID) {
	for (n = 0; n <= i; n++) {
	  params->x[c][n] = params->x[cc][n];
	  params->y[c][n] = params->y[cc][n];
	}
	params->bathy[c] = params->bathy[cc];
	params->npe2[c] = params->npe2[cc];
	cmap[cc] = c;
	if (cc == debug) printf("New params->x[i] coordinate after land stripping = %d\n", c);
	c++;
      }
    } else if (params->bathyfill & B_MINIMUM) {
      if (params->bathy[cc] == LANDCELL || params->bathy[cc] == NOTVALID) 
	params->bathy[c] = -params->bmin;
    }
    /* Bathymetry filling using surrounding average (B_AVERAGE)      */
    /* computed below after neighbour maps have been created.        */
  }
  if (params->bathyfill & B_REMOVE) params->ns2 = c - 1;

  /*-----------------------------------------------------------------*/
  /* Allocate                                                        */
  free_mesh(params->mesh);
  params->mesh = mesh_init(params, params->ns2, 0);
  m = params->mesh;
  strcpy(params->gridtype, "UNSTRUCTURED");

  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh structure allocated OK\n");

  /*-----------------------------------------------------------------*/
  /* Open boundaries                                                 */
  if (m->nobc) {
    int oldcode = 0;
    for (n = 0; n < params->nobc; n++) {
      open_bdrys_t *open = params->open[n];
      convert_obc_list(params, open, n, NULL, cmap);
      if (oldcode) {
      if (open->posx == NULL || open->posy == NULL) continue;
      for (cc = 0; cc < open->npts; cc++) {
	/* Note: open->npts loops from 0:npts-1, m->npts loops from */
	/* 1:npts.                                                  */
	double xb1 = open->posx[cc][0];
	double yb1 = open->posy[cc][0];
	double xb2 = open->posx[cc][1];
	double yb2 = open->posy[cc][1];
	cco = open->locu[cc];	
	c = cmap[cco];
	if(xb2 == NOTVALID && yb2 == NOTVALID) {
	  /* Open boundaries are specified with mesh indices. Set   */
	  /* the mesh structure directly. Note: obc indices do not  */
	  /* need to be remapped in convert_mesh_input(); set       */
	  /* m->loc[n][0] = NOTVALID to indicate this.              */
	  m->loc[n][cc+1] = c;
	  m->obc[n][cc+1][0] = (int)xb1;
	  m->obc[n][cc+1][1] = (int)yb1;
	  m->loc[n][0] = NOTVALID;
	  if (verbose) printf("OBC%d %d : cc=%d (%d %d)\n",n, cc+1, cco, 
			      m->obc[n][cc+1][0], m->obc[n][cc+1][1]);
	  continue;
	}
	for (j = 1; j <= params->npe2[c]; j++) {
	  double x1, y1, x2, y2;
	  double eps = 1e-6;    /* Precision for OBC comparisons    */
	  x1 = params->x[c][j];
	  y1 = params->y[c][j];
	  jj = (j == m->npe[c]) ? 1 : j + 1;
	  x2 = params->x[c][jj];
	  y2 = params->y[c][jj];
	  if ((fabs(xb1-x1) < eps && fabs(yb1-y1) < eps && 
	       fabs(xb2-x2) < eps && fabs(yb2-y2) < eps) ||
	      (fabs(xb1-x2) < eps && fabs(yb1-y2) < eps && 
	       fabs(xb2-x1) < eps && fabs(yb2-y1) < eps)) {
	    m->loc[n][cc+1] = c;
	    m->obc[n][cc+1][0] = j;
	    m->obc[n][cc+1][1] = jj;
	    if (verbose) printf("OBC%d %d : cc=%d (%d %d)\n",n, cc+1, cco, j, jj); 
	  }
	}
      }
      }
    }
  }

  if (verbose) {
    for (cc = 1; cc <= m->ns2; cc++) {
      printf("%d %d %f %f\n",cc,params->npe2[cc],params->x[cc][0],params->y[cc][0]);
      for (n = 1; n <= params->npe2[cc]; n++) {
	printf(" %d %f %f\n",n,params->x[cc][n],params->y[cc][n]);
      }
    }
  }

  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh structure OBCs set OK\n");

  /*-----------------------------------------------------------------*/
  /* Write to file from the parameter structure                      */
  if (filef) {
    mesh_ofile_name(params, key);
  }
  if (filef == 1) {
    FILE *ef;
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= m->ns2; cc++) {
	for (n = 1; n <= params->npe2[cc]; n++) {
	  fprintf(ef, "%f %f\n",params->x[cc][n],params->y[cc][n]);
	}
	fprintf(ef, "%f %f\n",params->x[cc][1],params->y[cc][1]);
	fprintf(ef, "NaN Nan\n");
      }
      fclose(ef);
    }
    sprintf(buf,"%s_c.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= m->ns2; cc++)
	fprintf(ef, "%f %f\n",params->x[cc][0],params->y[cc][0]);
      fclose(ef);
    }
    if (params->nobc) {
      sprintf(buf,"%s_b.txt", key);
      if ((ef = fopen(buf, "w")) == NULL) filef = 0;
      if (filef) {
	for (n = 0; n < params->nobc; n++) {
	  open_bdrys_t *open = params->open[n];
	  for (cc = 0; cc < open->npts; cc++) {
	    fprintf(ef, "%f %f\n", open->posx[cc][0], open->posy[cc][0]);
	    fprintf(ef, "%f %f\n", open->posx[cc][1], open->posy[cc][1]);
	    fprintf(ef, "NaN NaN\n");
	  }
	}
	fclose(ef);
      }
    }
  }

  convert_mesh_input(params, m, params->x, params->y, NULL, 1);
  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh converted OK\n");

  /*-----------------------------------------------------------------*/
  /* Overwrite bathymetry values if required                         */
  if (params->runmode & (AUTO | DUMP)) set_bathy_us(params);

  /* Now performed in set_bathy_us()
  if (params->bathyfill & B_AVERAGE) {
    for (cc = 1; cc <= m->ns2; cc++) {
      if (params->bathy[cc] == LANDCELL || params->bathy[cc] == NOTVALID) {
	double bath = 0.0;
	double nb= 0.0;
	for (j = 1; j <= m->npe[cc]; j++) {
	  if ((c = m->neic[j][cc])) {
	    if (params->bathy[c] != LANDCELL && params->bathy[c] != NOTVALID) {
	      bath += params->bathy[c];
	      nb += 1.0;
	    }
	  }
	}
	params->bathy[cc] = (nb) ? bath / nb : -params->bmin;
      }
    }
  }
  */

  /*-----------------------------------------------------------------*/
  /* Write to file from the mesh structure                           */
  if (filef == 2) {
    FILE *ef;
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= m->ns2; cc++) {
	for (n = 1; n <= m->npe[cc]; n++) {
	  fprintf(ef, "%f %f\n",m->xloc[m->eloc[0][cc][n]], m->yloc[m->eloc[0][cc][n]]);
	}
	fprintf(ef, "%f %f\n",m->xloc[m->eloc[0][cc][1]], m->yloc[m->eloc[0][cc][1]]);
	fprintf(ef, "NaN Nan\n");
      }
      fclose(ef);
    }
    sprintf(buf,"%s_c.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= m->ns2; cc++)
	fprintf(ef, "%f %f\n",m->xloc[m->eloc[0][cc][0]], m->yloc[m->eloc[0][cc][0]]);
      fclose(ef);
    }
    if (m->nobc) {
      sprintf(buf,"%s_b.txt", key);
      if ((ef = fopen(buf, "w")) == NULL) filef = 0;
      if (filef) {
	for (n = 0; n < m->nobc; n++) {
	  if (params->open[n]->intype & O_COR) {
	    fprintf(ef, "Boundary information written to file 'boundary.txt' and 'obc_spec.txt'.\n");
	    break;
	  }
	  for (cc = 1; cc <= m->npts[n]; cc++) {
	    fprintf(ef, "%f %f\n", m->xloc[m->obc[n][cc][0]], m->yloc[m->obc[n][cc][0]]);
	    fprintf(ef, "%f %f\n", m->xloc[m->obc[n][cc][1]], m->yloc[m->obc[n][cc][1]]);
	    fprintf(ef, "NaN NaN\n");
	  }
	}
	fclose(ef);
      }
    }
  }

  /* Decide whether to take the negative of the bathymetry value     */
  i = j = jj = 0;
  for (cc = 1; cc <= m->ns2; cc++) {
    double b = params->bathy[cc];
    if (b < 0) i++;
    if (b == NOTVALID) j++;
    if (b == LANDCELL) jj++;
  }
  if (!i && !j && !jj) {
    for (cc = 1; cc <= m->ns2; cc++) {
      params->bathy[cc] *= -1.0;
    }
  }

  /* Test if a closed perimeter can be made for open boundaries      */
  if (testp)
    perimeter_mask(params, m->neic);

  i_free_1d(cmap);
  d_free_2d(x);
  d_free_2d(y);
  if (DEBUG("init_m"))
    dlog("init_m", "\nMesh structure built OK\n");
}

/* END meshstruct_us()                                               */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Initialize a mesh structure.                                      */
/*-------------------------------------------------------------------*/
mesh_t *mesh_init(parameters_t *params, /* Input parameters          */
		  int ns2,              /* Mesh size                 */
		  int npe               /* Constant nodes per cell   */
		  )
{
  int cc, n;

  mesh_t *mesh = (mesh_t *)malloc(sizeof(mesh_t));
  memset(mesh, 0, sizeof(mesh_t));

  mesh->ns2 = ns2;

  /*-----------------------------------------------------------------*/
  /* Vertices per cell                                               */
  mesh->npe = i_alloc_1d(mesh->ns2+1);
  mesh->mnpe = 0;
  for (cc = 1; cc <= mesh->ns2; cc++) {
    if (npe)
      mesh->npe[cc] = npe;
    else
      mesh->npe[cc] = params->npe2[cc];
    if (mesh->npe[cc] > mesh->mnpe)
      mesh->mnpe = mesh->npe[cc];
  }

  /*-----------------------------------------------------------------*/
  /* Mesh coordinates                                                */
  mesh->yloc = d_alloc_1d((mesh->ns2 * (mesh->mnpe+1)) + 1);
  mesh->xloc = d_alloc_1d((mesh->ns2 * (mesh->mnpe+1)) + 1);
  mesh->eloc = i_alloc_3d(mesh->mnpe+1, mesh->ns2+1, 2);

  if (params->us_type & US_IJ) {
    mesh->nce1 = params->nce1;
    mesh->nce2 = params->nce2;
    mesh->iloc = i_alloc_1d(mesh->ns2+1);
    mesh->jloc = i_alloc_1d(mesh->ns2+1);
  }

  /*-----------------------------------------------------------------*/
  /* Open boundaries                                                 */
  mesh_init_OBC(params, mesh);

  return mesh;
}

/* END mesh_init()                                                   */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Allocate the open boundary arrays in the mesh structure           */
/*-------------------------------------------------------------------*/
void mesh_init_OBC(parameters_t *params, /* Input parameters         */
		   mesh_t *mesh          /* Mesh info                */
		   )
{
  int n, mo2;
  int uf = -1, sf = -1;

  if (params->nobc) {

    /* Check that START_LOC and other OBC specifications are not     */
    /* mixed.                                                        */
    for (n = 0; n < params->nobc; n++) {
      open_bdrys_t *open = params->open[n];
      if (!(open->intype & O_COR)) uf = n;
      if (open->intype & O_COR) sf = n;
    }
    if (uf >=0 && sf >=0)
      hd_quit("read_grid_us: Can't mix OBC specification UPOINTS and START_LOC; boundaries %d and %d.\n", uf, sf);

    mesh->nobc = params->nobc;
    mesh->npts = i_alloc_1d(mesh->nobc);

    mo2 = 0;
    for (n = 0; n < params->nobc; n++) {
      open_bdrys_t *open = params->open[n];
      mesh->npts[n] = open->npts;
      if (open->npts > mo2) mo2 = open->npts;
    }
    mo2++;
    mesh->loc = i_alloc_2d(mo2, mesh->nobc);
    mesh->obc =  i_alloc_3d(2, mo2, mesh->nobc);
  }
}

/* END mesh_init_OBC()                                               */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Frees an input mesh structure                                     */
/*-------------------------------------------------------------------*/
void free_mesh(mesh_t *mesh)
{
  if (mesh != NULL) {
    i_free_1d(mesh->npe);
    d_free_1d(mesh->xloc);
    d_free_1d(mesh->yloc);
    i_free_3d(mesh->eloc);
    if (mesh->nobc) {
      i_free_1d(mesh->npts);
      i_free_2d(mesh->loc);
      i_free_3d(mesh->obc);
    }
    if (mesh->iloc) i_free_1d(mesh->iloc);
    if (mesh->jloc) i_free_1d(mesh->jloc);
    free((mesh_t *)mesh);
  }
}

/* END free_mesh()                                                   */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Creates a list of segments locations at the grid perimeter        */
/* corresponding to OBC locations.                                   */
/*-------------------------------------------------------------------*/
int get_mesh_obc(parameters_t *params, 
		 int **neic
		 )
{
  FILE *fp, *op;
  int n, m, cc, c, cn, cs, i, j, jj, jn, js;
  int np;               /* Size of the perimeter array               */
  int *perm;            /* Mesh indices of the perimeter             */
  double *lon, *lat;    /* Coordinates of the perimeter              */
  int filef = 1;        /* Print perimeter in 'perimeter.txt'        */
  int filep = 1;        /* Print OBCs in 'obc_spec.txt'              */
  int verbose = 0;      /* Print to screen                           */
  int dof = 1;          /* Do this routine when dof = 1              */
  int *si, *ei, mi;     /* Start, end and mid indices in perimeter   */
  int *dir;             /* Direction to traverse the perimeter       */
  int mobc = 0;         /* Maximum points in the OBCs                */
  int *mask;            /* Mask for perimeter cells visited          */
  int isclosed = 0;     /* Set for closed perimeters                 */
  mesh_t *mesh = params->mesh;

  if (!params->nobc) return(0);

  for (n = 0; n < params->nobc; n++) {
    open_bdrys_t *open = params->open[n];
    if (open->slat == NOTVALID || open->slon == NOTVALID ||
	open->elat == NOTVALID || open->elon == NOTVALID)
      dof = 0;
  }
  if (!dof) return(0);

  mesh->nobc = params->nobc;
  mesh->npts = i_alloc_1d(mesh->nobc);

  /*-----------------------------------------------------------------*/
  /* Make a path of cells at the perimeter of the mesh               */
  /* First perimeter cell                                            */
  for (cc = 1; cc <= mesh->ns2; cc++) {
    if ((js = has_neighbour(cc, mesh->npe[cc], neic)))
      break;
  }
  cs = cn = cc;
  n = 1;
  jn = js;
  mask = i_alloc_1d(mesh->ns2+1);
  memset(mask, 0, (mesh->ns2+1) * sizeof(int));
  /*mask[cs] = 1;*/
  /* Count the number of perimeter cells                             */
  for (cc = 1; cc <= mesh->ns2; cc++) {
    int found = 0;
    /* Cycle edges clockwise from the perimeter edge                 */
    j = jn;
    for (jj = 1; jj <= mesh->npe[cn]; jj++) {
      c = neic[j][cn];
      j = (j >= mesh->npe[cn]) ? 1 : j + 1;
      if (c == 0) continue;
      if ((js = has_neighbour(c, mesh->npe[c], neic)) && !mask[c]) {
	cn = c;
	jn = js;
	mask[cn] = 1;
	found = 1;
	n++;
	break;
      }
    }
    /* Dead end : try to back out                                    */
    if (!found) {
      int je, cp = cn;
      j = jn;
      for (jj = 1; jj <= mesh->npe[cn]; jj++) {
	c = neic[j][cn];
	j = (j >= mesh->npe[cn]) ? 1 : j + 1;
	if (c == 0) continue;
	if ((js = has_neighbour(c, mesh->npe[c], neic))) {
	  cn = c;
	  for (je = 1; je <= mesh->npe[cn]; je++) {
	    if (neic[je][cn] == cp)
	      jn = (je + 1 > mesh->npe[cn]) ? 1 : je + 1;
	  }
	  break;
	}
      }
    }
    if (cn == cs) {
      isclosed = 1;
      break;
    }
  }
  if (!isclosed) hd_quit("get_mesh_obc: Can't make a closed perimeter for mesh (%d != %d).\n", cs, cn);
  np = n;
  /* Allocate                                                        */
  lon = d_alloc_1d(np+1);
  lat = d_alloc_1d(np+1);
  perm = i_alloc_1d(np+1);
  if (filep)
    fp = fopen("perimeter.txt", "w");
  /* Get the perimeter path                                          */
  memset(mask, 0, (mesh->ns2+1) * sizeof(int));
  n = 1;
  cn = cs;
  /*
  lon[n] = mesh->xloc[mesh->eloc[0][cn][0]];
  lat[n] = mesh->yloc[mesh->eloc[0][cn][0]];
  perm[n] = cn;
  mask[cn] = 1;
  n++;
  */
  jn = js;
  for (cc = 1; cc <= mesh->ns2; cc++) {
    int found = 0;
    j = jn;
    for (jj = 1; jj <= mesh->npe[cn]; jj++) {
      c = neic[j][cn];
      j = (j >= mesh->npe[cn]) ? 1 : j + 1;
      if (c == 0) continue;
      if ((js = has_neighbour(c, mesh->npe[c], neic)) && !mask[c]) {
	cn = c;
	jn = js;
	mask[cn] = 1;
	found = 1;
	lon[n] = mesh->xloc[mesh->eloc[0][cn][0]];
	lat[n] = mesh->yloc[mesh->eloc[0][cn][0]];
	if (verbose) printf("%f %f\n",lon[n],lat[n]);
	if (filep) fprintf(fp, "%f %f\n",lon[n],lat[n]);
	perm[n] = cn;
	n++;
	break;
      }
    }
    /* Dead end : try to back out                                    */
    if (!found) {
      int je, cp = cn;
      j = jn;
      for (jj = 1; jj <= mesh->npe[cn]; jj++) {
	c = neic[j][cn];
	j = (j >= mesh->npe[cn]) ? 1 : j + 1;
	if (c == 0) continue;
	if ((js = has_neighbour(c, mesh->npe[c], neic))) {
	  cn = c;
	  for (je = 1; je <= mesh->npe[cn]; je++) {
	    if (neic[je][cn] == cp)
	      jn = (je + 1 > mesh->npe[cn]) ? 1 : je + 1;
	  }
	  break;
	}
      }
    }
    if (cn == cs) break;
  }
  i_free_1d(mask);
  if (filep) fclose(fp);

  /*-----------------------------------------------------------------*/
  /* Count the boundary edges within each segment                    */
  dir = i_alloc_1d(mesh->nobc);
  si = i_alloc_1d(mesh->nobc);
  ei = i_alloc_1d(mesh->nobc);
  for (m = 0; m < mesh->nobc; m++) {
    open_bdrys_t *open = params->open[m];
    if (open->slat == NOTVALID || open->slon == NOTVALID ||
	open->elat == NOTVALID || open->elon == NOTVALID)
      continue;
    /* Get the indices of start, med and end locations               */
    mesh->npts[m] = 0;
    si[m] = find_mindex(open->slon, open->slat, lon, lat, np, NULL);
    ei[m] = find_mindex(open->elon, open->elat, lon, lat, np, NULL);
    /* Swap if required so that si < ei                              */
    if (ei[m] < si[m]) {
      n = si[m];
      si[m] = ei[m];
      ei[m] = n;
    }
    if (si[m] == ei[m]) {
      /* si and ei are the same location                             */
      dir[m] = 1;
    } else {
      mi = find_mindex(open->mlon, open->mlat, lon, lat, np, NULL);
      /* si is next to ei (mi==si or mi==ei)                         */
      if (mi == si[m] || mi == ei[m]) {
	dir[m] = 0;
	if (ei[m] == si[m] + 1)
	  dir[m] = 1;
	else
	  dir[m] = -1;
      } else {
	/* Get the direction to cycle along the path                 */
	dir[m] = 0;
	n = si[m];
	for (i = 1; i <= np; i++) {
	  if (n == mi) {
	    dir[m] = 1;
	    break;
	  }
	  n = (n == np) ? 1 : n+1;
	  if (n == ei[m]) break;
	}
	if (!dir[m]) {
	  n = si[m];
	  for (i = 1; i <= np; i++) {
	    if (n == mi) {
	      dir[m] = -1;
	      break;
	    }
	    n = (n == 1) ? np : n-1;
	    if (n == ei[m]) break;
	  }
	}
      }
    }
    if (!dir[m])
      hd_quit("get_mesh_obc: Can't find OBC mid-point (%f %f) in boundary %d\n", 
	      open->mlon, open->mlat, m);
    /* Count the boundary edges along the segment                    */
    n = si[m];
    for (i = si[m]; i <= ei[m]; i++) {
      cc = perm[n];
      for (j = 1; j <= mesh->npe[cc]; j++)
	if (!neic[j][cc]) mesh->npts[m]++;
      if (n == ei[m]) break;
      n += dir[m];
      if (n <= 0 && dir[m] == -1) n = np;
      if (n > np && dir[m] == 1) n = 1;
    }
    if (mesh->npts[m] > mobc) mobc = mesh->npts[m];
  }

  /*-----------------------------------------------------------------*/
  /* Allocate memory                                                 */
  if (mobc) {
    mesh->loc = i_alloc_2d(mobc+1, mesh->nobc);
    mesh->obc =  i_alloc_3d(2, mobc+1, mesh->nobc);
  }

  /*-----------------------------------------------------------------*/
  /* Save the OBC locations to the mesh structure                    */
  fp = fopen("boundary.txt", "w");
  op = fopen("obc_spec.txt", "w");
  for (m = 0; m < mesh->nobc; m++) {
    open_bdrys_t *open = params->open[m];
    if (open->slat == NOTVALID || open->slon == NOTVALID ||
	open->elat == NOTVALID || open->elon == NOTVALID)
      continue;

    /* Save to mesh                                                  */
    n = si[m];
    mesh->npts[m] = 1;
    for (i = si[m]; i <= ei[m]; i++) {
      cc = perm[n];
      for (j = 1; j <= mesh->npe[cc]; j++) {
	if (!neic[j][cc]) {
	  mesh->loc[m][mesh->npts[m]] = cc;
	  mesh->obc[m][mesh->npts[m]][0] = mesh->eloc[0][cc][j];
	  mesh->obc[m][mesh->npts[m]][1] = mesh->eloc[1][cc][j];
	  mesh->npts[m]++;
	}
      }
      if (n == ei[m]) break;
      n += dir[m];
      if (n <= 0 && dir[m] == -1) n = np;
      if (n > np && dir[m] == 1) n = 1;
    }
    mesh->npts[m]--;

    /* Verbose output if required                                    */
    if (verbose) {
      printf("\nBOUNDARY%d     %d\n", m, mesh->npts[m]);
      for (cc = 1; cc <= mesh->npts[m]; cc++) {
	printf("%d (%f,%f)-(%f,%f)\n", mesh->loc[m][cc], mesh->xloc[mesh->obc[m][cc][0]],
	       mesh->yloc[mesh->obc[m][cc][0]], mesh->xloc[mesh->obc[m][cc][1]],
	       mesh->yloc[mesh->obc[m][cc][1]]);
      }
    }

    /* File output if required                                       */
    if (filef) {
      if (fp != NULL) {
	for (cc = 1; cc <= mesh->npts[m]; cc++) {
	  fprintf(fp, "%f %f\n", mesh->xloc[mesh->obc[m][cc][0]], mesh->yloc[mesh->obc[m][cc][0]]);
	  fprintf(fp, "%f %f\n", mesh->xloc[mesh->obc[m][cc][1]], mesh->yloc[mesh->obc[m][cc][1]]);
	}
	fprintf(fp, "NaN NaN\n");
      }
    }
    if (op  != NULL) {
      fprintf(op, "\nBOUNDARY%d.UPOINTS     %d\n", m, mesh->npts[m]);
      for (cc = 1; cc <= mesh->npts[m]; cc++) {
	fprintf(op, "%d (%d %d)\n", mesh->loc[m][cc], mesh->obc[m][cc][0],
		mesh->obc[m][cc][1]);
      }
    }
  }
  fclose(fp);
  fclose(op);
  i_free_1d(si);
  i_free_1d(ei);
  i_free_1d(dir);
  i_free_1d(perm);
  d_free_1d(lon);
  d_free_1d(lat);
  return(1);
}

/* END get_mesh_obc()                                                */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Gets the perimeter cell centres of a mesh                         */
/*-------------------------------------------------------------------*/
int perimeter_mask(parameters_t *params, 
		   int **neic
		   )
{
  FILE *fp;
  mesh_t *mesh = params->mesh;
  int n, m, cc, c, cn, cs, i, j, jj, jn, js, np;
  int *mask;
  int verbose = 0;
  int isclosed = 0;

  fp = fopen("perimeter.txt", "w");

  /*-----------------------------------------------------------------*/
  /* Make a path of cells at the perimeter of the mesh               */
  /* First perimeter cell                                            */
  for (cc = 1; cc <= mesh->ns2; cc++) {
    if ((js = has_neighbour(cc, mesh->npe[cc], neic)))
      break;
  }

  /* Set the mask for all cells surrounded by other cells            */
  mask = i_alloc_1d(mesh->ns2+1);
  memset(mask, 0, (mesh->ns2+1) * sizeof(int));
  /*
  for (cc = 1; cc <= mesh->ns2; cc++) {
    if (has_neighbour(cc, mesh->npe[cc], neic) == 0 && !mask[cc])
      mask[cc] = 1;
  }
  */

  cs = cn = cc;
  n = 1;
  jn = js;
  /*mask[cs] = 1;*/
  printf("Start coordinate = %d[%f %f]\n",cs, mesh->xloc[mesh->eloc[0][cs][0]], mesh->yloc[mesh->eloc[0][cs][0]]);
  printf("Edge direction = %d\n", jn);
  /* Count the number of perimeter cells                             */
  for (cc = 1; cc <= mesh->ns2; cc++) {
    int found = 0;
    /* Cycle edges clockwise from the perimeter edge                 */
    j = jn;
    for (jj = 1; jj <= mesh->npe[cn]; jj++) {
      c = neic[j][cn];
      j = (j >= mesh->npe[cn]) ? 1 : j + 1;
      /*
      fprintf(fp, "cell %d : j=%d cn=%d[%f %f]\n",cn, j, c, mesh->xloc[mesh->eloc[0][c][0]], mesh->yloc[mesh->eloc[0][c][0]]);
      */
      if (c == 0) continue;
      /* If the cell hasn't been encountered, add it to the          */
      /* perimeter.                                                  */
      if ((js = has_neighbour(c, mesh->npe[c], neic)) && !mask[c]) {
	cn = c;
	jn = js;
	mask[cn] = 1;
	found = 1;
	fprintf(fp, "%f %f %d %d\n",mesh->xloc[mesh->eloc[0][cn][0]], mesh->yloc[mesh->eloc[0][cn][0]], cn, jn);
	n++;
	break;
      }
    }
    /* Dead end : try to back out                                    */
    if (!found) {
      int je, cp = cn;
      j = jn;
      for (jj = 1; jj <= mesh->npe[cn]; jj++) {
	c = neic[j][cn];
	j = (j >= mesh->npe[cn]) ? 1 : j + 1;
	if (c == 0) continue;
	if ((js = has_neighbour(c, mesh->npe[c], neic))) {
	  cn = c;
	  for (je = 1; je <= mesh->npe[cn]; je++) {
	    if (neic[je][cn] == cp)
	      jn = (je + 1 > mesh->npe[cn]) ? 1 : je + 1;
	  }
	  /*fprintf(fp, "dead end %d %f %f %d %d\n",cc, mesh->xloc[mesh->eloc[0][cn][0]], mesh->yloc[mesh->eloc[0][cn][0]], cn, jn);*/
	  break;
	}
      }
    }

    if (cn == cs) {
      isclosed = 1;
      break;
    }
  }
  printf("End coordinate = %d[%f %f]\n",cn, mesh->xloc[mesh->eloc[0][cn][0]], mesh->yloc[mesh->eloc[0][cn][0]]);
  printf("Number perimeter cells = %d\n", n);
  i_free_1d(mask);
  fclose(fp);
  if (!isclosed) hd_quit("perimeter_mask: Can't make a closed perimeter for mesh (%d != %d).\n", cs, cn);
  return(n);
}

/* END perimeter_mask()                                              */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Writes grid infromation in a mesh structure to a nominated file   */
/*-------------------------------------------------------------------*/
void write_mesh_us(parameters_t *params, 
		   double *bathy,
		   FILE *op,
		   int mode     /* 1 = _us.txt output with OBCs      */
		                /* 2 = _us.txt output without OBCs   */
		                /* 4 = grid output from params       */
		                /* 8 = grid output from mesh         */
		   )
{
  int n, cc, i, j, m;
  mesh_t *mesh = params->mesh;
  int ns2 = mesh->ns2;

  /*-----------------------------------------------------------------*/
  /* Write the mesh info to a file with pointer op                   */
  if (mode == 1 || mode == 2) {
    fprintf(op, "\nCoordinates\n");
    for (n = 1; n <= mesh->ns; n++) {
      fprintf(op, "%d %f %f\n", n, mesh->xloc[n], mesh->yloc[n]);
    }

    fprintf(op, "\nIndices\n");
    for (cc = 1; cc <= ns2; cc++) {
      if (mesh->iloc && mesh->jloc)
	fprintf(op, "%d %d %d : %d %d\n",cc, mesh->npe[cc], mesh->eloc[0][cc][0],
		mesh->iloc[cc], mesh->jloc[cc]);
      else
	fprintf(op, "%d %d %d\n",cc, mesh->npe[cc], mesh->eloc[0][cc][0]);
      for (j = 1; j <= mesh->npe[cc]; j++)
	fprintf(op, "%d %d %d\n", j, mesh->eloc[0][cc][j], mesh->eloc[1][cc][j]);
    }
    
    if (mode == 1) {
      if (mesh->nobc) {
	fprintf(op, "\nNBOUNDARIES    %d\n", mesh->nobc);
	for (n = 0; n < mesh->nobc; n++) {
	  fprintf(op, "BOUNDARY%1d.NPOINTS  %d\n", n, mesh->npts[n]);
	  for (cc = 1; cc <= mesh->npts[n]; cc++) {
	    fprintf(op, "%d (%d %d)\n", mesh->loc[n][cc], mesh->obc[n][cc][0], mesh->obc[n][cc][1]);
	  }
	}
      } else
	fprintf(op, "\nNBOUNDARIES    0\n");
    }
    if (params->bathy != NULL) {
      fprintf(op, "\nBATHY   %d\n", ns2);
      for (cc = 1; cc <= ns2; cc++)
	fprintf(op, "%f\n", params->bathy[cc]);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Write the mesh info to files suitable to plot; edge _e.txt,     */
  /* centre _c.txt and boundary _b.txt. Mesh information is          */
  /* extracted from the params structure.                            */
  if (mode == 4) {
    FILE *ef;
    char key[MAXSTRLEN], buf[MAXSTRLEN];
    int filef = 1;

    mesh_ofile_name(params, key);
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= mesh->ns2; cc++) {
	for (n = 1; n <= mesh->npe[cc]; n++) {
	  fprintf(ef, "%f %f\n",mesh->xloc[mesh->eloc[0][cc][n]], mesh->yloc[mesh->eloc[0][cc][n]]);
	}
	fprintf(ef, "%f %f\n",mesh->xloc[mesh->eloc[0][cc][1]], mesh->yloc[mesh->eloc[0][cc][1]]);
	fprintf(ef, "NaN Nan\n");
      }
      fclose(ef);
    }
    sprintf(buf,"%s_c.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= mesh->ns2; cc++)
	fprintf(ef, "%f %f\n",mesh->xloc[mesh->eloc[0][cc][0]], mesh->yloc[mesh->eloc[0][cc][0]]);
      fclose(ef);
    }
    if (mesh->nobc) {
      sprintf(buf,"%s_b.txt", key);
      if ((ef = fopen(buf, "w")) == NULL) filef = 0;
      if (filef) {
	for (n = 0; n < mesh->nobc; n++) {
	  for (cc = 1; cc <= mesh->npts[n]; cc++) {
	    fprintf(ef, "%f %f\n", mesh->xloc[mesh->obc[n][cc][0]], mesh->yloc[mesh->obc[n][cc][0]]);
	    fprintf(ef, "%f %f\n", mesh->xloc[mesh->obc[n][cc][1]], mesh->yloc[mesh->obc[n][cc][1]]);
	    fprintf(ef, "NaN NaN\n");
	  }
	}
	fclose(ef);
      }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Write the mesh info to files suitable to plot; edge _e.txt,     */
  /* centre _c.txt and boundary _b.txt. Mesh information is          */
  /* extracted from the mesh structure.                              */
  if (mode == 8) {
    FILE *ef;
    char key[MAXSTRLEN], buf[MAXSTRLEN];
    int filef = 1;

    if (mesh == NULL) return;
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= mesh->ns2; cc++) {
	for (n = 1; n <= params->npe2[cc]; n++) {
	  fprintf(ef, "%f %f\n",params->x[cc][n],params->y[cc][n]);
	}
	fprintf(ef, "%f %f\n",params->x[cc][1],params->y[cc][1]);
	fprintf(ef, "NaN Nan\n");
      }
      fclose(ef);
    }
    sprintf(buf,"%s_c.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    if (filef) {
      for (cc = 1; cc <= mesh->ns2; cc++)
	fprintf(ef, "%f %f\n",params->x[cc][0],params->y[cc][0]);
      fclose(ef);
    }
    if (params->nobc) {
      sprintf(buf,"%s_b.txt", key);
      if ((ef = fopen(buf, "w")) == NULL) filef = 0;
      if (filef) {
	for (n = 0; n < params->nobc; n++) {
	  open_bdrys_t *open = params->open[n];
	  for (cc = 0; cc < open->npts; cc++) {
	    fprintf(ef, "%f %f\n", open->posx[cc][0], open->posy[cc][0]);
	    fprintf(ef, "%f %f\n", open->posx[cc][1], open->posy[cc][1]);
	    fprintf(ef, "NaN NaN\n");
	  }
	}
	fclose(ef);
      }
    }
  }
}

/* END write_mesh_us()                                               */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Reads and populates the input mesh structure                      */
/*-------------------------------------------------------------------*/
int read_mesh_us(parameters_t *params)
{
  mesh_t *mesh;
  char buf[MAXSTRLEN], oname[MAXSTRLEN];
  char *fields[MAXSTRLEN * MAXNUMARGS];
  FILE *op;
  int i, n, c, cc, np, m, ns, j;
  int ns2, npe;
  int verbose = 0;
  int rbf = 0;

  /* Read the mesh dimensions                                        */
  op = params->prmfd;
  memset(projection, 0, sizeof(projection));
  prm_read_char(op, "PROJECTION", params->projection);

  /*
  if (prm_read_char(op, "JIGSAW_FILE", buf))
    return(-1);
  prm_read_int(op, "nMaxMesh2_face_nodes", &npe);
  prm_read_int(op, "nMesh2_face_indices", &ns);
  prm_read_int(op, "nMesh2_face", &ns2);
  */
  npe = params->npe;
  ns = params->ns;
  ns2 = params->ns2;
  params->nce1 = params->nce2 = 0;
  if (params->nce1 && params->nce2) params->us_type |= US_IJ;
  params->us_type |= US_IUS;

  params->mesh = mesh_init(params, ns2, npe);
  mesh = params->mesh;
  mesh->ns = ns;

  /* Set the grid code                                               */
  params->us_type |= US_MIX;
  if (params->npe == 3) params->us_type |= US_TRI;
  if (params->npe == 4) params->us_type |= US_QUAD;
  if (params->npe == 6) params->us_type |= US_HEX;

  if (verbose) {
    printf("nMaxMesh2_face_nodes  %d\n", mesh->mnpe);
    printf("nMesh2_face           %d\n", mesh->ns2);
    printf("nMesh2_face_indices   %d\n", mesh->ns);
  }

  /* The grid infromation is read from netCDF in dumpdata_read_us()  */
  /* Check the number of entries                                     */
  prm_skip_to_end_of_key(op, "Indices");
  prm_read_char(op, "1", buf);
  n = parseline(buf, fields, MAXNUMARGS);
  if (!(params->us_type & US_IJ) && n > 3)
    hd_quit("read_mesh_us: Too many entries (%d) for grid topology (indicies not required).\n", n);
  if (params->us_type & US_IJ && n <= 3)
    hd_quit("read_mesh_us: Too few entries (%d) for grid topology (indicies required).\n", n);
    
  /* Read the entries                                                */
  fprintf(op, "\nCoordinates\n");
  for (n = 1; n <= mesh->ns; n++) {
    fprintf(op, "%d %f %f\n", n, mesh->xloc[n], mesh->yloc[n]);
  }

  prm_skip_to_end_of_key(op, "Coordinates");
  if (verbose) printf("\nCoordinates\n");
  for (cc = 1; cc <= mesh->ns; cc++) {
    i = fscanf(op, "%d %lf %lf", &m, &mesh->xloc[cc], &mesh->yloc[cc]);
    if (verbose) printf("%d %f %f\n",cc, mesh->xloc[cc], mesh->yloc[cc]);
  }
  prm_skip_to_end_of_key(op, "Indices");
  if (verbose) printf("\nIndices\n");
  for (cc = 1; cc <= mesh->ns2; cc++) {
    if (params->us_type & US_IJ) {
      fscanf(op, "%d %d %d : %d %d",&i, &mesh->npe[cc], &mesh->eloc[0][cc][0],
	     &mesh->iloc[cc], &mesh->jloc[cc]);
      if (verbose) printf("%d %d %d : %d %d\n",cc, mesh->npe[cc], mesh->eloc[0][cc][0], mesh->iloc[cc], mesh->jloc[cc]);
    } else {
      fscanf(op, "%d %d %d",&i, &mesh->npe[cc], &mesh->eloc[0][cc][0]);
      if (verbose) printf("%d %d %d\n",cc, mesh->npe[cc], mesh->eloc[0][cc][0]);
    }
    mesh->eloc[1][cc][0] = mesh->eloc[0][cc][0];
    for (j = 1; j <= mesh->npe[cc]; j++) {
      fscanf(op, "%d %d %d\n",&i, &mesh->eloc[0][cc][j], &mesh->eloc[1][cc][j]);
      if (verbose) printf(" %d %d %d\n", j, mesh->eloc[0][cc][j], mesh->eloc[1][cc][j]);
    }
  }

  /* Open boundaries                                                 */
  if (rbf) {
  prm_read_int(op, "NBOUNDARIES", &mesh->nobc);
  if (mesh->nobc) {
    int uf = -1, sf = -1;
    mesh->npts = i_alloc_1d(mesh->nobc);
    i = 0;
    for (n = 0; n < mesh->nobc; n++) {
      sprintf(buf, "BOUNDARY%1d.UPOINTS", n);
      if (prm_read_int(op, buf, &mesh->npts[n])) {
	if (mesh->npts[n] > i) i = mesh->npts[n];
	uf = n;
      }
      sprintf(buf, "BOUNDARY%1d.START_LOC", n);
      if (prm_skip_to_end_of_key(op, buf)) sf = n;
    }
    if (uf >=0 && sf >=0)
      hd_quit("read_grid_us: Can't mix OBC specification UPOINTS and START_LOC; boundaries %d and %d.\n", uf, sf);
    if (i) {
      mesh->loc = i_alloc_2d(i+1, mesh->nobc);
      mesh->obc =  i_alloc_3d(2, i+1, mesh->nobc);
      printf("alloc in read_mesh_us %x\n", mesh->loc);
      for (n = 0; n < mesh->nobc; n++) {
	sprintf(buf, "BOUNDARY%1d.UPOINTS", n);
	prm_skip_to_end_of_key(op, buf);
	prm_flush_line(op);
	for (cc = 1; cc <= mesh->npts[n]; cc++) {
	  if (fscanf(op, "%d (%d %d)", &mesh->loc[n][cc], &mesh->obc[n][cc][0], &mesh->obc[n][cc][1]) != 3)
	    hd_warn("read_mesh_us: Can't read edge coordinates in boundary points list. Format '1 (2 3)'\n");
	  /* Flush the remainder of the line */
	  prm_flush_line(op);
	}
      }
    }
  } else {
    mesh->npts = NULL;
    mesh->loc = NULL;
    mesh->obc = NULL;
  }
  }

  /* Bathymetry                                                      */
  prm_read_int(op, "BATHY", &np);
  if (verbose) printf("\nBATHY\n");
  if (params->bathy != NULL) d_free_1d(params->bathy);
  params->bathy = d_alloc_1d(mesh->ns2+1);
  for (cc = 1; cc <= mesh->ns2; cc++) {
    fscanf(op, "%lf", &params->bathy[cc]);
    if (verbose) printf("%f\n", params->bathy[cc]);
  }

  /* Reconstruct the parameter coordinate arrays                     */
  if (params->x == NULL && params->y == NULL) {
    params->x = d_alloc_2d(mesh->mnpe+1, mesh->ns2+1);
    params->y = d_alloc_2d(mesh->mnpe+1, mesh->ns2+1);
    for (cc = 1; cc <= mesh->ns2; cc++) {
      params->x[cc][0] = mesh->xloc[mesh->eloc[0][cc][0]];
      params->y[cc][0] = mesh->yloc[mesh->eloc[0][cc][0]];
      for (j = 1; j <= mesh->npe[cc]; j++) {
	params->x[cc][j] = mesh->xloc[mesh->eloc[0][cc][j]];
	params->y[cc][j] = mesh->yloc[mesh->eloc[0][cc][j]];
      }
    }
  }

  neighbour_finder(mesh);

  /*-----------------------------------------------------------------*/
  /* Expand the mesh if required                                     */
  mesh_expand(params, params->bathy, params->x, params->y);

  set_params_mesh(params, mesh, params->x, params->y);

  write_mesh_us(params, params->bathy, 0, 4);

  return(1);
}

/* END read_mesh_us()                                                */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Creates a circular coastline msh and resolution hfun file         */
/* for JIGSAW                                                        */
/*                                                                   */
/* TODO: Fix projection issues                                       */
/*-------------------------------------------------------------------*/
#ifdef HAVE_JIGSAWLIB
void create_hex_radius(double crad,   /* Radius in metres     */
		       double x00,    /* Centre longitude     */
		       double y00,    /* Centre latitude      */
		       double rmin,   /* Min. resolution in m */
		       double rmax,   /* Max. resolution in m */
		       double gscale, /* Gaussian func scaler */
		       jigsaw_msh_t *J_mesh
		       )
{
  /* Constants */
  int cst_res  = 250;    /* resolution for coastline in m       */
  int nHfun    = 100;    /* (Nhfun x Nhfun) resolution matrix   */
  double deg2m = 60.0 * 1852.0;
  
  /* local variables */
  double cang = crad / deg2m; /* Radius in degress */
  int npts, n, i, j;
  int jret = 0;
  
  /* Jigsaw variables */
  jigsaw_jig_t J_jig;
  jigsaw_msh_t J_geom;
  jigsaw_msh_t J_hfun;

  /* Convenient pointers */
  jigsaw_VERT2_array_t *jpoints = &J_geom._vert2;
  jigsaw_EDGE2_array_t *jedges  = &J_geom._edge2;

  jigsaw_REALS_array_t *hfx    = &J_hfun._xgrid;
  jigsaw_REALS_array_t *hfy    = &J_hfun._ygrid;
  jigsaw_REALS_array_t *hfvals = &J_hfun._value;

  /* Jigsaw initialisation routines */
  jigsaw_init_jig_t (&J_jig);
  jigsaw_init_msh_t (&J_geom);
  jigsaw_init_msh_t (&J_hfun);

  /* Flag input mesh type */
  J_geom._flags = JIGSAW_EUCLIDEAN_MESH;

  /* Number of points needed for the coastline */
  npts = 2*PI*crad / cst_res;
  
  /* Allocate JIGSAW geom (input mesh) arrays */
  jigsaw_alloc_vert2(jpoints, npts);
  jigsaw_alloc_edge2(jedges,  npts);
  
  /* Iterate using polar coordinates */
  for (n=0; n<npts; n++) {
    double theta = (2*PI*n)/npts;
    /* Points */
    jpoints->_data[n]._ppos[0] = cang * cos(theta) + x00;
    jpoints->_data[n]._ppos[1] = cang * sin(theta) + y00;
    /* Connecting edges */
    jedges->_data[n]._node[0] = n;
    if (n<npts-1)
      jedges->_data[n]._node[1] = n+1;
    else
      jedges->_data[n]._node[1] = 0; // close polygon
  }
  /*
   * Create the gaussian resolution matrix
   *   Uniformly spaced grid
   */
  jigsaw_alloc_reals(hfx, nHfun+1);
  jigsaw_alloc_reals(hfy, nHfun+1);
  jigsaw_alloc_reals(hfvals, (nHfun+1)*(nHfun+1));

  /* Fill in the x & y grids */
  for (n=0; n<=nHfun; n++) {
    hfx->_data[n] = (x00 - cang) + 2*n*cang/nHfun;
    hfy->_data[n] = (y00 - cang) + 2*n*cang/nHfun;
  }

  /* Fill in the gaussian values */
  /* Note: this needs to be in COLUMN major format but it doesn't
           really matter here due to the symmetry (although if we do
           the projection right, it won't be)                          */
  n = 0;
  for (i=0; i<=nHfun; i++)
    for (j=0; j<=nHfun; j++) {
      double gval = wgt_gaussian_2d(hfx->_data[i] - x00, /* x grid value */
				    hfy->_data[j] - y00, /* y grid value */
				    gscale);       /* scale        */
      hfvals->_data[n++] = ((rmax * (1.-gval)) + rmin) / deg2m;
    }
  /* Set up some flags/parameters */
  J_hfun._flags = JIGSAW_EUCLIDEAN_GRID;
  J_jig._hfun_scal = JIGSAW_HFUN_ABSOLUTE ;
  J_jig._hfun_hmin = rmin / deg2m;
  J_jig._hfun_hmax = rmax / deg2m;

  // Call Jigsaw
  J_jig._verbosity = 0;
  jret = jigsaw_make_mesh(&J_jig, &J_geom, &J_hfun, J_mesh);
  if (jret) hd_quit("Error calling JIGSAW\n");

  /* Cleanup */
  jigsaw_free_msh_t(&J_geom);
  jigsaw_free_msh_t(&J_hfun);
  
}

/* END create_hex_radius()                                           */
/*-------------------------------------------------------------------*/

#define I_NC    1
#define I_BTY   2
#define I_USR   4
#define I_MESH  8
#define I_GRID  16

/*-------------------------------------------------------------------*/
/* Reads a bathymetry file and creates a hfun file                   */
/*-------------------------------------------------------------------*/
void hfun_from_bathy(parameters_t *params, char *fname, coamsh_t *cm, jigsaw_msh_t *J_hfun)
{
  FILE *fp = params->prmfd, *ef, *bf, *hf;
  char buf[MAXSTRLEN], key[MAXSTRLEN];
  char i_rule[MAXSTRLEN];
  char vname[MAXSTRLEN];
  GRID_SPECS *gs = NULL;
  int nbath;
  int **nei;
  double *x, *y, *b, *r;
  double *hfun, xloc, yloc;
  int n, m, i, j, nhfun, intype, bmf = 1;
  int imeth = 0;
  int verbose = 0;
  int filef = 1;
  int gridf = 0;
  int sn, smooth = 0;
  int nce1, nce2;
  int fid;
  int ncerr;
  int dims;
  double stime;
  double bmin, bmax, hmin, hmax, s;
  double deg2m = 60.0 * 1852.0;
  double expf = 0.0;
  delaunay *d;
  jigsaw_REALS_array_t *hfvals = &J_hfun->_value;
  jigsaw_REALS_array_t *hfx    = &J_hfun->_xgrid;
  jigsaw_REALS_array_t *hfy    = &J_hfun->_ygrid;
  int perimf = 1; /* Set hfun perimeter to maximum resolution        */
  int npass = 0;
  double *hmx, *hmn, *bmx, *bmn, *exf;
  int nord;

  /*-----------------------------------------------------------------*/
  /* Get the interpolation method                                    */
  if(endswith(fname,".nc")) {
    size_t ncx, ncy;
    /* Open the dump file for reading                                */
    if ((ncerr = nc_open(fname, NC_NOWRITE, &fid)) != NC_NOERR) {
      printf("Can't find input bathymetry file %s\n", fname);
      hd_quit((char *)nc_strerror(ncerr));
    }

    /* Get dimensions                                                */
    sprintf(vname, "%c", '\0');
    prm_read_char(fp, "HFUN_VAR", vname);
    stime = 0.0;
    if (prm_read_char(fp, "HFUN_TIME", buf)) {
      char *fields[MAXSTRLEN * MAXNUMARGS];
      i = parseline(buf, fields, MAXNUMARGS);
      stime = atoi(fields[0]);
    }
    i = 0;
    while (bathy_dims[i][0] != NULL) {
      if (nc_inq_dimlen(fid, ncw_dim_id(fid, bathy_dims[i][2]), &ncy) == 0 &&
	  nc_inq_dimlen(fid, ncw_dim_id(fid, bathy_dims[i][1]), &ncx) == 0) {
	intype = i;
	break;
      }
      i++;
    }
    if (intype < 0)      
      hd_quit("hfun_from_bathy: Can't find batyhmetry attributes in file %s.\n", fname);

    if (strcmp(bathy_dims[intype][5], "nc_bathy") == 0)
      imeth = (I_NC|I_GRID);
    else
      imeth = (I_NC|I_MESH);
  } else if (endswith(fname,".bty")) {
    imeth = (I_BTY|I_MESH);
  } else {
    imeth = (I_USR|I_MESH);
  }

  /*-----------------------------------------------------------------*/
  /* Define and read parameters                                      */
  /* Note: bmin and bmax are negative for wet cells.                 */
  if (prm_read_int(fp, "NHFUN", &npass)) {
    bmin = hmax = -HUGE;
    bmax = hmin = HUGE;
    bmn = d_alloc_1d(npass);
    bmx = d_alloc_1d(npass);
    hmn = d_alloc_1d(npass);
    hmx = d_alloc_1d(npass);
    exf = d_alloc_1d(npass);
    for (n = 0; n < npass; n++) {
      sprintf(buf, "HMIN%d", n);
      prm_read_double(fp, buf, &hmn[n]);
      hmn[n] /= deg2m;
      sprintf(buf, "HMAX%d", n);
      prm_read_double(fp, buf, &hmx[n]);
      hmx[n] /= deg2m;
      sprintf(buf, "BMIN%d", n);
      prm_read_double(fp, buf, &bmn[n]);
      sprintf(buf, "BMAX%d", n);
      prm_read_double(fp, buf, &bmx[n]);
      sprintf(buf, "TYPE%d", n);
      prm_read_double(fp, buf, &exf[n]);
      bmin = max(bmin, bmn[n]);
      bmax = min(bmax, bmx[n]);
      hmax = max(hmax, hmx[n]);
      hmin = min(hmin, hmn[n]);
    }
    cm->hfun_min = hmin * deg2m;
    cm->hfun_max = hmax * deg2m;
  } else {
    hmin = cm->hfun_min / deg2m;
    hmax = cm->hfun_max / deg2m;
    bmin = bmax = 0.0;
    prm_read_double(fp, "HFUN_BMIN", &bmin);
    prm_read_double(fp, "HFUN_TYPE", &expf);
    if (prm_read_double(fp, "HFUN_BMAX", &bmax)) bmf = 1;
    if (strlen(vname) == 0 && bmin > 0.0 && bmax > 0.0 && bmin < bmax) {
      bmin *= -1.0;
      bmax *= -1.0;
    }
  }
  prm_read_int(fp, "HFUN_SMOOTH", &smooth);

  /* Get the interpolation rule                                      */
  strcpy(i_rule, "linear");
  prm_read_char(params->prmfd, "HFUN_INTERP_RULE", i_rule);
 
  /* Set on a regular grid if required                               */
  if (prm_read_char(params->prmfd, "HFUN_GRID", buf)) {
    if (imeth & (I_NC|I_BTY|I_USR) && is_true(buf)) {
      if (imeth & I_MESH) imeth &= ~I_MESH;
      imeth |= I_GRID;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Set up the grid to interpolate onto.                            */
  if (imeth & I_GRID) {
    /* Make a regular grid in the bounding box at the maximum        */
    /* resolution.                                                   */
    double res = 0.25 * (hmin + hmax);
    /*double res = 0.05;*/
    nce1 = (int)(fabs(cm->belon - cm->bslon) / res);
    nce2 = (int)(fabs(cm->belat - cm->bslat) / res);

    nhfun = nce1 * nce2;
    jigsaw_alloc_reals(hfx, nce1);
    jigsaw_alloc_reals(hfy, nce2);

    /* Fill in the x & y grids                                       */
    s = (cm->belon - cm->bslon) / (double)(nce1 - 1);
    for (i = 0; i < nce1; i++)
      hfx->_data[i] = s * (double)i + cm->bslon;
    s = (cm->belat - cm->bslat) / (double)(nce2 - 1);
    for (j = 0; j < nce2; j++)
      hfy->_data[j] = s * (double)j + cm->bslat;
  } else {
    /* Get a triangulation based on the mesh perimeter               */
    /*xy_to_d(d, cm->np, cm->x, cm->y);*/
    if (imeth & I_BTY)
      hd_quit("Can't create a mesh with .bty input. Use HFUN_GRID.\n");
    if (imeth & I_USR)
      hd_quit("Can't create a mesh with user input. Use HFUN_GRID.\n");

    create_bounded_mesh(cm->np, cm->x, cm->y, hmin, hmax, J_hfun);
    nhfun = J_hfun->_vert2._size;
  }
  jigsaw_alloc_reals(hfvals, nhfun);

  /*-----------------------------------------------------------------*/
  /* Make a gridded bathymetry                                       */
  if(imeth & I_NC) {
    /* Interpolation from structured input using ts_read().          */
    size_t start[4];
    size_t count[4];
    timeseries_t *ts = NULL;
    int idb;

    /* Initialize the bathymetry file                                */
    ts = (timeseries_t *)malloc(sizeof(timeseries_t));
    if (ts == NULL)
      hd_quit("hfun_from_bathy: No memory available.\n");
    memset(ts, 0, sizeof(timeseries_t));
    
    /* Read the bathymetry file                                      */
    ts_read(fname, ts);
    if (strlen(vname)) {
      strcpy(key, vname);
      nc_inq_varndims(fid, ncw_var_id(fid, vname), &dims);
    } else
      strcpy(key, fv_get_varname(fname, bathy_dims[intype][0], buf));

    if ((idb = ts_get_index(ts, key)) == -1)
      hd_quit("hfun_from_bathy: Can't find variable %s in file %s\n", 
	      key, fname);

    b = d_alloc_1d(nhfun);

    if (imeth & I_GRID) {
      /* Interpolate bathymetry onto the grid                        */
      n = 0;
      for (i = 0; i < nce1; i++) {
	for (j = 0; j < nce2; j++) {
	  xloc = hfx->_data[i];
	  yloc = hfy->_data[j];
	  if (dims == 4)
	    b[n] = ts_eval_xyz(ts, idb, stime, xloc, yloc, 0.0);
	  else
	    b[n] = ts_eval_xy(ts, idb, stime, xloc, yloc);
	  if (!bmf && b[n] < 0.0 && b[n] < bmax) bmax = b[n];
	  if (!bmf && b[n] > 0.0 && b[n] > bmax) bmax = b[n];
	  if (verbose) printf("%d %f %f : %f %f\n",n, xloc, yloc, b[n], bmax);
	  n++;
	}
      }
      J_hfun->_flags = JIGSAW_EUCLIDEAN_GRID;
    } else {
      /* Interpolate the bathymetry values onto the triangulation and  */
      /* find the maximum depth if required.                           */
      for (n = 0; n < nhfun; n++) {
	xloc = J_hfun->_vert2._data[n]._ppos[0];
	yloc = J_hfun->_vert2._data[n]._ppos[1];
	b[n] = ts_eval_xy(ts, idb, 0.0, xloc, yloc);
	if (!bmf && b[n] < bmax) bmax = b[n];
	if (verbose) printf("%d %f %f : %f %f\n",n, xloc, yloc, b[n], bmax);
      }
      J_hfun->_flags = JIGSAW_EUCLIDEAN_MESH;
    }
    ts_free((timeseries_t*)ts);
    free(ts);
  } else if(imeth & I_BTY) {

    /* Read the bathymetry values                                    */
    if ((bf = fopen(fname, "r")) == NULL)
      hd_quit("hfun_from_bathy: Can't open bathymetry file %s.\n", fname);
    nbath = 0;
    while (fgets(buf, MAXSTRLEN, bf) != NULL) {
      nbath++;
    }
    rewind(bf);
    x = d_alloc_1d(nbath);
    y = d_alloc_1d(nbath);
    r = d_alloc_1d(nbath);
    n = 0;
    while (fgets(buf, MAXSTRLEN, bf) != NULL) {
      char *fields[MAXSTRLEN * MAXNUMARGS];
      i = parseline(buf, fields, MAXNUMARGS);
      x[n] = atof(fields[0]);
      y[n] = atof(fields[1]);
      r[n] = atof(fields[2]);
      if (r[n] > 0) r[n] *= -1.0;
      if (!bmf && r[n] < bmax) bmax = r[n];
      if (verbose) printf("%d %f %f : %f %f\n",n, x[n], y[n], r[n], bmax);
      n++;
    }
    fclose(bf);

    /* Set up the interpolation triangulation                        */
    gs = grid_interp_init(x, y, r, nbath, i_rule);

    /* Interpolte bathymetry onto the mesh                           */
    b = d_alloc_1d(nhfun+1);
    if (imeth & I_GRID) {
      /* Interpolate bathymetry onto the grid                        */
      n = 0;
      for (i = 0; i < nce1; i++) {
	for (j = 0; j < nce2; j++) {
	  xloc = hfx->_data[i];
	  yloc = hfy->_data[j];
	  b[n] = grid_interp_on_point(gs, xloc, yloc);
	  if (!bmf && b[n] < bmax) bmax = b[n];
	  n++;
	}
      }
      grid_specs_destroy(gs);
      J_hfun->_flags = JIGSAW_EUCLIDEAN_GRID;
    } else {
      for (n = 0; n < nhfun; n++) {
	xloc = J_hfun->_vert2._data[n]._ppos[0];
	yloc = J_hfun->_vert2._data[n]._ppos[1];
	b[n] = grid_interp_on_point(gs, xloc, yloc);
	b[n] = min(max(bmax, b[n]), bmin);
      }
      J_hfun->_flags = JIGSAW_EUCLIDEAN_MESH;
      grid_specs_destroy(gs);
      d_free_1d(x);
      d_free_1d(y);
    }
    d_free_1d(r);
  } else {
    int constf = 0;
    double d1, d2, d3;

    prm_skip_to_end_of_key(fp, "HFUN_BATHY_FILE");
    nbath = atoi(fname);    
    if (nbath == 0 || fscanf(fp, "%lf %lf %lf", &d1, &d2, &d3) != 3) {
      constf = 1;
      d1 = atof(fname) / deg2m;
      for (n = 0; n < nhfun; n++) {
	hfvals->_data[n] = d1;
      }
      J_hfun->_flags = JIGSAW_EUCLIDEAN_GRID;
    } else {
      prm_skip_to_end_of_key(fp, "HFUN_BATHY_FILE");
      x = d_alloc_1d(nbath);
      y = d_alloc_1d(nbath);
      b = d_alloc_1d(nbath);
      for (i = 0; i < nbath; i++) {
	if ((fscanf(fp, "%lf %lf %lf", x[i], y[i], b[i])) != 3)
	  hd_quit("hfun_from_bathy: Incorrect resolution specification.\n");
      }

      /* Interpolate from a triangulation                              */
      gs = grid_interp_init(x, y, b, nbath, i_rule);

      for (n = 0; n < nhfun; n++) {
	xloc = J_hfun->_vert2._data[n]._ppos[0];
	yloc = J_hfun->_vert2._data[n]._ppos[1];
	hfvals->_data[n] = grid_interp_on_point(gs, xloc, yloc);
	if (verbose) printf("%d %f : %f %f\n",n, hfvals->_data[n], xloc, yloc);
      }
      J_hfun->_flags = JIGSAW_EUCLIDEAN_GRID;
      grid_specs_destroy(gs);
      d_free_1d(x);
      d_free_1d(y);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Over-ride bathymetry values if required                         */
  if (prm_read_int(fp, "HFUN_OVERRIDE", &nord)) {
    double *exlon, *exlat, *exrad, *exbth;
    exlon = d_alloc_1d(nord);
    exlat = d_alloc_1d(nord);
    exrad = d_alloc_1d(nord);
    exbth = d_alloc_1d(nord);
    for (n = 0; n < nord; n++) {
      if (fscanf(fp, "%lf %lf %lf %lf", &exlon[n], &exlat[n], &exrad[n], &exbth[n]) != 4)
	hd_quit("hfun_from_bathy: Format for HFUN_OVERRIDE is 'lon lat radius value'.\n");
    }
    if (imeth & I_GRID) {
      n = 0;
      for (i = 0; i < nce1; i++) {
	for (j = 0; j < nce2; j++) {
	  for (m = 0; m < nord; m++) {
	    xloc = exlon[m] - hfx->_data[i];
	    yloc = exlat[m] - hfy->_data[j];
	    s = sqrt(xloc * xloc + yloc * yloc) * deg2m;
	    if (s < exrad[m]) b[n] = (b[n] - exbth[m]) * s / exrad[m] + exbth[m];
	  }
	  n++;
	}
      }
    } else {
      for (n = 0; n < nhfun; n++) {
	for (m = 0; m < nord; m++) {
	  xloc = exlon[m] - J_hfun->_vert2._data[n]._ppos[0];
	  yloc = exlat[m] - J_hfun->_vert2._data[n]._ppos[1];
	  s = sqrt(xloc * xloc + yloc * yloc) * deg2m;
	  if (s < exrad[m]) b[n] = (b[n] - exbth[m]) * s / exrad[m] + exbth[m];
	}
      }
    }
    d_free_1d(exlon);
    d_free_1d(exlat);
    d_free_1d(exrad);
    d_free_1d(exbth);
  }

  /*-----------------------------------------------------------------*/
  /* Convert to the hfun function                                    */
  if (imeth & (I_NC|I_BTY)) {
    int dof = 0;

    for (n = 0; n < nhfun; n++) {
      b[n] = min(max(bmax, b[n]), bmin);
      if (npass) {
	for (m = 0; m < npass; m++) {
	  if (b[n] >= bmx[m] && b[n] <= bmn[m])
	    hfvals->_data[n] = bathyset(b[n], bmn[m], bmx[m], 
					hmn[m], hmx[m], exf[m]);
	}
      } else 
	hfvals->_data[n] = bathyset(b[n], bmin, bmax, hmin, hmax, expf);

      /*printf("%d %f %f\n",n, hfvals->_data[n], b[n]);*/
      if (dof) {
      if (expf > 0.0) {
	/* Exponential                                               */
	hfvals->_data[n] = (hmin - hmax) * exp(b[n] / expf) + 
	  (hmax - hmin * exp(-fabs(bmax) / expf));
      } else if (expf == 0) {
	/* Linear                                                    */
	hfvals->_data[n] = ((b[n] - bmax) * s + hmax);
      } else {
	double bn = fabs(bmin);
	double bx = fabs(bmax);
	double ba = fabs(b[n]);
	double dd = bx - bn;
	if (ba < bn)
	  hfvals->_data[n] = hmin;
	else if (ba > bx)
	  hfvals->_data[n] = hmax;
	else
	  hfvals->_data[n] = 0.5 * ((hmin - hmax) * cos(ba * PI / dd - bn * PI / dd) + (hmin + hmax));
      }
      if (verbose) printf("%d %f %f\n",n, hfvals->_data[n], b[n]);
    }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Set the perimeter to maximum resolution                         */
  if (perimf && imeth & I_MESH) {
    for (n = 0; n < J_hfun->_edge2._size; n++) {
      i = J_hfun->_edge2._data[n]._node[0];
      hfvals->_data[i] = hmin;
      i = J_hfun->_edge2._data[n]._node[1];
      hfvals->_data[i] = hmin;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Smooth if required                                              */
  if (smooth) {
    if (imeth & I_MESH) {
      int nv2 = J_hfun->_vert2._size;
      int *nv, **eiv;
      /*neighbour_finder_j(J_hfun, &nei);*/
      nv = i_alloc_1d(nv2);
      memset(nv, 0, nv2 * sizeof(int));
      for (n = 0; n < J_hfun->_tria3._size; n++) {
	for (j = 0; j < 3; j++) {
	  i = J_hfun->_tria3._data[n]._node[j];
	  nv[i]++;
	}
      }
      m = 0;
      for (n = 0; n < nv2; n++)
	if (nv[n] > m) m = nv[n];
      eiv = i_alloc_2d(nv2, m);
      memset(nv, 0, nv2 * sizeof(int));
      for (n = 0; n < J_hfun->_tria3._size; n++) {
	for (j = 0; j < 3; j++) {
	  i = J_hfun->_tria3._data[n]._node[j];
	  eiv[nv[i]][i] = n;
	  nv[i]++;
	}
      }
      r = d_alloc_1d(nhfun);
      for (sn = 0; sn < smooth; sn++) {
	/* Loop over all vertices                                    */
	for (m = 0; m < nv2; m++) {
	  r[m] = hfvals->_data[m];
	  s = 1.0;
	  /* Loop over all neighbouring triangles                    */
	  for (j = 0; j < nv[m]; j++) {
	    n = eiv[j][m];
	    for (i = 0; i < 3; i++) {
	      int nn = J_hfun->_tria3._data[n]._node[i];
	      if (nn != m && nn < nhfun) {
		r[m] += hfvals->_data[nn];
	      s += 1.0;
	      }
	    }
	  }
	  r[m] /= s;
	}
	memcpy(hfvals->_data, r, nhfun * sizeof(double));
      }
      d_free_1d(r);
      i_free_1d(nv);
      i_free_2d(eiv);
    }
    if (imeth & I_GRID) {
      int ii, jj, **ij2n;

      /* Get a mapping from ij to hfvals indices                     */
      ij2n = i_alloc_2d(nce1, nce2);
      n = 0;
      for (i = 0; i < nce1; i++) {
	for (j = 0; j < nce2; j++) {
	  ij2n[j][i] = n;
	  n++;
	}
      }

      /* Smooth                                                      */
      for (sn = 0; sn < smooth; sn++) {
	/* Loop over all vertices                                    */
	r = d_alloc_1d(nhfun);
	for (i = 0; i < nce1; i++) {
	  int i1 = max(i-1, 0), i2 = min(i+1, nce1-1);
	  for (j = 0; j < nce2; j++) {
	    int j1 = max(j-1, 0), j2 = min(j+1, nce2-1);
	    s = 0.0;
	    n = ij2n[j][i];
	    r[n] = 0.0;
	    for (ii = i1; ii <= i2; ii++) {
	      for (jj = j1; jj <= j2; jj++) {
		ii = max(0, min(nce1-1, ii));
		jj = max(0, min(nce2-1, jj));
		m = ij2n[jj][ii];
		r[n] += hfvals->_data[m];
		s += 1.0;
	      }
	    }
	    r[n] /= s;
	  }
	}
	memcpy(hfvals->_data, r, nhfun * sizeof(double));
      }
      d_free_1d(r);
      i_free_2d(ij2n);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_h.txt", key);
    if ((ef = fopen(buf, "w")) != NULL) {
      if (imeth & I_MESH) { 
	for (n = 0; n < nhfun; n++) {
	  if (imeth & I_USR)
	    fprintf(ef, "%f %f %f\n",J_hfun->_vert2._data[n]._ppos[0], 
		    J_hfun->_vert2._data[n]._ppos[1], 
		    hfvals->_data[n]);
	  else
	    fprintf(ef, "%f %f %f %f\n",J_hfun->_vert2._data[n]._ppos[0], 
		    J_hfun->_vert2._data[n]._ppos[1], 
		    hfvals->_data[n], b[n]);
	}
      } else {
	n = 0;
	for (i = 0; i < nce1; i++) {
	  for (j = 0; j < nce2; j++) {
	    if (imeth & I_USR)
	      fprintf(ef, "%f %f %f\n",hfx->_data[i], hfy->_data[j],
		      hfvals->_data[n]);
	    else
	      fprintf(ef, "%f %f %f %f\n",hfx->_data[i], hfy->_data[j],
		      hfvals->_data[n], b[n]);
	    n++;
	  }
	}
      }
      fclose(ef);
    }
    sprintf(buf,"%s_hfun.msh", key);
    if ((hf = fopen(buf, "w")) != NULL) {
      fprintf(hf, "# .msh geometry file\n");
      if (imeth & I_MESH) {
	/* EUCLIDEAN-MESH */
	fprintf(hf, "MSHID=2;EUCLIDEAN-MESH\n");
      } else {
	/* EUCLIDEAN-GRID */
	fprintf(hf, "MSHID=2;EUCLIDEAN-GRID\n");
      }
      fprintf(hf, "NDIMS=2\n");
      if (imeth & I_MESH) {
	fprintf(hf, "POINT=%d\n",nhfun);
	for (n = 0; n < nhfun; n++)
	  fprintf(hf, "%f;%f;0\n",J_hfun->_vert2._data[n]._ppos[0],
		  J_hfun->_vert2._data[n]._ppos[1]);
      } else {
	fprintf(hf, "coord=1;%d\n",nce1);
	for (i = 0; i < nce1; i++)
	  fprintf(hf, "%f\n",hfx->_data[i]);
	fprintf(hf, "coord=2;%d\n",nce2);
	for (j = 0; j < nce2; j++)
	  fprintf(hf, "%f\n",hfy->_data[j]);
      }
      if (imeth & I_MESH) {
	fprintf(hf, "value=%d;1\n",nhfun);
	for (n = 0; n < nhfun; n++)
	  fprintf(hf, "%f\n",hfvals->_data[n]);
      } else {
	fprintf(hf, "value=%d;1\n",nce1*nce2);
	n = 0;
	for (i = 0; i < nce1; i++)
	  for (j = 0; j < nce2; j++)
	    fprintf(hf, "%f\n",hfvals->_data[n++]);
      }
      fclose(hf);
    }
  }
  if (b) d_free_1d(b); 
  if (DEBUG("init_m"))
    dlog("init_m", "Bathymetric weighting function computed OK\n");
}

/* END hfun_from_bathy()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Computes the weighting function from bathymetry                   */
/*-------------------------------------------------------------------*/
double bathyset(double b, 
		double bmin,
		double bmax,
		double hmin,
		double hmax,
		double expf
		)
{
  double ret;
  double s = (bmin == bmax) ? 0.0 : (hmin - hmax) / (bmin - bmax);

  if (expf > 0.0) {
    /* Exponential                                                   */
    ret = (hmin - hmax) * exp(b / expf) + 
      (hmax - hmin * exp(-fabs(bmax) / expf));
  } else if (expf == 0) {
    /* Linear                                                        */
    ret = ((b - bmax) * s + hmax);
  } else {
    double bn = fabs(bmin);
    double bx = fabs(bmax);
    double ba = fabs(b);
    double dd = bx - bn;
    if (ba < bn)
      ret = hmin;
    else if (ba > bx)
      ret = hmax;
    else
      ret = 0.5 * ((hmin - hmax) * cos(ba * PI / dd - bn * PI / dd) + (hmin + hmax));
  }
  return(ret);
}

/* END bathyset()                                                    */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Reads a coastline file and creates a hfun file                    */
/*-------------------------------------------------------------------*/
void hfun_from_coast(parameters_t *params, coamsh_t *cm, jigsaw_msh_t *J_hfun)
{
  FILE *fp = params->prmfd, *ef, *bf, *hf;
  char buf[MAXSTRLEN], key[MAXSTRLEN];
  int nbath;
  double *x, *y, *b, *r;
  double *hfun, xloc, yloc;
  int n, m, i, j, nhfun, intype, bmf = 0;
  int imeth = 0;
  int verbose = 0;
  int filef = 1;
  int sn, smooth = 0;
  int nce1, nce2;
  int nexp;
  double *exlat, *exlon, *exrad;
  double bmin, bmax, hmin, hmax, s;
  double deg2m = 60.0 * 1852.0;
  double expf = 0.0;
  jigsaw_REALS_array_t *hfvals = &J_hfun->_value;
  jigsaw_REALS_array_t *hfx    = &J_hfun->_xgrid;
  jigsaw_REALS_array_t *hfy    = &J_hfun->_ygrid;
  int perimf = 0; /* Set hfun perimeter to maximum resolution        */
  msh_t *msh = cm->msh;

  /*-----------------------------------------------------------------*/
  /* Define and read parameters                                      */
  /* Note: bmin and bmax are negative for wet cells.                 */
  hmin = cm->hfun_min / deg2m;
  hmax = cm->hfun_max / deg2m;
  bmin = bmax = 0.0;
  if (prm_read_double(fp, "HFUN_BMIN", &bmin) && 
      prm_read_double(fp, "HFUN_BMAX", &bmax)) {
    bmf = 1;
    bmin /= deg2m;
    bmax /= deg2m;
  }
  prm_read_int(fp, "HFUN_SMOOTH", &smooth);
  prm_read_double(fp, "HFUN_TYPE", &expf);
  nexp = 0;
  if (prm_read_int(fp, "HFUN_EXCLUDE", &nexp)) {
    exlon = d_alloc_1d(nexp);
    exlat = d_alloc_1d(nexp);
    exrad = d_alloc_1d(nexp);
    for (n = 0; n < nexp; n++) {
      if (fscanf(fp, "%lf %lf %lf", &exlon[n], &exlat[n], &exrad[n]) != 3)
	hd_quit("hfun_from_coast: Format for HFUN_EXCLUDE is 'lon lat radius'.\n");
    }
    for (i = 0; i < msh->npoint; i++) {
      if (msh->flag[i] & (S_OBC|S_LINK)) continue;
      for (n = 0; n < nexp; n++) {
	xloc = exlon[n] - msh->coords[0][i];
	yloc = exlat[n] - msh->coords[1][i];
	if (sqrt(xloc * xloc + yloc * yloc) * deg2m < exrad[n])
	  msh->flag[i] |= S_LINK;
      }
    }
  }

  /* Set on a regular grid if required                               */
  imeth = I_MESH;
  if (prm_read_char(params->prmfd, "HFUN_GRID", buf)) {
    if (is_true(buf)) {
      if (imeth & I_MESH) imeth &= ~I_MESH;
      imeth |= I_GRID;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Set up the grid to interpolate onto.                            */
  if (imeth & I_GRID) {
    /* Make a regular grid in the bounding box at the maximum        */
    /* resolution.                                                   */
    double res = 0.25 * (hmin + hmax);
    /*double res = 0.05;*/
    nce1 = (int)(fabs(cm->belon - cm->bslon) / res);
    nce2 = (int)(fabs(cm->belat - cm->bslat) / res);

    nhfun = nce1 * nce2;
    jigsaw_alloc_reals(hfx, nce1);
    jigsaw_alloc_reals(hfy, nce2);

    /* Fill in the x & y grids                                       */
    s = (cm->belon - cm->bslon) / (double)(nce1 - 1);
    for (i = 0; i < nce1; i++)
      hfx->_data[i] = s * (double)i + cm->bslon;
    s = (cm->belat - cm->bslat) / (double)(nce2 - 1);
    for (j = 0; j < nce2; j++)
      hfy->_data[j] = s * (double)j + cm->bslat;

  } else {
    /* Get a triangulation based on the mesh perimeter               */
    /*xy_to_d(d, cm->np, cm->x, cm->y);*/
    create_bounded_mesh(cm->np, cm->x, cm->y, hmin, hmax, J_hfun);
    nhfun = J_hfun->_vert2._size;
  }
  jigsaw_alloc_reals(hfvals, nhfun);

  /*-----------------------------------------------------------------*/
  /* Make a gridded distance to coast array                          */
  b = d_alloc_1d(nhfun);
  if (imeth & I_GRID) {
    /* Get the minimum distance to the coast                         */
    n = 0;
    for (i = 0; i < nce1; i++) {
      xloc = hfx->_data[i];
      for (j = 0; j < nce2; j++) {
	yloc = hfy->_data[j];
	b[n] = coast_dist(msh, xloc, yloc);
	if (verbose) printf("%d %f %f : %f\n",n, xloc, yloc, b[n]);
	n++;
      }
    }
    J_hfun->_flags = JIGSAW_EUCLIDEAN_GRID;
  } else {
    for (n = 0; n < nhfun; n++) {
      xloc = J_hfun->_vert2._data[n]._ppos[0];
      yloc = J_hfun->_vert2._data[n]._ppos[1];
      b[n] = coast_dist(msh, xloc, yloc);
      if (verbose) printf("%d %f %f : %f\n",n, xloc, yloc, b[n]);
    }
    J_hfun->_flags = JIGSAW_EUCLIDEAN_MESH;
  }

  /* Get the minimum and maximum distances                           */
  if (!bmf) {
    bmin = HUGE;
    bmax = 0.0;
    for (n = 0; n < nhfun; n++) {
      bmin = min(bmin, b[n]);
      bmax = max(bmax, b[n]);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Convert to the hfun function                                    */
  s = (bmin == bmax) ? 0.0 : (hmin - hmax) / (bmin - bmax);
  for (n = 0; n < nhfun; n++) {
    b[n] = max(min(bmax, b[n]), bmin);
    if (expf > 0.0) {
      /* Exponential                                               */
      hfvals->_data[n] = (hmin - hmax) * exp(b[n] / expf) + 
	(hmax - hmin * exp(-fabs(bmax) / expf));
    } else if (expf == 0) {
      /* Linear                                                    */
      hfvals->_data[n] = ((b[n] - bmax) * s + hmax);
    } else {
      double bn = fabs(bmin);
      double bx = fabs(bmax);
      double ba = fabs(b[n]);
      double dd = bx - bn;
      if (ba < bn)
	hfvals->_data[n] = hmin;
      else if (ba > bx)
	hfvals->_data[n] = hmax;
      else
	hfvals->_data[n] = 0.5 * ((hmin - hmax) * cos(ba * PI / dd - s) + (hmin + hmax));
    }
    if (verbose) printf("%d %f %f\n",n, hfvals->_data[n], b[n]);
  }

  /*-----------------------------------------------------------------*/
  /* Set the perimeter to maximum resolution                         */
  if (perimf && imeth & I_MESH) {
    for (n = 0; n < J_hfun->_edge2._size; n++) {
      i = J_hfun->_edge2._data[n]._node[0];
      hfvals->_data[i] = hmin;
      i = J_hfun->_edge2._data[n]._node[1];
      hfvals->_data[i] = hmin;
    }
  }

  /*-----------------------------------------------------------------*/
  /* Smooth if required                                              */
  if (smooth) {
    if (imeth & I_MESH) {
      int nv2 = J_hfun->_vert2._size;
      int *nv, **eiv;
      /*neighbour_finder_j(J_hfun, &nei);*/
      nv = i_alloc_1d(nv2);
      memset(nv, 0, nv2 * sizeof(int));
      for (n = 0; n < J_hfun->_tria3._size; n++) {
	for (j = 0; j < 3; j++) {
	  i = J_hfun->_tria3._data[n]._node[j];
	  nv[i]++;
	}
      }
      m = 0;
      for (n = 0; n < nv2; n++)
	if (nv[n] > m) m = nv[n];
      eiv = i_alloc_2d(nv2, m);
      memset(nv, 0, nv2 * sizeof(int));
      for (n = 0; n < J_hfun->_tria3._size; n++) {
	for (j = 0; j < 3; j++) {
	  i = J_hfun->_tria3._data[n]._node[j];
	  eiv[nv[i]][i] = n;
	  nv[i]++;
	}
      }
      r = d_alloc_1d(nhfun);
      for (sn = 0; sn < smooth; sn++) {
	/* Loop over all vertices                                      */
	for (m = 0; m < nv2; m++) {
	  r[m] = hfvals->_data[m];
	  s = 1.0;
	  /* Loop over all neighbouring triangles                       */
	  for (j = 0; j < nv[m]; j++) {
	    n = eiv[j][m];
	    for (i = 0; i < 3; i++) {
	      int nn = J_hfun->_tria3._data[n]._node[i];
	      if (nn != m && nn < nhfun) {
		r[m] += hfvals->_data[nn];
	      s += 1.0;
	      }
	    }
	  }
	  r[m] /= s;
	}
	memcpy(hfvals->_data, r, nhfun * sizeof(double));
      }
      d_free_1d(r);
      i_free_1d(nv);
      i_free_2d(eiv);
    }
    if (imeth & I_GRID) {
      int ii, jj, **ij2n;

      /* Get a mapping from ij to hfvals indices                     */
      ij2n = i_alloc_2d(nce1, nce2);
      n = 0;
      for (i = 0; i < nce1; i++) {
	for (j = 0; j < nce2; j++) {
	  ij2n[j][i] = n;
	  n++;
	}
      }

      /* Smooth                                                      */
      r = d_alloc_1d(nhfun);
      for (sn = 0; sn < smooth; sn++) {
	double d1;
	/* Loop over all vertices                                      */
	/*memcpy(r, hfvals->_data, nhfun * sizeof(double));*/
	for (i = 0; i < nce1; i++) {
	  int i1 = max(i-1, 0), i2 = min(i+1, nce1-1);
	  for (j = 0; j < nce2; j++) {
	    int j1 = max(j-1, 0), j2 = min(j+1, nce2-1);
	    s = 0.0;
	    n = ij2n[j][i];
	    r[n] = 0.0;
	    for (ii = i1; ii <= i2; ii++) {
	      for (jj = j1; jj <= j2; jj++) {
		ii = max(0, min(nce1-1, ii));
		jj = max(0, min(nce2-1, jj));
		m = ij2n[jj][ii];
		r[n] += hfvals->_data[m];
		/*
		r[n] = 1.0;
		d1 = hfvals->_data[m];
		r[n] = r[n] + d1;
		*/
		s += 1.0;
	      }
	    }
	    if (s) r[n] /= s;
	    if(s==0)printf("zero\n");
	  }
	}
	/*memcpy(hfvals->_data, r, nhfun * sizeof(double));*/
      }

      d_free_1d(r);
      i_free_2d(ij2n);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_h.txt", key);
    if ((ef = fopen(buf, "w")) != NULL) {
      fprintf(ef, "# Minimum distance to coast = %f m\n", bmin * deg2m);
      fprintf(ef, "# Maximum distance to coast = %f m\n", bmax * deg2m);
      if (imeth & I_MESH) { 
	for (n = 0; n < nhfun; n++) {
	  fprintf(ef, "%f %f %f %f\n",J_hfun->_vert2._data[n]._ppos[0], 
		  J_hfun->_vert2._data[n]._ppos[1], 
		  hfvals->_data[n], b[n]);
	}
      } else {
	n = 0;
	for (i = 0; i < nce1; i++) {
	  for (j = 0; j < nce2; j++) {
	    fprintf(ef, "%f %f %f %f\n",hfx->_data[i], hfy->_data[j],
		    hfvals->_data[n], b[n]);
	    n++;
	  }
	}
      }
      fclose(ef);
    }
    sprintf(buf,"%s_hfun.msh", key);
    if ((hf = fopen(buf, "w")) != NULL) {
      fprintf(hf, "# .msh geometry file\n");
      if (imeth & I_MESH) {
	/* EUCLIDEAN-MESH */
	fprintf(hf, "MSHID=2;EUCLIDEAN-MESH\n");
      } else {
	/* EUCLIDEAN-GRID */
	fprintf(hf, "MSHID=2;EUCLIDEAN-GRID\n");
      }
      fprintf(hf, "NDIMS=2\n");
      if (imeth & I_MESH) {
	fprintf(hf, "POINT=%d\n",nhfun);
	for (n = 0; n < nhfun; n++)
	  fprintf(hf, "%f;%f;0\n",J_hfun->_vert2._data[n]._ppos[0],
		  J_hfun->_vert2._data[n]._ppos[1]);
      } else {
	fprintf(hf, "coord=1;%d\n",nce1);
	for (i = 0; i < nce1; i++)
	  fprintf(hf, "%f\n",hfx->_data[i]);
	fprintf(hf, "coord=2;%d\n",nce2);
	for (j = 0; j < nce2; j++)
	  fprintf(hf, "%f\n",hfy->_data[j]);
      }
      if (imeth & I_MESH) {
	fprintf(hf, "value=%d;1\n",nhfun);
	for (n = 0; n < nhfun; n++)
	  fprintf(hf, "%f\n",hfvals->_data[n]);
      } else {
	fprintf(hf, "value=%d;1\n",nce1*nce2);
	n = 0;
	for (i = 0; i < nce1; i++)
	  for (j = 0; j < nce2; j++) {
	    fprintf(hf, "%f\n",hfvals->_data[n++]);
	  }
      }
      fclose(hf);
    }
  }
  /*for (n = 0; n < nhfun; n++) hfvals->_data[n] = 1.0;*/
  d_free_1d(b); 
  if (DEBUG("init_m"))
    dlog("init_m", "Coastline weighting function computed OK\n");
}

/* END hfun_from_coast()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Computs the minimum distance to a coastline                       */
/*-------------------------------------------------------------------*/
double coast_dist(msh_t *msh, double xloc, double yloc)
{
  int n;
  double d, x, y, dx, dy;
  double dist = HUGE;

  for (n = 0; n < msh->npoint; n++) {
    /* Coastline coordinates                                         */
    if (msh->flag[n] & (S_OBC|S_LINK)) continue;
    x = xloc - msh->coords[0][n];
    y = yloc - msh->coords[1][n];
    d = sqrt(x * x + y * y);
    dist = min(dist, d);
  }
  return(dist);
}

/* END coast_dist()                                                  */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Creates a constant triangulation given a bounding perimeter       */
/*-------------------------------------------------------------------*/
void create_bounded_mesh(int npts,     /* Number of perimeter points */
			 double *x,    /* Perimeter x coordinates    */
			 double *y,    /* Perimeter y coordinates    */
			 double rmin,  /* Min. resolution in m       */
			 double rmax,  /* Max. resolution in m       */
			 jigsaw_msh_t *J_mesh
			 )
{
  int filef = 1;
  FILE *ef;
  char buf[MAXSTRLEN], key[MAXSTRLEN];
  int np = npts - 1;

  /* local variables                                                 */
  int n, i, j;
  int jret = 0;
  
  /* Jigsaw variables                                                */
  jigsaw_jig_t J_jig;
  jigsaw_msh_t J_geom;

  /* Convenient pointers                                             */
  jigsaw_VERT2_array_t *jpoints = &J_geom._vert2;
  jigsaw_EDGE2_array_t *jedges  = &J_geom._edge2;

  /* Jigsaw initialisation routines                                  */
  jigsaw_init_jig_t (&J_jig);
  jigsaw_init_msh_t (&J_geom);

  /* Flag input mesh type                                            */
  J_geom._flags = JIGSAW_EUCLIDEAN_MESH;

  /* Allocate JIGSAW geom (input mesh) arrays                        */
  jigsaw_alloc_vert2(jpoints, npts);
  jigsaw_alloc_edge2(jedges,  npts);
  
  /* Set up the coordinates and points of the perimeter              */
  for (n = 0; n < np; n++) {
    /* Coordinates                                                   */
    jpoints->_data[n]._ppos[0] = x[n];
    jpoints->_data[n]._ppos[1] = y[n];

    /* Connecting edges                                              */
    jedges->_data[n]._node[0] = n;
    if (n<np-1)
      jedges->_data[n]._node[1] = n+1;
    else
      jedges->_data[n]._node[1] = 0; // close polygon
  }
  /*
  for (n = 0; n < np; n++) {
    printf("%f %f\n",jpoints->_data[jedges->_data[n]._node[0]]._ppos[0],
	   jpoints->_data[jedges->_data[n]._node[0]]._ppos[1]);
    printf("%f %f\n",jpoints->_data[jedges->_data[n]._node[1]]._ppos[0],
	   jpoints->_data[jedges->_data[n]._node[1]]._ppos[1]);
  }
  */
  /* Set up some flags/parameters                                    */
  J_jig._hfun_scal = JIGSAW_HFUN_ABSOLUTE;
  J_jig._hfun_hmin = 0.25 * (rmin + rmax);
  J_jig._hfun_hmax = 0.25 * (rmin + rmax);

  /*
  J_jig._hfun_scal = JIGSAW_HFUN_RELATIVE;
  J_jig._hfun_hmin = 0.0;
  J_jig._hfun_hmax = 0.001;
  */
  /* Call Jigsaw                                                     */
  J_jig._verbosity = 0;
  jret = jigsaw_make_mesh(&J_jig, &J_geom, NULL, J_mesh);
  if (jret) hd_quit("create_bounded_mesh: Error calling JIGSAW\n");

  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_e.txt", key);
    if ((ef = fopen(buf, "w")) != NULL) {
      /*
      for (n = 0; n < J_mesh->_vert2._size; n++) {
	fprintf(ef, "%f %f\n", J_mesh->_vert2._data[n]._ppos[0],
		J_mesh->_vert2._data[n]._ppos[1]);
      }
      */
      for (n = 0; n < J_mesh->_tria3._size; n++) {
	fprintf(ef, "%f %f\n", 
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[0]]._ppos[0],
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[0]]._ppos[1]);
	fprintf(ef, "%f %f\n", 
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[1]]._ppos[0],
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[1]]._ppos[1]);
	fprintf(ef, "%f %f\n", 
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[2]]._ppos[0],
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[2]]._ppos[1]);
	fprintf(ef, "NaN NaN\n");
      }
      fclose(ef);
    }
  }
  /* Cleanup                                                         */
  jigsaw_free_msh_t(&J_geom);
}

/* END create_bounded_mesh()                                         */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/
void init_J_jig(jigsaw_jig_t *J_jig)
{
  J_jig->_verbosity = 0;
  J_jig->_geom_seed = 8;
  J_jig->_geom_feat = 0;
  J_jig->_geom_eta1 = 45.0;
  J_jig->_geom_eta2 = 45.0;
  J_jig->_hfun_scal = JIGSAW_HFUN_RELATIVE;
  J_jig->_hfun_hmax = 0.02;
  J_jig->_hfun_hmin = 0.0;
  J_jig->_mesh_dims = 3;
  /* JIGSAW_KERN_DELAUNAY or JIGSAW_KERN_DELFRONT */
  J_jig->_mesh_kern = JIGSAW_KERN_DELFRONT;
  /*J_jig->_mesh_iter = Inf;*/
  J_jig->_mesh_top1 = 0;
  J_jig->_mesh_top2 = 0;
  J_jig->_mesh_rad2 = 1.05;
  J_jig->_mesh_rad3 = 2.05;
  J_jig->_mesh_off2 = 0.9;
  J_jig->_mesh_off3 = 1.1;
  J_jig->_mesh_snk2 = 0.2;
  J_jig->_mesh_snk3 = 0.33;
  J_jig->_mesh_eps1 = 0.33;
  J_jig->_mesh_eps2 = 0.33;
  J_jig->_mesh_vol3 = 0.0;
  J_jig->_optm_iter = 32;
  J_jig->_optm_qtol = 1.E-04;
  J_jig->_optm_qlim = 0.9250;


}

/* END init_J_jig()                                                  */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Creates a constant triangulation given a bounding perimeter       */
/*-------------------------------------------------------------------*/
void create_jigsaw_mesh(coamsh_t *cm, 
			jigsaw_msh_t *J_mesh,
			jigsaw_msh_t *J_hfun
			)
{
  int filef = 1;
  FILE *ef;
  char buf[MAXSTRLEN], key[MAXSTRLEN];

  /* Constants                                                       */
  double deg2m = 60.0 * 1852.0;
  msh_t *msh = cm->msh;
  jig_t *jig = cm->jig;

  /* local variables                                                 */
  int n, i, j;
  int jret = 0;
  
  /* Jigsaw variables                                                */
  jigsaw_jig_t J_jig;
  jigsaw_msh_t J_geom;

  /* Convenient pointers                                             */
  jigsaw_VERT2_array_t *jpoints = &J_geom._vert2;
  jigsaw_EDGE2_array_t *jedges  = &J_geom._edge2;

  /* Jigsaw initialisation routines                                  */
  jigsaw_init_jig_t (&J_jig);
  jigsaw_init_msh_t (&J_geom);
  init_J_jig(&J_jig);

  jigsaw_VERT2_array_t *hf    = &J_hfun->_vert2;
  jigsaw_REALS_array_t *hfvals = &J_hfun->_value;

  /* Flag input mesh type                                            */
  J_geom._flags = JIGSAW_EUCLIDEAN_MESH;

  /* Allocate JIGSAW geom (input mesh) arrays                        */
  jigsaw_alloc_vert2(jpoints, msh->npoint);
  jigsaw_alloc_edge2(jedges,  msh->npoint);
  
  /* Set up the coordinates and points of the perimeter              */
  for (n = 0; n < msh->npoint; n++) {
    /* Coordinates                                                   */
    jpoints->_data[n]._ppos[0] = msh->coords[0][n];
    jpoints->_data[n]._ppos[1] = msh->coords[1][n];
  }
  for (n = 0; n < msh->edge2; n++) {
    /* Connecting edges                                              */
    jedges->_data[n]._node[0] = msh->edges[0][n];
    jedges->_data[n]._node[1] = msh->edges[1][n];
  }

  /* Set up some flags/parameters                                    */
  J_jig._hfun_scal = JIGSAW_HFUN_ABSOLUTE;
  J_jig._hfun_hmin = cm->hfun_min / deg2m;
  J_jig._hfun_hmax = cm->hfun_max / deg2m;
  J_jig._verbosity = 1;
  /*J_jig._mesh_eps1 = 10.0;*/
  /*
  J_jig._hfun_hmin = 0.01;
  J_jig._hfun_hmax = 0.02;
  */
  /* Call Jigsaw                                                     */
  jret = jigsaw_make_mesh(&J_jig, &J_geom, J_hfun, J_mesh);
  if (jret) hd_quit("create_jigsaw_mesh: Error calling JIGSAW\n");

  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_t.txt", key);
    if ((ef = fopen(buf, "w")) != NULL) {
      for (n = 0; n < J_mesh->_tria3._size; n++) {
	fprintf(ef, "%f %f\n", 
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[0]]._ppos[0],
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[0]]._ppos[1]);
	fprintf(ef, "%f %f\n", 
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[1]]._ppos[0],
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[1]]._ppos[1]);
	fprintf(ef, "%f %f\n", 
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[2]]._ppos[0],
		J_mesh->_vert2._data[J_mesh->_tria3._data[n]._node[2]]._ppos[1]);
	fprintf(ef, "NaN NaN\n");
      }
      fclose(ef);
    }
  }

  /* Cleanup                                                         */
  jigsaw_free_msh_t(&J_geom);
  jigsaw_free_msh_t(J_hfun);
  if (DEBUG("init_m"))
    dlog("init_m", "JIGSAW mesh created OK\n");
}

/* END create_jigsaw_mesh()                                          */
/*-------------------------------------------------------------------*/
#endif

/*-------------------------------------------------------------------*/
/* Reads a JIGSAW .msh triangulation file and converts to a Voronoi  */
/* mesh.                                                             */
/*-------------------------------------------------------------------*/
void convert_jigsaw_msh(parameters_t *params, char *infile,
			void *jmsh)
{
  FILE *fp, *ef, *vf, *tf, *cf, *jf, *cef;
  int meshid;
  int ndims;
  int npoints;
  int n, nn, i, j, m, mm, id;
  int e1, e2, t1, t2;
  char buf[MAXSTRLEN], code[MAXSTRLEN], key[MAXSTRLEN];
  double x1, y1, x2, y2, d1, d2;
  double nvoints, nvedges;
  point *voints;
  delaunay* d;
  triangle* t, tn;
  int ns, *sin, *edges, *mask, **nei, **e2t;
  int intype;
  int verbose = 0;
  int filef = 1;
  int filefj = 0;         /* Print JIGSAW .msh output file           */
  int centref = 1;        /* Use circumcentres for triangle centres  */
  int newcode = 1;        /* Optimised neighbour finding             */
  int obtusef = 1;        /* Use centroid for obtuse triangles        */
  int dtri = -1;          /* Debugging to find triangles              */
  int dedge = -1;         /* Debugging for edges                     */

#ifdef HAVE_JIGSAWLIB
  jigsaw_msh_t *msh = (jigsaw_msh_t*)jmsh;
#endif
  params->d = d = malloc(sizeof(delaunay));
  params->us_type |= US_JUS;
  if (!centref && obtusef) obtusef = 0;

  if (jmsh == NULL) {
    if ((fp = fopen(infile, "r")) == NULL)
      hd_quit("convert_msh: Can't open JIGSAW file %s\n", infile);

    prm_skip_to_end_of_tok(fp, "mshid", "=", buf);
    meshid = atoi(buf);
    prm_skip_to_end_of_tok(fp, "ndims", "=", buf);
    ndims = atoi(buf);
    prm_skip_to_end_of_tok(fp, "point", "=", buf);
    d->npoints = atoi(buf);
    intype = 1;
  } else {
#ifdef HAVE_JIGSAWLIB
    /* Always the case for now */
    ndims = 2;
    d->npoints = msh->_vert2._size;
    intype = 0;
#endif
  }

  /*
  prm_read_int(fp, "mshid=", &meshid);
  prm_read_int(fp, "ndims=", &ndims);
  prm_read_int(fp, "point=", &d->npoints);
  */

  /*-----------------------------------------------------------------*/
  /* Write to file                                                   */
  if (filef) {
    mesh_ofile_name(params, key);
    sprintf(buf,"%s_jv.txt", key);
    if ((vf = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_je.txt", key);
    if ((ef = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_jc.txt", key);
    if ((cf = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_jce.txt", key);
    if ((cef = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_jt.txt", key);
    if ((tf = fopen(buf, "w")) == NULL) filef = 0;
    sprintf(buf,"%s_jig.msh", key);
    if ((jf = fopen(buf, "w")) == NULL) 
      filefj = 0;
    else {
      fprintf(jf, "# .msh geometry file\n");
      fprintf(jf, "mshid=1\n");
      fprintf(jf, "ndims=2\n");
      fprintf(jf, "point=%d\n",d->npoints);
    }
  }

  /*-----------------------------------------------------------------*/
  /* Read the triangulation specification.                           */
  /* Points in the triangulation.                                    */
  d->points = malloc(d->npoints * sizeof(point));
  for (i = 0; i < d->npoints; i++) {
    point* p = &d->points[i];
    if (intype) {
      if (fscanf(fp, "%lf;%lf;%d", &p->x, &p->y, &id) != 3)
	hd_warn("convert_jigsaw_msh: Incorrect number of point entries in %s, line %d\n", infile, i);
    } else {
#ifdef HAVE_JIGSAWLIB
	p->x = msh->_vert2._data[i]._ppos[0];
	p->y = msh->_vert2._data[i]._ppos[1];
#endif
    }
    if (filef) fprintf(cf, "%f %f\n", p->x, p->y);
    if (filefj) fprintf(jf, "%f;%f;0\n", p->x, p->y);
  }
  if (filef) fclose(cf);

  /*-----------------------------------------------------------------*/
  /* Boundary edges in the triangulation.                            */
  /*prm_read_int(fp, "edge2=", &ns);*/
  ns = 0;
  if (intype) {
    if (prm_skip_to_end_of_tok(fp, "edge2", "=", buf)) {
      ns = atoi(buf);
    }
  } else {
#ifdef HAVE_JIGSAWLIB
    ns = msh->_edge2._size;
    /*
    if (ns) {
      sin = i_alloc_1d(2 * ns);
      j = 0;
      for (i = 0; i < ns; i++) {
	if (intype) {
	  if (fscanf(fp, "%d;%d;%d", &e1, &e2, &id) != 3)
	    hd_warn("convert_msh: Incorrect number of edge entries in %s, line %d\n", infile, i);
	} else {
	  e1 = msh->_edge2._data[i]._node[0];
	  e2 = msh->_edge2._data[i]._node[1];
	}
	sin[j++] = e1;
	sin[j++] = e2;
	if (filef) {
	  fprintf(ef, "%f %f\n", d->points[e1].x, d->points[e1].y);
	  fprintf(ef, "%f %f\n", d->points[e2].x, d->points[e2].y);
	  fprintf(ef, "NaN NaN\n");
	}
      }
    }
    */
#endif
  }
  if (filefj) fprintf(jf, "edge2=%d\n",ns);
  if (ns) {
    sin = i_alloc_1d(2 * ns);
    j = 0;
    for (i = 0; i < ns; i++) {
      if (intype) {
	if (fscanf(fp, "%d;%d;%d", &e1, &e2, &id) != 3)
	  hd_warn("convert_msh: Incorrect number of edge entries in %s, line %d\n", infile, i);
      } else {
#ifdef HAVE_JIGSAWLIB
	e1 = msh->_edge2._data[i]._node[0];
	e2 = msh->_edge2._data[i]._node[1];
#endif
      }
      sin[j++] = e1;
      sin[j++] = e2;
      if (filef) {
	fprintf(ef, "%f %f\n", d->points[e1].x, d->points[e1].y);
	fprintf(ef, "%f %f\n", d->points[e2].x, d->points[e2].y);
	fprintf(ef, "NaN NaN\n");
      }
      if (filefj) fprintf(jf, "%d;%d;%d\n",e1, e2);
    }
  }
  if (filef) fclose(ef);

  /*-----------------------------------------------------------------*/
  /* Triangles in the triangulation.                                 */
  if (intype) {
    prm_skip_to_end_of_tok(fp, "tria3", "=", buf);
    d->ntriangles = atoi(buf);
  } else {
#ifdef HAVE_JIGSAWLIB
    d->ntriangles = msh->_tria3._size;
#endif
  }
  d->triangles = malloc(d->ntriangles * sizeof(triangle));
  if (filefj) fprintf(jf, "tria3=%d\n", d->ntriangles);
  for (i = 0; i < d->ntriangles; i++) {
    t = &d->triangles[i];
    if (intype) {
      if (fscanf(fp, "%d;%d;%d;%d", &t->vids[0], &t->vids[1], &t->vids[2], &id) != 4)
	hd_warn("convert_msh: Incorrect number of triangle entries in %s, line %d\n", infile, i);
    } else {
#ifdef HAVE_JIGSAWLIB
      t->vids[0] = msh->_tria3._data[i]._node[0];
      t->vids[1] = msh->_tria3._data[i]._node[1];
      t->vids[2] = msh->_tria3._data[i]._node[2];
#endif
    }
    if (filefj) fprintf(jf, "%d;%d;%d;0\n",t->vids[0], t->vids[1], t->vids[2]);
    /* Print the centre of mass if required                          */
    if (filef) {
      double po[2];
      double p1[3][2];
      for (n = 0; n < 3; n++) {
	j = t->vids[n];
	p1[n][0] = d->points[j].x;
	p1[n][1] = d->points[j].y;
      }
      tria_com(po, p1[0], p1[1], p1[2]);
      fprintf(cef, "%f %f\n", po[0], po[1]);

      /* Find a triange with location limits                         */
      if (dtri >= 0) {
	if(po[0] > 130.766 && po[0] < 130.768 && 
	   po[1] > -8.3175 && po[1] < -8.316) {
	  printf("TRI %d\n",i);
	  dtri = i;
	  for (n = 0; n < 3; n++) {
	    j = t->vids[n];
	    printf("Vertex%d = [%f %f]\n",n, d->points[j].x, d->points[j].y);
	  }
	}
      }
    }
  }
  if (filefj) fclose(jf);
  if (filef) fclose(cef);

  /*-----------------------------------------------------------------*/
  /* Extract the edges from the triangles and populate d->edges.      */
  /* Count the number of edges.                                      */
  d->nedges = 0;
  edges = i_alloc_1d(7 * d->ntriangles);
  e2t = i_alloc_2d(2, 7 * d->ntriangles);
  for (j = 0; j < d->ntriangles; j++) {
    t = &d->triangles[j];
    for (n = 0; n < 3; n++) {
      int found = 0;
      nn = (n == 2) ? 0 : n + 1;
      e1 = t->vids[n];
      e2 = t->vids[nn];
      for (i = 0; i < d->nedges; i++) {
	if ((edges[2*i] == e1 && edges[2*i+1] == e2) ||
	    (edges[2*i] == e2 && edges[2*i+1] == e1)) {
	  found = 1;
	  break;
	}
      }
      if (e1 < 0 || e1 >= d->npoints) hd_quit("convert_jigsaw_msh: found invalid coordinate index in triangle %d (%d)\n", j, e1);
      if (e2 < 0 || e2 >= d->npoints) hd_quit("convert_jigsaw_msh: found invalid coordinate index in triangle %d (%d)\n", j, e2);
      if (!found) {
	edges[2*d->nedges] = e1;
	edges[2*d->nedges+1] = e2;
	e2t[d->nedges][0] = j;
	e2t[d->nedges][1] = n;
	d->nedges++;
      }
    }
  }
  d->edges = i_alloc_1d(2*d->nedges);
  memcpy(d->edges, edges, 2 * d->nedges * sizeof(int));
  i_free_1d(edges);
  if (verbose) printf("npoints=%d nedges=%d ntriangles=%d\n", d->npoints, d->nedges, d->ntriangles);
  if (filef) {
    for (i = 0; i < d->nedges; i++) {
      fprintf(tf, "%f %f\n",i,d->points[d->edges[2*i]].x,d->points[d->edges[2*i]].y);
      fprintf(tf, "%f %f\n",d->points[d->edges[2*i+1]].x,d->points[d->edges[2*i+1]].y);
      fprintf(tf, "NaN NaN\n");
    }
  fclose(tf);
  }

  /*-----------------------------------------------------------------*/
  /* Find the triangle neighbours                                    */
  if (newcode) 
    neighbour_finder_b(d, &nei);
  /*
  else {
    nei = i_alloc_2d(3, d->ntriangles);
    for (j = 0; j < d->ntriangles; j++)
      for (n = 0; n < 2; n++)
	nei[j][n] = -1;
    for (j = 0; j < d->ntriangles; j++) {
      t = &d->triangles[j];
      for (n = 0; n < 3; n++) {
	nn = (n == 2) ? 0 : n + 1;
	t1 = t->vids[n];
	t2 = t->vids[nn];
	for (i = 0; i < d->ntriangles, i != j; i++) {
	  tn = &d->triangles[i];
	  for (m = 0; m < 3; m++) {
	    mm = (m == 2) ? 0 : m + 1;
	    t1 = tn->vids[m];
	    t2 = tn->vids[mm];
	  }
	}
      }
    }
  }
  */

  /*-----------------------------------------------------------------*/
  /* Find the triangles that share each edge and save the centroids. */
  /* Note: triangle centroids are the vertices of Voronoi cells      */
  /* (i.e. the endpoints of Voronoi edges; vdges[]) and are common   */
  /* to multiple Voronoi edges (3 for hexagons). mask[] ensures that */
  /* these vertices are only accounted for once.                     */
  d->nvoints = 0;
  d->nvdges = d->nedges;
  d->voints = malloc(3 * d->ntriangles * sizeof(point));
  d->vdges = malloc(d->nvdges * 2 * sizeof(int));
  mask = i_alloc_1d(d->ntriangles);
  for (j = 0; j < d->ntriangles; j++) 
    mask[j] = -1;

  for (i = 0; i < d->nedges; i++) {
    int tri[2];
    double po[2];

    /* Coordinate indices (points[e1], points[e2]) for the edge      */
    e1 = d->edges[2*i];
    e2 = d->edges[2*i+1];
    m = 0;

    /* Find the two triangles that share the edge. For boundaries     */
    /* there is only one trianlge.                                   */                            
    if (newcode) {
      m = 1;
      tri[0] = e2t[i][0];
      j = e2t[i][1];
      if ((tri[1] = nei[j][tri[0]]) >= 0) m++;
    } else {
    for (j = 0; j < d->ntriangles; j++) {
      t = &d->triangles[j];
      for (n = 0; n < 3; n++) {
	nn = (n == 2) ? 0 : n + 1;
	t1 = t->vids[n];
	t2 = t->vids[nn];
	if ((e1 == t1 && e2 == t2 ) ||
	    (e2 == t1 && e1 == t2 )) {
	  tri[m++] = j;
	  break;
	}
      }
      if (m == 2) break;
    }
    }

    /* Debugging for edges                                           */
    if (i == dedge)
      printf("edge %d shares %d triangles (%d & %d) %d %d\n",
	     i, m, tri[0], tri[1], e2t[i][0], e2t[i][1]);

    /* Debugging for triangles                                        */
    if (dtri >= 0) {
    if (tri[0] == dtri|| tri[1] == dtri)
      printf("tri0=%d, tri1=%d, edge=%d[%f %f]-[%f %f], m=%d\n",
	     tri[0], tri[1], i, d->points[e1].x, d->points[e1].y,
	     d->points[e2].x, d->points[e2].y, m);
    }

    if (verbose) {
      printf("%f %f\n",d->points[d->edges[2*i]].x,d->points[d->edges[2*i]].y);
      printf("%f %f\n",d->points[d->edges[2*i+1]].x,d->points[d->edges[2*i+1]].y);
      printf("NaN NaN\n");
    }

    /* Get the centres of the triangles. These define the vertices of */
    /* Voronoi cells.                                                */
    if (m == 1) {
      /* If only one triangle is a neighbour to the edge, then use   */
      /* the centroid of that triangle and the midpoint of the edge  */
      /* as the Voronoi edge.                                        */
      double p1[3][2];
      int o1;
      x1 = y1 = x2 = y2 = d1 = 0.0;
      for (n = 0; n < 3; n++) {
	t = &d->triangles[tri[0]];
	j = t->vids[n];
	p1[n][0] = d->points[j].x;
	p1[n][1] = d->points[j].y;

	d1 += 1.0;
	x2 = 0.5 * (d->points[e1].x + d->points[e2].x);
	y2 = 0.5 * (d->points[e1].y + d->points[e2].y);
      }
      o1 = (centref) ? 0 : 1;
      if (obtusef) {
	if ((d2 = is_obtuse(p1[0],p1[1],p1[2]))) {
	  if (DEBUG("init_m"))
	    dlog("init_m", "convert_jigsaw_msh: Obtuse triangle at edge %d, tri %d (%5.2f)\n", i, tri[0], d2);
	  /*hd_warn("convert_jigsaw_msh: Obtuse triangle at edge %d, tri %d (%5.2f)\n", i, tri[0], d2);*/
	  o1 = 1;
	}
      }
      if (o1) {
	tria_com(po, p1[0], p1[1], p1[2]);
      	x1 = po[0]; y1 = po[1];
      } else {
	/*
	circumcentre(p1[0], p1[1], p1[2], &x1, &y1);
	*/
	tria_ball_2d(po, p1[0], p1[1], p1[2]);
	x1 = po[0]; y1 = po[1];
      }
      if(i == dedge) 
	printf("TRI%d centres [%f %f] and [%f %f] : %f\n",tri[0],x1,y1,x2,y2,d1);

    } else if (m == 2) {
      /* If two triangles are neighbours to the edge, then use the   */
      /* centroids of each triangle as the Voronoi edge.             */
      double p1[3][2], p2[3][2];
      int o1, o2;
      x1 = y1 = x2 = y2 = d1 = 0.0;
      for (n = 0; n < 3; n++) {
	t = &d->triangles[tri[0]];
	j = t->vids[n];
	p1[n][0] = d->points[j].x;
	p1[n][1] = d->points[j].y;

	t = &d->triangles[tri[1]];
	j = t->vids[n];
	p2[n][0] = d->points[j].x;
	p2[n][1] = d->points[j].y;

	d1 += 1.0;
      }
      o1 = o2 = (centref) ? 0 : 1;
      if (obtusef) {
	if ((d2 = is_obtuse(p1[0],p1[1],p1[2]))) {
	  if (DEBUG("init_m"))
	    dlog("init_m", "convert_jigsaw_msh: Obtuse triangle at edge %d, tri %d (%5.2f@[%f %f])\n", 
		 i, tri[0], d2, p1[0][0], p1[0][1]);
	  /*
	  hd_warn("convert_jigsaw_msh: Obtuse triangle at edge %d, tri %d (%5.2f@[%f %f])\n", 
		  i, tri[0], d2, p1[0][0], p1[0][1]);
	  */
	  o1 = 1;
	}
	if ((d2 = is_obtuse(p2[0],p2[1],p2[2]))) {
	  if (DEBUG("init_m"))
	    dlog("init_m", "convert_jigsaw_msh: Obtuse triangle at edge %d, tri %d (%5.2f@[%f %f])\n", 
		 i, tri[1], d2, p2[0][0], p2[0][1]);
	  /*
	  hd_warn("convert_jigsaw_msh: Obtuse triangle at edge %d, tri %d (%5.2f@[%f %f])\n", 
		  i, tri[1], d2, p2[0][0], p2[0][1]);
	  */
	  o2 = 1;
	}			      
      }
      if (o1) {
	tria_com(po, p1[0], p1[1], p1[2]);
      	x1 = po[0]; y1 = po[1];
      } else {
	/*
	circumcentre(p1[0], p1[1], p1[2], &x1, &y1);
	*/
	tria_ball_2d(po, p1[0], p1[1], p1[2]);
	x1 = po[0]; y1 = po[1];
      }
      if (o2) {
	tria_com(po, p2[0], p2[1], p2[2]);
      	x2 = po[0]; y2 = po[1];
      } else {
	/*	
	circumcentre(p2[0], p2[1], p2[2], &x2, &y2);
	*/
	tria_ball_2d(po, p2[0], p2[1], p2[2]);
	x2 = po[0]; y2 = po[1];
      }
      if (obtusef && o1) {
	if (DEBUG("init_m"))
	  dlog("init_m", "  tri%d [%f %f], [%f %f], [%f %f]: centre [%f %f]\n",
	       tri[0], p1[0][0], p1[0][1],
	       p1[1][0], p1[1][1], p1[2][0], p1[2][1], x1, y1);
	/*
	hd_warn("  tri%d [%f %f], [%f %f], [%f %f]: centre [%f %f]\n",
		tri[0], p1[0][0], p1[0][1],
		p1[1][0], p1[1][1], p1[2][0], p1[2][1], x1, y1);
	*/
      }
      if (obtusef && o2) {
	if (DEBUG("init_m"))
	  dlog("init_m", "  tri%d [%f %f], [%f %f], [%f %f]: centre [%f %f]\n",
		tri[1], p2[0][0], p2[0][1],
		p2[1][0], p2[1][1], p2[2][0], p2[2][1], x2, y2);
	/*
	hd_warn("  tri%d [%f %f], [%f %f], [%f %f]: centre [%f %f]\n",
		tri[1], p2[0][0], p2[0][1],
		p2[1][0], p2[1][1], p2[2][0], p2[2][1], x2, y2);
	*/
      }
    }

    /* Set a new Voronoi point coordinate (vpoints[]) if it doesn't  */
    /* already exist and set the Voronoi edge endpoints (vdges[]) to */
    /* the corresponding coordinate indices. The triangle edges and  */
    /* Voronoi edges having the same index should be perpendicular.  */
    if (d1) {
      point *p;
      /* First Voronoi edge point. Should always be the centre of a  */
      /* triangle.                                                   */
      if (mask[tri[0]] < 0) {
	p = &d->voints[d->nvoints];
	p->x = x1;
	p->y = y1;
	mask[tri[0]] = d->nvoints;
	d->nvoints++;
      }
      d->vdges[2*i] = mask[tri[0]];
      if( i == dedge) 
	printf("first Voronoi edge = [%f %f]\n", 
	       d->voints[d->vdges[2*i]].x, d->voints[d->vdges[2*i]].y);

      /* Second Voronoi edge point. May be a triangle centroid or    */
      /* the midpoint of a triangle edge.                            */
      if (m == 1 || (m == 2 && mask[tri[1]] < 0)) {
	p = &d->voints[d->nvoints];
	p->x = x2;
	p->y = y2;
	if (m == 2) mask[tri[1]] = d->nvoints;
	mm = d->nvoints;
	d->nvoints++;
      } else
	mm = mask[tri[1]];
      d->vdges[2*i+1] = mm;
      if(i == dedge) 
	printf("second Voronoi edge = [%f %f]\n", 
	       d->voints[d->vdges[2*i+1]].x, d->voints[d->vdges[2*i+1]].y);

      if (filef) {
	fprintf(vf, "%f %f\n",x1, y1);
	fprintf(vf, "%f %f\n",x2, y2);
	fprintf(vf, "NaN NaN\n");
      }
    } else
      printf("Can't find Voronoi edge at %d\n", i);
  }
  if (filef) fclose(vf);

  /*
  if (params->uscf & US_TRI)
    convert_tri_mesh(params, params->d);
  if (params->uscf & US_HEX)
    convert_hex_mesh(params, params->d, 1);
  */
  if (DEBUG("init_m"))
    dlog("init_m", "JIGSAW mesh converted OK\n");
  convert_hex_mesh(params, params->d, 1);
  if (DEBUG("init_m"))
    dlog("init_m", "Voronoi mesh created OK\n");
}

/* END convert_jigsaw_msh()                                          */
/*-------------------------------------------------------------------*/

double is_obtuse(double *p0, double *p1, double *p2)
{
  double a, b, c, r = 0.0;
  double x1 = p0[0] - p1[0];
  double y1 = p0[1] - p1[1];
  a = sqrt(x1 * x1 + y1 * y1);
  x1 = p0[0] - p2[0];
  y1 = p0[1] - p2[1];
  b = sqrt(x1 * x1 + y1 * y1);
  x1 = p1[0] - p2[0];
  y1 = p1[1] - p2[1];
  c = sqrt(x1 * x1 + y1 * y1);

  if (a > b && a > c)
    r = (b * b + c * c - a * a) / (2.0 * b * c);
  else if (b > a && b > c)
    r = (a * a + c * c - b * b) / (2.0 * a * c);
  else
    r = (a * a + b * b - c * c) / (2.0 * a * b);
  r = acos(r);
  r = (r > PI/2.0) ? r * 180.0 / PI : 0.0;
  return(r);
}

      
int iswetc(unsigned long flag)
{
  if (!(flag & (SOLID|OUTSIDE)))
    return(1);
  else
    return(0);
}

int isdryc(unsigned long flag)
{
  if (flag & (SOLID|OUTSIDE))
    return(1);
  else
    return(0);
}

int find_mindex(double slon, double slat, double *lon, double *lat, int nsl, double *d)
{
  double dist, d1, d2;
  double dmin = HUGE;
  int i;
  int ret = -1;

  for (i = 1; i <= nsl; i++) {
    d1 = slon - lon[i];
    d2 = slat - lat[i];
    dist = sqrt(d1 * d1 + d2 * d2);
    if (dist < dmin) {
      dmin = dist;
      if (d) *d = dist;
      ret = i;
    }
  }
  return(ret);
}

void addpoint(parameters_t *params, 
	      int i, 
	      int j, 
	      int *n, 
	      point *pin,
	      int **mask)
{
  if (mask[j][i] < 0) {
    pin[*n].x = params->x[j*2][i*2];
    pin[*n].y = params->y[j*2][i*2];
    mask[j][i] = *n;
    *n += 1;
  }
}

int find_mesh_vertex(int c, double x, double y, double **xloc, double **yloc, int *mask, int ns2, int *npe)
{
  int cc, j;
  int found = 0;

  for (cc = 1; cc <= ns2; cc++) {
    if(c == cc) continue;
    if (mask[cc]) {
      for (j = 1; j <= npe[cc]; j++) {
	if (x == xloc[cc][j] && y == yloc[cc][j]) {
	  found = 1;
	}
      }
    }
  } 
 return(found);
}

/** Skip forward from the current file position to
  * the next line beginning with key, positioned at the character
  * immediately after the key.
  *
  * @param fp pointer to stdio FILE structure.
  * @param key keyname to locate in file.
  * @return non-zero if successful.
  */
int prm_skip_to_end_of_tok(FILE * fp, char *key, char *token, char *ret)
{
  char buf[MAXLINELEN], *tok;
  int len = strlen(key);
  long fpos;
  char *s;
  char *r;
  int rewound = 0;
  /*UR 10/06/2005
   * reduced failure of finding a key to 
   * 'TRACE'
   * since it is not vital and allowed. 
   */
  do {
    fpos = ftell(fp);
    s = fgets(buf, MAXLINELEN, fp);
    if (s == NULL) {
      if (!rewound) {
        fpos = 0L;
        if (fseek(fp, fpos, 0))
          quit("prm_skip_to_end_of_tok: %s\n", strerror(errno));
        s = fgets(buf, MAXLINELEN, fp);
        rewound = 1;
      } else {
        emstag(LTRACE,"lib:prmfile:prm_skip_to_end_of_tok"," key %s not found\n",
                         key);
        /*
        (*keyprm_errfn) ("prm_skip_to_end_of_key: key %s not found\n",
                         key);*/
        return (0);
      }
    }

    if (s == NULL)
      break;
    tok = strtok(s, token);

    /* Truncate the string at the first space after the key length. */
    if (strlen(s) > len && is_blank(s[len]))/*UR added length check to prevent invalid write*/
      s[len] = '\000';
  } while (strcasecmp(key, s) != 0);

  if (s == NULL) {
    emstag(LTRACE,"lib:prmfile:prm_skip_to_end_of_key","key %s not found\n", key);
    /*(*keyprm_errfn) ("prm_skip_to_end_of_key: key %s not found\n", key);*/
    return (0);
  }

  /* seek to character after key */
  if (fseek(fp, fpos + (s - buf) + len + 1, 0))
    quit("prm_skip_to_end_of_key: %s\n", strerror(errno));

  fgets(buf, MAXLINELEN, fp);

  /* Strip leading space */
  for (s = buf; *s && is_blank(*s); s++) /* loop */
    ;

  /* Copy out result */
  for (r = ret; *s && (*s != '\n'); *r++ = *s++)  /* loop */
    ;
  *r = 0;

  return (1);
}

void bisector(double x1a, 
	      double y1a, 
	      double x2a, 
	      double y2a,
	      double x1b, 
	      double y1b, 
	      double x2b, 
	      double y2b,
	      double *x,
	      double *y
	      )
{
  double mxa = 0.5 * (x1a + x2a);
  double mya = 0.5 * (x2a + y2a);
  double sa = -(x2a - x1a) / (y2a - y1a);
  double ia = mya - sa * mxa;
  double mxb = 0.5 * (x1b + x2b);
  double myb = 0.5 * (x2b + y2b);
  double sb = -(x2b - x1b) / (y2b - y1b);
  double ib = myb - sb * mxb;
  *x = (ia - ib) / (sb - sa);
  *y = sa * (*x) + ia;
}


/*------------------------------ unrolled matrix indexing */

#define uij(ir, ic, nr)  ((ir)+(ic)*(nr))

/*-------------------------------------------------------------------*/
/* Determinant of a 2x2 matrix                                       */
/*-------------------------------------------------------------------*/
double det_2x2(
	       int la ,    /* Leading dimension of aa                */
	       double *aa  /* Matrix                                 */
	       )
{   
  double ret;
  ret = aa[uij(0,0,la)] * aa[uij(1,1,la)] -
    aa[uij(0,1,la)] * aa[uij(1,0,la)] ;
  
  return(ret);
}

/* END det_2x2()                                                     */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* 2-by-2 matrix inversion                                           */
/*-------------------------------------------------------------------*/
void inv_2x2(int la,     /* Leading dim. of aa                       */
	     double *aa, /* Matrix                                   */
	     int lx,     /* Leading dim. of xx                       */
	     double *xx, /* Matrix inversion * det                   */
	     double *da  /* matrix determinant                       */
	     )
{
  *da = det_2x2(la, aa);
    
  xx[uij(0,0,lx)] = aa[uij(1,1,la)];
  xx[uij(1,1,lx)] = aa[uij(0,0,la)];
  xx[uij(0,1,lx)] = -aa[uij(0,1,la)];
  xx[uij(1,0,lx)] = -aa[uij(1,0,la)];
}

/* END inv_2x2()                                                     */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Triangle circumcentre. Contributed by Darren Engwirda (JIGSAW     */
/* code).                                                            */
/*-------------------------------------------------------------------*/
void tria_ball_2d(double *bb,    /* centre: [x,y]                    */
		  double *p1,    /* vertex 1: [x,y]                  */
		  double *p2,    /* vertex 2: [x,y]                  */
		  double *p3     /* vertex 3: [x,y]                  */
		  )
{

  double xm[2*2];
  double xi[2*2];
  double xr[2*1];
  double dd;

  /* LHS matrix                                                      */
  xm[uij(0,0,2)] = p2[0]-p1[0] ;
  xm[uij(0,1,2)] = p2[1]-p1[1] ;
  xm[uij(1,0,2)] = p3[0]-p1[0] ;
  xm[uij(1,1,2)] = p3[1]-p1[1] ;

  /* RHS vector                                                      */
  xr[0] = (double)+0.5 * (xm[uij(0,0,2)] * xm[uij(0,0,2)] +
			 xm[uij(0,1,2)] * xm[uij(0,1,2)]);
        
  xr[1] = (double)+0.5 * (xm[uij(1,0,2)] * xm[uij(1,0,2)] +
			  xm[uij(1,1,2)] * xm[uij(1,1,2)]);

  /* Matrix inversion                                                */
  inv_2x2(2, xm, 2, xi, &dd) ;
    
  /* linear solver                                                   */
  bb[0] = (xi[uij(0,0,2)] * xr[0] + xi[uij(0,1,2)] * xr[1]);
  bb[1] = (xi[uij(1,0,2)] * xr[0] + xi[uij(1,1,2)] * xr[1]);
  bb[0] /= dd ;
  bb[1] /= dd ;
  
  /* Offset                                                          */    
  bb[0] += p1[0] ;
  bb[1] += p1[1] ;
}

/* END tri_ball_2d()                                                 */
/*-------------------------------------------------------------------*/	


/*-------------------------------------------------------------------*/
/* Computes the centre of mass of a triangle                         */
/*-------------------------------------------------------------------*/
void tria_com(double *bb,         /* centre: [x,y]                   */
	      double *p1,         /* vertex 1: [x,y]                 */
	      double *p2,         /* vertex 2: [x,y]                 */
	      double *p3          /* vertex 3: [x,y]                 */
	      )
{
  int n;

  bb[0] = 0.0;  bb[1] = 0.0;
  for (n = 0; n < 2; n++) {
    bb[n] = (p1[n] + p2[n] + p3[n]) / 3.0;
  }
}

/* END tri_com()                                                     */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Edge sort function for dual row / index sorting                   */
/* Written by Darren Engwirda, 9/2017                                */
/* return -1 if "edge-1" < "edge-2"                                  */
/* return +1 if "edge-1" > "edge-2"                                  */
/* return +0 if "edge-1" = "edge-2"                                  */
/*-------------------------------------------------------------------*/
int edge_sort_compare (void const *x1, void const *x2)
{
  int  const *e1 = (int *)x1 ;
  int  const *e2 = (int *)x2 ;
    
  if (e1[0]  < e2[0]) {         /* less on 1st col. */
    return -1 ;
  } else {
    if (e1[0]  > e2[0]) {       /* more on 1st col. */
      return +1 ;
    } else {
      if (e1[0] == e2[0]) {     /* test on 2nd col. */
	if (e1[1]  < e2[1]) {   /* less on 2nd col. */
	  return -1 ;
	} else {
	  if (e1[1]  > e2[1])   /* more on 2nd col. */
	    return +1 ;
	}
      }
    }
  }
  return +0 ;                   /* have exact match */
}

int edge_sort_double (void const *x1, void const *x2)
{
  double  const *e1 = (double *)x1 ;
  double  const *e2 = (double *)x2 ;
    
  if (e1[0]  < e2[0]) {         /* less on 1st col. */
    return -1 ;
  } else {
    if (e1[0]  > e2[0]) {       /* more on 1st col. */
      return +1 ;
    } else {
      if (e1[0] == e2[0]) {     /* test on 2nd col. */
	if (e1[1]  < e2[1]) {   /* less on 2nd col. */
	  return -1 ;
	} else {
	  if (e1[1]  > e2[1])   /* more on 2nd col. */
	    return +1 ;
	}
      }
    }
  }
  return +0 ;                   /* have exact match */
}

/* END edge_sort_compare()                                           */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Routine to find the neighbors in the mesh structure.              */
/* Uses O(nlog(n)) operations.                                       */
/* Originally written by Darren Engwirda.                            */
/*-------------------------------------------------------------------*/
void neighbour_finder(mesh_t *mesh)
{
  int cc, c1, c2, j, j1, j2, nedge, next;
  int **edge;

  /* Allocate                                                        */
  nedge = 0;
  for (cc = 1; cc <= mesh->ns2; cc++) {
    nedge += mesh->npe[cc];
  }
  edge = i_alloc_2d(4, nedge);

  /* Populate the edge structure                                     */
  next = 0;
  for (cc = 1; cc <= mesh->ns2; cc++) {
    for (j = 1; j <= mesh->npe[cc]; j++) {
      j1 = mesh->eloc[0][cc][j];
      j2 = mesh->eloc[1][cc][j];
      if (j1 < j2) {
	edge[next][0] = j1;
	edge[next][1] = j2;
      } else {
	edge[next][0] = j2;
	edge[next][1] = j1;
      }
      edge[next][2] = cc;
      edge[next][3] = j;
      next++;
    }
  }

  /* Sort edges, duplicates are consecutive!                         */
  qsort(edge[0], nedge, sizeof(int)*4, edge_sort_compare);

  /* Create the mappings                                             */
  mesh->neic = i_alloc_2d(mesh->ns2+1, mesh->mnpe+1);
  mesh->neij = i_alloc_2d(mesh->ns2+1, mesh->mnpe+1);
  for (cc = 1; cc <= mesh->ns2; cc++) {
    for (j = 1; j <= mesh->npe[cc]; j++) {
      mesh->neic[j][cc] = 0;
      mesh->neij[j][cc] = j;
    }
  }

  /* Create the mappings                                             */
  for (cc = 0; cc < nedge-1; cc++) {
    if ((edge[cc][0] == edge[cc+1][0]) &&
	(edge[cc][1] == edge[cc+1][1])) {
      c1 = edge[cc][2];
      c2 = edge[cc+1][2];
      j1 = edge[cc][3];
      j2 = edge[cc+1][3];
      mesh->neic[j1][c1] = c2;
      mesh->neij[j1][c1] = j2;
      mesh->neic[j2][c2] = c1;
      mesh->neij[j2][c2] = j1;
    }
  }
  i_free_2d(edge);
}    

/* Neighbour maps from delaunay structure */
void neighbour_finder_b(delaunay* d, int ***neic)
{
  int cc, c1, c2, j, next, n, nn, j1, j2;
  int **edge, **eic;
  int mnpe = 3;
  int ns2 = d->ntriangles;
  int nedge = ns2 * 3;
  triangle *t;

  edge = i_alloc_2d(4, nedge);
  /* Populate the edge structure                                     */
  next = 0;
  for (cc = 0; cc < ns2; cc++) {
    t = &d->triangles[cc];
    for (n = 0; n < 3; n++) {
      nn = (n == 2) ? 0 : n + 1;
      j1 = t->vids[n];
      j2 = t->vids[nn];
      if (j1 < j2) {
	edge[next][0] = j1;
	edge[next][1] = j2;
      } else {
	edge[next][0] = j2;
	edge[next][1] = j1;
      }
      edge[next][2] = cc;
      edge[next][3] = n;
      next++;
    }
  }

  /* Sort edges, duplicates are consecutive!                         */
  qsort(edge[0], nedge, sizeof(int)*4, edge_sort_compare);

  /* Create the mappings                                             */
  eic = i_alloc_2d(ns2, mnpe);
  for (cc = 0; cc < ns2; cc++) {
    for (j = 0; j < mnpe; j++) {
      eic[j][cc] = -1;
    }
  }

  /* Create the mappings                                             */
  for (cc = 0; cc < nedge-1; cc++) {
    /*printf("%d %d %d\n",cc,edge[cc][0],edge[cc][1]);*/
    if ((edge[cc][0] == edge[cc+1][0]) &&
	(edge[cc][1] == edge[cc+1][1])) {
      c1 = edge[cc][2];
      c2 = edge[cc+1][2];
      j1 = edge[cc][3];
      j2 = edge[cc+1][3];
      eic[j1][c1] = c2;
      eic[j2][c2] = c1;
    }
  }
  *neic = eic;
  i_free_2d(edge);
}

#ifdef HAVE_JIGSAWLIB
/* Neighbour maps from JIGSAW .msh input file */
void neighbour_finder_j(jigsaw_msh_t *msh, int ***neic)
{
  int cc, c1, c2, j, next, n, nn, j1, j2;
  int **edge, **eic, *nv, **eiv, meiv;
  int mnpe = 3;
  int ns2 = msh->_tria3._size;
  int nv2 = msh->_vert2._size;
  int nedge = ns2 * 3;

  edge = i_alloc_2d(4, nedge);
  nv = i_alloc_1d(nv2);
  memset(nv, 0, nv2 * sizeof(int));
  /* Populate the edge structure                                     */
  next = 0;
  for (cc = 0; cc < ns2; cc++) {
    for (n = 0; n < 3; n++) {
      nn = (n == 2) ? 0 : n + 1;
      j1 = msh->_tria3._data[cc]._node[n];
      j2 = msh->_tria3._data[cc]._node[nn];
      nv[j1]++;
      if (j1 < j2) {
	edge[next][0] = j1;
	edge[next][1] = j2;
      } else {
	edge[next][0] = j2;
	edge[next][1] = j1;
      }
      edge[next][2] = cc;
      edge[next][3] = n;
      next++;
    }
  }
  meiv = 0;
  for (cc = 0; cc < nv2; cc++)
    if (nv[cc] > meiv) meiv = nv[cc];
  eiv = i_alloc_2d(nv2, meiv);

  /* Sort edges, duplicates are consecutive!                         */
  qsort(edge[0], nedge, sizeof(int)*4, edge_sort_compare);

  /* Create the mappings                                             */
  eic = i_alloc_2d(ns2, mnpe);
  for (cc = 0; cc < ns2; cc++) {
    for (j = 0; j < mnpe; j++) {
      eic[j][cc] = -1;
    }
  }

  /* Create the mappings                                             */
  for (cc = 0; cc < nedge-1; cc++) {
    /*printf("%d %d %d\n",cc,edge[cc][0],edge[cc][1]);*/
    if ((edge[cc][0] == edge[cc+1][0]) &&
	(edge[cc][1] == edge[cc+1][1])) {
      c1 = edge[cc][2];
      c2 = edge[cc+1][2];
      j1 = edge[cc][3];
      j2 = edge[cc+1][3];
      eic[j1][c1] = c2;
      eic[j2][c2] = c1;
    }
  }
  *neic = eic;
  i_free_2d(edge);
}
#endif

/* Neighbour maps from Voronoi edges set in convert_hex_mesh() */
void neighbour_finder_a(int ns2, int *npe, int **vedge, int **neic, int **neij)
{
  int cc, c1, c2, j, j1, j2, nedge, next;
  int **edge, **eic, **eij, mnpe;

  /* Allocate                                                        */
  nedge = mnpe = 0;
  for (cc = 0; cc < ns2; cc++) {
    nedge += npe[cc];
    mnpe = max(mnpe, npe[cc]);
  }
  edge = i_alloc_2d(4, nedge);

  /* Populate the edge structure                                     */
  next = 0;
  for (cc = 0; cc < ns2; cc++) {
    for (j = 0; j < npe[cc]; j++) {
      j1 = vedge[cc][j];
      j2 = (j1 == npe[cc]-1) ? 0: j1 + 1;
      if (j1 < j2) {
	edge[next][0] = j1;
	edge[next][1] = j2;
      } else {
	edge[next][0] = j2;
	edge[next][1] = j1;
      }
      edge[next][2] = cc;
      edge[next][3] = j;
      next++;
    }
  }

  /* Sort edges, duplicates are consecutive!                         */
  qsort(edge[0], nedge, sizeof(int)*4, edge_sort_compare);

  /* Create the mappings                                             */
  eic = i_alloc_2d(ns2+1, mnpe+1);
  eij = i_alloc_2d(ns2+1, mnpe+1);
  for (cc = 1; cc <= ns2; cc++) {
    for (j = 1; j <= npe[cc]; j++) {
      eic[j][cc] = -1;
      eij[j][cc] = -1;
    }
  }

  /* Create the mappings                                             */
  for (cc = 0; cc < nedge-1; cc++) {
    if ((edge[cc][0] == edge[cc+1][0]) &&
	(edge[cc][1] == edge[cc+1][1])) {
      c1 = edge[cc][2]+1;
      c2 = edge[cc+1][2]+1;
      j1 = edge[cc][3]+1;
      j2 = edge[cc+1][3]+1;
      eic[j1][c1] = c2;
      eij[j1][c1] = j2;
      eic[j2][c2] = c1;
      eij[j2][c2] = j1;
    }
  }
  neic = eic;
  neij = eij;
}

/* END neighbour_finder()                                            */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Removes duplicate locations in a list. O(nlog(n)) operations.     */
/*-------------------------------------------------------------------*/
void remove_duplicates(int ns2, double **x, double **y, mesh_t *mesh)
{
  int cc, j, jj, nn, n, next, nedge;
  double **edge;
  int **cc2n;
  int checkf = 0;

  /* Map the locations into a continuous vector                      */
  /* Get the vector size                                             */
  /* Allocate                                                        */
  nedge = ns2;
  for (cc = 1; cc <= ns2; cc++) {
    nedge += mesh->npe[cc];
  }
  edge = d_alloc_2d(4, nedge);

  if (mesh->xloc && mesh->yloc) {
    d_free_1d(mesh->xloc);
    d_free_1d(mesh->yloc);
    mesh->xloc = d_alloc_1d(nedge + ns2 + 1);
    mesh->yloc = d_alloc_1d(nedge + ns2 + 1);
  }
  cc2n = i_alloc_2d(mesh->mnpe+1, ns2+1);

  /* Make the vector                                                 */
  next = 0;
  for (cc = 1; cc <= ns2; cc++) {
    for (j = 1; j <= mesh->npe[cc]; j++) {
      edge[next][0] = x[cc][j];
      edge[next][1] = y[cc][j];
      edge[next][2] = (double)cc;
      edge[next][3] = (double)j;
      if (cc == checkf) printf("old%d = [%f %f]\n",j, x[cc][j], y[cc][j]);
      next++;
    }
  }
  for (cc = 1; cc <= ns2; cc++) {
      edge[next][0] = x[cc][0];
      edge[next][1] = y[cc][0];
      edge[next][2] = (double)cc;
      edge[next][3] = 0.0;
      if (cc == checkf) printf("old%d = [%f %f]\n",j, x[cc][0], y[cc][0]);
      next++;
  }

  /* Sort edges, duplicates are consecutive!                         */
  qsort(edge[0], nedge, sizeof(double)*4, edge_sort_double);

  if (checkf) {
    for (n = 0; n < nedge; n++) {
      cc = (int)edge[n][2];
      j = (int)edge[n][3];
      if (cc == checkf) printf("new%d = %d[%f %f]\n", j, n, edge[n][0], edge[n][1]);
    }
  }

  /* Remove the duplicates                                           */
  mesh->ns = 1;
  for (n = 0; n < nedge; n++) {
    nn = (n == nedge - 1) ? n - 1 : n + 1;
    if ((edge[n][0] == edge[nn][0]) &&
	(edge[n][1] == edge[nn][1])) {
      cc = (int)edge[n][2];
      j = (int)edge[n][3];
      cc2n[cc][j] = mesh->ns;
      /* If the last entries are duplicates, make sure these are     */
      /* also included in the mesh array.                            */
      if (n == nedge - 1) cc2n[cc][j]--;
      if (nn == nedge - 1) {
	mesh->xloc[mesh->ns] = edge[n][0];
	mesh->yloc[mesh->ns] = edge[n][1];
	mesh->ns++;
      }
    } else {
      mesh->xloc[mesh->ns] = edge[n][0];
      mesh->yloc[mesh->ns] = edge[n][1];
      cc = (int)edge[n][2];
      j = (int)edge[n][3];
      cc2n[cc][j] = mesh->ns;
      mesh->ns++;
    }
  }
  mesh->ns--;

  /* Make the mapping from indices to coordinates                    */
  for (cc = 1; cc <= ns2; cc++) {
     for (j = 1; j <= mesh->npe[cc]; j++) {
       jj = (j == mesh->npe[cc]) ? 1 : j + 1;
       mesh->eloc[0][cc][j] = cc2n[cc][j];
       mesh->eloc[1][cc][j] = cc2n[cc][jj];
     }
     mesh->eloc[0][cc][0] = mesh->eloc[1][cc][0] = cc2n[cc][0];
  }

  if (checkf) {
    for (cc = 1; cc <= ns2; cc++) {
      for (j = 0; j <= mesh->npe[cc]; j++) {
	double xc = x[cc][j];
	double yc = y[cc][j];
	int found = 0;
	for (n = 1; n <= mesh->ns; n++) {
	  if (xc == mesh->xloc[n] && yc == mesh->yloc[n]) {
	    found = 1;
	    break;
	  }
	}
	if (!found) printf("Can't find coordinate cc=%d, j=%d [%f %f]\n", cc, j, xc, yc);
	if (mesh->eloc[0][cc][j] < 1 || mesh->eloc[0][cc][j] > mesh->ns) 
	  printf("Invalid0 index cc=%d, j=%d [%f %f] : %d\n", cc, j, xc, yc, mesh->eloc[0][cc][j]);
	if (mesh->eloc[1][cc][j] < 1 || mesh->eloc[1][cc][j] > mesh->ns) 
	  printf("Invalid1 index cc=%d, j=%d [%f %f] : %d\n", cc, j, xc, yc, mesh->eloc[1][cc][j]);
	if (cc == checkf) {
	  printf("check: cc=%d j=%d %f %f\n",cc, j, mesh->xloc[mesh->eloc[0][cc][j]], mesh->yloc[mesh->eloc[0][cc][j]]);
	}

      }
    }
  }
  d_free_2d(edge);
}

/* END remove_duplicates()                                           */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Expand the mesh around a nominated point to isolate and remove    */
/* any 'lakes' in the mesh. The mesh indices are re-ordered to       */
/* reflect the removal of the lakes.                                 */
/*-------------------------------------------------------------------*/
void mesh_expand(parameters_t *params, double *bathy, double **xc, double **yc)
{
  mesh_t *mesh = params->mesh;
  int cc, c, ci, cn, j, n, ns, ns2i;
  double dist, dmin;
  int found, ni, *film, *filla;
  double mlat, mlon, x, y;
  int verbose = 0;
  int **neic;
  int *n2o, *o2n;

  /*-----------------------------------------------------------------*/
  /* Expand inwards from open boundaries to remove 'lakes'           */
  if (params->mlon != NOTVALID && params->mlat != NOTVALID) {

    /* Get the index of the closest cell to the interior coordinate  */
    dmin = HUGE;
    ns2i = mesh->ns2;
    for (cc = 1; cc <= mesh->ns2; cc++) {
      x = mesh->xloc[mesh->eloc[0][cc][0]];
      y = mesh->yloc[mesh->eloc[0][cc][0]];
      dist = sqrt((params->mlon - x) * (params->mlon - x) +
		  (params->mlat - y) * (params->mlat - y));
      if (dist < dmin) {
	dmin = dist;
	ci = cc;
      }
    }

    /* Set the mask fanning out from the interior coordinate         */
    ns = mesh->ns2 + 2;
    film = i_alloc_1d(ns);
    memset(film, 0, ns * sizeof(int));
    filla = i_alloc_1d(ns);
    memset(filla, 0, ns * sizeof(int));
    found = 1;
    ni = 1;
    film[ni++] = ci;
    filla[ci] = 1;
    while (found) {
      found = 0;
      for (cc = 1; cc < ni; cc++) {
	c = film[cc];
	if (filla[c] == 2) continue;
	for (j = 1; j <= mesh->npe[c]; j++) {
	  cn = mesh->neic[j][c];
	  if (!filla[cn]) {
	    filla[cn] = 1;
	    filla[c] = 2;
	    film[ni] = cn;
	    found = 1;
	    ni++;
	  }
	}
      }
    }

    /* Make a copy of the current map                                */
    neic = i_alloc_2d(mesh->ns2+1, mesh->mnpe+1);
    n2o = i_alloc_1d(mesh->ns2+1);
    o2n = i_alloc_1d(mesh->ns2+1);
    for (cc = 1; cc <= mesh->ns2; cc++) {
      for (j = 1; j <= mesh->npe[cc]; j++) {
	neic[j][cc] = mesh->neic[j][cc];
      }
    }

    /* Remove cells which are part of 'lakes'                        */
    /* Note; the coordinates remain unchanged, with some redundant   */
    /* information corresponding to the lakes.                       */
    cn = 1;
    memset(film, 0, ns * sizeof(int));
    for (cc = 1; cc <= mesh->ns2; cc++) {
      if (filla[cc]) {
	mesh->npe[cn] = mesh->npe[cc];
	bathy[cn] = bathy[cc];
	for (j = 0; j <= mesh->npe[cc]; j++) {
	  xc[cn][j] = xc[cc][j];
	  yc[cn][j] = yc[cc][j];
	  mesh->eloc[0][cn][j] = mesh->eloc[0][cc][j];
	  mesh->eloc[1][cn][j] = mesh->eloc[1][cc][j];
	  mesh->neij[j][cn] = mesh->neij[j][cc];
	  n2o[cn] = cc;
	  o2n[cc] = cn;
	}
	film[cn++] = cc;
      } else {
	if (verbose) printf("%d %f %f\n",cc, mesh->xloc[mesh->eloc[0][cc][0]],
			    mesh->yloc[mesh->eloc[0][cc][0]]);
      }
    }
    mesh->ns2 = cn - 1;

    /* Remap the mapping function                                    */
    for (cc = 1; cc <= mesh->ns2; cc++) {
      for (j = 0; j <= mesh->npe[cc]; j++) {
	c = n2o[cc];
	mesh->neic[j][cc] = o2n[neic[j][c]];
      }
    }
    i_free_2d(neic);
    i_free_1d(n2o);
    i_free_1d(o2n);

    hd_warn("mesh_expand: %d cells eliminated\n", ns2i - mesh->ns2);

    /* OBCs cell centres are referenced to the indices and need to   */
    /* be remapped. The edge locations are referenced directly to    */
    /* the coordinates and do not.                                   */
    for (n = 0; n < mesh->nobc; n++) {
      for (cc = 1; cc <= mesh->npts[n]; cc++) {
	c = mesh->loc[n][cc];
	mesh->loc[n][cc] = film[c];
      }
    }
    i_free_1d(film);
    i_free_1d(filla);
  }
}

/* END mesh_expand()                                                 */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Creates a triangulation using triangle using a bounding           */
/* perimeter as input. Output is stored in the delaunay structure.   */
/*-------------------------------------------------------------------*/
void xy_to_d(delaunay *d, int np, double *x, double *y)
{
  int i, j;
  point *pin;
  int ns, *sin;

  pin = malloc(np * sizeof(point));
  for(i = 0; i < np; i++) {
    point* p = &params->d->points[i];
    pin[i].x = x[i];
    pin[i].y = y[i];
  }
  j = 0;
  ns = 2 * np;
  sin = i_alloc_1d(2 * np);
  for(i = 0; i < np; i++) {
    sin[j++] = 2 * i;
    sin[j++] = 2 * i + 1;
  }
  sin[np - 1] = 0;
  d = delaunay_build(np, pin, ns, sin, 0, NULL);
}

/* END xy_to_d()                                                     */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Creates a name for mesh output files                              */
/*-------------------------------------------------------------------*/
void mesh_ofile_name(parameters_t *params, char *key)
{
  char buf[MAXSTRLEN];
  int n, i, j;

  if (endswith(params->oname, ".nc")) {
    j = 3;
    strcpy(buf, params->oname);
  } else if (endswith(params->prmname, ".tran")) {
    j = 5;
    strcpy(buf, params->prmname);
  } else if (endswith(params->prmname, ".prm")) {
    j = 4;
    strcpy(buf, params->prmname);
  }
  n = strlen(buf);
  for (i = 0; i < n-j; i++)
    key[i] = buf[i];
  key[i] = '\0';
}
/* END mesh_ofile_name()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Writes a summary of output files from unstructured grid           */
/* generation.                                                       */
/*-------------------------------------------------------------------*/
void write_mesh_desc(parameters_t *params, coamsh_t *cm, FILE *fp, int mode)
{
  FILE *op;
  char buf[MAXSTRLEN], key[MAXSTRLEN], key1[MAXSTRLEN];
  int n, i;

  mesh_ofile_name(params, key);

  if (fp != NULL)
    op = fp;
  else {
    sprintf(buf,"%s_meshfiles.txt", key);
    op = fopen(buf, "w");
  }

  fprintf(op, "\n---------------------------------------------\n");
  fprintf(op, "Output diagnostic files from mesh generation.\n");
  fprintf(op, "---------------------------------------------\n\n");
  if (cm != NULL) {
    if (strlen(cm->pname)) {
      fprintf(op, "---------------------------------------------\n");
      fprintf(op, "coastmesh files.\n");
      sprintf(buf, "%s_in.txt", cm->pname);
      fprintf(op, "FILE %s contains the input perimeter for coastmesh\n", buf);
      fprintf(op, "and some output diagnostics.\n\n");
      sprintf(buf, "%s_out.txt", cm->pname);
      fprintf(op, "FILE %s contains the output perimeter for coastmesh.\n\n", buf);
    }
    if (strlen(cm->ofile)) {
      fprintf(op, "FILE %s contains the input geom_file .msh file for JIGSAW.\n", cm->ofile);
      fprintf(op, "This may be used to run JIGSAW offline.\n\n");
      for (i = 0; i < strlen(cm->ofile); i++) {
	if (cm->ofile[i] == '.')
	  break;
	else
	  buf[i] = cm->ofile[i];
      }
      buf[i] = '\0';
      sprintf(key1, "%s.jig", buf);
      fprintf(op, "FILE %s contains the input parameter .jig file for JIGSAW.\n", key1);
      fprintf(op, "This may be used to run JIGSAW offline.\n\n");
    }
  }
  if (mode & M_BOUND_HFUN) {
    fprintf(op, "---------------------------------------------\n");
    fprintf(op, "Files produced in creating the hfun function.\n");
    sprintf(buf,"%s_e.txt", key);
    fprintf(op, "FILE %s contains edges of the hfun file. This is likely\n", buf);
    fprintf(op, "overwritten at a later date, unless the model fails\n");
    fprintf(op, "during the construction of the JIGSAW mesh. May be used for\n");
    fprintf(op, "plotting; e.g. 'plot(in_e(:,1),in_e(:,2),'g-')'.\n\n");

    sprintf(buf,"%s_h.txt", key);
    fprintf(op, "FILE %s contains the coordinates and value of the hfun function.\n", buf);
    fprintf(op, "The bathymetry used in the conversion is also included. May be used\n");
    fprintf(op, "for plotting; e.g. 'scatter(in_h(:,1),in_h(:,2),100,in_h(:,3),'filled')'.\n\n");

    sprintf(buf,"%s_hfun.msh", key);
    fprintf(op, "FILE %s contains the values of the hfun function in .msh format.\n", buf);
    fprintf(op, "This may be used when running JIGSAW offline.\n\n");
  }

  if (mode & M_CREATE_JIG) {
    fprintf(op, "---------------------------------------------\n");
    fprintf(op, "Files produced by invoking JIGSAW.\n");
    sprintf(buf,"%s_t.txt", key);
    fprintf(op, "FILE %s contains the triangulation of the JIGSAW output.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
  }

  if (mode & M_CONVERT_JIG) {
    fprintf(op, "---------------------------------------------\n");
    fprintf(op, "Files produced converting JIGSAW input to a Voronoi mesh.\n");
    sprintf(buf,"%s_jc.txt", key);
    fprintf(op, "FILE %s contains the vertices of the JIGSAW triangulation.\n", buf);
    sprintf(buf,"%s_jce.txt", key);
    fprintf(op, "FILE %s contains the centre of mass of the JIGSAW triangulation.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
    sprintf(buf,"%s_je.txt", key);
    fprintf(op, "FILE %s contains the perimeter edges used in JIGSAW.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
    sprintf(buf,"%s_jt.txt", key);
    fprintf(op, "FILE %s contains the Delaunay edges of the JIGSAW triangulation.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
    sprintf(buf,"%s_jv.txt", key);
    fprintf(op, "FILE %s contains the Voronoi edges.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
    sprintf(buf,"%s_jig.msh", key);
    fprintf(op, "FILE %s may be produced containing a .msh format of the JIGSAW output.\n\n", buf);
  }

  if (mode & M_MESHSTRUCT) {
    mesh_t *mesh = params->mesh;
    fprintf(op, "---------------------------------------------\n");
    fprintf(op, "Files produced converting to COMPAS mesh specification.\n");
    sprintf(buf,"%s_e.txt", key);
    fprintf(op, "FILE %s contains the Voronoi edges.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
    sprintf(buf,"%s_c.txt", key);
    fprintf(op, "FILE %s contains the Voronoi centres.\n", buf);
    fprintf(op, "May be used for plotting.\n\n");
    if (params->nobc) {
      sprintf(buf,"%s_b.txt", key);
      fprintf(op, "FILE %s contains the open boundary perimeter.\n", buf);
      fprintf(op, "May be used for plotting.\n\n");
    }
    sprintf(buf,"%s_us.txt", key);
    fprintf(op, "FILE %s contains the COMPAS specification of the mesh.\n", buf);
    fprintf(op, "May be used to define a grid in the input file.\n\n");
  }
  fclose(op);
}

/* END write_mesh_desc()                                             */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Makes a Delaunay dual from a Voronoi mesh, layer k                */
/*-------------------------------------------------------------------*/
delaunay*  make_dual(geometry_t *geom, int kin)
{
  delaunay* d = delaunay_create();
  int c, c2, ci, cn, cc, ee, e;
  int i, j, jj, k, n;
  int **nei;
  int *mask;
  int verbose = 1;

  mask = i_alloc_1d(geom->szc);
  memset(mask, 0, geom->szc * sizeof(int));
  geom->c2p = i_alloc_2d(geom->szcS, geom->nz+1);

  /*-----------------------------------------------------------------*/
  /* Get the points                                                  */
  d->npoints = 0;
  /* Count the points in layer k                                     */
  for (cc = 1; cc <= geom->n3_t; cc++) {
    c = geom->w3_t[cc];
    k = geom->s2k[c];
    if (k == kin) {
      d->npoints++;
    }
  }

  n = 0;
  d->points = malloc(d->npoints * sizeof(point));
  /* Wet cells                                                       */
  for (cc = 1; cc <= geom->b3_t; cc++) {
    c = geom->w3_t[cc];
    c2 = geom->m2d[c];
    k = geom->s2k[c];
    if (k == kin) {
      geom->c2p[k+1][c2] = n;
      d->points[n].x = geom->cellx[c2];
      d->points[n++].y = geom->celly[c2];
    }
  }
  /* Ghost cells                                                     */
  for (cc = geom->b3_t+1; cc <= geom->n3_t; cc++) {
    c = geom->w3_t[cc];
    ci = geom->wgst[c];
    c2 = geom->m2d[c];
    k = geom->s2k[ci];
    if (k == kin) {
      if (!mask[ci]) {
	geom->c2p[k][geom->m2d[c]] = n;
	d->points[n].x = geom->cellx[c2];
	d->points[n++].y = geom->celly[c2];
	mask[ci] = 1;
      }
      /*
      for (n = 1; n <= geom->npe[c2]; n++) {
	if (c == geom->c2c[n][ci])
	  break;
      }
      e = geom->m2de[geom->c2e[n][ci]];
      geom->c2p[k][geom->m2d[c]] = n;
      d->points[n]->x = geom->u1x[e];
      d->points[n++]->y = geom->u1y[e];
      */
    }
  }
  /* Open boundary ghosts                                            */
  for (n = 0; n < geom->nobc; n++) {
    open_bdrys_t *open = geom->open[n];
    for (ee = 1; ee <= open->no3_e1; ee++) {
      c = open->ogc_t[ee];
      ci = open->obc_e2[ee];
      c2 = geom->m2d[c];
      k = geom->s2k[ci];
      if (k == kin && geom->c2p[k][c2] == -1) {
	geom->c2p[k][c2] = n;
	d->points[n].x = geom->cellx[c2];
	d->points[n++].y = geom->celly[c2];
      }
    }
  }
  if (verbose) printf("npoints = %d\n", d->npoints);

  /*-----------------------------------------------------------------*/
  /* Get the edges                                                   */
  d->nedges = 0;
  memset(mask, 0, geom->szc * sizeof(int));
  for (cc = 1; cc <= geom->b3_t; cc++) {
    c = geom->w3_t[cc];
    c2 = geom->m2d[c];
    k = geom->s2k[c];
    if (k == kin) {
      for (j = 1; j <= geom->npe[c2]; j++) {
	ci = geom->c2c[j][c];
	if (!mask[c] && !mask[ci]) {
	  d->nedges++;
	  mask[c] = 1;
	  mask[ci] = 1;
	}
      }
    }
  }
  memset(mask, 0, geom->szc * sizeof(int));
  d->edges = i_alloc_1d(2 * d->nedges);
  n = 0;
  for (cc = 1; cc <= geom->b3_t; cc++) {
    c = geom->w3_t[cc];
    c2 = geom->m2d[c];
    k = geom->s2k[c];
    if (k == kin) {
      for (j = 1; j <= geom->npe[c2]; j++) {
	ci = geom->c2c[j][c];
	if (!mask[c] && !mask[ci]) {
	  d->edges[n++] = geom->c2p[k+1][c2];
	  d->edges[n++] = geom->c2p[k+1][geom->m2d[ci]];
	  mask[c] = 1;
	  mask[ci] = 1;
	}
      }
    }
  }
  if (verbose) printf("nedges = %d\n", d->nedges);

  /*-----------------------------------------------------------------*/
  /* Get the triangles. First count the triangles.                   */
  d->ntriangles = 0;
  memset(mask, 0, geom->szc * sizeof(int));
  for (cc = 1; cc <= geom->b3_t; cc++) {
    c = geom->w3_t[cc];
    c2 = geom->m2d[c];
    k = geom->s2k[c];
    if (k == kin) {
      for (j = 1; j <= geom->npe[c2]; j++) {
	jj = (j == geom->npe[c2]) ? 1 : j + 1;
	ci = geom->c2c[j][c];
	cn = geom->c2c[jj][c];
	if (!mask[c] || !mask[ci] || !mask[cn]) {
	  d->ntriangles++;
	  mask[c] = 1;
	  mask[ci] = 1;
	  mask[cn] = 1;
	  printf("%d : %d %d %d\n",d->ntriangles,c,ci,cn);
	}
      }
    }
  }
  if (verbose) printf("ntriangles = %d\n", d->ntriangles);
  /* Next set the triangles                                          */
  d->triangles = malloc(d->ntriangles * sizeof(triangle));
  n = 0;
  memset(mask, 0, geom->szc * sizeof(int));
  for (cc = 1; cc <= geom->b3_t; cc++) {
    c = geom->w3_t[cc];
    c2 = geom->m2d[c];
    k = geom->s2k[c];
    if (k == kin) {
      for (j = 1; j <= geom->npe[c2]; j++) {
	jj = (j == geom->npe[c2]) ? 1 : j + 1;
	ci = geom->c2c[j][c];
	cn = geom->c2c[jj][c];
	if (!mask[c] || !mask[ci] || !mask[cn]) {
	  triangle* t = &d->triangles[n++];
	  t->vids[0] = geom->c2p[k+1][c2];
	  t->vids[1] = geom->c2p[k+1][geom->m2d[ci]];
	  t->vids[2] = geom->c2p[k+1][geom->m2d[cn]];
	  mask[c] = 1;
	  mask[ci] = 1;
	  mask[cn] = 1;
	}
      }
    }
  }

  /*-----------------------------------------------------------------*/
  /* Get the neighbours                                              */
  neighbour_finder_b(d, &nei);
  d->neighbours = malloc(d->ntriangles * sizeof(triangle_neighbours));
  for (i = 0; i < d->ntriangles; ++i) {
    triangle_neighbours* ne = &d->neighbours[i];
    ne->tids[0] = nei[0][i];
    ne->tids[1] = nei[1][i];
    ne->tids[2] = nei[2][i];
  }
  i_free_2d(nei);
  i_free_1d(mask);
  return d;
}

/*-------------------------------------------------------------------*/
