/*
 *
 *  ENVIRONMENTAL MODELLING SUITE (EMS)
 *  
 *  File: model/hd/misc/messages.c
 *  
 *  Description:
 *  Handles the warn, quit and hd_quit_and_dump messages
 *  generated by meco.
 *  
 *  Copyright:
 *  Copyright (c) 2018. Commonwealth Scientific and Industrial
 *  Research Organisation (CSIRO). ABN 41 687 119 230. All rights
 *  reserved. See the license file for disclaimer and full
 *  use/redistribution conditions.
 *  
 *  $Id: messages.c 5845 2018-06-29 04:08:51Z riz008 $
 *
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hd.h"

void hd_silent_warn(const char *s, ...)
{
  va_list args;

  va_start(args, s);
  /* UR-ADDED */
  writelog(LTRACE,"",s,args);
  /* Absorb all error messages and move on. */
  va_end(args);
}

void hd_warn(const char *s, ...)
{
  va_list args;
  va_start(args, s);
  writelog(LWARN,"",s,args);
  va_end(args);
}

void hd_error(const char *s, ...)
{
  va_list args;
  va_start(args, s);
  writelog(LERROR,"",s,args);
  va_end(args);
}

void hd_quit(const char *s, ...)
{

  va_list args;
  va_start(args, s);
  writelog(LFATAL,"",s,args);
  va_end(args);

#if defined(HAVE_MPI) || defined(HAVE_MPI_2WAY)
  MPI_Finalize();
#endif

  /* exit with non-zero status so the shell can detect that something has
     gone wrong */
  exit(1);
}


void hd_quit_and_dump(const char *s, ...)
{
  va_list args;
  time_t now;

  va_start(args, s);
  writelog(LFATAL,"",s,args);
  va_end(args);

  if (crash_restart) {
    hd_data->master->crf = RS_RESTART;
    return;
  } else
    if (hd_data != NULL)
      hd_data->master->crf = RS_FAIL;

  if (model_running) {
    monitor(hd_data->master, hd_data->window, 3);
    now = time(NULL);
    emslog(LPANIC,"","Attempting to write dumps at %s\n", ctime(&now));
    sched_end(schedule);
  }

  
#if defined(HAVE_MPI) || defined(HAVE_MPI_2WAY)
  MPI_Finalize();
#endif

  /* exit with non-zero status so the shell can detect that something has
     gone wrong */
  exit(1);
}
