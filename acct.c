
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
#include <sys/stat.h>

extern SERVERDATA server;

/* lastlogin file stuff */

#define LASTLOGIN      "lastlogin"

void get_lastlog_file(char *userid, char *buf)
{
  get_home_directory(userid, buf);
  strcat(buf, "/");
  strcat(buf, LASTLOGIN);
}

void get_lastlog_time(char *userid, time_t *tbuf)
{
  PATH lastlog;
  struct stat stbuf;
  *tbuf = 0;
  get_lastlog_file(userid, lastlog);
  if (stat(lastlog, &stbuf) == 0) {
    *tbuf = (LONG)stbuf.st_mtime;
  }
}

void get_lastlog_host(char *userid, char *host)
{
  PATH lastlog;
  FILE *fp;
  host[0] = '\0';
  get_lastlog_file(userid, lastlog);
  if ((fp = fopen(lastlog, "r"))) {
    fgets(host, HOSTLEN, fp);
    fclose(fp);
  }
}    

void set_lastlog(char *userid, char *host)
{
  PATH lastlog;
  FILE *fp;
  get_lastlog_file(userid, lastlog);
  if ((fp = fopen(lastlog, "w"))) {
    fputs(host, fp);
    fclose(fp);
  }
}    

/* 
   Profile. Bunch of optional information stored one per line, 
   XXXX=data, where XXXX is TERM, ADDR, NAME, MAIL, PROT.
*/

#define PROFILE       "profile"
#define PROFILE_TERM  "TERM"
#define PROFILE_CSET  "CSET"
#define PROFILE_NAME  "NAME"
#define PROFILE_ADDR  "ADDR"
#define PROFILE_MAIL  "MAIL"
#define PROFILE_PROT  "PROT"
#define PROFILE_EDIT  "EDIT"

#define DEFAULT_TERMINAL "vt100"
#define DEFAULT_EDITOR   "builtin"
#define DEFAULT_CHARSET  "ascii"

struct profile_data {
  char *key;
  char *data;
};

void get_profile_file(char *userid, char *buf)
{
  get_home_directory(userid, buf);
  strcat(buf, "/");
  strcat(buf, PROFILE);
}

int _match_profile_key(char *rec, char *key)
{
  if (!strncmp(rec, key, 4) && rec[4] == '=') return S_RECEXISTS;
  return S_OK;
}

int _profile_upd(char *newrec, char *oldrec, char *data)
{
  strncpy(newrec, oldrec, 5);
  strcpy(newrec+5, data);
  strcat(newrec, "\n");
  return S_OK;
}

int _profile_format(char *rec, struct profile_data *pd)
{
  sprintf(rec, "%s=%s\n", pd->key, pd->data);
  return S_OK;
}

int set_profile_data(char *userid, char *key, char *data)
{
  int rc;
  PATH profile;
  struct stat stbuf;
  get_profile_file(userid, profile);

  if (data[0] == '\0' || 
     (!strcmp(key, PROFILE_TERM) && !strcmp(data, DEFAULT_TERMINAL)) ||
     (!strcmp(key, PROFILE_CSET) && !strcmp(data, DEFAULT_CHARSET)) ||
     (!strcmp(key, PROFILE_EDIT) && !strcmp(data, DEFAULT_EDITOR))) {
    _record_delete(profile, _match_profile_key, key);
    return S_OK;
  }

  rc = _record_replace(profile, _match_profile_key, key, _profile_upd, data);
  if (rc != S_OK) {
    struct profile_data pd;
    pd.key = key;
    pd.data = data;
    rc = _record_add(profile, _match_profile_key, key, _profile_format, &pd);
  }
  return rc;
}

/*ARGSUSED*/
int _profile_fill(int indx, char *rec, ACCOUNT *acct)
{
  strip_trailing_space(rec);
  if (rec[4] != '=') return S_OK;
  rec[4] = '\0';
  if (!strcmp(rec, PROFILE_TERM)) 
    strncpy(acct->terminal, rec+5, TERMLEN);
  else if (!strcmp(rec, PROFILE_CSET))
    strncpy(acct->charset, rec+5, CSETLEN);
  else if (!strcmp(rec, PROFILE_NAME)) 
    strncpy(acct->realname, rec+5, RNAMELEN);
  else if (!strcmp(rec, PROFILE_ADDR)) 
    strncpy(acct->address, rec+5, ADDRLEN);
  else if (!strcmp(rec, PROFILE_MAIL)) 
    strncpy(acct->email, rec+5, MAILLEN);
  else if (!strcmp(rec, PROFILE_PROT)) 
    strncpy(acct->protocol, rec+5, NAMELEN);
  else if (!strcmp(rec, PROFILE_EDIT)) 
    strncpy(acct->editor, rec+5, NAMELEN);
  return S_OK;
}

int enum_profile_data(char *userid, ACCOUNT *acct)
{
  PATH profile;
  get_profile_file(userid, profile);
  _record_enumerate(profile, 0, _profile_fill, acct);
  if (acct->terminal[0] == '\0')
    strcpy(acct->terminal, DEFAULT_TERMINAL);
  if (acct->charset[0] == '\0')
    strcpy(acct->charset, DEFAULT_CHARSET);

  return S_OK;
}

int _fill_profile_name(char *rec, char *buf)
{
  strip_trailing_space(rec);
  strncpy(buf, rec+5, RNAMELEN);
  return S_OK;
}

int lookup_profile_name(char *userid, char *namebuf)
{
  PATH profile;
  int rc;
  get_profile_file(userid, profile);
  rc = _record_find(profile, _match_profile_key, PROFILE_NAME, 
                             _fill_profile_name, namebuf);
  return rc;
}  

/* Now for the passfile itself. */

#define PASSFILE       "etc/passwds"

#define PF_USERID_OFFSET      0
#define PF_PASSWD_OFFSET      (PF_USERID_OFFSET+NAMELEN+1)
#define PF_PERMS_OFFSET       (PF_PASSWD_OFFSET+PASSLEN+1)
#define PF_FLAGS_OFFSET       (PF_PERMS_OFFSET+9)
#define PF_USERNAME_OFFSET    (PF_FLAGS_OFFSET+5)
#define PF_END_OF_RECORD      (PF_USERNAME_OFFSET+UNAMELEN+1)

int format_passent(char *rec, ACCOUNT *acct)
{
  memset(rec, ' ', PF_USERNAME_OFFSET+UNAMELEN);
  memcpy(rec+PF_USERID_OFFSET, acct->userid, strlen(acct->userid));
  *(rec+PF_PASSWD_OFFSET-1) = ':';
  memcpy(rec+PF_PASSWD_OFFSET, acct->passwd, strlen(acct->passwd));
  *(rec+PF_PERMS_OFFSET-1) = ':';
  LONGcpy(rec+PF_PERMS_OFFSET, acct->perms);
  *(rec+PF_FLAGS_OFFSET-1) = ':';
  SHORTcpy(rec+PF_FLAGS_OFFSET, acct->flags);
  *(rec+PF_USERNAME_OFFSET-1) = ':';
  memcpy(rec+PF_USERNAME_OFFSET, acct->username, strlen(acct->username));
  memcpy(rec+PF_END_OF_RECORD-1, ":\n", 3);
  return S_OK;
}

int passent_to_account(char *rec, ACCOUNT *acct)
{
  acct->userid[NAMELEN] = '\0';
  acct->passwd[PASSLEN] = '\0';
  acct->username[UNAMELEN] = '\0';
  strncpy(acct->userid, rec+PF_USERID_OFFSET, NAMELEN);
  strip_trailing_space(acct->userid);
  strncpy(acct->passwd, rec+PF_PASSWD_OFFSET, PASSLEN);
  strip_trailing_space(acct->passwd);
  strncpy(acct->username, rec+PF_USERNAME_OFFSET, UNAMELEN);
  strip_trailing_space(acct->username);
  acct->perms = hex2LONG(rec+PF_PERMS_OFFSET);
  acct->flags = hex2SHORT(rec+PF_FLAGS_OFFSET);
  return S_OK;
}

void hide_priv_acct_fields(ACCOUNT *acct)
{
  int myself = is_me(acct->userid);
  if (!myself) memset(acct->passwd, '\0', PASSLEN);
  if (!_has_perms(PERM_SYSOP)) acct->perms = 0;
  if (!_has_access(C_SEEALLAINFO)) {
    if (myself) acct->flags &= ~FLG_EXEMPT;   /* hide only this flag */    
    else acct->flags = 0;                     /* hide all from others */
  }
}  

int _lookup_account(char *userid, ACCOUNT *acct)
{
  int rc;
  rc = _record_find(PASSFILE, _match_first, userid, passent_to_account, acct);
  return rc;
}

int local_bbs_add_account(ACCOUNT *newacct, SHORT is_encrypted)
{

  int rc;
  ACCOUNT acct;
  PATH homedir;
  PATH maildir;
  char *adder;

  memcpy(&acct, newacct, sizeof acct);
  if (!is_valid_userid(acct.userid)) return S_BADUSERID;
  if (!is_valid_password(acct.passwd)) return S_BADPASSWD;

  if (!is_encrypted) {
    encrypt_passwd(acct.passwd, newacct->passwd);
  }

  acct.flags = 0;
  acct.perms = PERM_DEFAULT;

  rc = _record_add(PASSFILE, _match_first, acct.userid, format_passent, &acct);
  if (rc != S_OK) return S_USEREXISTS; 

  get_home_directory(acct.userid, homedir);
  get_mail_directory(acct.userid, maildir);
  recursive_rmdir (homedir);
  if (mkdir(homedir, 0700) || mkdir(maildir, 0700)) {
    _record_delete(PASSFILE, _match_first, acct.userid);
    bbslog(0, "ERROR local_bbs_add_account: mkdir failed: %s or %s\n",
           homedir, maildir);
    return S_SYSERR;
  }

  if (acct.terminal[0]) 
    set_profile_data(acct.userid, PROFILE_TERM, acct.terminal);
  if (acct.charset[0])
    set_profile_data(acct.userid, PROFILE_CSET, acct.charset);
  if (acct.realname[0]) 
    set_profile_data(acct.userid, PROFILE_NAME, acct.realname);
  if (acct.address[0]) 
    set_profile_data(acct.userid, PROFILE_ADDR, acct.address);
  if (acct.email[0] && is_valid_address(acct.email)) 
    set_profile_data(acct.userid, PROFILE_MAIL, acct.email);
  if (acct.protocol[0]) 
    set_profile_data(acct.userid, PROFILE_PROT, acct.protocol);
  if (acct.editor[0]) 
    set_profile_data(acct.userid, PROFILE_EDIT, acct.editor);

  adder = my_userid();
  if (*adder == '\0')
    bbslog(1, "NEWACCT %s\n", acct.userid);
  else bbslog(1, "ADDACCT %s by %s\n", acct.userid, adder);

  return S_OK;
}

int local_bbs_delete_account(char *userid)
{
  int rc;
  ACCOUNT acct;
  PATH homedir;

  rc = _record_find(PASSFILE, _match_first, userid, passent_to_account, &acct);
  if (rc != S_OK) return S_NOSUCHUSER;

  if ((acct.perms & PERM_SYSOP) && !_has_perms(PERM_SYSOP)) {
    return S_DENIED;
  }

  rc = _record_delete(PASSFILE, _match_first, acct.userid);
  if (rc != S_OK) return S_SYSERR;

  get_home_directory(acct.userid, homedir);
  recursive_rmdir(homedir);
#if FULL_USER_DELETE
  _board_enum_fix_managers(acct.userid, NULL);
  _acct_enum_fix_overrides(acct.userid, NULL);
#endif

  bbslog(2, "DELETEACCT %s by %s\n", acct.userid, my_userid());
  return S_OK;
}

/*ARGSUSED*/
int update_passent(char *newrec, char *oldrec, ACCOUNT *acct)
{
  format_passent(newrec, acct);    
  return S_OK;
}

int _set_account(char *userid, ACCOUNT *newacct, SHORT flags)
{
  int rc;
  ACCOUNT acct;
  NAME saveid;
    
  rc = _record_find(PASSFILE, _match_first, userid, passent_to_account, &acct);
  if (rc != S_OK) return S_NOSUCHUSER;

  if ((acct.perms & PERM_SYSOP) && !_has_perms(PERM_SYSOP)) {
    return S_DENIED;
  }

  if (flags & MOD_USERID) {
    if (!is_valid_userid(newacct->userid)) return S_BADUSERID;
    rc = _record_find(PASSFILE, _match_first, newacct->userid, NULL, NULL);
    if (rc != S_NOSUCHREC) return S_USEREXISTS;
    strcpy(saveid, acct.userid);
    strncpy(acct.userid, newacct->userid, NAMELEN);
  }

  if (flags & MOD_PASSWD) {
    if (!is_valid_password(newacct->passwd)) return S_BADPASSWD;
    encrypt_passwd(acct.passwd, newacct->passwd);
  }

  if (flags & MOD_USERNAME)
    strncpy(acct.username, newacct->username, UNAMELEN);
    
  if (flags & _MOD_PERMS)
    acct.perms = newacct->perms;

  if (flags & _TOGGLE_FLAGS)
    acct.flags ^= newacct->flags;
    
  rc = _record_replace(PASSFILE, _match_first, userid, update_passent, &acct);

  if (rc != S_OK) return S_SYSERR;

  if (flags & MOD_USERID) {
    PATH oldhome, newhome;
    get_home_directory(saveid, oldhome);
    get_home_directory(acct.userid, newhome);
    recursive_rmdir(newhome);
    rename(oldhome, newhome);
#if FULL_USER_DELETE
    _board_enum_fix_managers(saveid, acct.userid);
    _acct_enum_fix_overrides(saveid, acct.userid);
#endif
  }

  if (flags & MOD_TERMINAL)
    set_profile_data(acct.userid, PROFILE_TERM, newacct->terminal);
  if (flags & MOD_CHARSET)
    set_profile_data(acct.userid, PROFILE_CSET, newacct->charset);
  if (flags & MOD_REALNAME) 
    set_profile_data(acct.userid, PROFILE_NAME, newacct->realname);
  if (flags & MOD_ADDRESS) 
    set_profile_data(acct.userid, PROFILE_ADDR, newacct->address);
  if ((flags & MOD_EMAIL) && is_valid_address(newacct->email)) 
    set_profile_data(acct.userid, PROFILE_MAIL, newacct->email);
  if (flags & MOD_PROTOCOL) 
    set_profile_data(acct.userid, PROFILE_PROT, newacct->protocol);
  if (flags & MOD_EDITOR) 
    set_profile_data(acct.userid, PROFILE_EDIT, newacct->editor);

  return S_OK;
}

int local_bbs_modify_account(char *userid, ACCOUNT *acct, SHORT flags)
{
  /* Clear the internal-only flags */
  flags &= MOD_ACCOUNT_MASK;

  if (flags & MOD_USERID)
    bbslog(2, "MODIFYACCT %s to %s by %s\n", 
           userid, acct->userid, my_userid());
  else bbslog(2, "MODIFYACCT %s by %s\n", userid, my_userid());

  return(_set_account(userid, acct, flags));
}

int local_bbs_modify_perms(char *userid, LONG perms)
{
  ACCOUNT acct;
  acct.perms = perms;
  bbslog(2, "SETPERMS %s to %08x by %s\n", userid, perms, my_userid());
  return (_set_account(userid, &acct, _MOD_PERMS));
}

int local_bbs_toggle_exempt(char *userid)
{
  ACCOUNT acct;
  acct.flags = FLG_EXEMPT;
  bbslog(2, "EXEMPT %s by %s\n", userid, my_userid());
  return (_set_account(userid, &acct, _TOGGLE_FLAGS));
}

int local_bbs_query(char *userid, ACCOUNT *acct)
{
  int rc;
  memset(acct, 0, sizeof(*acct));
  rc = _record_find(PASSFILE, _match_first, userid, passent_to_account, acct);
  if (rc != S_OK) return S_NOSUCHUSER;
  if (my_utable_slot() == -1) {
    /* Not logged in -- only allow test of userid's existence */
    memset(acct, 0, sizeof(*acct));
    return S_OK;
  }
  hide_priv_acct_fields(acct);
  get_lastlog_time(acct->userid, &acct->lastlogin);
  if (!server.hidehostname || _has_access(C_SEEALLAINFO) || 
      is_me(acct->userid)) {
    get_lastlog_host(acct->userid, acct->fromhost);
  }
  else {
    strcpy(acct->fromhost, "localhost");
  }
  if (server.queryreal && _has_access(C_SEEREALNAME)) {
    lookup_profile_name(acct->userid, acct->realname);    
  }
  return S_OK;
}

int local_bbs_get_userinfo(char *userid, ACCOUNT *acct)
{
  int rc;
  memset(acct, 0, sizeof(*acct));
  rc = _record_find(PASSFILE, _match_first, userid, passent_to_account, acct);
  if (rc != S_OK) return S_NOSUCHUSER;
  hide_priv_acct_fields(acct);
  get_lastlog_time(acct->userid, &acct->lastlogin);
  if (!server.hidehostname || _has_access(C_SEEALLAINFO) || 
      is_me(acct->userid)) {
    get_lastlog_host(acct->userid, acct->fromhost);
  }
  else {
    strcpy(acct->fromhost, "localhost");
  }
  enum_profile_data(acct->userid, acct);
  return S_OK;
}  

int _enum_accounts(int indx, char *rec, struct enumstruct *en)
{
  ACCOUNT acct;
  memset(&acct, 0, sizeof acct);
  passent_to_account(rec, &acct);
  get_lastlog_time(acct.userid, &acct.lastlogin);
  hide_priv_acct_fields(&acct);
  if (((int (*)(int, ACCOUNT *, void *))en->fn)(indx, &acct, en->arg) == ENUM_QUIT) return ENUM_QUIT;
  return S_OK;
}

/*ARGSUSED*/
int local_bbs_enum_accounts(SHORT chunk, SHORT startrec, int (*enumfn)(), void *arg)
{
  struct enumstruct en;
  en.fn = enumfn;
  en.arg = arg;
  _record_enumerate(PASSFILE, startrec, _enum_accounts, &en);
  return S_OK;
}

int _fill_acctnames(int indx, char *rec, struct listcomplete *lc)
{
  NAME userid;
  memcpy(userid, rec+PF_USERID_OFFSET, NAMELEN);
  userid[NAMELEN] = '\0';
  strip_trailing_space(userid);
  if (!strncasecmp(userid, lc->str, strlen(lc->str)))
    add_namelist(lc->listp, userid, NULL);

  return S_OK;
}

int local_bbs_acctnames(NAMELIST *list, char *complete)
{
  struct listcomplete lc;
  create_namelist(list);
  lc.listp = list;
  lc.str = complete == NULL ? "" : complete;
  _record_enumerate(PASSFILE, 0, _fill_acctnames, &lc);  
  return S_OK;
}

/* 
   Called from bbs_delete_board and bbs_modify_board:
   loop through each account and fix the readbits file.
   (jumping thru lots of hoops in the name of modularity!)
*/

extern int fix_readbit_entry();

int _acct_enum_fix_readbits(char *oldbname, char *newbname)
{
  struct namechange ncs;
  ncs.oldname = oldbname;
  ncs.newname = newbname;
  _record_enumerate(PASSFILE, 0, fix_readbit_entry, &ncs);
  return S_OK;
}

#if FULL_USER_DELETE
/* 
   Called from bbs_delete_account and bbs_modify_account;
   loop through all each user's override file and fix it for them.
   Aren't we nice?
*/

extern int fix_override_entry();

int _acct_enum_fix_overrides(char *oldname, char *newname)
{
  struct namechange ncs;
  ncs.oldname = oldname;
  ncs.newname = newname;
  _record_enumerate(PASSFILE, 0, fix_override_entry, &ncs);
  return S_OK;
}
#endif
