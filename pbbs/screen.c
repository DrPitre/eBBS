/*
    Pirate Bulletin Board System
    Copyright (C) 1990, Edward Luke, lush@Athena.EE.MsState.EDU
    Eagles Bulletin Board System
    Copyright (C) 1992, Raymond Rocker, rocker@datasync.com
                        Guy Vega, gtvega@seabass.st.usm.edu
                        Dominic Tynes, dbtynes@seabass.st.usm.edu

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 1, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "osdeps.h"
#include "io.h"
#include "screen.h"
#if WANTS_VARARGS_H
# include <varargs.h>
#else
# include <stdarg.h>
int prints(char *fmt, ...);
#endif
#if LACKS_MALLOC_H
# include <stdlib.h>
#else
# include <malloc.h>
#endif

extern char clearbuf[] ;
extern char cleolbuf[] ;
extern char scrollrev[] ;
extern char strtstandout[] ;
extern char endstandout[] ;
extern int clearbuflen ;
extern int cleolbuflen ;
extern int scrollrevlen ;
extern int strtstandoutlen ;
extern int endstandoutlen ;
#if COLOR
extern char strtcolor[] ;
extern char endcolor[] ;
extern int strtcolorlen ;
extern int endcolorlen ;
#endif

extern int automargins ;

#define o_clear()     output(clearbuf,clearbuflen)
#define o_cleol()     output(cleolbuf,cleolbuflen) 
#define o_scrollrev() output(scrollrev,scrollrevlen)
#define o_standup()   output(strtstandout,strtstandoutlen)
#define o_standdown() output(endstandout,endstandoutlen)

int scrint = 0 ;
unsigned char scr_lns, scr_cols ;
unsigned char cur_ln = 0, cur_col = 0 ;
unsigned char roll ;
int scrollcnt ;
unsigned char docls ;
unsigned char downfrom ;
int standing = 0 ;
#if COLOR
int coloring = 0;
#endif

struct screenline *big_picture = NULL ;

void init_screen(int slns, int scols)
{
    scr_lns = slns ;
    scr_cols = MIN(scols,LINELEN) ;
    big_picture = (struct screenline *) calloc(scr_lns,
                                               sizeof(struct screenline)) ;
    for(slns=0;slns<scr_lns;slns++) {
        big_picture[slns].mode = 0 ;
        big_picture[slns].len = 0 ;
        big_picture[slns].oldlen = 0 ;
      }
    scrint = 1;
    docls = 1 ;
    downfrom = 0 ;
    roll = 0 ;
}

void initscr(void)
{
    if(!dumb_term && !big_picture)
      init_screen(t_lines,t_columns) ;
}

int fullscreen(void)
{
    return (scrint);
}

int tc_col, tc_line ;

void rel_move(int was_col, int was_ln, int new_col, int new_ln)
{
    extern void ochar(unsigned char);
#ifndef HPUX_TERMCAP
    extern char *BC ;
#endif

    if(new_ln >= t_lines  || new_col >= t_columns)
      return ;
    tc_col = new_col ;
    tc_line = new_ln ;
    if((new_col == 0) && (new_ln == was_ln+1)) {
        ochar('\n') ;
        if(was_col != 0)
          ochar('\r') ;
        return ;
    }
    if((new_col == 0) && (new_ln == was_ln)) {
        if(was_col != 0)
          ochar('\r') ;
        return ;
    }
    if(was_col == new_col && was_ln == new_ln)
      return ;
    if(new_col == was_col - 1 && new_ln == was_ln) {
#ifndef HPUX_TERMCAP
        if(BC)
          tputs(BC,1,ochar) ;
        else
#endif
          ochar(CTRL('H'));
        return ;
    }

    do_move(new_col,new_ln,ochar) ;
}


void standoutput(char *buf, int ds, int de, int sso, int eso)
{
    int st_start, st_end ;

    if(eso <= ds || sso >= de) {
        output(buf+ds,de-ds) ;
        return ;
    }
    st_start = MAX(sso,ds) ;
    st_end = MIN(eso,de) ;
    if(sso > ds)
      output(buf+ds,sso-ds) ;
    o_standup() ;
    output(buf+st_start,st_end-st_start) ;
    o_standdown() ;
    if(de > eso)
      output(buf+eso,de-eso) ;
}

#if COLOR
/* Yeah, it's ripped off from the above function */
void coloroutput(char *buf, int ds, int de, int sco, int eco, int colnum)
{
    int st_start, st_end ;
    char strtcolbuf[80];

    if(eco <= ds || sco >= de) {
        output(buf+ds,de-ds) ;
        return ;
    }
    st_start = MAX(sco,ds) ;
    st_end = MIN(eco,de) ;
    if(sco > ds)
      output(buf+ds,sco-ds) ;
    /* strtcolor better have a %d in it, and only one */
    sprintf(strtcolbuf, strtcolor, colnum);
    output(strtcolbuf, strlen(strtcolbuf));
    output(buf+st_start,st_end-st_start) ;
    output(endcolor, endcolorlen);
    if(de > eco)
      output(buf+eco,de-eco) ;
}
#endif

void redoscr(void)
{
    register int i,j ;
    register struct screenline *bp = big_picture ;

    if(dumb_term)
      return ;
    o_clear() ;
    tc_col = 0 ;
    tc_line = 0 ;
    for(i=0;i<scr_lns;i++) {
        j = (i+roll)%scr_lns ;
        if(bp[j].len == 0)
            continue ;
        rel_move(tc_col,tc_line,0,i) ;
        if(bp[j].mode&STANDOUT)
          standoutput((char *)bp[j].data,0,bp[j].len,bp[j].sso,bp[j].eso) ;
#if COLOR
        /* Note how standout has precedence... */
        else if(bp[j].mode&COLORON)
          coloroutput((char *)bp[j].data,0,bp[j].len,bp[j].sco,bp[j].eco,bp[j].color) ;
#endif
        else
          output(bp[j].data,bp[j].len) ;
        tc_col+=bp[j].len ;
        if(tc_col >= t_columns) {
            if(!automargins) {
                tc_col -= t_columns ;
                tc_line++ ;
                if(tc_line >= t_lines)
                  tc_line = t_lines - 1 ;
            }  else
              tc_col = t_columns-1 ;
        }
        bp[j].mode &= ~(MODIFIED) ;
        bp[j].oldlen = bp[j].len ;
    }
    rel_move(tc_col,tc_line,cur_col,cur_ln) ;
    docls = 0 ;
    scrollcnt = 0 ;
    oflush() ;
}

void refresh(void)
{
    register int i,j ;
    register struct screenline *bp = big_picture ;
    extern int automargins ;
    extern int scrollrevlen ;
    extern void ochar(unsigned char) ;

    if(dumb_term)
      return ;
    if(num_in_buf() != 0)
      return ;
    if((docls) || (abs(scrollcnt) >= (scr_lns-3)) ) {
        redoscr() ;
        return ;
    }
    if(scrollcnt < 0) {
        if(!scrollrevlen) {
            redoscr() ;
            return ;
        }
        rel_move(tc_col,tc_line,0,0) ;
        while(scrollcnt < 0) {
            o_scrollrev() ;
            scrollcnt++ ;
        }
    }
    if(scrollcnt > 0) {
        rel_move(tc_col,tc_line,0,t_lines-1) ;
        while(scrollcnt > 0) {
            ochar('\n') ;
            scrollcnt-- ;
        }
    }
    for(i=0;i<scr_lns;i++) {
        j = (i+roll)%scr_lns ;
        if(bp[j].mode&MODIFIED && bp[j].smod < bp[j].len) {
            bp[j].mode &= ~(MODIFIED) ;
            if(bp[j].emod >= bp[j].len)
              bp[j].emod = bp[j].len - 1 ;
            rel_move(tc_col,tc_line,bp[j].smod,i) ;
            if(bp[j].mode&STANDOUT)
              standoutput((char *)bp[j].data,bp[j].smod,bp[j].emod+1,
                          bp[j].sso,bp[j].eso) ;
#if COLOR
            else if(bp[j].mode&COLORON)
              coloroutput((char *)bp[j].data,bp[j].smod,bp[j].emod+1,
                          bp[j].sco,bp[j].eco,bp[j].color) ;
#endif
            else
              output(&bp[j].data[bp[j].smod],bp[j].emod-bp[j].smod+1) ;
            tc_col = bp[j].emod+1 ;
            if(tc_col >= t_columns) {
                if(automargins) {
                    tc_col -= t_columns ;
                    tc_line++ ;
                    if(tc_line >= t_lines)
                      tc_line = t_lines - 1 ;
                } else
                  tc_col = t_columns-1 ;
            }
        }
        if(bp[j].oldlen > bp[j].len) {
            rel_move(tc_col,tc_line,bp[j].len,i) ;
            o_cleol() ;
        }
        bp[j].oldlen = bp[j].len ;
    }
    rel_move(tc_col,tc_line,cur_col,cur_ln) ;
    oflush() ;
}

void clear(void)
{
    register int i ;
    extern void move(int, int) ;

    if(dumb_term)
      return ;
    roll = 0 ;
    docls = 1 ;
    downfrom = 0 ;
    for(i=0 ;i<scr_lns;i++) {
        big_picture[i].mode = 0 ;
        big_picture[i].len = 0 ;
        big_picture[i].oldlen = 0 ;
    }
    move(0,0) ;
}

void clrtoeol(void)
{
    register struct screenline *slp ;

    if(dumb_term)
      return ;
    standing = 0 ;
#if COLOR
    coloring = 0 ;
#endif    
    slp = &big_picture[((cur_ln+roll)%scr_lns)] ;
    if(cur_col <= slp->sso)
      slp->mode &= ~STANDOUT ;
#if COLOR
    if(cur_col <= slp->sco)
      slp->mode &= ~COLORON ;
#endif
    if(cur_col > slp->oldlen) {
        register int i ;
        for(i=slp->len;i<=cur_col;i++)
          slp->data[i] = ' ' ;
    }
    slp->len = cur_col ;
}

void clrtobot(void)
{
    register int i ;

    if(dumb_term)
      return ;
    for(i=cur_ln; i<scr_lns;i++) {
        big_picture[(i+roll)%scr_lns].mode = 0 ;
        big_picture[(i+roll)%scr_lns].len = 0 ;
        if(big_picture[(i+roll)%scr_lns].oldlen > 0)
          big_picture[(i+roll)%scr_lns].oldlen = 255 ;
    }
}

void clrstandout(void)
{
    register int i ;
    if(dumb_term)
      return ;
    for(i=0;i<scr_lns;i++)
      big_picture[i].mode &= ~(STANDOUT) ;
}

static char nullstr[] = "(null)" ;

void move(int y, int x)
{
    cur_col = x ;
    cur_ln = y ;
}

void getyx(int *y, int *x)
{
    *y = cur_ln ;
    *x = cur_col ;
}

void outc(unsigned char c)
{
    register struct screenline *slp ;
    extern void ochar(unsigned char) ;

#ifdef DO_ISTRIP
    c &= 0x7f ;
#endif
    if(dumb_term) {
        if(!isprint(c)) {
            if(c == '\n') {
                ochar('\r') ;
                ochar('\n') ;
                return ;
            }
            ochar('*') ;
            return ;
        }
        ochar(c) ;
        return ;
    }
    slp = &big_picture[((cur_ln+roll)%scr_lns)] ;
    if(!isprint(c)) {  /* deal with non-printables */
        if(c == '\n' || c == '\r') {  /* do the newline thing */
            if(standing) {
                slp->eso = MAX(slp->eso,cur_col) ;
                standing = 0 ;
            }
#if COLOR
            if(coloring) {
                slp->eco = MAX(slp->eco,cur_col) ;
                coloring = 0 ;
            }
#endif
            if(cur_col > slp->len) {
                register int i ;
                for(i=slp->len;i<=cur_col;i++)
                  slp->data[i] = ' ' ;
            }
            slp->len = cur_col ;
            cur_col = 0 ;
            if(cur_ln < scr_lns)
              cur_ln++ ;
            return ;
        }
#if COLOR
        if (c != '\033') /* let ESC through for ansi color */
#endif
          c = '*' ;  /* else substitute a '*' for non-printable */
    }
    if(cur_col >= slp->len) {
        register int i ;
        for(i=slp->len;i<cur_col;i++)
          slp->data[i] = ' ';
        slp->data[cur_col] = '\0' ;
        slp->len = cur_col+1 ;
    }
    if(slp->data[cur_col] != c) {
        if((slp->mode & MODIFIED) != MODIFIED)
          slp->smod = (slp->emod = cur_col) ;
        slp->mode |= MODIFIED ;
        if(cur_col > slp->emod)
          slp->emod = cur_col ;
        if(cur_col < slp->smod)
          slp->smod = cur_col ;
    }
    slp->data[cur_col] = c ;
    cur_col++ ;
    if(cur_col >= scr_cols) {
        if(standing && slp->mode&STANDOUT) {
            standing = 0 ;
            slp->eso = MAX(slp->eso,cur_col) ;
        }
#if COLOR
        if(coloring && slp->mode&COLORON) {
            coloring = 0 ;
            slp->eco = MAX(slp->eco,cur_col) ;
        }
#endif
        cur_col = 0 ;
        if(cur_ln < scr_lns)
          cur_ln++ ;
    }
}

void outs(register char *str)
{
	while(*str != '\0')
		outc(*str++) ;
}

void outns(register char *str, register int n)
{
    for(;n>0;n--)
      outc(*str++) ;
}


void addch(int ch)
{
    outc(ch) ;
}

void scroll(void)
{
    if(dumb_term) {
        prints("\n") ;
        return ;
    }
    scrollcnt++ ;
    roll = (roll+1)%scr_lns ;  /* subtract one from roll mod scr_lns*/
    move(scr_lns-1,0) ;
    clrtoeol() ;
}

void rscroll(void)
{
    if(dumb_term) {
        prints("\n\n") ;
        return ;
    }
    scrollcnt-- ;
    roll = (roll+(scr_lns-1))%scr_lns ;
    move(0,0) ;
    clrtoeol() ;
}

void standout(void)
{
    register struct screenline *slp ;

    if(dumb_term  || !strtstandoutlen)
      return ;
    if(!standing) {
        slp = &big_picture[((cur_ln+roll)%scr_lns)] ;
        standing = 1 ;
        slp->sso = cur_col ;
        slp->eso = cur_col ;
        slp->mode |= STANDOUT ;
    }
}

void standend(void)
{
    register struct screenline *slp ;

    if(dumb_term || !strtstandoutlen)
      return ;
    if(standing) {
        slp = &big_picture[((cur_ln+roll)%scr_lns)] ;
        standing= 0 ;
        slp->eso = MAX(slp->eso,cur_col) ;
    }
}

#if COLOR
void colorstart(int colnum)
{
    register struct screenline *slp ;

    if(dumb_term || !strtcolorlen)
      return ;
    if(!coloring) {
        slp = &big_picture[((cur_ln+roll)%scr_lns)] ;
        coloring = 1 ;
        slp->sco = cur_col ;
        slp->eco = cur_col ;
        slp->color = colnum ;
        slp->mode |= COLORON ;
    }
}

void colorend(void)
{
    register struct screenline *slp ;

    if(dumb_term || !strtcolorlen)
      return ;
    if(coloring) {
        slp = &big_picture[((cur_ln+roll)%scr_lns)] ;
        coloring = 0 ;
        slp->eco = MAX(slp->eco,cur_col) ;
    }
}
#endif

int dec[] = {1000000000,100000000,10000000,1000000,100000,10000,1000,100,10,1} ;
 
#if WANTS_VARARGS_H
int prints(va_alist)
va_dcl
#else
int prints(char *fmt, ...)
#endif
{
	va_list ap ;
#if WANTS_VARARGS_H
	register char *fmt ;
#endif
    char *bp ;
	register int i, count, hd, indx ;

#if WANTS_VARARGS_H
	va_start(ap) ;
	fmt = va_arg(ap, char *) ;
#else
	va_start(ap, fmt);
#endif

	while(*fmt != '\0')
      {
          if(*fmt == '%')
            {
                int sgn = 1 ;
                int val = 0 ;
                int len,negi ;

                fmt++ ;
                while(*fmt == '-') {
                    sgn *= -1 ;
                    fmt++ ;
                }
                while(isdigit(*fmt)) {
                    val *= 10 ;
                    val += *fmt - '0' ;
                    fmt++ ;
                }
                switch(*fmt)
                  {
                    case 's':
                      bp = va_arg(ap, char *) ;
                      if(bp == NULL)
                        bp = nullstr ;
                      if(val) {
                          register int slen = strlen(bp) ;
                          if(val <= slen)
                            outns(bp,val) ;
                          else if(sgn > 0) {
                              for(slen=val-slen;slen > 0; slen--)
                                outc(' ') ;
                              outs(bp) ;
                          } else {
                              outs(bp) ;
                              for(slen=val-slen;slen > 0; slen--)
                                outc(' ') ;
                          }
                      } else outs(bp) ;
                      break ;
                    case 'd':
                      i = va_arg(ap, int) ;

                      negi = 0 ;
                      if(i < 0)
                        {
                            negi = 1 ;
                            i *= -1 ;
                        }
                      for(indx=0;indx < 10;indx++)
                        if(i >= dec[indx])
                          break ;
                      if(i == 0)
                        len = 1 ;
                      else
                        len = 10 - indx ;
                      if(negi)
                        len++ ;
                      if(val >= len && sgn > 0) {
                          register int slen ;
                          for(slen = val-len;slen>0;slen--)
                            outc(' ') ;
                      }
                      if(negi)
                        outc('-') ;
                      hd = 1, indx = 0;
                      while(indx < 10)
                        {
                            count = 0 ;
                            while(i >= dec[indx])
                              {
                                  count++ ;
                                  i -= dec[indx] ;
                              }
                            indx++ ;
                            if(indx == 10)
                              hd = 0 ;
                            if(hd && !count)
                              continue ;
                            hd = 0 ;
                            outc('0'+count) ;
                        }
                      if(val >= len && sgn < 0) {
                          register int slen ;
                          for(slen = val-len;slen>0;slen--)
                            outc(' ') ;
                      }
                      break ;
                    case 'c':
                      i = va_arg(ap, int) ;
                      outc(i) ;
                      break ;
                    case '\0':
                      goto endprint ;
                    default:
                      outc(*fmt) ;
                      break ;
                  }
                fmt++ ;
                continue ;
            }
          
          outc(*fmt) ;
          fmt++ ;
      }
  endprint:
    
	return(0) ;
}
