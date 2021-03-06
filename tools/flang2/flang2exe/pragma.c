/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/** \file
 *  \brief PGC & PGFTN directive scan & semantic module
 */

#include "gbldefs.h"
#include "global.h"
#include "error.h"
#include "symtab.h"
#include "semant.h"

#include "direct.h"

#if defined(TARGET_X8664) && (!defined(FE90) || defined(PGF90))
#include "x86.h"
#endif

#if TARGET_OSX
extern void add_osx_init_fini(char *, int);
#endif

#if DEBUG
static char *xx[] = {"no", "loop", "routine", "na", "global"};
#define TR0(s)         \
  if (DBGBIT(1, 1024)) \
    fprintf(gbl.dbgfil, s);
#define TR1(s, a)      \
  if (DBGBIT(1, 1024)) \
    fprintf(gbl.dbgfil, s, a);
#define TR2(s, a, b)   \
  if (DBGBIT(1, 1024)) \
    fprintf(gbl.dbgfil, s, a, b);
#define TR3(s, a, b, c) \
  if (DBGBIT(1, 1024))  \
    fprintf(gbl.dbgfil, s, a, b, c);

#else
#define TR0(s)
#define TR1(s, a)
#define TR2(s, a, b)
#define TR3(s, a, b, c)

#endif

/* possible scope values, defined as bit masks */

#define S_NONE 0
#define S_LOOP 1
#define S_ROUTINE 2
#define S_GLOBAL 4

static int scope;              /* scope specified by pragma/directive */
static LOGICAL do_now = FALSE; /* routine/global directive must be stored
                                * in dirset.rou_begin immediately; must
                                * be in effect when main calls remaining
                                * compiler phases including expand.
                                */

static char *currp;
static int lineno;
static DIRSET *currdir;

#define DIR_OFFSET(x, y) (&(x->y) - (int *)x)

typedef struct svstg {
  int type;
  int lineno;
  int sptr;
  struct svstg *next;
} SVS;

static SVS *svs_list = NULL;

#define GET_SVS (SVS *) getitem(12, sizeof(SVS))

static LOGICAL do_sw(void);
void rouprg_enter(void);

/* -----------  declarations for token handling -------------- */
/* char classification macros */

#undef _CS
#undef _DI
#undef _BL
#undef _HD
#undef _WS
#define _CS 1  /* c symbol */
#define _DI 2  /* digit */
#define _BL 4  /* c's blank */
#define _HD 8  /* hex digit */
#define _WS 16 /* white space (' ' or '\t') */

#define _MASK(c) ((int)c & 0xFF)
#undef isident
#define isident(c) (ctable[_MASK(c)] & (_CS | _DI))

#undef iswhite
#define iswhite(c) (ctable[_MASK(c)] & _WS)

#define ishex(c) (ctable[_MASK(c)] & (_HD | _DI))
#define isdig(c) (ctable[_MASK(c)] & _DI)

static char ctable[256] = {
    0,         /* nul */
    0,         /* soh */
    0,         /* stx */
    0,         /* etx */
    0,         /* eot */
    0,         /* enq */
    0,         /* ack */
    0,         /* bel */
    0,         /* bs  */
    _BL | _WS, /* ht  */
    0,         /* nl  */
    _BL,       /* vt  */
    _BL,       /* ff  */
    _BL,       /* cr  */
    0,         /* so  */
    0,         /* si */
    0,         /* dle */
    0,         /* dc1 */
    0,         /* dc2 */
    0,         /* dc3 */
    0,         /* dc4 */
    0,         /* nak */
    0,         /* syn */
    0,         /* etb */
    0,         /* can */
    0,         /* em  */
    0,         /* sub */
    0,         /* esc */
    0,         /* fs  */
    0,         /* gs  */
    0,         /* rs  */
    0,         /* us  */
    _BL | _WS, /* sp  */
    0,         /* !  */
    0,         /* "  */
    0,         /* #  */
    _CS,       /* $  */
    0,         /* %  */
    0,         /* &  */
    0,         /* '  */
    0,         /* (  */
    0,         /* )  */
    0,         /* *  */
    0,         /* +  */
    0,         /* ,  */
    0,         /* -  */
    0,         /* .  */
    0,         /* /  */
    _DI,       /* 0  */
    _DI,       /* 1  */
    _DI,       /* 2  */
    _DI,       /* 3  */
    _DI,       /* 4  */
    _DI,       /* 5  */
    _DI,       /* 6  */
    _DI,       /* 7  */
    _DI,       /* 8  */
    _DI,       /* 9  */
    0,         /* :  */
    0,         /* ;  */
    0,         /* <  */
    0,         /* =  */
    0,         /* >  */
    0,         /* ?  */
    0,         /* @  */
    _CS | _HD, /* A  */
    _CS | _HD, /* B  */
    _CS | _HD, /* C  */
    _CS | _HD, /* D  */
    _CS | _HD, /* E  */
    _CS | _HD, /* F  */
    _CS,       /* G  */
    _CS,       /* H  */
    _CS,       /* I  */
    _CS,       /* J  */
    _CS,       /* K  */
    _CS,       /* L  */
    _CS,       /* M  */
    _CS,       /* N  */
    _CS,       /* O  */
    _CS,       /* P  */
    _CS,       /* Q  */
    _CS,       /* R  */
    _CS,       /* S  */
    _CS,       /* T  */
    _CS,       /* U  */
    _CS,       /* V  */
    _CS,       /* W  */
    _CS,       /* X  */
    _CS,       /* Y  */
    _CS,       /* Z  */
    0,         /* [  */
    0,         /* \  */
    0,         /* ]  */
    0,         /* ^  */
    _CS,       /* _  */
    0,         /* `  */
    _CS | _HD, /* a  */
    _CS | _HD, /* b  */
    _CS | _HD, /* c  */
    _CS | _HD, /* d  */
    _CS | _HD, /* e  */
    _CS | _HD, /* f  */
    _CS,       /* g  */
    _CS,       /* h  */
    _CS,       /* i  */
    _CS,       /* j  */
    _CS,       /* k  */
    _CS,       /* l  */
    _CS,       /* m  */
    _CS,       /* n  */
    _CS,       /* o  */
    _CS,       /* p  */
    _CS,       /* q  */
    _CS,       /* r  */
    _CS,       /* s  */
    _CS,       /* t  */
    _CS,       /* u  */
    _CS,       /* v  */
    _CS,       /* w  */
    _CS,       /* x  */
    _CS,       /* y  */
    _CS,       /* z  */
    0,         /* {  */
    0,         /* |  */
    0,         /* }  */
    0,         /* ~  */
    0,         /* del */
};

#define T_END 0
#define T_ERR -1
#define T_NULL -2
#define T_MINUS '-'
#define T_LP '('
#define T_RP ')'
#define T_IDENT 'a'
#define T_INT '1'
#define T_EQUAL '='
#define T_COMMA ','
#define T_STR '"'
#define T_COLON ':'
#define T_PCENT '%'
#define T_QUEST '?'
#define T_LSB '['
#define T_RSB ']'
#define T_STAR '*'
#define TOKMAX 2048

static char ctok[TOKMAX];
static INT itok;
static int upper_to_lower = 0;

static char *skip_ws(char *);
static int gtok(void);
static int g_id(char *);

static void lcase(char *);

#define LCASE(x) lcase(x)
static LOGICAL craydir; /* TRUE if cray directive */
static LOGICAL sundir;  /* TRUE if sun directive */

/* ----------------------------------------------------------- */

/*
 * pg points to char following "#pragma"
 */
void
p_pragma(char *pg, int pline)
{
  char *p;
  LOGICAL err;
  char c;

  /* turn off pragma processing */
  if (XBIT(59, 0x2))
    return;

  lineno = pline;
  err = TRUE;
  currp = pg;
  p = currp;
  while (*p != '\n')
    p++;
  *p = '\0';

  upper_to_lower = flg.ucase ? 0 : 32;

  TR2("line(%4d) cpgi$%s\n", lineno, pg);

  sundir = craydir = FALSE;
  if (strncmp(pg, "cray", 4) == 0) {
    craydir = TRUE;
    currp = pg + 4;
  } else if (strncmp(pg, "sun", 3) == 0) {
    sundir = TRUE;
    currp = pg + 3;
  }
  scope = S_NONE;
  c = *currp++;
  if (c == 'l' || c == 'L')
    scope = S_LOOP;
  else if (c == 'r' || c == 'R')
    scope = S_ROUTINE;
  else if (c == 'g' || c == 'G')
    scope = S_GLOBAL;
  else if (c == ' ')
    scope = S_NONE;
  else if (sundir) {
    /*
     * NOTE: certain sun directives can immediately follow the prefix
     *       "c$pragma"; instead of issuing an error, let the directive
     *       begin with the postition of this character.
     */
    currp--;
  } else {
    error(280, 2, lineno, ": G, R, L, or blank must follow $", CNULL);
    return;
  }

  TR2("%s scope, rest of pragma $%s$\n", xx[scope], currp);

  err = do_sw();

  if (err && XBIT(0, 0x8000))
    error(299, 2, lineno, pg, CNULL);
}

#define SW_ASSOC 0
#define SW_DEPCHK 1
#define SW_EQVCHK 2
#define SW_LSTVAL 3
#define SW_PERMUTE 4
#define SW_SMALLVECT 5
#define SW_SPLIT 6
#define SW_VECTOR 7
#define SW_VINTR 8
#define SW_RELATION 9
#define SW_RECOG 10
#define SW_TRANSFORM 11
#define SW_IVDEP 12
#define SW_SWPIPE 13
#define SW_OPT 14
#define SW_DUAL 15
#define SW_ALTCODE 16
#define SW_SAFEPTR 17
#define SW_SAFE 18
#define SW_STREAM 19
#define SW_BOUNDS 20
#define SW_FCON 21
#define SW_SINGLE 22
#define SW_FUNC32 23
#define SW_FRAME 24
#define SW_INFO 25
#define SW_STRIPSZ 26
#define SW_X 27
#define SW_Y 28
#define SW_C 29
#define SW_FINI 30
#define SW_INIT 31
#define SW_IDENT 32
#define SW_WEAK 33
#define SW_INVARIF 34
#define SW_CONCUR 35
#define SW_UNROLL 36
#define SW_CNCALL 37
#define SW_DIST 38
#define SW_INDEP 39
#define SW_LASTDIM 40
#define SW_SAFELASTVAL 41
#define SW_PARANDSER 42
#define SW_PARALLEL 43
#define SW_SERIAL 44
#define SW_L3F 45
#define SW_CACHE_ALIGN 46
#define SW_SSE 47
#define SW_GP16 48
#define SW_GP32 49
#define SW_SUJ 50
#define SW_INDEX_REUSE 51
#define SW_ZEROTRIP 52
#define SW_LAI 53
#define SW_UNROLLFACTOR 54
#define SW_PRECOND 55
#define SW_TRIPCOUNT 56
#define SW_MSCHED 57
#define SW_NOINLINE 58
#define SW_LPTEST1 59
#define SW_LPTEST2 60
#define SW_ALIGN 61
#define SW_LOOPCOUNT 62
#define SW_CB10WA 63
#define SW_ESCTYALIAS 64
#define SW_TP 65
#define SW_SAVE_ALL_GP 66
#define SW_SAVE_ALL 67
#define SW_SAVE_USED_GP 68
#define SW_SAVE_USED 69
#define SW_LIBM 70
#define SW_SIMD 71

struct c {
  char *cmd;
  int caselabel;
  LOGICAL no;
  int def_scope;
  int scopes_allowed;
};

/* table of all pragmas & directives including the compiler switch.
 * in the table, don't bother with marking fortran- and c-specific
 * directives/pragmas;  worry about this when the pragma/directive
 * is semantically processed.
 */

static struct c table[] = {
    {"align", SW_ALIGN, FALSE, S_NONE, S_NONE},
    {"altcode", SW_ALTCODE, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"assoc", SW_ASSOC, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"bounds", SW_BOUNDS, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"c", SW_C, FALSE, S_NONE, S_NONE},
    {"cache_align", SW_CACHE_ALIGN, FALSE, S_NONE, S_NONE},
    {"cncall", SW_CNCALL, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"concur", SW_CONCUR, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"depchk", SW_DEPCHK, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"dist", SW_DIST, FALSE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"dual", SW_DUAL, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"eqvchk", SW_EQVCHK, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    /* "esctyalias" is an internal pragma to specify a variable escaping type
     * alias rules. An example of using this is to implement libc inline
     * functions, e.g. memset(), in libmem.il.
     */
    {"esctyalias", SW_ESCTYALIAS, TRUE, S_NONE, S_NONE},
    {"fcon", SW_FCON, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"frame", SW_FRAME, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"func32", SW_FUNC32, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"info", SW_INFO, TRUE, S_ROUTINE, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"invarif", SW_INVARIF, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"ivdep", SW_IVDEP, FALSE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"l3f", SW_L3F, TRUE, S_NONE, S_NONE},
    {"lastdim", SW_LASTDIM, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
#ifdef LIBMG
    {"libm", SW_LIBM, FALSE, S_NONE, S_NONE},
#endif
    {"loopcount", SW_LOOPCOUNT, FALSE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"lstval", SW_LSTVAL, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"noinline", SW_NOINLINE, FALSE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"opt", SW_OPT, FALSE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"permutation", SW_PERMUTE, FALSE, S_NONE, S_NONE},
    {"recog", SW_RECOG, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"relation", SW_RELATION, FALSE, S_NONE, S_NONE},
    {"safe", SW_SAFE, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"safe_lastval", SW_SAFELASTVAL, FALSE, S_LOOP,
     S_LOOP | S_ROUTINE | S_GLOBAL},
    {"safeptr", SW_SAFEPTR, TRUE, S_ROUTINE, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"save_all_gp_regs", SW_SAVE_ALL_GP, FALSE, S_ROUTINE,
     S_ROUTINE | S_GLOBAL},
    {"save_all_regs", SW_SAVE_ALL, FALSE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"save_used_gp_regs", SW_SAVE_USED_GP, FALSE, S_ROUTINE,
     S_ROUTINE | S_GLOBAL},
    {"save_used_regs", SW_SAVE_USED, FALSE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"shortloop", SW_SMALLVECT, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"simd", SW_SIMD, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"single", SW_SINGLE, TRUE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"smallvect", SW_SMALLVECT, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"split", SW_SPLIT, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"sse", SW_SSE, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"suj", SW_SUJ, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"stream", SW_STREAM, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"stripsize", SW_STRIPSZ, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"swpipe", SW_SWPIPE, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"tp", SW_TP, FALSE, S_ROUTINE, S_ROUTINE | S_GLOBAL},
    {"transform", SW_TRANSFORM, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"unroll", SW_UNROLL, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"vector", SW_VECTOR, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"vintr", SW_VINTR, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"x", SW_X, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"y", SW_Y, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
    {"zerotrip", SW_ZEROTRIP, TRUE, S_LOOP, S_LOOP | S_ROUTINE | S_GLOBAL},
};

#define NTAB (sizeof(table) / sizeof(struct c))

static LOGICAL no_specified;
static void set_flg(int, int);
static void assn(int, int);
static void bset(int, int);
static void bclr(int, int);
static int getindex(struct c table[], int, char *);

static LOGICAL
do_sw(void)
{
  int typ;
  int indx;
  int i, j, k, m, err;
  char *p;
  char *errstr;
  int xindx;
  int xval;
  int sptr;
  int got_init;
#if defined(TARGET_X8664) && (!defined(FE90) || defined(PGF90))
  int tpvalue[TPNVERSION];
#endif

  p = ctok;
  typ = gtok();
  if (typ == T_MINUS) {
    typ = gtok();
    if (ctok[0] == 'm' || ctok[0] == 'M') /* ignore -m */
      p++;
  }
  if (typ != T_IDENT)
    return TRUE;
  indx = getindex(table, NTAB, p);
  if (indx < 0)
    return TRUE;
  TR2("sw %s, rest:%s:\n", table[indx].cmd, currp);
  TR2("%s scope, %s\n", xx[scope], no_specified ? "no" : "");
  if (scope != S_NONE && ((table[indx].scopes_allowed & scope) == 0)) {
    switch (scope) {
    case S_GLOBAL:
      error(281, 2, lineno, "global scope illegal for", table[indx].cmd);
      break;
    case S_ROUTINE:
      error(281, 2, lineno, "routine scope illegal for", table[indx].cmd);
      break;
    case S_LOOP:
      error(281, 2, lineno, "loop scope illegal for", table[indx].cmd);
      break;
    default:
      error(281, 2, lineno, "illegal scope for", table[indx].cmd);
    }
    return FALSE;
  }
  /*
   * always apply any pragmas regardless of scope to the next loop seen.
   */
  direct.loop_flag = TRUE;

  if (scope == S_ROUTINE || scope == S_GLOBAL) {
    if (gbl.currsub != 0)
      direct.carry_fwd = TRUE;
    else if (sem.pgphase) {
      rouprg_enter();
      direct.carry_fwd = TRUE;
    }
  }

  switch (table[indx].caselabel) {
  case SW_ASSOC:
    if (no_specified)
      bset(DIR_OFFSET(currdir, vect), 0x04);
    else
      bclr(DIR_OFFSET(currdir, vect), 0x04);
    break;
  case SW_IVDEP:
    no_specified = TRUE;
  /*  fall thru  */
  case SW_DEPCHK:
    if (no_specified)
      assn(DIR_OFFSET(currdir, depchk), FALSE);
    else
      assn(DIR_OFFSET(currdir, depchk), TRUE);
    break;
  case SW_EQVCHK:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x1);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x1);
    break;
  case SW_LASTDIM:
    do_now = TRUE; /* bset or bclr resets do_now */
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[34]), 0x100000);
    else
      bclr(DIR_OFFSET(currdir, x[34]), 0x100000);
    break;
  case SW_LSTVAL:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x2);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x2);
    break;
  case SW_SAFELASTVAL:
    bset(DIR_OFFSET(currdir, x[34]), 0x800);
    break;
  case SW_SMALLVECT:
    if (no_specified)
      assn(DIR_OFFSET(currdir, x[35]), 0);
    else if (gtok() == T_EQUAL && gtok() == T_INT)
      assn(DIR_OFFSET(currdir, x[35]), (int)itok);
    else
      assn(DIR_OFFSET(currdir, x[35]), 100); /* default */
    break;
  case SW_SPLIT:
    /*
     * nosplit    -> -My,42,0x80 -Mx,152,0
     * split      -> -Mx,42,0x80 -Mx,152,0
     * split = n  -> -Mx,42,0x80 -Mx,152,n
     */
    assn(DIR_OFFSET(currdir, x[152]), 0);
    if (no_specified)
      bclr(DIR_OFFSET(currdir, x[42]), 0x80);
    else {
      bset(DIR_OFFSET(currdir, x[42]), 0x80);

      typ = gtok();
      if (typ == T_EQUAL) {
        typ = gtok();
        if (typ == T_INT)
          assn(DIR_OFFSET(currdir, x[152]), (int)itok);
      }
    }
    break;

  case SW_STREAM:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x40);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x40);
    break;
  case SW_VECTOR:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x18); /* notransform | norecog */
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x18);
    break;
  case SW_VINTR:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[34]), 0x8);
    else
      bclr(DIR_OFFSET(currdir, x[34]), 0x8);
    break;
  case SW_PERMUTE:
  case SW_RELATION:
    break;
  case SW_SWPIPE:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x20);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x20);
    break;
  case SW_RECOG:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x10);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x10);
    break;
  case SW_STRIPSZ:
    if (no_specified)
      assn(DIR_OFFSET(currdir, x[38]), 256);
    else if (gtok() == T_EQUAL && gtok() == T_INT)
      assn(DIR_OFFSET(currdir, x[38]), (int)itok);
    else
      assn(DIR_OFFSET(currdir, x[38]), 256); /* default */
    break;
  case SW_TP:
#if defined(TARGET_X8664) && (!defined(FE90) || defined(PGF90))
    i = 0;
    err = 0;
    typ = gtok();
    while (typ == T_IDENT && i < 9) {
      LCASE(ctok);
      if (strcmp(ctok, "x64") == 0) {
        tpvalue[i++] = TP_K8;
        tpvalue[i++] = TP_P7;
        typ = gtok();
      } else if (strcmp(ctok, "amd64") == 0) {
        tpvalue[i++] = TP_K8;
        typ = gtok();
      } else if (strcmp(ctok, "amd64e") == 0) {
        tpvalue[i++] = TP_K8E;
        typ = gtok();
      } else if (strcmp(ctok, "core2_64") == 0) {
        tpvalue[i++] = TP_CORE2;
        typ = gtok();
      } else if (strcmp(ctok, "k8_64") == 0) {
        tpvalue[i++] = TP_K8;
        typ = gtok();
      } else if (strcmp(ctok, "k8_64e") == 0) {
        tpvalue[i++] = TP_K8E;
        typ = gtok();
      } else if (strcmp(ctok, "p7_64") == 0) {
        tpvalue[i++] = TP_P7;
        typ = gtok();
      } else if (strcmp(ctok, "core2") == 0) {
        /* look for core2-64, accept core2 alone */
        typ = gtok();
        if (typ != T_MINUS) {
          tpvalue[i++] = TP_CORE2;
        } else {
          typ = gtok();
          if (typ == T_INT && itok == 64) {
            tpvalue[i++] = TP_CORE2;
            typ = gtok();
          } else {
            err = 1;
          }
        }
      } else if (strcmp(ctok, "k8") == 0) {
        /* look for k8-64 or k8-64e, accept k8 alone */
        typ = gtok();
        if (typ != T_MINUS) {
          tpvalue[i++] = TP_K8;
        } else {
          typ = gtok();
          if (typ == T_INT && itok == 64) {
            typ = gtok();
            if (typ == T_IDENT &&
                (strcmp(ctok, "e") == 0 || strcmp(ctok, "E") == 0)) {
              tpvalue[i++] = TP_K8E;
              typ = gtok();
            } else {
              tpvalue[i++] = TP_K8;
            }
          } else {
            err = 1;
          }
        }
      } else if (strcmp(ctok, "k8e") == 0) {
        /* look for k8e-64, accept k8e alone */
        typ = gtok();
        if (typ != T_MINUS) {
          tpvalue[i++] = TP_K8E;
        } else {
          typ = gtok();
          if (typ == T_INT && itok == 64) {
            tpvalue[i++] = TP_K8E;
            typ = gtok();
          } else {
            err = 1;
          }
        }
      } else if (strcmp(ctok, "p7") == 0) {
        /* look for p7-64, accept p7 alone */
        typ = gtok();
        if (typ != T_MINUS) {
          tpvalue[i++] = TP_P7;
        } else {
          typ = gtok();
          if (typ == T_INT && itok == 64) {
            tpvalue[i++] = TP_P7;
            typ = gtok();
          } else {
            err = 1;
          }
        }
      } else {
        err = 1;
        break;
      }
    }
    if (i > 1) {
      /* sort the tpvalue settings, so most enabled processor is first */
      /* since there are only a few of these, we use a simple bubble sort */
      for (j = 0; j < i; ++j) {
        for (k = j; k < i; ++k) {
          if (tpvalue[j] < tpvalue[k]) {
            m = tpvalue[j];
            tpvalue[j] = tpvalue[k];
            tpvalue[k] = m;
          }
        }
      }
    }
    tpvalue[i++] = 0;
    for (j = 0; j < i; ++j) {
      assn(DIR_OFFSET(currdir, tpvalue[j]), tpvalue[j]);
    }
    if (err) {
      return TRUE;
    }
#endif
    break;
  case SW_TRANSFORM:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x8);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x8);
    break;
  case SW_OPT:
    typ = gtok();
    if (typ == T_EQUAL)
      typ = gtok();
    if (typ != T_INT)
      return TRUE;
    do_now = TRUE; /* assn or whatever it calls resets do_now */
    assn(DIR_OFFSET(currdir, opt), (int)itok);
    break;
  case SW_DUAL:
    if (gtok() == T_IDENT) {
      LCASE(ctok);
      if (strcmp(ctok, "pipe") == 0)
        i = 0x1;
      else if (strcmp(ctok, "mode") == 0)
        i = 0x2;
      else
        return TRUE;
      if (no_specified)
        bclr(DIR_OFFSET(currdir, x[4]), i);
      else
        bset(DIR_OFFSET(currdir, x[4]), i);
    } else {
      if (no_specified)
        bclr(DIR_OFFSET(currdir, x[4]), 0x3);
      else
        bset(DIR_OFFSET(currdir, x[4]), 0x3);
    }
    break;
  case SW_ALTCODE:
    if (no_specified) {
      /*------------------
       * #pragma noaltcode
       *----------------*/
      assn(DIR_OFFSET(currdir, x[16]), 0);        /* scalar | vector */
      assn(DIR_OFFSET(currdir, x[17]), 0);        /* swpipe */
      assn(DIR_OFFSET(currdir, x[18]), 0);        /* unroll */
      assn(DIR_OFFSET(currdir, x[43]), 0);        /* concurreduction */
      assn(DIR_OFFSET(currdir, x[44]), 0);        /* concur */
      assn(DIR_OFFSET(currdir, x[149]), 0);       /* nopeel */
      assn(DIR_OFFSET(currdir, x[150]), 0);       /* nontemporal */
      bclr(DIR_OFFSET(currdir, x[34]), 0x400000); /* alignment */
      break;
    }
    typ = gtok();

    if (typ == T_EQUAL) {
      /*--------------------
       * #pragma altcode = n
       *------------------*/
      if (gtok() != T_INT)
        return TRUE;

      /* Equivalent to -Mx,34,0x400000 -Mx,149,1 -Mx,150,1, which
       * enables alignment, nopeel and nontemporal altcode.
       * 'n' is ignored!
       */
      bset(DIR_OFFSET(currdir, x[34]), 0x400000); /* alignment */
      assn(DIR_OFFSET(currdir, x[149]), 1);       /* nopeel */
      assn(DIR_OFFSET(currdir, x[150]), 1);       /* nontemporal */
      break;
    }

    if (typ == T_END) {
/*----------------
 * #pragma altcode
 *--------------*/
      /* Equivalent to -Mx,34,0x400000 -Mx,149,1 -Mx,150,1, which
       * enables alignment, nopeel and nontemporal altcode.
       */
      bset(DIR_OFFSET(currdir, x[34]), 0x400000); /* alignment */
      assn(DIR_OFFSET(currdir, x[149]), 1);       /* nopeel */
      assn(DIR_OFFSET(currdir, x[150]), 1);       /* nontemporal */
      break;
    }

    /*-----------------------------------------------------------------
     * altcode [(n)] scalar | vector | swpipe | unroll | concur |
     *               concurreduction | nopeel | nontemporal | alignment
     *---------------------------------------------------------------*/
    if (typ == T_LP) {
      if (gtok() != T_INT)
        return TRUE;
      i = itok;
      if (gtok() != T_RP)
        return TRUE;
      typ = gtok();
    } else
      i = -1; /* select default count later */
    if (typ != T_IDENT)
      return TRUE;
    LCASE(ctok);
    if (strcmp(ctok, "scalar") == 0 || strcmp(ctok, "vector") == 0) {
      if (i < 0)
        i = 1; /* i.e. the compiler determines the value */
      assn(DIR_OFFSET(currdir, x[16]), i);
    } else if (strcmp(ctok, "swpipe") == 0) {
      if (i < 0)
        i = 100;
      assn(DIR_OFFSET(currdir, x[17]), i);
    } else if (strcmp(ctok, "unroll") == 0) {
      if (i < 0)
        i = 4;
      assn(DIR_OFFSET(currdir, x[18]), i);
    } else if (strcmp(ctok, "concur") == 0) {
      if (i < 0)
        i = 100;
      assn(DIR_OFFSET(currdir, x[44]), i);
    } else if (strcmp(ctok, "concurreduction") == 0) {
      if (i < 0)
        i = 200;
      assn(DIR_OFFSET(currdir, x[43]), i);
    } else if (strcmp(ctok, "nopeel") == 0) {
      if (i < 0)
        i = 1; /* i.e. the compiler determines the value */
      assn(DIR_OFFSET(currdir, x[149]), i);
    } else if (strcmp(ctok, "nontemporal") == 0) {
      if (i < 0)
        i = 1; /* i.e. the compiler determines the value */
      assn(DIR_OFFSET(currdir, x[150]), i);
    } else if (strcmp(ctok, "alignment") == 0) {
      bset(DIR_OFFSET(currdir, x[34]), 0x400000);
    } else
      return TRUE;
    break;
  case SW_SAFEPTR: /* XBIT(2, <i>) */
    if (gtok() != T_EQUAL)
      return TRUE;
    while (TRUE) {
      typ = gtok();
      if (typ != T_IDENT)
        return TRUE;
      if (strcmp(ctok, "arg") == 0)
        i = 0x01;
      else if (strcmp(ctok, "auto") == 0)
        i = 0x02;
      else if (strcmp(ctok, "local") == 0)
        i = 0x02;
      else if (strcmp(ctok, "static") == 0)
        i = 0x04;
      else if (strcmp(ctok, "global") == 0)
        i = 0x08;
      else if (strcmp(ctok, "all") == 0)
        i = 0x0f;
      else
        return TRUE;

      TR2("safeptr %s, rest:%s:\n", ctok, currp);
      if (no_specified)
        bclr(DIR_OFFSET(currdir, x[2]), i);
      else
        bset(DIR_OFFSET(currdir, x[2]), i);
      typ = gtok();
      if (typ == T_END)
        break;
      if (typ != T_COMMA)
        return TRUE;
    };
    break;
  case SW_SAFE:
    break;
  case SW_ESCTYALIAS:
    break;
  case SW_ALIGN:
    break;
  case SW_BOUNDS:
    if (no_specified) {
      bclr(DIR_OFFSET(currdir, x[70]), 0x02);
      flg.x[70] &= ~0x02; /* affect semant immediately */
    } else {
      bset(DIR_OFFSET(currdir, x[70]), 0x02);
      flg.x[70] |= 0x02; /* affect semant immediately */
    }
    break;
  case SW_FCON:
    break;
  case SW_SINGLE:
    break;
  case SW_FRAME:
    do_now = TRUE; /* bclr/bset or whatever it calls resets do_now */
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[121]), 0x01);
    else
      bclr(DIR_OFFSET(currdir, x[121]), 0x01);
    break;
  case SW_FUNC32:
    do_now = TRUE; /* bclr/bset or whatever it calls resets do_now */
    if (no_specified)
      bclr(DIR_OFFSET(currdir, x[119]), 0x04);
    else
      bset(DIR_OFFSET(currdir, x[119]), 0x04);
    break;
  case SW_INFO: /* XBIT(0, <i>) */
    if (gtok() != T_EQUAL)
      return TRUE;
    while (TRUE) {
      typ = gtok();
      if (typ != T_IDENT)
        return TRUE;
      if (strcmp(ctok, "time") == 0)
        i = 0x01;
      else if (strcmp(ctok, "stat") == 0)
        i = 0x01;
      else if (strcmp(ctok, "loop") == 0)
        i = 0x02;
      else if (strcmp(ctok, "unroll") == 0)
        i = 0x04;
      else if (strcmp(ctok, "inline") == 0)
        i = 0x08;
      else if (strcmp(ctok, "block") == 0)
        i = 0x10;
      else if (strcmp(ctok, "cycles") == 0)
        i = 0x10;
      else if (strcmp(ctok, "all") == 0)
        i = 0x1f;
      else
        return TRUE;
      TR2("info %s, rest:%s:\n", ctok, currp);
      if (no_specified)
        bclr(DIR_OFFSET(currdir, x[0]), i);
      else
        bset(DIR_OFFSET(currdir, x[0]), i);
      typ = gtok();
      if (typ == T_END)
        break;
      if (typ != T_COMMA)
        return TRUE;
    };
    break;
  case SW_X:
    while (TRUE) {
      typ = gtok();
      if (typ == T_END || typ == T_ERR)
        return TRUE;
      if (typ == T_INT)
        break;
    }
    xindx = itok;
    while (TRUE) {
      typ = gtok();
      if (typ == T_END || typ == T_ERR)
        return TRUE;
      if (typ == T_INT)
        break;
    }
    xval = itok;
    TR2("   SW_X, x[%d] %08x\n", xindx, xval);
    if (is_xflag_bit(xindx))
      bset(DIR_OFFSET(currdir, x[xindx]), xval);
    else
      assn(DIR_OFFSET(currdir, x[xindx]), xval);
    break;
  case SW_Y:
    while (TRUE) {
      typ = gtok();
      if (typ == T_END || typ == T_ERR)
        return TRUE;
      if (typ == T_INT)
        break;
    }
    xindx = itok;
    while (TRUE) {
      typ = gtok();
      if (typ == T_END || typ == T_ERR)
        return TRUE;
      if (typ == T_INT)
        break;
    }
    xval = itok;
    TR2("   SW_Y, x[%d] %08x\n", xindx, xval);
    if (is_xflag_bit(xindx))
      bclr(DIR_OFFSET(currdir, x[xindx]), xval);
    else
      assn(DIR_OFFSET(currdir, x[xindx]), 0);
    break;
  case SW_C:
    /*
     *  c$pragma c ( <id> [, <id> ] ... )
     */
    while (TRUE) {
      typ = g_id("c$pragma c");
      if (typ != T_IDENT)
        break;
      if (!flg.ucase)
        LCASE(ctok);
      sptr = getsymbol(ctok);
      sptr = declsym(sptr, ST_PROC, FALSE);
      CFUNCP(sptr, 1);
#ifdef PASSBYREFP
      PASSBYREFP(sptr, 1);
#endif
    }
    break;
  case SW_L3F:
    /*
     *  cpgi$ [no]l3f ( <id> [, <id> ] ... )
     */
    /* 11/24/98 - ignore */
    break;

  case SW_INVARIF:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x80); /* invar if */
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x80);
    break;
  case SW_CONCUR:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[34]), (0x20 | 0x10));
    else
      bclr(DIR_OFFSET(currdir, x[34]), (0x20 | 0x10));
    break;
  case SW_CNCALL:
    if (no_specified)
      bclr(DIR_OFFSET(currdir, x[42]), 0x04);
    else
      bset(DIR_OFFSET(currdir, x[42]), 0x04);
    break;
  case SW_DIST:
    if (gtok() != T_EQUAL)
      return TRUE;
    typ = gtok();
    if (typ != T_IDENT)
      return TRUE;
    LCASE(ctok);
    if (strcmp(ctok, "block") == 0)
      bclr(DIR_OFFSET(currdir, x[34]), 0x100);
    else if (strcmp(ctok, "cyclic") == 0)
      bset(DIR_OFFSET(currdir, x[34]), 0x100);
    else
      return TRUE;
    break;
  case SW_SUJ:
    /* suj          -x 42 0x200
     * suj=v        -x 42 0x200 -x 106 v
     */
    if (no_specified)
      bclr(DIR_OFFSET(currdir, x[42]), 0x200);
    else
      bset(DIR_OFFSET(currdir, x[42]), 0x200);
    typ = gtok();
    if (typ != T_EQUAL) {
      return TRUE;
    } else if (gtok() == T_INT) {
      assn(DIR_OFFSET(currdir, x[106]), (int)itok);
    } else
      return TRUE;
    break;
  case SW_UNROLL:
    /* [no]unroll		-x/y 11 3
     * [no]unroll = c	-x/y 11 1
     * [no]unroll = n	-x/y 11 2
     *     unroll = c:v     -y   11 3  -x    9 v
     *     unroll = n:v     -y   11 3  -x   10 v
     */
    typ = gtok();
    if (typ != T_EQUAL) {
      if (no_specified)
        bset(DIR_OFFSET(currdir, x[11]), 0x3);
      else
        bclr(DIR_OFFSET(currdir, x[11]), 0x3);
    } else if (gtok() == T_IDENT) {
      if (strcmp(ctok, "c") == 0)
        i = 9;
      else if (strcmp(ctok, "n") == 0)
        i = 10;
      else
        return TRUE;
      if (no_specified)
        bset(DIR_OFFSET(currdir, x[11]), i - 8);
      else if (gtok() != T_COLON)
        bclr(DIR_OFFSET(currdir, x[11]), i - 8);
      else if (gtok() != T_INT)
        return TRUE;
      else {
        if (itok <= 0)
          itok = 1;
        assn(DIR_OFFSET(currdir, x[i]), (int)itok);
        bclr(DIR_OFFSET(currdir, x[11]), 3);
      }
    } else
      return TRUE;
    break;
  case SW_CACHE_ALIGN:
    /*
     *  cpgi$ cache_align ( <id> [, <id> ] ... )
     *  <id> may be enclosed in '/'.
     */
    while (TRUE) {
      typ = g_id("cpgi$ cache_align");
      if (typ != T_IDENT)
        break;
      if (!flg.ucase)
        LCASE(ctok);
      sptr = getsymbol(ctok);
      QALNP(sptr, 1);
    }
    break;
  case SW_SSE:
  case SW_SIMD:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x400);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x400);
    break;
  case SW_NOINLINE:
    /*
     * #pragma [scope] noinline
     *
     * mark routine or all routines as not-to-be-extracted, and therefore
     * not to be inlined
     */
    bset(DIR_OFFSET(currdir, x[14]), 8);
    break;
  case SW_ZEROTRIP:
    if (no_specified)
      bset(DIR_OFFSET(currdir, x[19]), 0x800);
    else
      bclr(DIR_OFFSET(currdir, x[19]), 0x800);
    break;
  case SW_LOOPCOUNT:
    if (gtok() == T_EQUAL && gtok() == T_IDENT) {
      LCASE(ctok);
      if (strcmp(ctok, "multiple") == 0) {
        if (gtok() == T_COLON && gtok() == T_INT) {
          assn(DIR_OFFSET(currdir, x[141]), (int)itok);
        }
      } else
        return TRUE;
    } else
      return TRUE;
    break;

  case SW_SAVE_ALL_GP:
    bset(DIR_OFFSET(currdir, x[164]), 1);
    break;

  case SW_SAVE_ALL:
    bset(DIR_OFFSET(currdir, x[164]), 2);
    break;

  case SW_SAVE_USED_GP:
    bset(DIR_OFFSET(currdir, x[164]), 4);
    break;

  case SW_SAVE_USED:
    bset(DIR_OFFSET(currdir, x[164]), 8);
    break;

  case SW_LIBM:
#ifdef LIBMG
    /*
     *  #pragma libm <id> [, <id>] ...
     */
    while (TRUE) {
      typ = g_id(NULL);
      if (typ == T_ERR || typ == T_END)
        break;
      if (typ == T_COMMA)
        continue;
      if (typ != T_IDENT) {
        error(281, 2, lineno, "malformed #pragma libm id [, id]...", CNULL);
        break;
      }
      sptr = getsymbol(ctok);
      sptr = declsym(sptr, ST_PROC, FALSE);
      LIBMP(sptr, 1);
    }
#endif
    break;
  default:
    interr("do_sw: sw not recog", indx, 2);
    break;
  }
  return FALSE;
}

/* pragma/directive takes effect when the function is processed by the
 * phases after the front-end.
 */
static void
set_flg(int diroff, int v)
{
#if DEBUG
  if (diroff < 0 || diroff > sizeof(DIRSET) / sizeof(int))
    interr("pragma set_flg()d-unexp.diroff", diroff, 3);
#endif
  ((int *)(&direct.rou_begin))[diroff] = v;
  TR2("   set_flg, diroff %d, v %08x\n", diroff, v);
}

static void
assn(int diroff, int v)
{
  switch (scope) {
  case S_NONE:
    break; /* TBDLOOP */
  case S_GLOBAL:
    ((int *)(&direct.gbl))[diroff] = v;
  /* fall thru */
  case S_ROUTINE:
    ((int *)(&direct.rou))[diroff] = v;
    /*
     * if we're not in a subprogram, the directive takes effect
     * now, i.e., when main() calls the phases after the front-end.
     * Also, there's no need for this directive to effect creating
     * a lpstk for the next loop.
     */
    if (do_now || (gbl.currsub == 0 && sem.pgphase == 0)) {
      set_flg(diroff, ((int *)(&direct.rou))[diroff]);
      direct.loop_flag = FALSE;
      do_now = FALSE;
    }
  /* fall thru */
  case S_LOOP:
    ((int *)(&direct.loop))[diroff] = v;
    break;
  }
}

static void
bset(int diroff, int v)
{
  switch (scope) {
  case S_NONE:
    break; /* TBDLOOP */
  case S_GLOBAL:
    ((int *)(&direct.gbl))[diroff] |= v;
  /* fall thru */
  case S_ROUTINE:
    ((int *)(&direct.rou))[diroff] |= v;
    if (do_now || (gbl.currsub == 0 && sem.pgphase == 0)) {
      set_flg(diroff, ((int *)(&direct.rou))[diroff]);
      direct.loop_flag = FALSE;
      do_now = FALSE;
    }
  /* fall thru */
  case S_LOOP:
    ((int *)(&direct.loop))[diroff] |= v;
    break;

  }
}

static void
bclr(int diroff, int v)
{
  switch (scope) {
  case S_NONE:
    break; /* TBDLOOP */
  case S_GLOBAL:
    ((int *)(&direct.gbl))[diroff] &= ~v;
  /* fall thru */
  case S_ROUTINE:
    ((int *)(&direct.rou))[diroff] &= ~v;
    if (do_now || (gbl.currsub == 0 && sem.pgphase == 0)) {
      set_flg(diroff, ((int *)(&direct.rou))[diroff]);
      direct.loop_flag = FALSE;
      do_now = FALSE;
    }
  /* fall thru */
  case S_LOOP:
    ((int *)(&direct.loop))[diroff] &= ~v;
    break;
  }
}

/**
   \brief Entering a loop. Apply the loop pragmas which occurred before the
   loop.
 */
void
push_lpprg(int beg_line)
{
  int i;
  LPG_STK *p;

  i = ++direct.lpg_stk.top;
  TR1("---push_lpprg: top %d,", i);
  NEED(direct.lpg_stk.top + 1, direct.lpg_stk.stgb, LPG_STK,
       direct.lpg_stk.size, direct.lpg_stk.size + 16);
  p = direct.lpg_stk.stgb + i;
  p->dirx = i = direct.lpg.avail++;
  TR1("lpprg %d\n", i);
  NEEDB(direct.lpg.avail, direct.lpg.stgb, LPPRG, direct.lpg.size,
        direct.lpg.size + 8);
  direct.lpg.stgb[i].dirset = direct.loop; /* current state */
  direct.lpg.stgb[i].beg_line = beg_line;
  direct.lpg.stgb[i].end_line = 0;
  if (!XBIT(59, 1)) {
    direct.loop = direct.rou;
  }
  direct.in_loop = TRUE;

}

static LOGICAL entering_routine = FALSE;
static LOGICAL exiting_routine = FALSE;

void
rouprg_enter(void)
{
  load_dirset(&direct.rou_begin);
}

/*
 *  getindex()
 *   Sequentially searches table[].cmd for elements with prefix string.
 *   Returns   index if found,  -1 if not found , -2 if matches  >1 elements
 *   NOTE: table must be in lexic. order to find duplicate prefix matches
 */
static int
getindex(struct c table[], int num_elem, char *string)
{
  register int i;
  register int k;
  register int l;
  int fnd;

  no_specified = FALSE;
  /* be nice and convert string to lower case */
  LCASE(string);
retry:
  l = -1;
  fnd = -1;
  i = 0;
  k = strlen(string);
  while ((i < num_elem) && ((l = strncmp(string, table[i].cmd, k)) > 0)) {
    i++;
  }
  if (!l) {
    if (k == strlen(table[i].cmd))
      fnd = i;
    /* check next value to see if it matches, too */
    else if ((++i < num_elem) && ((l = strncmp(string, table[i].cmd, k)) == 0))
      fnd = -2;
    else /* found unique match */
      fnd = --i;
  }
  if (no_specified) {
    if (fnd >= 0 && !table[fnd].no)
      return -1;
    goto return_it;
  }
  if (fnd == -1) {
    if (*string++ == 'n' && *string++ == 'o') {
      no_specified = TRUE;
      goto retry;
    }
  }

return_it:
  if (fnd >= 0) {
    if (scope == S_NONE) {
      if (craydir && table[fnd].caselabel == SW_VECTOR)
        scope = S_ROUTINE;
      else
        scope = table[fnd].def_scope;
    }
    switch (scope) {
    case S_LOOP:
      currdir = &direct.loop;
      break;
    case S_ROUTINE:
      currdir = &direct.rou;
      break;
    case S_GLOBAL:
      currdir = &direct.gbl;
      break;
    case S_NONE:
      break;
    }
  }
  return (fnd);
}

static char *
skip_ws(char *p)
{
  while (iswhite(*p))
    p++;
  return p;
}

static int allowextended = 0;

static int
gtok(void)
{
  int typ;
  char *p;
  int i;
  char c;

retry:
  switch (c = *currp++) {
  case '\0':
    typ = T_END;
    break;
  case '(':
    typ = T_LP;
    break;
  case ')':
    typ = T_RP;
    break;
  case '-':
    typ = T_MINUS;
    break;
  case ',':
    typ = T_COMMA;
    break;
  case '=':
    typ = T_EQUAL;
    break;
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
  case 'F':
  case 'G':
  case 'H':
  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
  case 'O':
  case 'P':
  case 'Q':
  case 'R':
  case 'S':
  case 'T':
  case 'U':
  case 'V':
  case 'W':
  case 'X':
  case 'Y':
  case 'Z':
    c += upper_to_lower;
  /* fall thru */
  case 'a':
  case 'b':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
  case 'g':
  case 'h':
  case 'i':
  case 'j':
  case 'k':
  case 'l':
  case 'm':
  case 'n':
  case 'o':
  case 'p':
  case 'q':
  case 'r':
  case 's':
  case 't':
  case 'u':
  case 'v':
  case 'w':
  case 'x':
  case 'y':
  case 'z':
  case '_':
  case '$':
    ctok[0] = c;
    p = ctok + 1;
    i = 1;
    while (TRUE) {
      c = *currp;
      if (isupper(c))
        *p = c + upper_to_lower;
      else if (isident(c))
        *p = c;
      else
        break;
      if (++i >= TOKMAX) {
        error(232, 3, lineno, CNULL, CNULL);
        break;
      }
      p++;
      currp++;
    }
    *p = '\0';
    typ = T_IDENT;
    break;
  case '0':
    if (*currp == 'x' || *currp == 'X') {
      currp++;
      p = currp;
      while (ishex(*currp))
        currp++;
      if (atoxi(p, &itok, (int)(currp - p), 16))
        itok = 0;
      typ = T_INT;
      break;
    }
  /* fall thru */
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    p = currp - 1;
    while (isdig(*currp))
      currp++;
    if (atoxi(p, &itok, (int)(currp - p), 10))
      itok = 0;
    typ = T_INT;
    break;
  case ':':
    typ = T_COLON;
    break;
  case '%':
    if (allowextended) {
      typ = T_PCENT;
      break;
    }
    goto LDEF;
  case '?':
    if (allowextended) {
      typ = T_QUEST;
      break;
    }
    goto LDEF;
  case '[':
    if (allowextended) {
      typ = T_LSB;
      break;
    }
    goto LDEF;
  case ']':
    if (allowextended) {
      typ = T_RSB;
      break;
    }
    goto LDEF;
  case '*':
    if (allowextended) {
      typ = T_STAR;
      break;
    }
    goto LDEF;
  default:
  LDEF:
    if (iswhite(c))
      goto retry;
    typ = T_ERR;
    break;
  }

  return typ;
}

static int
g_id(char *errstr)
{
  /*
   *  "general" scan routine to find identifiers.  syntax allows:
   *       <id>			--> one id only
   *       ( <id> [, <id>] )	--> list of id's enclosed in parens
   */
  int typ;
  static int g_id_state = 0;

again:
  typ = gtok();
  switch (g_id_state) {
  case 0: /* first time thru */
    if (typ == T_LP) {
      g_id_state = 2;
      goto again;
    }
    if (typ == T_IDENT)
      g_id_state = 1;
    else if (typ == T_END) /* indicate null list */
      typ = T_NULL;
    else
      goto err;
    break;

  case 1: /* one identifier only */
    if (typ != T_END)
      goto err;
    g_id_state = 0;
    break;

  case 2: /* seen lparen or comma, expecting <id> */
    if (typ != T_IDENT)
      goto err;
    g_id_state = 3;
    break;

  case 3: /* seen <id>, expect comma or rparen */
    if (typ == T_COMMA) {
      g_id_state = 2;
      goto again;
    }
    if (typ != T_RP)
      goto err;
    g_id_state = 0;
    typ = T_END;
    break;
#if DEBUG
  default:
    interr("pragma-g_id:ill.state", g_id_state, 3);
#endif
  }

  return typ;

err:
  if (errstr != NULL)
    error(281, 2, lineno, errstr, "- syntax error in identifier list");
  g_id_state = 0;
  return T_ERR;
}

static void
lcase(char *str)
{
  char c;
  while ((c = *str)) {
    if (isupper(c))
      *str = tolower(c);
    str++;
  }

}

