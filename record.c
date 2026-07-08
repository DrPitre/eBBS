
/*
Eagles Bulletin Board System
Copyright (C) 1995, Ray Rocker, rocker@datasync.com

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#include "server.h"
#include <ctype.h>
#include <unistd.h>

#ifdef NO_FLOCK
# define LOCK(fp)   lockf(fileno(fp), F_LOCK, 0)
# define UNLOCK(fp) lockf(fileno(fp), F_ULOCK, 0)
#else
# include <sys/file.h>
# define LOCK(fp)   flock(fileno(fp), LOCK_EX)
# define UNLOCK(fp) flock(fileno(fp), LOCK_UN)
#endif

#define IS_COMMENT(s) (*s == '#' || isspace(*s))

#define BBS_MAX_RECORD 4096

int _match_first(char *rec, char *fld)
{
  register char c1, c2;
  while (*fld) {
    c1 = *fld++;
    c2 = *rec++;
    if (toupper(c1) != toupper(c2)) return S_OK;
  }
  return ((*rec == ' ' || *rec == ':') ? S_RECEXISTS : S_OK);
}

int _match_full(char *rec, char *fld)
{
  register char c1, c2;
  while (*fld) {
    c1 = *fld++;
    c2 = *rec++;
    if (toupper(c1) != toupper(c2)) return S_OK;
  }
  return ((*rec == '\n' || *rec == '\0') ? S_RECEXISTS : S_OK);
}

/*ARGSUSED*/
int _change_name(char *newrec, char *oldrec, char *newname)
{
  sprintf(newrec, "%s\n", newname);
  return S_OK;
}

char *
_append_quoted(char *rec, char *str)
{
  while (*str) {
    if (*str == ':' || *str == '\\') *rec++ = '\\';
    *rec++ = *str++;
  }
  *rec++ = ':';
  *rec = '\0';
  return rec;
}

char *
_extract_quoted(char *rec, char *str, int len)
{
  len--;    /* leave space for terminating null */
  while (*rec != ':' && *rec != '\n' && *rec != '\0') {
    if (*rec == '\\') rec++;
    if (len) {
      *str++ = *rec;
      len--;
    }
    rec++;
  }
  *str = '\0';
  if (*rec == ':') rec++;
  return rec;
}

int _record_add(char *fname, int (*testf)(char *, void *), void *targ,
                int (*formatf)(char *, void *), void *farg)
{
  FILE *fp;
  char rec[BBS_MAX_RECORD];
  int rc;

  if ((fp = fopen(fname, "r+")) == NULL) {
    if ((fp = fopen(fname, "w")) == NULL) {
      return S_SYSERR;
    }
  }

  LOCK(fp);

  if (testf) {
    while (fgets(rec, sizeof rec, fp)) {
      if (IS_COMMENT(rec)) continue;
      rc = testf(rec, targ);
      if (rc != S_OK) {
        UNLOCK(fp);
        fclose(fp);
        return rc;
      }
    }
  }

  rc = formatf(rec, farg);
  if (rc != S_OK) {
    UNLOCK(fp);
    fclose(fp);
    return rc;
  }

  fseek(fp, 0, SEEK_END);
  fputs(rec, fp);
  UNLOCK(fp);
  fclose(fp);
  return S_OK;
}

int _do_record_delete(char *fname, int (*testf)(char *, void *), void *targ,
                      int single)
{
  FILE *fp;
  char rec[BBS_MAX_RECORD];
  int rc, len;
  unsigned delsz = 0, savesz = 0;

  if ((fp = fopen(fname, "r+")) == NULL) {
    return S_NOSUCHREC;
  }

  LOCK(fp);

  while (fgets(rec, sizeof rec, fp)) {
    len = strlen(rec);

    if (IS_COMMENT(rec) || (single && delsz)) rc = S_OK;
    else rc = testf(rec, targ);

    if (rc == S_OK) {
      savesz += len;
      if (delsz) {
        fseek(fp, -(delsz+len), SEEK_CUR);
        fputs(rec, fp);
        fseek(fp, delsz, SEEK_CUR);
      }
    }
    else {
      delsz += len;
    }
  }

  if (savesz == 0) unlink(fname);
  else if (delsz) ftruncate(fileno(fp), savesz);
  UNLOCK(fp);
  fclose(fp);
  return (delsz ? S_OK : S_NOSUCHREC);
}

int _record_delete(char *fname, int (*testf)(char *, void *), void *targ)
{
  return (_do_record_delete(fname, testf, targ, 1));
}

int _record_delete_many(char *fname, int (*testf)(char *, void *), void *targ)
{
  return (_do_record_delete(fname, testf, targ, 0));
}

int _record_find(char *fname, int (*testf)(char *, void *), void *targ,
                 int (*formatf)(char *, void *), void *farg)
{
  FILE *fp;
  char rec[BBS_MAX_RECORD];
  int rc;

  if ((fp = fopen(fname, "r")) == NULL) {
    return S_NOSUCHREC;
  }

  while (fgets(rec, sizeof rec, fp)) {
    if (IS_COMMENT(rec)) continue;
    rc = testf(rec, targ);
    if (rc != S_OK) {
      if (formatf) rc = formatf(rec, farg);
      else rc = S_OK;
      fclose(fp);
      return rc;
    }
  }

  fclose(fp);
  return S_NOSUCHREC;
}

int _record_enumerate(char *fname, int start, int (*enumf)(int, char *, void *),
                      void *earg)
{
  FILE *fp;
  char rec[BBS_MAX_RECORD];
  int indx = 0;

  if ((fp = fopen(fname, "r")) == NULL) {
    return 0;
  }

  while (fgets(rec, sizeof rec, fp)) {
    if (IS_COMMENT(rec)) continue;
    if (indx >= start) {
      if (enumf(indx, rec, earg) == ENUM_QUIT) break;
    }
    indx++;
  }

  fclose(fp);
  return indx;
}

/* This is one UGLY function, but it's as neat as I can make it! */

int _record_replace(char *fname, int (*testf)(char *, void *), void *targ,
                    int (*replf)(char *, char *, void *), void *rarg)
{
  FILE *fp;
  char rec[BBS_MAX_RECORD];
  char newrec[BBS_MAX_RECORD];
  char scratch[BBS_MAX_RECORD];
  int rc = S_OK, len, newlen, difflen, chunk;
  int savesz = 0;

  if ((fp = fopen(fname, "r+")) == NULL) {
    return S_NOSUCHREC;
  }

  LOCK(fp);

  while (rc == S_OK && fgets(rec, sizeof rec, fp)) {
    len = strlen(rec);

    if (IS_COMMENT(rec)) rc = S_OK;
    else rc = testf(rec, targ);

    if (rc == S_OK) savesz += len;
  }

  if (rc == S_OK) {
    UNLOCK(fp);
    fclose(fp);
    return S_NOSUCHREC;
  }

  rc = replf(newrec, rec, rarg);
  if (rc != S_OK) {
    UNLOCK(fp);
    fclose(fp);
    return rc;
  }
  newlen = strlen(newrec);
  difflen = len - newlen;

  if (difflen) chunk = fread(rec, 1, sizeof rec, fp);
  else chunk = 0;

  fseek(fp, -(chunk+len), SEEK_CUR);
  fputs(newrec, fp);

  if (difflen) {
    savesz += newlen;
    while (chunk) {
      len = chunk+difflen;
      newlen = chunk;
      memcpy(scratch, rec, chunk);

      if (len > 0) {
        fseek(fp, len, SEEK_CUR);
        chunk = fread(rec, 1, sizeof rec, fp);
      }
      else chunk = len = 0;

      fseek(fp, -(chunk+len), SEEK_CUR);
      fwrite(scratch, 1, newlen, fp);
      savesz += newlen;
    }
    ftruncate(fileno(fp), savesz);
  }

  UNLOCK(fp);
  fclose(fp);
  return S_OK;
}
