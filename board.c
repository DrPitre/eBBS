
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
#include <time.h>
#include <sys/stat.h>
#ifdef NeXT
# include <sys/stat.h>
#endif
 
#define BOARDFILE  "etc/boardlist"

#define MGR_FILE    "managers"

int 
get_managers_file (char *bname, char *buf)
{
  get_board_directory(bname, buf);
  strcat(buf, "/");
  strcat(buf, MGR_FILE);
  return S_OK;
}

int 
local_bbs_set_boardmgrs (char *bname, NAMELIST list)
{
  BOARD board;
  PATH buf;
  if (_lookup_board(bname, &board) != S_OK) return S_NOSUCHBOARD;
  get_managers_file(board.name, buf);
  return (write_namelist(buf, list));
}

int 
local_bbs_get_boardmgrs (char *bname, NAMELIST *list)
{
  BOARD board;
  PATH buf;
  if (_lookup_board(bname, &board) != S_OK) return S_NOSUCHBOARD;
  get_managers_file(board.name, buf);
  return (read_namelist(buf, list));
}

int 
_has_read_access (BOARD *board)
{
  if (board->readmask == 0) return 1;
  return (_has_perms(board->readmask));
}

int 
_has_post_access (BOARD *board)
{
  if (!_has_access(C_POST)) return 0;
  if (board->postmask == 0) return 1;
  return (_has_perms(board->postmask));
}

int 
_has_manager_access (BOARD *board)
{
  PATH mgrfile;
  if (_has_access(C_ALLBOARDMGR)) return 1;
  get_managers_file(board->name, mgrfile);
  if (_record_find(mgrfile, _match_full, my_userid(), NULL, NULL) == S_OK) 
    return 1;
  else return 0;
}  

int
format_boardent (char *rec, void *boardarg)
{
  BOARD *board = (BOARD *)boardarg;
  rec[0] = '\0';
  rec = _append_quoted(rec, board->name);
  rec = LONGcpy(rec, board->readmask);
  *rec++ = ':';
  rec = LONGcpy(rec, board->postmask);
  *rec++ = ':';
  rec = _append_quoted(rec, board->description);
  strcat(rec, "\n");
  return S_OK;
}

int
boardent_to_board (char *rec, void *boardarg)
{
  BOARD *board = (BOARD *)boardarg;
  rec = _extract_quoted(rec, board->name, sizeof(board->name));
  board->readmask = hex2LONG(rec);
  rec+=9;
  board->postmask = hex2LONG(rec);
  rec+=9;
  rec = _extract_quoted(rec, board->description, sizeof(board->description));
  return S_OK;
}

int 
hide_priv_board_fields (BOARD *board)
{
  if (!_has_access(C_SEEALLBINFO)) {
    board->readmask = 0;
    board->postmask = 0;
  }
  return S_OK;
}

int 
_lookup_board (char *bname, BOARD *board)
{
  int rc;
  memset(board, 0, sizeof *board);
  rc = _record_find(BOARDFILE, _match_first, bname, boardent_to_board, board);
  return rc;
}

int 
local_bbs_add_board (BOARD *newboard)
{
  int rc;
  BOARD board;
  PATH homedir;

  memcpy(&board, newboard, sizeof board);
  if (!is_valid_boardname(board.name)) return S_BADBOARDNAME;

  rc = _record_add(BOARDFILE,_match_first,board.name,format_boardent,&board);
  if (rc != S_OK) return S_BOARDEXISTS; 

  get_board_directory(board.name, homedir);
  recursive_rmdir (homedir);
  if (mkdir(homedir, 0700)) {
    _record_delete(BOARDFILE, _match_first, board.name);
    bbslog(0, "ERROR local_bbs_add_board: mkdir failed: %s\n", homedir);
    return S_SYSERR;
  }

  bbslog(2, "ADDBOARD %s by %s\n", board.name, my_userid());
  return S_OK;
}

int 
local_bbs_delete_board (char *bname)
{
  int rc;
  BOARD board;
  PATH homedir;

  rc = _lookup_board(bname, &board);
  if (rc != S_OK) return S_NOSUCHBOARD;

  rc = _record_delete(BOARDFILE, _match_first, board.name);
  if (rc != S_OK) return S_SYSERR;

  get_board_directory(board.name, homedir);
  recursive_rmdir(homedir);
  _acct_enum_fix_readbits(board.name, NULL);

  bbslog(2, "DELETEBOARD %s by %s\n", board.name, my_userid());
  return S_OK;
}

int 
local_bbs_get_board (char *bname, BOARD *board)
{
  int rc;
  BOARD myboard;
  SHORT order;
  rc = _lookup_board(bname, &myboard);
  if (rc != S_OK) return S_NOSUCHBOARD;
  if (!_has_read_access(&myboard)) return S_NOSUCHBOARD;
  get_read_order(myboard.name, &order);
  if (order & READ_ORDER_ZAPPED) myboard.flags |= BOARD_ZAPPED;
  hide_priv_board_fields(&myboard);
  memcpy(board, &myboard, sizeof(*board));
  return S_OK;
}

/*ARGSUSED*/
int
update_boardent (char *newrec, char *oldrec, void *board)
{
  format_boardent(newrec, board);
  return S_OK;
}

int 
_set_board (char *bname, BOARD *newboard, SHORT flags)
{
  int rc;
  BOARD board;
  NAME saveid;
    
  rc = _lookup_board(bname, &board);
  if (rc != S_OK) return S_NOSUCHBOARD;

  strcpy(saveid, board.name);

  if (flags & MOD_BNAME) {
    if (!is_valid_boardname(newboard->name)) return S_BADBOARDNAME;
    rc = _record_find(BOARDFILE, _match_first, newboard->name, NULL, NULL);
    if (rc != S_NOSUCHREC) return S_BOARDEXISTS;
    strncpy(board.name, newboard->name, NAMELEN);
  }

  if (flags & MOD_READMASK)
    board.readmask = newboard->readmask;

  if (flags & MOD_POSTMASK)
    board.postmask = newboard->postmask;

  if (flags & MOD_BOARDDESC)
    strncpy(board.description, newboard->description, TITLELEN);
    
  rc = _record_replace(BOARDFILE,_match_first,saveid,update_boardent,&board);

  if (rc != S_OK) return S_SYSERR;

  if (flags & MOD_BNAME) {
    PATH oldhome, newhome;
    get_board_directory(saveid, oldhome);
    get_board_directory(board.name, newhome);
    recursive_rmdir(newhome);
    rename(oldhome, newhome);
    _acct_enum_fix_readbits(saveid, board.name);
  }

  return S_OK;
}

int 
local_bbs_modify_board (char *bname, BOARD *board, SHORT flags)
{
  if (flags & MOD_BNAME)
    bbslog(2, "MODIFYBOARD %s to %s by %s\n", bname, board->name, my_userid());
  else bbslog(2, "MODIFYBOARD %s by %s\n", bname, my_userid());

  return(_set_board(bname, board, flags));
}

int
_enum_boards(int indx, char *rec, void *enarg)
{
  struct enumstruct *en = (struct enumstruct *)enarg;
  BOARD board;
  SHORT order;
  int zapped;
  memset(&board, '\0', sizeof board);
  boardent_to_board(rec, &board);
  if (!_has_read_access(&board)) return S_OK;

  get_read_order(board.name, &order);

  if (order == READ_ORDER_ZAPPED) {
    if (!(en->flags & BE_ZAPPED)) return S_OK;
    board.flags |= BOARD_ZAPPED;
  }  
  else if (!(en->flags & BE_UNZAPPED)) return S_OK;

  if (en->flags & BE_DO_COUNTS) {
    READINFO rinfo;
    get_bitfile_ent(board.name, &rinfo);
    _board_count(&board, &rinfo);
  }      
  
  hide_priv_board_fields(&board);
  if (en->fn(indx, &board, en->arg) == ENUM_QUIT) return ENUM_QUIT;
  return S_OK;
}

/*ARGSUSED*/
int
local_bbs_enum_boards(SHORT chunk, SHORT startrec, SHORT flags, int (*enumfn)(int, BOARD *, void *), void *arg)
{
  struct enumstruct en;
  en.fn = (int (*)(int, void *, void *))enumfn;
  en.arg = arg;
  en.flags = flags;
  _record_enumerate(BOARDFILE, startrec, _enum_boards, &en);
  return S_OK;
}

int
_fill_boardnames (int indx, char *rec, void *lcarg)
{
  struct listcomplete *lc = (struct listcomplete *)lcarg;
  BOARD board;
  boardent_to_board(rec, &board);
  if (_has_read_access(&board)) {
    if (!strncasecmp(board.name, lc->str, strlen(lc->str)))
      add_namelist(lc->listp, board.name, NULL);
  }
  return S_OK;
}

int 
local_bbs_boardnames (NAMELIST *list, char *complete)
{
  struct listcomplete lc;
  create_namelist(list);
  lc.listp = list;
  lc.str = complete == NULL ? "" : complete;
  _record_enumerate(BOARDFILE, 0, _fill_boardnames, &lc);  
  return S_OK;
}

/*ARGSUSED*/
int 
_visit_all_boards (int indx, char *rec, void *arg)
{
  BOARD board;
  boardent_to_board(rec, &board);
  if (_has_read_access(&board))
    _mark_all_as_read(board.name);

  return S_OK;
}

int 
local_bbs_visit_board (char *bname)
{
  int rc;
  BOARD board;
  if (!strcmp(bname, "*")) {
    _record_enumerate(BOARDFILE, 0, _visit_all_boards, NULL);
  }
  else {
    rc = _lookup_board(bname, &board);
    if (rc != S_OK) return S_NOSUCHBOARD;
    if (!_has_read_access(&board)) return S_NOSUCHBOARD;
    _mark_all_as_read(board.name);
  }
  return S_OK;
}

#if FULL_USER_DELETE
/* 
   For changing the board manager files when a user changes id
   or gets deleted.
*/

/*ARGSUSED*/
int 
_do_fix_manager (int indx, char *rec, struct namechange *ncs)
{
  BOARD board;
  PATH mgrfile;
  boardent_to_board(rec, &board);
  get_managers_file(board.name, mgrfile);

  if (ncs->newname == NULL)
    _record_delete(mgrfile, _match_full, ncs->oldname);
  else _record_replace(mgrfile, _match_full, ncs->oldname,
                       _change_name, ncs->newname);
  return S_OK;
}

int 
_board_enum_fix_managers (char *oldname, char *newname)
{
  struct namechange ncs;
  ncs.oldname = oldname;
  ncs.newname = newname;
  _record_enumerate(BOARDFILE, 0, _do_fix_manager, &ncs);
  return S_OK;
}
#endif
