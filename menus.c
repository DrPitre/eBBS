
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

#include "client.h"
#include <ctype.h>

char permtab[MAX_CLNTCMDS];
char *currentboard = currboard;

int 
DoReadHelp (NREADMENU *m, int openflags)
{
  NREADMENUITEM *mi;
  for (mi = m->commlist; mi != NULL; mi = mi->next) {
    if (HasReadMenuPerm(mi->boardprivs, openflags) &&
        HasPerm(mi->mainprivs) && mi->help[0] != '\0')
      prints("%s\n", mi->help);
  }
  return 0;
}

int 
MovementReadHelp (void)
{
  standout();
  prints("Movement Commands\n");
  standend();
  prints("p               Previous Message\n");
  prints("n               Next Message\n");
  prints("P               Previous Page\n");
  prints("N               Next Page\n");
  prints("## <cr>         Goto Message ##\n");
  prints("$               Goto Last Message\n");
  return 0;
}

int 
MiscReadHelp (void)
{
  prints("CTRL-L          Redraw Screen\n");
  prints("e               EXIT Read Menu\n");
  return 0;
}

int 
ReadMenuHelp (NREADMENU *menu, int flags)
{
  clear();
  standout();
  prints("%s\n", menu->menu_helptitle);
  standend();
  move(2,0);
  MovementReadHelp();
  prints("\n");
  standout();
  prints("Miscellaneous Commands\n");
  standend();
  DoReadHelp(menu, flags);
  MiscReadHelp();
  pressreturn();
  clear();
  return FULLUPDATE;    
}

/*ARGSUSED*/
int 
MailReadHelp (void *hptr, LONG curr, LONG num, LONG flags)
{
  return (ReadMenuHelp(MailReadMenu, flags));
}

/*ARGSUSED*/
int 
MainReadHelp (void *hptr, LONG curr, LONG num, LONG flags)
{
  return (ReadMenuHelp(PostReadMenu, flags));
}

/*ARGSUSED*/
int 
FileReadHelp (void *hptr, LONG curr, LONG num, LONG flags)
{
  return (ReadMenuHelp(FileReadMenu, flags));
}

int 
NotImpl (void)
{
  clear();
  prints("This function is not yet implemented.\n");
  pressreturn();
  return FULLUPDATE;
}

int 
EndMenu (void)
{
  return EXITMENU;
}

int 
SetPermTable (void)
{
  memcpy(permtab, myinfo.access, MAX_CLNTCMDS);
  return 0;
}

int 
HasPerm (int perm)
{
  if (perm == 0) return 1;
  else if (perm < 0 || perm >= MAX_CLNTCMDS) return 0;
  else return (permtab[perm]-'0');
}

int 
HasReadMenuPerm (int perm, int flags)
{
  if (perm == 0) return 1;
  else return (perm & flags);
}

#define KEY_NORMAL (0)
#define KEY_ESCAPE (1)
#define KEY_VTKEYS (2)

int 
MenuGetch (void)
{
  char c;
  int keymode = KEY_NORMAL;
  while (1) {
    c = igetch();
    switch (keymode) {
    case KEY_NORMAL:
      if (isprint(c)) return c;
      if (c == '\r' || c == '\n' || c == '\010' || c == '\177') {
	c = '\n';
	return c;
      }
      if (c == CTRL('L')) {
        redoscr() ;  
      }
      if (c == 27) keymode = KEY_ESCAPE;   /* ESC */
      break;
    case KEY_ESCAPE:
      if (c == '[') keymode = KEY_VTKEYS;
      else {
	bell();
	keymode = KEY_NORMAL;
      }
      break;
    case KEY_VTKEYS:
      if (c == 'A') return MENU_UP;
      else if (c == 'B') return MENU_DOWN;
      else {
	bell();
	keymode = KEY_NORMAL;
      }
    }
  }
}

