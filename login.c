
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
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#ifdef NeXT
# include <sys/stat.h>
#endif

#ifdef HOST_ENV_VAR
# include <stdlib.h>
#else
# if USES_UTMPX
#  include <utmpx.h>
#  undef UTMP_FILE
#  define UTMP_FILE UTMPX_FILE
# else
#  include <utmp.h>
#  ifndef UTMP_FILE
#   define UTMP_FILE "/etc/utmp"
#  endif
# endif
#endif

#define LOGONFILE "etc/logons"

typedef struct _LOGONDATA {
  NAME userid;
  SHORT logonlimit;
} LOGONDATA;

USERDATA user_params;
int user_number = -1;

extern SERVERDATA server;
extern char mode_pageable[];

SHORT 
my_real_mode (void)
{
  return (user_params.u.mode & ~MODE_FLG_NOPAGE);
}

int 
set_real_mode (SHORT mode)
{
  utable_get_record(user_number, &user_params);
  switch (mode) {
  case 0: 
    /* Magic value: clear the non-pageable flag */
    user_params.u.mode &= ~MODE_FLG_NOPAGE;
    break;
  case 1:
    /* Magic value: set the non-pageable flag */
    user_params.u.mode |= MODE_FLG_NOPAGE;
    break;
  default:
    user_params.u.mode = mode;
    if (mode_pageable[mode] == 'N') user_params.u.mode |= MODE_FLG_NOPAGE;
  }
  utable_set_record(user_number, &user_params);
  return S_OK;
}  

int 
my_utable_slot (void)
{
  return user_number;
}

int 
_has_perms (LONG mask)
{
  return(user_params.perms & mask);
}

int 
is_me (char *userid)
{
  return(!strcasecmp(userid, user_params.u.userid));
}

char *
my_userid (void)
{
  return user_params.u.userid;
}

char *
my_username (void)
{
  return user_params.u.username;
}

char *
my_host (void)
{
  return user_params.u.fromhost;
}

int 
my_flag (SHORT flag)
{
  return (user_params.u.flags & flag);
}

int
get_remote_host(char *host)
{
#ifdef HOST_ENV_VAR
  char *ep = getenv(HOST_ENV_VAR);
  if (ep != NULL && *ep != '\0') {
    strncpy(host, ep, sizeof(HOST)-1);
    return S_OK;
  }
#else
  int fd;
#if USES_UTMPX
  struct utmpx ut;
#else
  struct utmp ut;
#endif /* USES_UTMPX */
  int uthostlen = sizeof ut.ut_host;
  char *tt = ttyname(0);
  if (uthostlen > HOSTLEN) uthostlen = HOSTLEN;
  if (tt == NULL) tt = "???";
  if (!strncmp(tt, "/dev/", 5)) tt+=5;
  if ((fd = open(UTMP_FILE, O_RDONLY)) != -1) {
    while (read(fd, &ut, sizeof ut) == sizeof ut) {
#ifdef DEAD_PROCESS
      if (!strcmp(tt, ut.ut_line) && ut.ut_type != DEAD_PROCESS) {
#else
      if (!strcmp(tt, ut.ut_line)) {
#endif /* DEAD_PROCESS */
        memset(host, 0, HOSTLEN);
        if (ut.ut_host[0] == '\0') strcpy(host, "localhost");
        else strncpy(host, ut.ut_host, uthostlen);
        close(fd);
        return S_OK;
      }
    }
    close(fd);
    strcpy(host, "localhost");
    return S_OK;
  }
#endif /* HOST_ENV_VAR */
  strcpy(host, "localhost");
  return S_SYSERR;
}

int
get_client_host(char *host)
{
  struct sockaddr_in sin;
  socklen_t sinsize = sizeof(sin);
  struct hostent *hent;
  if (getsockname(0, (struct sockaddr *)&sin, &sinsize) == -1) {
    strcpy(host, "unknown");
    return S_SYSERR;
  }
  hent = gethostbyaddr((char *)&sin.sin_addr.s_addr, 
                       sizeof(sin.sin_addr.s_addr), AF_INET);
  if (hent == NULL) {
    char *netaddr = inet_ntoa(sin.sin_addr);
    if (netaddr == NULL) strcpy(host, "unknown");
    else strncpy(host, netaddr, HOSTLEN);            
  }
  else {
    strncpy(host, hent->h_name, HOSTLEN);
  }
  return S_OK;
}

int
_logent_to_data(char *rec, void *ldataarg)
{
  LOGONDATA *ldata = (LOGONDATA *)ldataarg;
  rec =_extract_quoted(rec, ldata->userid, sizeof(ldata->userid));
  ldata->logonlimit = atoi(rec);
  return S_OK;
}

/*ARGSUSED*/
int
_count_logons(int indx, USERDATA *udata, void *ctrarg)
{
  int *ctr = (int *)ctrarg;
  (*ctr)++;
  return S_OK;
}

int
logon_limit_exceeded(char *userid)
{
  LOGONDATA ldata;
  int rc, online = 0;
  rc = _record_find(LOGONFILE, _match_first, userid, _logent_to_data, &ldata);
  if (rc != S_OK) ldata.logonlimit = server.maxlogons;
  if (ldata.logonlimit == 0) return 0;    /* 0 means unlimited */
  utable_enumerate(0, userid, _count_logons, &online);  
  if (online >= ldata.logonlimit) return 1;
  return 0;
}

/*ARGSUSED*/
int
_userid_to_pid(int indx, USERDATA *udata, void *pidarg)
{
  LONG *pid = (LONG *)pidarg;
  *pid = udata->u.pid;
  return ENUM_QUIT;
}

int
kick_previous_logon(char *userid)
{
  LONG pid = 0;
  utable_enumerate(0, userid, _userid_to_pid, &pid);
  if (pid > 0) {
    if (kill(pid, SIGTERM) == 0) return 0;
  }
  return -1;
}

int
local_bbs_login(char *userid, char *passwd, SHORT kick, LOGININFO *loginfo)
{
  ACCOUNT acct;
  OPENINFO oinfo;
  int usernum;
  
  if (user_number != -1) {
    /* Already logged in! */
    return S_LOGGEDIN;
  }

  if (user_params.u.fromhost[0] == '\0') {
    if (bbslib_user == BBSLIB_BBSD)
      get_client_host(user_params.u.fromhost);
    else
      get_remote_host(user_params.u.fromhost);
  }

  if (_lookup_account(userid, &acct) != S_OK) {
    bbslog(1, "BADUSERID %s from %s\n", userid, user_params.u.fromhost);
    return S_LOGINFAIL;
  }

  if (!is_passwd_good(acct.passwd, passwd)) {
    bbslog(1, "BADPASSWORD %s from %s\n", userid, user_params.u.fromhost);
    return S_LOGINFAIL;
  }

  if (acct.flags & FLG_DISABLED) {
    return S_ACCTDISABLED;
  }

  _determine_access(acct.perms, user_params.access);

  if (logon_limit_exceeded(acct.userid)) {
    if (acct.flags & FLG_SHARED) return S_LOGINLIMIT;
    if (kick) kick_previous_logon(acct.userid);
    else return S_LOGINMUSTBOOT;
  }

  if (utable_lock_record(&user_number) != S_OK) {
    return S_FULL;
  }

  strcpy(loginfo->userid, acct.userid);
  loginfo->flags = acct.flags & ~(FLG_EXEMPT | FLG_DISABLED);
  loginfo->idletimeout = _has_access(C_NOTIMEOUT) ? 0 : server.idletimeout;
  memcpy(loginfo->access, user_params.access, sizeof(loginfo->access));
  get_lastlog_time(acct.userid, &loginfo->lastlogin);
  get_lastlog_host(acct.userid, loginfo->fromhost);

  strcpy(user_params.u.userid, acct.userid);
  strcpy(user_params.u.username, acct.username);
  user_params.u.pid = getpid();
  user_params.u.flags = acct.flags;
  user_params.u.mode = M_UNDEFINED;
  user_params.usermode = M_UNDEFINED;
  user_params.perms = acct.perms;

  if (local_bbs_open_mailbox(&oinfo) == S_OK) {
    user_params.newmailmsgs = oinfo.newmsgs;
    local_bbs_close_board();
  }

  utable_set_record(user_number, &user_params);

  set_lastlog(user_params.u.userid, user_params.u.fromhost);

  bbslog(1, "LOGIN %s %s\n", user_params.u.userid, user_params.u.fromhost);
  return S_OK;
}

int
local_logout(void)
{
  if (user_number != -1) {
    utable_get_record(user_number, &user_params);

    if (bbslib_user == BBSLIB_DEFAULT) {
      if (user_params.u.mode == M_CHAT) {
        local_bbs_exit_chat();
      }
      else if (user_params.u.mode == M_TALK) {
        local_bbs_exit_talk();
      }
    }

    utable_free_record(user_number);
    bbslog(1, "LOGOUT %s\n", user_params.u.userid);
  }
  return S_OK;
}  

int
local_bbs_newlogin(ACCOUNT *acct, LOGININFO *loginfo)
{
  int rc;

  if (server.newok == 0) return S_DENIED;

  rc = local_bbs_add_account(acct, 0);
  if (rc != S_OK) return rc;

  rc = local_bbs_login(acct->userid, acct->passwd, 0, loginfo);
  return rc;
}

int
local_bbs_check_mail(void)
{
  utable_get_record(user_number, &user_params);
  return (user_params.newmailmsgs > 0 ? 1 : 0);  
}

int
local_bbs_set_mode(SHORT mode)
{
  if (mode > BBS_MAX_MODE) return S_MODEVIOLATION;
  utable_get_record(user_number, &user_params);
  switch (mode) {
  case 0:
    /* Magic value: clear the non-pageable flag */
    user_params.usermode &= ~MODE_FLG_NOPAGE;
    break;
  case 1:
    /* Magic value: set the non-pageable flag */
    user_params.usermode |= MODE_FLG_NOPAGE;
    break;
  default:
    user_params.usermode = mode;
    if (mode_pageable[mode] == 'N') user_params.usermode |= MODE_FLG_NOPAGE;
  }
  utable_set_record(user_number, &user_params);
  return S_OK;
}

int
local_bbs_set_passwd(char *passwd)
{
  ACCOUNT acct;
  strncpy(acct.passwd, passwd, PASSLEN);
  return (_set_account(user_params.u.userid, &acct, MOD_PASSWD));
}

int
local_bbs_set_username(char *uname)
{
  ACCOUNT acct;
  utable_get_record(user_number, &user_params);
  strncpy(user_params.u.username, uname, UNAMELEN);
  utable_set_record(user_number, &user_params);
  strncpy(acct.username, uname, UNAMELEN);
  return(_set_account(user_params.u.userid, &acct, MOD_USERNAME));
}

int
local_bbs_set_terminal(char *term)
{
  ACCOUNT acct;
  strncpy(acct.terminal, term, TERMLEN);
  return (_set_account(user_params.u.userid, &acct, MOD_TERMINAL));
}

int
local_bbs_set_charset(char *charset)
{
  ACCOUNT acct;
  strncpy(acct.charset, charset, CSETLEN);
  return (_set_account(user_params.u.userid, &acct, MOD_CHARSET));
}

int
local_bbs_set_email(char *email)
{
  ACCOUNT acct;
  if (!is_valid_address(email)) return S_BADADDRESS;
  strncpy(acct.email, email, MAILLEN);
  return (_set_account(user_params.u.userid, &acct, MOD_EMAIL));
}

int
local_bbs_set_protocol(char *protoname)
{
  ACCOUNT acct;
  strncpy(acct.protocol, protoname, NAMELEN);
  return (_set_account(user_params.u.userid, &acct, MOD_PROTOCOL));
}

int
local_bbs_set_editor(char *editor)
{
  ACCOUNT acct;
  strncpy(acct.editor, editor, NAMELEN);
  return (_set_account(user_params.u.userid, &acct, MOD_EDITOR));
}

int
local_bbs_owninfo(ACCOUNT *acct)
{
  return (local_bbs_get_userinfo(user_params.u.userid, acct));
}

int
local_bbs_toggle_cloak(void)
{
  ACCOUNT acct;
  int mode = my_real_mode();
  if (mode == M_TALK || mode == M_PAGE || mode == M_CHAT) {
    /* We don't allow cloak to be toggled in these modes */
    return(S_MODEVIOLATION);
  }
  utable_get_record(user_number, &user_params);
  user_params.u.flags ^= FLG_CLOAK;
  bbslog(2, "CLOAK %s %s\n", user_params.u.flags & FLG_CLOAK ? "ON" : "OFF",
         user_params.u.userid);
  utable_set_record(user_number, &user_params);
  acct.flags = FLG_CLOAK;
  return(_set_account(user_params.u.userid, &acct, _TOGGLE_FLAGS));
}

int
local_bbs_set_pager(SHORT pager, SHORT override)
{
  ACCOUNT acct;
  acct.flags = 0;
  pager = (pager ? 1 : 0);
  override = (override ? 1 : 0);
  utable_get_record(user_number, &user_params);
  if ((user_params.u.flags & FLG_NOPAGE ? 1 : 0) != pager)
    acct.flags |= FLG_NOPAGE;
  if ((user_params.u.flags & FLG_NOOVERRIDE ? 1 : 0) != override)
    acct.flags |= FLG_NOOVERRIDE;
  if (acct.flags == 0) return S_OK;
  user_params.u.flags ^= acct.flags;
  utable_set_record(user_number, &user_params);
  return(_set_account(user_params.u.userid, &acct, _TOGGLE_FLAGS));
}

int
local_bbs_set_cliopts(SHORT menulevel)
{
  /* At this time, only thing to set here is the local bbs menu style */
  ACCOUNT acct;
  int oldlevel;
  /* Shared accounts cannot save these settings. */
  if (my_flag(FLG_SHARED)) return S_OK;
  utable_get_record(user_number, &user_params);
  oldlevel = user_params.u.flags & FLG_EXPERT;
  if (oldlevel == menulevel) return S_OK;
  if (menulevel) user_params.u.flags |= FLG_EXPERT;
  else user_params.u.flags &= ~FLG_EXPERT;
  utable_set_record(user_number, &user_params);
  acct.flags = FLG_EXPERT;
  return(_set_account(user_params.u.userid, &acct, _TOGGLE_FLAGS));
}

int
local_bbs_set_plan(char *fname)
{
  PATH planfile;
  int rc = 0;
  local_bbs_get_plan(user_params.u.userid, planfile);
  if (fname == NULL) {
    unlink(planfile);
  }
  else if (strcmp(planfile, fname)) {
    rc = copy_file(fname, planfile, 0600, 0);
  }
  return (rc == 0 ? S_OK : S_SYSERR);
}

int
local_bbs_set_signature(char *fname)
{
  PATH sigfile;
  int rc = 0;
  local_bbs_get_signature(sigfile);
  if (fname == NULL) {
    unlink(sigfile);
  }
  else if (strcmp(sigfile, fname)) {
    rc = copy_file(fname, sigfile, 0600, 0);
  }
  return (rc == 0 ? S_OK : S_SYSERR);
}

int
_enum_users(int indx, USERDATA *info, void *enarg)
{
  struct enumstruct *en = (struct enumstruct *)enarg;
  int pageok = 1;
  if ((info->u.flags & FLG_CLOAK) && !_has_access(C_SEECLOAK))
    return S_OK;

  /* Assumption: real mode will never be M_UNDEFINED and unpageable */
  if (info->u.mode == M_UNDEFINED) info->u.mode = info->usermode;
  if (info->u.mode & MODE_FLG_NOPAGE) {
    info->u.mode &= ~MODE_FLG_NOPAGE;
    pageok = 0;
  }
  
  if (pageok) pageok = has_page_permission(info);
  info->u.flags &= ~(FLG_EXEMPT);
  info->u.flags &= ~(FLG_NOPAGE | FLG_NOOVERRIDE);
  if (!pageok) info->u.flags |= FLG_NOPAGE;
  if (en->fn(indx, &info->u, en->arg) == ENUM_QUIT) return ENUM_QUIT;
  return S_OK;
}

int
local_bbs_enum_users(SHORT chunk, SHORT startrec, char *userid, int (*enumfn)(int, USEREC *, void *), void *arg)
{
  struct enumstruct en;
  en.fn = (int (*)(int, void *, void *))enumfn;
  en.arg = arg;
  utable_enumerate(startrec, userid, _enum_users, &en);
  return S_OK;
}

/*ARGSUSED*/
int
_enum_user_names(int indx, USERDATA *info, void *lcarg)
{
  struct listcomplete *lc = (struct listcomplete *)lcarg;
  if ((info->u.flags & FLG_CLOAK) && !_has_access(C_SEECLOAK))
    return S_OK;

  if (!strncasecmp(info->u.userid, lc->str, strlen(lc->str)))
    if (!is_in_namelist(*(lc->listp), info->u.userid))
      add_namelist(lc->listp, info->u.userid, NULL);

  return S_OK;
}

int
local_bbs_usernames(NAMELIST *list, char *complete)
{
  struct listcomplete lc;
  create_namelist(list);
  lc.listp = list;
  lc.str = complete == NULL ? "" : complete;
  utable_enumerate(0, NULL, _enum_user_names, &lc);
  return S_OK;
}

int
local_bbs_kick_user(LONG pid)
{
  USERDATA udata;
  if (utable_find_record(pid, &udata) != S_OK) return S_NOSUCHUSER;
  if ((udata.perms & PERM_SYSOP) && !_has_perms(PERM_SYSOP)) {
    return S_DENIED;
  }
  bbslog(2, "KICK %s by %s\n", udata.u.userid, user_params.u.userid);
  if (kill(pid, SIGTERM) == -1) return S_SYSERR;
  else return S_OK;
}

/*ARGSUSED*/
int
_mail_adjust(int indx, USERDATA *info, void *adjvalarg)
{
  int *adjval = (int *)adjvalarg;
  info->newmailmsgs += *adjval;
  utable_set_record(indx, info);
  return S_OK;
}

int
notify_new_mail(char *userid, int adjust)
{
  utable_enumerate(0, userid, _mail_adjust, &adjust);
  return S_OK;
}
