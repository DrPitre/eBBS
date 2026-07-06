
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

#include "common.h"
#include <unistd.h>
#include <ctype.h>
#if LACKS_MALLOC_H
# include <stdlib.h>
#else
# include <malloc.h>
#endif

void 
free_namelist (NAMELIST *list)
{
  NAMENODE *trav = *list, *next;
  while (trav) {
    free(trav->word);
    next = trav->next;
    free(trav);
    trav = next;
  }
}

void 
create_namelist (NAMELIST *list)
{
  free_namelist(list);
  *list = NULL;
}

int 
add_namelist (NAMELIST *list, char *name, char *name_before)
{
  NAMENODE *node;
  NAMENODE *trav = *list;
  node = (NAMENODE *)malloc(sizeof(NAMENODE));
  if (!node) return S_SYSERR;
  node->word = (char *)malloc(strlen(name)+1);
  node->next = NULL;
  if (!node->word) return S_SYSERR;
  strcpy(node->word, name);
  if (*list == NULL || 
    (name_before && !strcasecmp(name_before, trav->word))) {
    node->next = *list;
    *list = node;
  }
  else {            
    while (trav->next) {
      if (name_before && !strcasecmp(name_before, trav->next->word)) {
	node->next = trav->next;
	break;
      }
      trav = trav->next;
    }
    trav->next = node;
  }
  return 0;
}

int 
remove_namelist (NAMELIST *list, char *name)
{
  NAMENODE *prev = NULL;
  NAMENODE *curr = *list;
  while (curr) {
    if (!strcasecmp(curr->word, name)) {
      if (prev == NULL) *list = curr->next;
      else prev->next = curr->next;
      curr->next = NULL;
      free(curr->word);
      free(curr);            
      return S_OK;
    }
    prev = curr;
    curr = curr->next;
  }
  return S_NOSUCHREC;
}            

int 
is_in_namelist (NAMELIST list, char *name)
{
  while (list) {
    if (!strcasecmp(list->word, name))
      return 1;
    list = list->next;
  }
  return 0;
}
            
int
apply_namelist(NAMELIST list, int (*fptr)(int, char *, void *), void *arg)
{
  int indx = 0;
  for(; list!=NULL; list=list->next)
    (*fptr)(indx++, list->word, arg);
  return indx;
}

/*ARGSUSED*/
int
_write_list_element(int indx, char *name, void *fparg)
{
  FILE *fp = (FILE *)fparg;
  fprintf(fp, "%s\n", name);
  return S_OK;
}

int 
write_namelist (char *fname, NAMELIST list)
{
  FILE *fp;
  if (list == NULL) unlink(fname);
  else {
    if ((fp = fopen(fname, "w")) == NULL) {
      return S_SYSERR;
    }
    apply_namelist(list, _write_list_element, fp);
    fclose(fp);
  }
  return S_OK;
}

/*ARGSUSED*/
int
_read_list_element (int indx, char *rec, void *arg)
{
  NAMELIST *list = (NAMELIST *)arg;
  strip_trailing_space(rec);
  rec[NAMELEN] = '\0';
  if (!isspace(*rec) && !is_in_namelist(*list, rec)) {
    add_namelist(list, rec, NULL);
  }
  return S_OK;
}

int 
read_namelist (char *fname, NAMELIST *list)
{
  create_namelist(list);
  _record_enumerate(fname, 0, _read_list_element, list);
  return S_OK;
}



