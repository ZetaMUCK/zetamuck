/*
 *  Compile.c   (This is really a tokenizer, not a compiler)
 */

#include "copyright.h"
#include "config.h"

#include "db.h"
#include "props.h"
#include "interface.h"
#include "inst.h"
#include "externs.h"
#include "params.h"
#include "tune.h"
#include "match.h"
#include "interp.h"
#ifndef NAN
#include "nan.h"
#endif
#ifdef JSON_SUPPORT
#include <jansson.h> /* for encoding/decoding flag defs */
#endif /* JSON_SUPPORT */
 

/* This file contains code for doing "byte-compilation" of
   mud-forth programs.  As such, it contains many internal
   data structures and other such which are not found in other
   parts of TinyMUCK.                                       */

/* The CONTROL_STACK is a stack for holding previous control statements.
   This is used to resolve forward references for IF/THEN and loops, as well
   as a placeholder for back references for loops. */

#define CTYPE_IF    1
#define CTYPE_ELSE  2
#define CTYPE_BEGIN 3
#define CTYPE_FOR   4           /* Get it?  CTYPE_FOUR!!  HAHAHAHAHA  -Fre'ta */
        /* C-4?  *BOOM!*  -Revar */
#define CTYPE_WHILE 5
#define CTYPE_TRY   6           /* reserved for exception handling */
#define CTYPE_CATCH 7           /* reserved for exception handling */


/* These would be constants, but their value isn't known until runtime. */
static int IN_FORITER;
static int IN_FOREACH;
static int IN_FORPOP;
static int IN_FOR;
static int IN_TRYPOP;


static hash_tab primitive_list[COMP_HASH_SIZE];

struct CONTROL_STACK {
    short type;
    struct INTERMEDIATE *place;
    struct CONTROL_STACK *next;
    struct CONTROL_STACK *extra;
};

struct LABEL_LIST {
    const char *name;
    struct INTERMEDIATE *code;
    struct LABEL_LIST *next;
};

struct LABEL_NOTE {
    const char *name;
    struct INTERMEDIATE *code;
    struct LABEL_NOTE *next;
};

/* This structure is an association list that contains both a procedure
   name and the place in the code that it belongs.  A lookup to the procedure
   will see both it's name and it's number and so we can generate a
   reference to it.  Since I want to disallow co-recursion,  I will not allow
   forward referencing.
   */

struct PROC_LIST {
    const char *name;
    int returntype;
    struct LABEL_LIST *labels;
    struct LABEL_NOTE *lnotes;
    struct INTERMEDIATE *code;
    struct PROC_LIST *next;
};

/* The intermediate code is code generated as a linked list
   when there is no hint or notion of how much code there
   will be, and to help resolve all references.
   There is always a pointer to the current word that is
   being compiled kept.
   */

struct INTERMEDIATE {
    int no;                     /* which number instruction this is */
    struct inst in;             /* instruction itself */
    short line;                 /* line number of instruction */
    struct INTERMEDIATE *next;  /* next instruction */
};

/* The state structure for a compile. */
typedef struct COMPILE_STATE_T {
    struct CONTROL_STACK *control_stack;
    struct PROC_LIST *procs;

    int nowords;                /* number of words compiled */
    struct INTERMEDIATE *curr_word; /* word being compiled */
    struct INTERMEDIATE *first_word; /* first word of the list */
    struct INTERMEDIATE *curr_proc; /* first word of curr. proc. */
    struct publics *currpubs;
    int nested_fors;
    int nested_trys;

    /* Address resolution data.  Used to relink addresses after compile. */
    struct INTERMEDIATE **addrlist; /* list of addresses to resolve */
    int *addroffsets;           /* list of offsets from instrs */
    int addrmax;                /* size of current addrlist array */
    int addrcount;              /* number of allocated addresses */

    /* variable names.  The index into cstat->variables give you what position
     * the variable holds.
     */
    const char *variables[MAX_VAR];
    int variabletypes[MAX_VAR];
    const char *localvars[MAX_VAR];
    int localvartypes[MAX_VAR];
    const char *scopedvars[MAX_VAR];
    int scopedvartypes[MAX_VAR];

    struct line *curr_line;     /* current line */
    int lineno;                 /* current line number */
    const char *next_char;      /* next char * */
    dbref player, program;      /* player and program for this compile */

    int compile_err;            /* 1 if error occured */

    char *line_copy;
    int use_macros;             /* Boolean if you want to use .macros or not. */
    int macrosubs;              /* Safeguard for macro-subst. infinite loops */
    int descr;                  /* the descriptor that initiated compiling */
    int force_err_display;      /* If true, always show compiler errors. */
    struct INTERMEDIATE *nextinst;
    hash_tab defhash[DEFHASHSIZE];
} COMPSTATE;


int primitive(const char *s);   /* returns primitive_number if

                                 * primitive */
void free_prog(dbref);
const char *next_token(COMPSTATE *);
const char *next_token_raw(COMPSTATE *);
struct INTERMEDIATE *next_word(COMPSTATE *, const char *);
struct INTERMEDIATE *process_special(COMPSTATE *, const char *);
struct INTERMEDIATE *primitive_word(COMPSTATE *, const char *);
struct INTERMEDIATE *string_word(COMPSTATE *, const char *);
struct INTERMEDIATE *number_word(COMPSTATE *, const char *);
struct INTERMEDIATE *float_word(COMPSTATE *, const char *);
struct INTERMEDIATE *object_word(COMPSTATE *, const char *);
struct INTERMEDIATE *quoted_word(COMPSTATE *, const char *);
struct INTERMEDIATE *quoted_label_word(COMPSTATE *, const char *);
struct INTERMEDIATE *quoted_note_word(COMPSTATE *, const char *);
struct INTERMEDIATE *call_word(COMPSTATE *, const char *);
struct INTERMEDIATE *jump_word(COMPSTATE *, const char *);
struct INTERMEDIATE *label_word(COMPSTATE *, const char *);
struct INTERMEDIATE *var_word(COMPSTATE *, const char *);
struct INTERMEDIATE *lvar_word(COMPSTATE *, const char *);
struct INTERMEDIATE *svar_word(COMPSTATE *, const char *);
const char *do_string(COMPSTATE *);
void do_comment(COMPSTATE *);
void do_comment_new(COMPSTATE *);
int do_directive(COMPSTATE *, char *direct);
struct prog_addr *alloc_addr(COMPSTATE *, int, struct inst *);
struct INTERMEDIATE *prealloc_inst(COMPSTATE *cstat);
struct INTERMEDIATE *new_inst(COMPSTATE *);
void cleanpubs(struct publics *mypub);

#ifdef MCP_SUPPORT
void clean_mcpbinds(struct mcp_binding *mcpbinds);
#endif
void cleanup(COMPSTATE *);
void add_proc(COMPSTATE *, const char *, struct INTERMEDIATE *, int rettype);
void add_label(COMPSTATE *, const char *, struct INTERMEDIATE *);
void add_control_structure(COMPSTATE *, int typ, struct INTERMEDIATE *);
void add_loop_exit(COMPSTATE *, struct INTERMEDIATE *);
int in_loop(COMPSTATE *cstat);
int innermost_control_type(COMPSTATE *cstat);
int count_trys_inside_loop(COMPSTATE *cstat);
struct INTERMEDIATE *locate_control_structure(COMPSTATE *cstat, int type1,
                                              int type2);
struct INTERMEDIATE *innermost_control_place(COMPSTATE *cstat, int type1);
struct INTERMEDIATE *pop_control_structure(COMPSTATE *cstat, int type1,
                                           int type2);
struct INTERMEDIATE *pop_loop_exit(COMPSTATE *);
void resolve_loop_addrs(COMPSTATE *, int where);
int add_variable(COMPSTATE *, const char *, int valtype);
int add_localvar(COMPSTATE *, const char *, int valtype);
int add_scopedvar(COMPSTATE *, const char *, int valtype);
int special(const char *);
int call(COMPSTATE *, const char *);
int jump(COMPSTATE *, const char *);
int quoted(COMPSTATE *, const char *);
int quoted_label(COMPSTATE *, const char *);
int quoted_note(COMPSTATE *, const char *);
int label(const char *);
int object(const char *);
int string(const char *);
int variable(COMPSTATE *, const char *);
int localvar(COMPSTATE *, const char *);
int scopedvar(COMPSTATE *, const char *);
void copy_program(COMPSTATE *);
void set_start(COMPSTATE *);
void free_intermediate_node(struct INTERMEDIATE *wd);

/* Character defines */
#define BEGINCOMMENT '('
#define ENDCOMMENT ')'
#define BEGINSTRING '"'
#define ENDSTRING '"'
#define BEGINMACRO '.'
#define BEGINDIRECTIVE '$'
#define BEGINESCAPE '\\'

#define SUBSTITUTIONS 50        /* How many nested macros will we allow? */

void
do_abort_compile(COMPSTATE *cstat, const char *c)
{
    static char _buf[BUFFER_LEN];

    sprintf(_buf, "Error in line %d: %s", cstat->lineno, c);
    if (cstat->line_copy) {
        free((void *) cstat->line_copy);
        cstat->line_copy = NULL;
    }
    if (((FLAGS(cstat->player) & INTERACTIVE)
         && !(FLAGS(cstat->player) & READMODE)) || cstat->force_err_display) {
        notify_nolisten(cstat->player, _buf, 1);
    } else {
        log_muf("MUF compiler warning in program %d:\n%s\n", cstat->program,
                _buf);
    }
    cstat->compile_err++;
    if (cstat->compile_err > 1) {
        return;
    }
    if (cstat->nextinst) {
        struct INTERMEDIATE *ptr;

        while (cstat->nextinst) {
            ptr = cstat->nextinst;
            cstat->nextinst = ptr->next;
            free(ptr);
        }
        cstat->nextinst = NULL;
    }
    cleanup(cstat);
    cleanpubs(cstat->currpubs);
    cstat->currpubs = NULL;
    free_prog(cstat->program);
    cleanpubs(DBFETCH(cstat->program)->sp.program.pubs);
    DBFETCH(cstat->program)->sp.program.pubs = NULL;
#ifdef MCP_SUPPORT
    clean_mcpbinds(DBFETCH(cstat->program)->sp.program.mcpbinds);
    DBFETCH(cstat->program)->sp.program.mcpbinds = NULL;
#endif
    DBFETCH(cstat->program)->sp.program.proftime.tv_sec = 0;
    DBFETCH(cstat->program)->sp.program.proftime.tv_usec = 0;
}

/* abort compile macro */
#define abort_compile(ST,C) { do_abort_compile(ST,C); return 0; }

/* abort compile for void functions */
#define v_abort_compile(ST,C) { do_abort_compile(ST,C); return; }


/*****************************************************************/


#define ADDRLIST_ALLOC_CHUNK_SIZE 256

int
get_address(COMPSTATE *cstat, struct INTERMEDIATE *dest, int offset)
{
    int i;

    if (!cstat->addrlist) {
        cstat->addrcount = 0;
        cstat->addrmax = ADDRLIST_ALLOC_CHUNK_SIZE;
        cstat->addrlist = (struct INTERMEDIATE **)
            malloc(cstat->addrmax * sizeof(struct INTERMEDIATE *));
        cstat->addroffsets = (int *)
            malloc(cstat->addrmax * sizeof(int));
    }

    for (i = 0; i < cstat->addrcount; i++)
        if (cstat->addrlist[i] == dest && cstat->addroffsets[i] == offset)
            return i;

    if (cstat->addrcount >= cstat->addrmax) {
        cstat->addrmax += ADDRLIST_ALLOC_CHUNK_SIZE;
        cstat->addrlist = (struct INTERMEDIATE **)
            realloc(cstat->addrlist,
                    cstat->addrmax * sizeof(struct INTERMEDIATE *));
        cstat->addroffsets = (int *)
            realloc(cstat->addroffsets, cstat->addrmax * sizeof(int));
    }

    cstat->addrlist[cstat->addrcount] = dest;
    cstat->addroffsets[cstat->addrcount] = offset;
    return cstat->addrcount++;
}

void
fix_addresses(COMPSTATE *cstat)
{
    struct INTERMEDIATE *ptr;
    struct publics *pub;
    int count = 0;

    /* renumber the instruction chain */
    for (ptr = cstat->first_word; ptr; ptr = ptr->next)
        ptr->no = count++;

    /* repoint publics to targets */
    for (pub = cstat->currpubs; pub; pub = pub->next)
        pub->addr.no = cstat->addrlist[pub->addr.no]->no +
            cstat->addroffsets[pub->addr.no];

    /* repoint addresses to targets */
    for (ptr = cstat->first_word; ptr; ptr = ptr->next) {
        switch (ptr->in.type) {
            case PROG_ADD:
            case PROG_IF:
            case PROG_TRY:
            case PROG_JMP:
            case PROG_EXEC:
                ptr->in.data.number = cstat->addrlist[ptr->in.data.number]->no +
                    cstat->addroffsets[ptr->in.data.number];
                break;
            default:
                break;
        }
    }
}

void
free_addresses(COMPSTATE *cstat)
{
    cstat->addrcount = 0;
    cstat->addrmax = 0;
    if (cstat->addrlist)
        free(cstat->addrlist);
    if (cstat->addroffsets)
        free(cstat->addroffsets);
    cstat->addrlist = NULL;
}

/*****************************************************************/

void
fixpubs(struct publics *mypubs, struct inst *offset)
{
    while (mypubs) {
        mypubs->addr.ptr = offset + mypubs->addr.no;
        mypubs = mypubs->next;
    }
}


int
size_pubs(struct publics *mypubs)
{
    int bytes = 0;

    while (mypubs) {
        bytes += sizeof(*mypubs);
        mypubs = mypubs->next;
    }
    return bytes;
}



char *
expand_def(COMPSTATE *cstat, const char *defname)
{
    hash_data *exp = find_hash(defname, cstat->defhash, DEFHASHSIZE);

    if (!exp) {
        if ((*defname == BEGINMACRO) && (cstat->use_macros)) {
            return (macro_expansion(macrotop, &defname[1]));
        } else {
            return (NULL);
        }
    }
    return (string_dup((char *) exp->pval));
}


void
kill_def(COMPSTATE *cstat, const char *defname)
{
    hash_data *exp = find_hash(defname, cstat->defhash, DEFHASHSIZE);

    if (exp) {
        free(exp->pval);
        (void) free_hash(defname, cstat->defhash, DEFHASHSIZE);
    }
}


void
insert_def(COMPSTATE *cstat, const char *defname, const char *deff)
{
    hash_data hd;

    (void) kill_def(cstat, defname);
    hd.pval = (void *) string_dup(deff);
    (void) add_hash(defname, hd, cstat->defhash, DEFHASHSIZE);
}


void
insert_intdef(COMPSTATE *cstat, const char *defname, int deff)
{
    char buf[sizeof(int) * 3];

    sprintf(buf, "%d", deff);
    insert_def(cstat, defname, buf);
}


void
purge_defs(COMPSTATE *cstat)
{
    kill_hash(cstat->defhash, DEFHASHSIZE, 1);
}


void
include_defs(COMPSTATE *cstat, dbref i)
{
    char dirname[BUFFER_LEN];
    char temp[BUFFER_LEN];
    const char *tmpptr;
    PropPtr j, pptr;

    strcpy(dirname, "/_defs/");
    j = first_prop(i, dirname, &pptr, temp);
    while (j) {
        strcpy(dirname, "/_defs/");
        strcat(dirname, temp);
        tmpptr = get_property_class(i, dirname);
        if (tmpptr && *tmpptr)
            insert_def(cstat, temp, (char *) tmpptr);
        j = next_prop(pptr, j, temp);
    }
}


void
include_internal_defs(COMPSTATE *cstat)
{
	int i;
	char buf[BUFFER_LEN];
#ifdef MODULAR_SUPPORT
	struct module *m = modules;
#endif

    /* Create standard server defines */
    insert_def(cstat, "__version", VERSION);
    insert_def(cstat, "__neon", NEONVER);
    insert_def(cstat, "__proto", PROTOVER);
    insert_def(cstat, "strip", "striplead striptail");
    insert_def(cstat, "instring", "tolower swap tolower swap instr");
    insert_def(cstat, "rinstring", "tolower swap tolower swap rinstr");
    insert_def(cstat, "stripspaces",
               "strip begin dup \"  \" instr while \" \" \"  \" subst repeat");
    insert_intdef(cstat, "bg_mode", BACKGROUND);
    insert_intdef(cstat, "fg_mode", FOREGROUND);
    insert_intdef(cstat, "pr_mode", PREEMPT);
    insert_intdef(cstat, "max_variable_count", MAX_VAR);
    insert_intdef(cstat, "sorttype_caseinsens", SORTTYPE_CASEINSENS);
    insert_intdef(cstat, "sorttype_descending", SORTTYPE_DESCENDING);
    insert_intdef(cstat, "sorttype_case_ascend", SORTTYPE_CASE_ASCEND);
    insert_intdef(cstat, "sorttype_nocase_ascend", SORTTYPE_NOCASE_ASCEND);
    insert_intdef(cstat, "sorttype_case_descend", SORTTYPE_CASE_DESCEND);
    insert_intdef(cstat, "sorttype_nocase_descend", SORTTYPE_NOCASE_DESCEND);
    insert_intdef(cstat, "sorttype_shuffle", SORTTYPE_SHUFFLE);
    insert_intdef(cstat, "sorttype_natural_case_ascend", SORTTYPE_NATURAL_CASE_ASCEND);
    insert_intdef(cstat, "sorttype_natural_nocase_ascend", SORTTYPE_NATURAL_NOCASE_ASCEND);
    insert_intdef(cstat, "sorttype_natural_case_descend", SORTTYPE_NATURAL_CASE_DESCEND);
    insert_intdef(cstat, "sorttype_natural_nocase_descend", SORTTYPE_NATURAL_NOCASE_DESCEND);


    insert_intdef(cstat, "filter_caseless",    FILTER_CASELESS);
	insert_intdef(cstat, "filter_equal",       FILTER_EQUAL);
	insert_intdef(cstat, "filter_greater",     FILTER_GREATER);
	insert_intdef(cstat, "filter_lesser",      FILTER_LESSER);
	insert_intdef(cstat, "filter_notequal",    FILTER_NOTEQUAL);
	insert_intdef(cstat, "filter_smatch",      FILTER_SMATCH);
	insert_intdef(cstat, "filter_prop_dbref",  FILTER_PROP_DBREF);
	insert_intdef(cstat, "filter_prop_exists", FILTER_PROP_EXISTS);
	insert_intdef(cstat, "filter_prop_float",  FILTER_PROP_FLOAT);
	insert_intdef(cstat, "filter_prop_int",    FILTER_PROP_INT);
	insert_intdef(cstat, "filter_prop_lock",   FILTER_PROP_LOCK);
	insert_intdef(cstat, "filter_prop_number", FILTER_PROP_NUMBER);
	insert_intdef(cstat, "filter_prop_string", FILTER_PROP_STRING);
	insert_intdef(cstat, "filter_propname_smatch", FILTER_PROPNAME_SMATCH);

    /* Make defines for compatability to removed primitives */
    insert_def(cstat, "nextthing", "#-1 \"\" \"T\" findnext");
    insert_def(cstat, "nextplayer", "#-1 \"\" \"P\" findnext");
    insert_def(cstat, "nextprogram", "#-1 \"\" \"F\" findnext");
    insert_def(cstat, "nextexit", "#-1 \"\" \"E\" findnext");
    insert_def(cstat, "nextroom", "#-1 \"\" \"R\" findnext");
    insert_def(cstat, "nextowned",
               "dup player? if #-1 swap else dup owner then \"\" \"!P\" findnext");
    insert_def(cstat, "next_flag", "#-1 \"\" rot flag_2char findnext");
    insert_def(cstat, "nextthing_flag",
               "#-1 \"\" rot flag_2char \"T\" swap strcat findnext");
    insert_def(cstat, "nextplayer_flag",
               "#-1 \"\" rot flag_2char \"P\" swap strcat findnext");
    insert_def(cstat, "nextprogram_flag",
               "#-1 \"\" rot flag_2char \"F\" swap strcat findnext");
    insert_def(cstat, "nextexit_flag",
               "#-1 \"\" rot flag_2char \"E\" swap strcat findnext");
    insert_def(cstat, "nextroom_flag",
               "#-1 \"\" rot flag_2char \"R\" swap strcat findnext");
    insert_def(cstat, "nextowned_flag",
               "over player? if #-1 swap else over owner swap then \"\" swap flag_2char \"!P\" swap strcat findnext");
    insert_def(cstat, "nextplayer_power",
               "#-1 \"\" rot power_2char \"P:\" swap strcat findnext");
    insert_def(cstat, "desc", "\"_/de\" getpropstr");
    insert_def(cstat, "idesc", "\"_/ide\" getpropstr");
    insert_def(cstat, "ansidesc", "\"_/anside\" getpropstr");
    insert_def(cstat, "iansidesc", "\"_/ianside\" getpropstr");
    insert_def(cstat, "htmldesc", "\"_/htmlde\" getpropstr");
    insert_def(cstat, "ihtmldesc", "\"_/ihtmlde\" getpropstr");
    insert_def(cstat, "succ", "\"_/sc\" getpropstr");
    insert_def(cstat, "fail", "\"_/fl\" getpropstr");
    insert_def(cstat, "drop", "\"_/dr\" getpropstr");
    insert_def(cstat, "osucc", "\"_/osc\" getpropstr");
    insert_def(cstat, "ofail", "\"_/ofl\" getpropstr");
    insert_def(cstat, "odrop", "\"_/odr\" getpropstr");
    insert_def(cstat, "setdesc", "\"_/de\" swap 0 addprop");
    insert_def(cstat, "setidesc", "\"_/ide\" swap 0 addprop");
    insert_def(cstat, "setansidesc", "\"_/anside\" swap 0 addprop");
    insert_def(cstat, "setiansidesc", "\"_/ianside\" swap 0 addprop");
    insert_def(cstat, "sethtmldesc", "\"_/htmlde\" swap 0 addprop");
    insert_def(cstat, "setihtmldesc", "\"_/ihtmlde\" swap 0 addprop");
    insert_def(cstat, "setsucc", "\"_/sc\" swap 0 addprop");
    insert_def(cstat, "setfail", "\"_/fl\" swap 0 addprop");
    insert_def(cstat, "setdrop", "\"_/dr\" swap 0 addprop");
    insert_def(cstat, "setosucc", "\"_/osc\" swap 0 addprop");
    insert_def(cstat, "setofail", "\"_/ofl\" swap 0 addprop");
    insert_def(cstat, "setodrop", "\"_/odr\" swap 0 addprop");
    insert_def(cstat, "preempt", "pr_mode setmode");
    insert_def(cstat, "background", "bg_mode setmode");
    insert_def(cstat, "foreground", "fg_mode setmode");
    insert_def(cstat, "notify_except", "1 swap notify_exclude");
    insert_def(cstat, "ansi_notify_except", "1 swap ansi_notify_exclude");
    insert_def(cstat, "notify_html_except", "1 swap notify_html_exclude");
    insert_def(cstat, "html_nocr_except", "1 swap notify_html_exclude_nocr");
    insert_def(cstat, "case", "begin dup");
    insert_def(cstat, "when", "if pop");
    insert_def(cstat, "end", "break then dup");
    insert_def(cstat, "default", "pop 1 if");
    insert_def(cstat, "endcase", "pop pop 1 until");
    insert_def(cstat, "sockopen",
               "nbsockopen \"Operation now in progress\" over strcmp not if pop 1 10 1 for 10 = if \"timed out\" break then dup sockcheck dup 1 = if pop \"noerr\" break then -1 = if \"refused\" break then 1 sleep repeat then");
    insert_def(cstat, "sockrecv", "nbsockrecv swap pop");
    /* MUF Error defines */
    insert_def(cstat, "err_divzero?", "0 is_set?");
    insert_def(cstat, "err_nan?", "1 is_set?");
    insert_def(cstat, "err_imaginary?", "2 is_set?");
    insert_def(cstat, "err_fbounds?", "3 is_set?");
    insert_def(cstat, "err_ibounds?", "4 is_set?");
	/* Read Wants Blanks defines */
    insert_def(cstat, "read_wants_blanks?", "get_read_wants_blanks");
    insert_def(cstat, "read_wants_blanks_on", "get_read_wants_blanks not if read_wants_blanks then");
    insert_def(cstat, "read_wants_blanks_off", "get_read_wants_blanks if read_wants_blanks then");
    insert_def(cstat, "read_wants_blanks_toggle", "read_wants_blanks get_read_wants_blanks");

    /* Array convenience defines */
    insert_def(cstat, "}array", "} array_make");
    insert_def(cstat, "}list", "} array_make");
    insert_def(cstat, "}dict", "} 2 / array_make_dict");
    insert_def(cstat, "}join", "} array_make \"\" array_join");
    insert_def(cstat, "}tell", "} array_make me @ 1 array_make array_notify");
    insert_def(cstat, "array_diff", "2 array_ndiff");
    insert_def(cstat, "array_union", "2 array_nunion");
    insert_def(cstat, "array_intersect", "2 array_nintersect");
    insert_def(cstat, "[]", "array_getitem");
    insert_def(cstat, "->[]", "array_setitem");
    insert_def(cstat, "[]<-", "array_appenditem");
    insert_def(cstat, "[^]", "array_insertitem");
    insert_def(cstat, "[..]", "array_getrange");
    insert_def(cstat, "[x]", "array_delitem");
    /* Versions of the same that operate upon variables instead of arrays */
    insert_def(cstat, "*[]", "over @ swap array_getitem swap !");
    insert_def(cstat, "*->[]", "over -4 rotate swap @ swap array_setitem swap !");
    insert_def(cstat, "*[]<-", "tuck @ array_appenditem swap !");
    insert_def(cstat, "*[^]", "over -4 rotate swap @ swap array_insertitem swap !");
    insert_def(cstat, "*[..]", "3 pick dup -5 rotate @ 3 put array_getrange swap !");
    insert_def(cstat, "*[x]", "over @ swap array_delitem swap !");
    /* fmtstringEx: a version of array_fmtstrings that operates on a single dict/array 
       and returns a single string.  Covers 99% of cases of use of that prim. */
    insert_def(cstat, "fmtstringEx", "swap 1 array_make swap array_fmtstrings 0 array_getitem");

#ifdef IGNORE_SUPPORT
    insert_def(cstat, "MAX_IGNORES", MUF_MAX_IGNORES);
    insert_def(cstat, "IGNORE_ADD", "\"/@/ignore\" swap reflist_add");
    insert_def(cstat, "IGNORE_DEL", "\"/@/ignore\" swap reflist_del");
    insert_def(cstat, "IGNORING?", "\"/@/ignore\" swap reflist_find");
    insert_def(cstat, "ARRAY_GET_IGNORELIST",
               "\"/@/ignore\" array_get_reflist");
#else
    insert_def(cstat, "MAX_IGNORES", "0");
#endif

#ifdef SSL_SOCKETS
    insert_def(cstat, "SSL_SOCKETS", "1");
    insert_def(cstat, "SSL_SOCKOPEN", "SSL_NBSOCKOPEN");
#else
    insert_def(cstat, "SSL_SOCKETS", "0");
#endif

#ifdef IPV6
    insert_def(cstat, "IPV6?", "1");
#else
    insert_def(cstat, "IPV6?", "0");
#endif 
    insert_def(cstat, "sock6open",
               "nbsock6open \"Operation now in progress\" over strcmp not if pop 1 10 1 for 10 = if \"timed out\" break then dup sockcheck dup 1 = if pop \"noerr\" break then -1 = if \"refused\" break then 1 sleep repeat then");

#ifdef MCP_SUPPORT

    /* GUI dialog types */
    insert_def(cstat, "d_simple", "\"simple\"");
    insert_def(cstat, "d_tabbed", "\"tabbed\"");
    insert_def(cstat, "d_helper", "\"helper\"");

    /* GUI control types */
    insert_def(cstat, "c_menu", "\"menu\"");
    insert_def(cstat, "c_datum", "\"datum\"");
    insert_def(cstat, "c_label", "\"text\"");
    insert_def(cstat, "c_image", "\"image\"");
    insert_def(cstat, "c_hrule", "\"hrule\"");
    insert_def(cstat, "c_vrule", "\"vrule\"");
    insert_def(cstat, "c_button", "\"button\"");
    insert_def(cstat, "c_checkbox", "\"checkbox\"");
    insert_def(cstat, "c_radiobtn", "\"radio\"");
    insert_def(cstat, "c_edit", "\"edit\"");
    insert_def(cstat, "c_multiedit", "\"multiedit\"");
    insert_def(cstat, "c_combobox", "\"combobox\"");
    insert_def(cstat, "c_spinner", "\"spinner\"");
    insert_def(cstat, "c_scale", "\"scale\"");
    insert_def(cstat, "c_listbox", "\"listbox\"");
    insert_def(cstat, "c_frame", "\"frame\"");
    insert_def(cstat, "c_notebook", "\"notebook\"");

    /* Backwards compatibility for old GUI dialog creation prims */
    insert_def(cstat, "gui_dlog_simple",
               "d_simple 0 array_make_dict gui_dlog_create");
    insert_def(cstat, "gui_dlog_tabbed",
               "d_tabbed swap \"panes\" over array_keys array_make \"names\" 4 rotate array_vals array_make 2 array_make_dict gui_dlog_create");
    insert_def(cstat, "gui_dlog_helper",
               "d_helper swap \"panes\" over array_keys array_make \"names\" 4 rotate array_vals array_make 2 array_make_dict gui_dlog_create");

#endif

    /* for SOCK_SETOPT */
    insert_def(cstat, "NOQUEUE", STRINGIFY(SOCKOPT_NOQUEUE));
    insert_def(cstat, "SIMPLEQUEUE",  STRINGIFY(SOCKOPT_SIMPLEQUEUE));
    insert_def(cstat, "TELNETQUEUE", STRINGIFY(SOCKOPT_TELNETQUEUE));
    insert_def(cstat, "RAWMODE", STRINGIFY(SOCKOPT_RAWMODE));
    insert_def(cstat, "HOMEINSTANCE", STRINGIFY(SOCKOPT_HOMEINSTANCE));
    /* For REG Expression support */
#ifdef PCRE_SUPPORT
    insert_def(cstat, "reg_none", "0");
    insert_def(cstat, "reg_icase", STRINGIFY(MUF_RE_ICASE));
    insert_def(cstat, "reg_all", STRINGIFY(MUF_RE_ALL));
    insert_def(cstat, "reg_extended", STRINGIFY(MUF_RE_EXTENDED));
#endif

    /* Some math stuff */
    insert_def(cstat, "Deg2Rad", "pi 180 / *");
    insert_def(cstat, "Rad2Deg", "180 pi / *");

    /* inserver str .. cat */
    insert_def(cstat, "}cat", "} array_make array_interpret");

    /* Defs for compatibility with FB6 */
    insert_def(cstat, "event_wait", "0 array_make event_waitfor");
    insert_def(cstat, "descrsecure?", "descr_ssl?");

/* $defs for specific MUF prim sets */
#ifdef SQL_SUPPPORT
    insert_def(cstat, "HAVE_SQL", "1");
#else
    insert_def(cstat, "HAVE_SQL", "0");
#endif
#ifdef FILE_PRIMS
    insert_def(cstat, "HAVE_FILE_PRIMS", "1");
#else
    insert_def(cstat, "HAVE_FILE_PRIMS", "0");
#endif
#ifdef MUF_SOCKETS
    insert_def(cstat, "HAVE_SOCKET_PRIMS", "1");
#else
    insert_def(cstat, "HAVE_SOCKET_PRIMS", "0");
#endif
#ifdef MUF_EDIT_PRIMS
    insert_def(cstat, "HAVE_MUF_EDIT_PRIMS", "1");
#else
    insert_def(cstat, "HAVE_MUF_EDIT_PRIMS", "0");
#endif
#ifdef SSL_SOCKETS
    insert_def(cstat, "HAVE_SSL", "1");
#else
    insert_def(cstat, "HAVE_SSL", "0");
#endif
#ifdef IGNORE_SUPPORT
    insert_def(cstat, "HAVE_IGNORES", "1");
#else
    insert_def(cstat, "HAVE_IGNORES", "0");
#endif
#ifdef IPV6
    insert_def(cstat, "HAVE_IPV6", "1");
#else
    insert_def(cstat, "HAVE_IPV6", "0");
#endif
#ifdef PCRE_SUPPORT
    insert_def(cstat, "HAVE_PCRE", "1");
#else
    insert_def(cstat, "HAVE_PCRE", "0");
#endif
#ifdef JSON_SUPPORT
    insert_def(cstat, "HAVE_JSON", "1");
#else
    insert_def(cstat, "HAVE_JSON", "0");
#endif

/*
#ifdef IGNORE_SUPPORT
    insert_def(cstat, "MAX_IGNORES", MUF_MAX_IGNORES);
    insert_def(cstat, "SQUELCH", "\"/@/ignore\" swap reflist_add");
    insert_def(cstat, "UNSQUELCH", "\"/@/ignore\" swap reflist_del");
    insert_def(cstat, "SQUELCHED?", "\"/@/ignore\" swap reflist_find");
    insert_def(cstat, "SQUELCH_ARRAY", "\"/@/ignore\" array_get_reflist");
#else
    insert_def(cstat, "MAX_IGNORES", "0");
#endif
*/
#ifdef SSL_SOCKETS
    insert_def(cstat, "SSL_SOCKETS", "1");
    insert_def(cstat, "SSL_SOCKOPEN", "SSL_NBSOCKOPEN");
#endif

	for (i = 0; i < BASE_MAX; i++) {
		sprintf(buf,  "prim(%s)", base_inst[i]);
		insert_def(cstat, buf, "\"system\"");
	}

#ifdef MODULAR_SUPPORT
	while (m) {
		if (!m->next)
			break;

		m = m->next;
	}

	while (m) {
		char buf2[BUFFER_LEN];

		sprintf(buf,  "module(%s)", m->info->name);
		sprintf(buf2, "%d",         (m->info->version ? m->info->version : 1));
        insert_def(cstat, buf, buf2);

		for (i = 0; m->prims[i].name; i++) {
			sprintf(buf,  "modprim(%s)", m->prims[i].name);
			sprintf(buf2, "\"%s\"",      m->info->name);
			insert_def(cstat, buf, buf2);
			sprintf(buf, "prim(%s)", m->prims[i].name);
			insert_def(cstat, buf, buf2);
		}

		m = m->prev;
	};
#endif

#ifdef JSON_SUPPORT
    /*** ENCODING FLAGS ***/
    sprintf(buf, "%d", JSON_INDENT(4));
    insert_def(cstat, "JSON_INDENT4", buf);
    sprintf(buf, "%d", JSON_INDENT(2));
    insert_def(cstat, "JSON_INDENT2", buf);
    sprintf(buf, "%d", JSON_COMPACT);
    insert_def(cstat, "JSON_COMPACT", buf);
    /* do not implement JSON_ENSURE_ASCII, we need UTF-8 for dbref types */
    sprintf(buf, "%d", JSON_SORT_KEYS);
    insert_def(cstat, "JSON_SORT_KEYS", buf);
    sprintf(buf, "%d", JSON_PRESERVE_ORDER);
    insert_def(cstat, "JSON_PRESERVE_ORDER", buf);
    /* do not implement JSON_ENCODE_ANY, our code assumes RFC compliance and
       that the root node can be mapped to a MUF array */

    /*** DECODING FLAGS ***/
    sprintf(buf, "%d", JSON_REJECT_DUPLICATES);
    insert_def(cstat, "JSON_REJECT_DUPLICATES", buf);
    /* we'll allow JSON_DISABLE_EOF_CHECKS, but it's up to the user to figure
       out how to not blow the post-EOF data away on overwrite */
    sprintf(buf, "%d", JSON_DISABLE_EOF_CHECK);
    insert_def(cstat, "JSON_DISABLE_EOF_CHECK", buf);
    /* do not implement JSON_DECODE_ANY */
#endif

}

void
init_defs(COMPSTATE *cstat)
{
    /* initialize hash table */
    int i;

    for (i = 0; i < DEFHASHSIZE; i++) {
        cstat->defhash[i] = NULL;
    }

    /* Include all internal defines. */
    include_internal_defs(cstat);

    /* Include any defines set in #0's _defs/ propdir. */
    include_defs(cstat, (dbref) 0);

    /* Include any defines set in program owner's _defs/ propdir. */
    include_defs(cstat, OWNER(cstat->program));
}


void
uncompile_program(dbref i)
{
    /* free program */
    (void) dequeue_prog(i, 1);
    free_prog(i);
    cleanpubs(DBFETCH(i)->sp.program.pubs);
    DBFETCH(i)->sp.program.pubs = NULL;
#ifdef MCP_SUPPORT
    clean_mcpbinds(DBFETCH(i)->sp.program.mcpbinds);
    DBFETCH(i)->sp.program.mcpbinds = NULL;
#endif
    DBFETCH(i)->sp.program.proftime.tv_sec = 0;
    DBFETCH(i)->sp.program.proftime.tv_usec = 0;
    DBFETCH(i)->sp.program.code = NULL;
    DBFETCH(i)->sp.program.siz = 0;
    DBFETCH(i)->sp.program.start = NULL;
}


void
do_uncompile(dbref player)
{
    dbref i;

    if (!Arch(OWNER(player))) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    for (i = 0; i < db_top; i++) {
        if (Typeof(i) == TYPE_PROGRAM) {
            uncompile_program(i);
        }
    }
    anotify_nolisten(player, CSUCC "All programs decompiled.", 1);
}

void
do_proginfo(dbref player, const char *arg)
{
    char buf[BUFFER_LEN];
    dbref thing, i;
    time_t now = current_systime;
    int tcnt = 0, tsize = 0, tinst = 0, timem = 0, ccnt, csize, cinst, cimem;

    if (*arg != '#') {
        anotify_nolisten(player, SYSYELLOW "Usage: @proginfo #prognum", 1);
        if (Mage(OWNER(player))) {
            anotify_nolisten(player, SYSAQUA "Inst Object ProgSz Insts "
                             SYSBROWN "Name", 1);
            for (i = 0; i < db_top; i++) {
                if (Typeof(i) == TYPE_PROGRAM && DBFETCH(i)->sp.program.siz) {
                    tcnt += ccnt = DBFETCH(i)->sp.program.instances;
                    tsize += csize = size_object(i, 0);
                    timem += cimem = size_prog(i);
                    tinst += cinst = DBFETCH(i)->sp.program.siz;
                    sprintf(buf, SYSCYAN "%4d %6d %6d %5d %s " SYSRED "by "
                            SYSGREEN "%s",
                            ccnt, csize, cimem, cinst,
                            ansi_unparse_object(player, i), NAME(OWNER(i))
                        );
                    anotify_nolisten(player, buf, 1);
                }
            }
            sprintf(buf, SYSAQUA "%4d %6d %6d %5d " SYSBROWN "Total",
                    tcnt, tsize, timem, tinst);
            anotify_nolisten(player, buf, 1);
        }
        return;
    }

    thing = (dbref) atoi(arg + 1);
    if (Typeof(thing) != TYPE_PROGRAM || !controls(player, thing)) {
        anotify_nolisten(player, SYSRED NOPERM_MESG, 1);
        return;
    }

    anotify_nolisten(player, SYSYELLOW
                     "Age: 1 = cleanable, 0 = used recently", 1);
    sprintf(buf, "AI: %d Age: %d Instances: %d",
            (FLAGS(thing) & (ABODE | INTERNAL)),
            (now - DBFETCH(thing)->ts.lastused) > tp_clean_interval,
            DBFETCH(thing)->sp.program.instances);
    notify(player, buf);
}


void
free_unused_programs()
{
    dbref i;
    time_t now = current_systime;

    for (i = 0; i < db_top; i++) {
        if ((Typeof(i) == TYPE_PROGRAM) && !(FLAGS(i) & (ABODE | INTERNAL)) &&
            (now - DBFETCH(i)->ts.lastused > tp_clean_interval)
            && (DBFETCH(i)->sp.program.instances == 0)) {
            uncompile_program(i);
        }
    }
}

/* INTERMEDIATE instruction flags */
#define IMMFLAG_REFERENCED 1    /* Referenced by a jump */

/* checks code for valid fetch-and-clear optims and does them */
void
MaybeOptimizeVarsAt(COMPSTATE *cstat, struct INTERMEDIATE *first, int AtNo,
                    int BangNo)
{
/* AtNo is the primitive reference # for @
 * BangNo is the primitive reference # for !
 */
    struct INTERMEDIATE *curr = first->next;
    struct INTERMEDIATE *ptr;
    int farthest = 0;
    int i = 0;
    int lvarflag = 0;

    if (first->in.type == PROG_LVAR_AT || first->in.type == PROG_LVAR_AT_CLEAR)
        lvarflag = 1;

    for (; curr; curr = curr->next) {
        switch (curr->in.type) {
            case PROG_PRIMITIVE:
                /* Don't trust physical @ or !'s in-code as they may be
                 * used for indirect referencing of scoped variables */
                /* Don't trust any explict JMPs in the code */
                if ((curr->in.data.number == AtNo) ||
                    (curr->in.data.number == BangNo) ||
                    (curr->in.data.number == IN_JMP)) {
                    return;
                }
                if (lvarflag) {
                    /* don't trust the following prims for lvars 
                     * EXIT escape the code path without leaving lvar scope
                     * EXECUTE escape path without leaving lvar scope
                     * CALLS cause re-entrancy problems */
                    if (curr->in.data.number == IN_RET ||
                        curr->in.data.number == IN_EXECUTE ||
                        curr->in.data.number == IN_CALL) {
                        return;
                    }
                }
                break;
            case PROG_LVAR_AT:
            case PROG_LVAR_AT_CLEAR:
                if (lvarflag) {
                    if (curr->in.data.number == first->in.data.number) {
                        /* references to variable found before a var! */
                        return;
                    }
                }
                break;
            case PROG_SVAR_AT:
            case PROG_SVAR_AT_CLEAR:
                if (!lvarflag) {
                    if (curr->in.data.number == first->in.data.number) {
                        /* can't optimize if references to var found before var */
                        return;
                    }
                }
                break;

            case PROG_LVAR_BANG:
                if (lvarflag) {
                    if (first->in.data.number == curr->in.data.number) {
                        if (curr->no <= farthest) {
                            /* within a branch so can't optimize */
                            return;
                        } else {
                            /* Optimize it */
                            first->in.type = PROG_LVAR_AT_CLEAR;
                            return;
                        }
                    }
                }
                break;

            case PROG_SVAR_BANG:
                if (!lvarflag) {
                    if (first->in.data.number == curr->in.data.number) {
                        if (curr->no <= farthest) {
                            /* cannot optimize within a branch */
                            return;
                        } else { /* optimize it */
                            first->in.type = PROG_SVAR_AT_CLEAR;
                            return;
                        }
                    }
                }
                break;

            case PROG_EXEC:
                if (lvarflag) {
                    /* don't optim lvars over execs */
                    return;
                }
                break;

            case PROG_IF:
            case PROG_TRY:
            case PROG_JMP:
                ptr = cstat->addrlist[curr->in.data.number];
                i = cstat->addroffsets[curr->in.data.number];
                while (ptr->next && i-- > 0)
                    ptr = ptr->next;
                if (ptr->no <= first->no) {
                    /* can't optimize as we've exited branch the @ is in */
                    return;
                }
                if (ptr->no > farthest)
                    farthest = ptr->no;
                break;
            case PROG_FUNCTION:
                /* Don't try to optimize over functions */
                return;
        }
    }
}

void
RemoveNextIntermediate(COMPSTATE *cstat, struct INTERMEDIATE *curr)
{
    struct INTERMEDIATE *tmp;
    int i;

    if (!curr->next)
        return;

    tmp = curr->next;
    for (i = 0; i < cstat->addrcount; ++i) {
        if (cstat->addrlist[i] == tmp) {
            cstat->addrlist[i] = curr;
        }
    }
    curr->next = curr->next->next;
    free_intermediate_node(tmp);
    cstat->nowords--;
}

void
RemoveIntermediate(COMPSTATE *cstat, struct INTERMEDIATE *curr)
{
    if (!curr->next)
        return;

    curr->no = curr->next->no;
    curr->in.line = curr->next->in.line;
    curr->in.type = curr->next->in.type;
    switch (curr->in.type) {
        case PROG_STRING:
            curr->in.data.string = curr->next->in.data.string;
            break;
        case PROG_FLOAT:
            curr->in.data.fnumber = curr->next->in.data.fnumber;
            break;
        case PROG_FUNCTION:
            curr->in.data.mufproc = curr->next->in.data.mufproc;
            break;
        case PROG_ADD:
            curr->in.data.addr = curr->next->in.data.addr;
            break;
        case PROG_IF:
        case PROG_TRY:
        case PROG_JMP:
        case PROG_EXEC:
            curr->in.data.call = curr->next->in.data.call;
            break;
        case PROG_LABEL:
            curr->in.data.labelname = curr->next->in.data.labelname;
            break;
        case PROG_OBJECT:
            curr->in.data.objref = curr->next->in.data.objref;
            break;
        default:
            curr->in.data.number = curr->next->in.data.number;
            break;
    }
    curr->next->in.type = PROG_INTEGER;
    curr->next->in.data.number = 0;
    RemoveNextIntermediate(cstat, curr);
}

/* Checks to make sure there are as many intermediates in a row
 * as indicated by the 'count' parameter, starting with but not
 * counting *ptr
 */
int
ContiguousIntermediates(int *Flags, struct INTERMEDIATE *ptr, int count)
{
    while (count-- > 0) {
        if (!ptr) {
            return 0;
        }
        if ((Flags[ptr->no] & IMMFLAG_REFERENCED)) {
            return 0;
        }
        ptr = ptr->next;
    }
    return 1;
}

/* Identifies if the Intermediate is an integer that matches primnum */
int
IntermediateIsPrimitive(struct INTERMEDIATE *ptr, int primnum)
{
    if (ptr && ptr->in.type == PROG_PRIMITIVE)
        if (ptr->in.data.number == primnum)
            return 1;

    return 0;
}

/* Identifies if the Intermediate is an integer that matches val */
int
IntermediateIsInteger(struct INTERMEDIATE *ptr, int val)
{
    if (ptr && ptr->in.type == PROG_INTEGER)
        if (ptr->in.data.number == val)
            return 1;

    return 0;
}

/* Identifies if the Intermediate is a string that matches val */
int
IntermediateIsString(struct INTERMEDIATE *ptr, const char *val)
{
    const char *myval;

    if (ptr && ptr->in.type == PROG_STRING) {
        myval = ptr->in.data.string ? ptr->in.data.string->data : "";
        if (!strcmp(myval, val))
            return 1;
    }
    return 0;
}

int
OptimizeIntermediate(COMPSTATE *cstat)
{
    struct INTERMEDIATE *curr;
    int *Flags;
    int i;
    int count = 0;
    int advance = 1;
    int old_instr_count = cstat->nowords;
    int AtNo = get_primitive("@");
    int BangNo = get_primitive("!");
    int SwapNo = get_primitive("swap");
    int PopNo = get_primitive("pop");
    int OverNo = get_primitive("over");
    int NipNo = get_primitive("nip");
    int TuckNo = get_primitive("tuck");
    int DupNo = get_primitive("dup");
    int RotNo = get_primitive("rot");
    int RotateNo = get_primitive("rotate");
    int PickNo = get_primitive("pick");
    int NotNo = get_primitive("not");
    int StrcmpNo = get_primitive("strcmp");
    int StringcmpNo = get_primitive("stringcmp");
    int EqualsNo = get_primitive("=");
    int PlusNo = get_primitive("+");
    int MinusNo = get_primitive("-");
    int MultNo = get_primitive("*");
    int DivNo = get_primitive("/");
    int ModNo = get_primitive("%");
    int DecrNo = get_primitive("--");
    int IncrNo = get_primitive("++");
    int NoteqNo = get_primitive("!=");

    /* code assumes everything setup nicely */
    if (!cstat->first_word)
        return 0;

    /* renumber instruction chain */
    for (curr = cstat->first_word; curr; curr = curr->next)
        curr->no = count++;

    if ((Flags = (int *) malloc(sizeof(int) * count)) == 0)
        return 0;

    memset(Flags, 0, sizeof(int) * count);

    /* mark instructions which jumps reference */

    for (curr = cstat->first_word; curr; curr = curr->next) {
        switch (curr->in.type) {
            case PROG_ADD:
            case PROG_IF:
            case PROG_TRY:
            case PROG_JMP:
            case PROG_EXEC:
                i = cstat->addrlist[curr->in.data.number]->no +
                    cstat->addroffsets[curr->in.data.number];
                Flags[i] |= IMMFLAG_REFERENCED;
                break;
        }
    }

    for (curr = cstat->first_word; curr;) {
        advance = 1;
        switch (curr->in.type) {
            case PROG_LVAR:
                /* lvar ! into lvar! */
                /* lvar @ into lvar@ */
                if (curr->next && curr->next->in.type == PROG_PRIMITIVE) {
                    if (curr->next->in.data.number == AtNo) {
                        if (ContiguousIntermediates(Flags, curr->next, 1)) {
                            curr->in.type = PROG_LVAR_AT;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                    if (curr->next->in.data.number == BangNo) {
                        if (ContiguousIntermediates(Flags, curr->next, 1)) {
                            curr->in.type = PROG_LVAR_BANG;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                }
                break;
            case PROG_SVAR:
                /* svar ! into svar! */
                /* svar @ into svar@ */
                if (curr->next && curr->next->in.type == PROG_PRIMITIVE) {
                    if (curr->next->in.data.number == AtNo) {
                        if (ContiguousIntermediates(Flags, curr->next, 1)) {
                            curr->in.type = PROG_SVAR_AT;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                    if (curr->next->in.data.number == BangNo) {
                        if (ContiguousIntermediates(Flags, curr->next, 1)) {
                            curr->in.type = PROG_SVAR_BANG;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                }
                break;
            case PROG_STRING:
                if (IntermediateIsString(curr, "")) {
                    if (ContiguousIntermediates(Flags, curr->next, 3)) {
                        /* "" strcmp 0 = into not */
                        if (IntermediateIsPrimitive(curr->next, StrcmpNo)) {
                            if (IntermediateIsInteger(curr->next->next, 0)) {
                                if (IntermediateIsPrimitive
                                    (curr->next->next->next, EqualsNo)) {
                                    if (curr->in.data.string)
                                        free((void *) curr->in.data.string);
                                    curr->in.type = PROG_PRIMITIVE;
                                    curr->in.data.number = NotNo;
                                    RemoveNextIntermediate(cstat, curr);
                                    RemoveNextIntermediate(cstat, curr);
                                    RemoveNextIntermediate(cstat, curr);
                                    advance = 0;
                                    break;
                                }
                            }
                        }
                        /* "" stringcmp 0 = into not */
                        if (IntermediateIsPrimitive(curr->next, StringcmpNo)) {
                            if (IntermediateIsInteger(curr->next->next, 0)) {
                                if (IntermediateIsPrimitive
                                    (curr->next->next->next, EqualsNo)) {
                                    if (curr->in.data.string)
                                        free((void *) curr->in.data.string);
                                    curr->in.type = PROG_PRIMITIVE;
                                    curr->in.data.number = NotNo;
                                    RemoveNextIntermediate(cstat, curr);
                                    RemoveNextIntermediate(cstat, curr);
                                    RemoveNextIntermediate(cstat, curr);
                                    advance = 0;
                                    break;
                                }
                            }
                        }
                    }
                }
                break;

            case PROG_INTEGER:
                /* consolidate constant integer calculations */
                if (ContiguousIntermediates(Flags, curr->next, 2)) {
                    if (curr->next->in.type == PROG_INTEGER) {
                        /* int int + into Sum */
                        if (IntermediateIsPrimitive(curr->next->next, PlusNo)) {
                            curr->in.data.number += curr->next->in.data.number;
                            RemoveNextIntermediate(cstat, curr);
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }

                        /* int int - into Diff */
                        if (IntermediateIsPrimitive(curr->next->next, MinusNo)) {
                            curr->in.data.number -= curr->next->in.data.number;
                            RemoveNextIntermediate(cstat, curr);
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }

                        /* int int * into Prod */
                        if (IntermediateIsPrimitive(curr->next->next, MultNo)) {
                            curr->in.data.number *= curr->next->in.data.number;
                            RemoveNextIntermediate(cstat, curr);
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }

                        /* int int / into Div */
                        if (IntermediateIsPrimitive(curr->next->next, DivNo)) {
                            if (curr->next->in.data.number != 0) {
                                curr->in.data.number /= curr->next->in.data.
                                    number;
                                RemoveNextIntermediate(cstat, curr);
                                RemoveNextIntermediate(cstat, curr);
                                advance = 0;
                                break;
                            }
                        }

                        /* int int % into Result */
                        if (IntermediateIsPrimitive(curr->next->next, ModNo)) {
                            if (curr->next->in.data.number != 0) {
                                curr->in.data.number %= curr->next->in.data.
                                    number;
                                RemoveNextIntermediate(cstat, curr);
                                RemoveNextIntermediate(cstat, curr);
                                advance = 0;
                                break;
                            }
                        }       /* end of % case */
                    }           /* end of if PROG_INTEGER if */
                }

                /* end of 2 contiguous */
                /* 0 = into not */
                if (IntermediateIsInteger(curr, 0)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, EqualsNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = NotNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                }

                /* 1 + into ++ */
                if (IntermediateIsInteger(curr, 1)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, PlusNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = IncrNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                }

                /* 1 - into -- */
                if (IntermediateIsInteger(curr, 1)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, MinusNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = DecrNo;
                            RemoveNextIntermediate(cstat, curr);
                        }
                    }
                }

                /* 1 pick into dup */
                if (IntermediateIsInteger(curr, 1)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, PickNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = DupNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                        }
                    }
                }

                /* 2 pick into over */
                if (IntermediateIsInteger(curr, 2)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, PickNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = OverNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                        }
                    }
                }

                /* 3 rotate into rot */
                if (IntermediateIsInteger(curr, 3)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, RotateNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = RotNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                        }
                    }
                }

                /* 2 rotate into swap */ /* -2 rotate into swap */
                if (IntermediateIsInteger(curr, 2) || IntermediateIsInteger(curr, -2)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, RotateNo)) {
                            curr->in.type = PROG_PRIMITIVE;
                            curr->in.data.number = SwapNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                        }
                    }
                }

                /* 1 rotate into nothing */ /* 0 rotate into nothing */ /* -1 rotate into nothing */
                if (IntermediateIsInteger(curr, 1) || IntermediateIsInteger(curr, 0) || IntermediateIsInteger(curr, -1)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, RotateNo)) {
                            RemoveNextIntermediate(cstat, curr);
							RemoveIntermediate(cstat, curr);
                            advance = 0;
                        }
                    }
                }

                break;

            case PROG_PRIMITIVE:
                /* rot rot swap into swap rot */
                if (IntermediateIsPrimitive(curr, RotNo)) {
                    if (ContiguousIntermediates(Flags, curr->next, 2)) {
                        if (IntermediateIsPrimitive(curr->next, RotNo)) {
                            if (IntermediateIsPrimitive(curr->next->next, SwapNo)) {
                                curr->in.data.number = SwapNo;
                                curr->next->in.data.number = RotNo;
                                RemoveNextIntermediate(cstat, curr->next);
                                advance = 0;
                                break;
                            }
                        }
                    }
                }
                /* not not if into if */
                if (IntermediateIsPrimitive(curr, NotNo)) {
                    if (ContiguousIntermediates(Flags, curr->next, 2)) {
                        if (IntermediateIsPrimitive(curr->next, NotNo)) {
                            if (curr->next->next->in.type == PROG_IF) {
                                RemoveIntermediate(cstat, curr);
                                RemoveIntermediate(cstat, curr);
                                advance = 0;
                                break;
                            }
                        }
                    }
                }
                /* = not into != */
                if (IntermediateIsPrimitive(curr, EqualsNo)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        if (IntermediateIsPrimitive(curr->next, NotNo)) {
                            curr->in.data.number = NoteqNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                    }
                }
                if (IntermediateIsPrimitive(curr, SwapNo)) {
                    if (ContiguousIntermediates(Flags, curr->next, 1)) {
                        /* swap pop into nip */
                        if (IntermediateIsPrimitive(curr->next, PopNo)) {
                            curr->in.data.number = NipNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                        /* swap over into tuck */
                        if (IntermediateIsPrimitive(curr->next, OverNo)) {
                            curr->in.data.number = TuckNo;
                            RemoveNextIntermediate(cstat, curr);
                            advance = 0;
                            break;
                        }
                        /* swap 2 pick into tuck */
                        if (IntermediateIsInteger(curr->next, 2)) {
                            if (ContiguousIntermediates(Flags, curr->next, 1)) {
                                if (IntermediateIsPrimitive(curr->next->next, PickNo)) {
                                    curr->in.data.number = TuckNo;
                                    RemoveNextIntermediate(cstat, curr);
                                    RemoveNextIntermediate(cstat, curr);
                                    advance = 0;
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
        }
        if (advance) {
            curr = curr->next;
        }
    }

    /* turn all 'var@' which have following var! into var@-clear */
    for (curr = cstat->first_word; curr; curr = curr->next)
        if (curr->in.type == PROG_SVAR_AT || curr->in.type == PROG_LVAR_AT)
            MaybeOptimizeVarsAt(cstat, curr, AtNo, BangNo);

    free(Flags);
    return (old_instr_count - cstat->nowords);
}


/* overall control code.  Does piece-meal tokenization parsing and
   backward checking.                                            */
void
do_compile(int descr, dbref player_in, dbref program_in, int force_err_display)
{
    const char *token;
    struct INTERMEDIATE *new_word;
    int i;
    int instrCount = 0;
    COMPSTATE cstat;

    /* set all compile state variables */
    cstat.force_err_display = force_err_display;
    cstat.descr = descr;
    cstat.control_stack = 0;
    cstat.procs = 0;
    cstat.nowords = 0;
    cstat.curr_word = cstat.first_word = NULL;
    cstat.curr_proc = NULL;
    cstat.currpubs = NULL;
    cstat.nested_fors = 0;
    cstat.nested_trys = 0;
    cstat.addrcount = 0;
    cstat.addrmax = 0;
    for (i = 0; i < MAX_VAR; i++) {
        cstat.variables[i] = NULL;
        cstat.variabletypes[i] = 0;
        cstat.localvars[i] = NULL;
        cstat.localvartypes[i] = 0;
        cstat.scopedvars[i] = NULL;
        cstat.scopedvartypes[i] = 0;
    }
    cstat.curr_line = DBFETCH(program_in)->sp.program.first;
    cstat.lineno = 1;
    cstat.next_char = NULL;
    if (cstat.curr_line)
        cstat.next_char = cstat.curr_line->this_line;
    cstat.player = player_in;
    cstat.program = program_in;
    cstat.compile_err = 0;
    cstat.line_copy = NULL;
    cstat.macrosubs = 0;
    cstat.use_macros = 1;
    cstat.nextinst = NULL;
    cstat.addrlist = NULL;
    cstat.addroffsets = NULL;
    init_defs(&cstat);

    cstat.variables[0] = "ME";
    cstat.variabletypes[0] = PROG_OBJECT;
    cstat.variables[1] = "LOC";
    cstat.variabletypes[1] = PROG_OBJECT;
    cstat.variables[2] = "TRIGGER";
    cstat.variabletypes[2] = PROG_OBJECT;
    cstat.variables[3] = "COMMAND";
    cstat.variabletypes[3] = PROG_STRING;

    /* free old stuff */
    if (DBFETCH(cstat.program)->sp.program.siz)
        /* only dequeue if compiled */
        (void) dequeue_prog(cstat.program, 0);
    free_prog(cstat.program);
    cleanpubs(DBFETCH(cstat.program)->sp.program.pubs);
    DBFETCH(cstat.program)->sp.program.pubs = NULL;
#ifdef MCP_SUPPORT
    clean_mcpbinds(DBFETCH(cstat.program)->sp.program.mcpbinds);
    DBFETCH(cstat.program)->sp.program.mcpbinds = NULL;
#endif
    DBFETCH(cstat.program)->sp.program.proftime.tv_sec = 0;
    DBFETCH(cstat.program)->sp.program.proftime.tv_usec = 0;
    DBFETCH(cstat.program)->sp.program.profstart = current_systime;
    DBFETCH(cstat.program)->sp.program.profuses = 0;

    if (!cstat.curr_line)
        v_abort_compile(&cstat, "Missing program text.");

    /* do compilation */
    while ((token = next_token(&cstat))) {
        new_word = next_word(&cstat, token);

        /* test for errors */
        if (cstat.compile_err) {
            free((void *) token);
            return;
        }

        if (new_word) {
            if (!cstat.first_word)
                cstat.first_word = cstat.curr_word = new_word;
            else {
                cstat.curr_word->next = new_word;
                cstat.curr_word = cstat.curr_word->next;
            }
        }
        while (cstat.curr_word && cstat.curr_word->next)
            cstat.curr_word = cstat.curr_word->next;

        free((void *) token);
    }

    if (cstat.compile_err)
        return;                 /* Added to abort cleanly from directive errors. */

    if (cstat.curr_proc)
        v_abort_compile(&cstat, "Unexpected end of file.");

    if (!cstat.procs)
        v_abort_compile(&cstat, "Missing procedure definition.");
    if (tp_optimize_muf && !(FLAG2(program_in) & F2NO_COMMAND)) {
        int maxpasses = 5;
        int passcount = 0;
        int optimcount = 0;
        int optcnt = 0;

        instrCount = cstat.nowords;
        do {
            optcnt = OptimizeIntermediate(&cstat);
            optimcount += optcnt;
            passcount++;
        } while (optcnt > 0 && --maxpasses > 0);

        if (force_err_display && optimcount > 0) {
            char buf[BUFFER_LEN];
            char buf2[BUFFER_LEN];
            double percent;

            percent = ((double) (optimcount)
                       / (double) instrCount) * 100;
            sprintf(buf2, "Program optimized by %d instructions "
                    "in %d %s. (%#.4g%%)", optimcount, passcount,
                    passcount == 1 ? "pass" : "passes", percent);
            strcpy(buf, SYSFOREST);
            strcat(buf, buf2);
            anotify_nolisten(cstat.player, buf, 1);
        } else if (force_err_display) {
            anotify_nolisten(cstat.player, SYSBROWN
                             "No optimization possible.", 1);
        }
    }

    /* do copying over */
    fix_addresses(&cstat);
    copy_program(&cstat);
    fixpubs(cstat.currpubs, DBFETCH(cstat.program)->sp.program.code);
    DBFETCH(cstat.program)->sp.program.pubs = cstat.currpubs;

    if (cstat.nextinst) {
        struct INTERMEDIATE *ptr;

        while (cstat.nextinst) {
            ptr = cstat.nextinst;
            cstat.nextinst = ptr->next;
            free(ptr);
        }
        cstat.nextinst = NULL;
    }
    if (cstat.compile_err)
        return;

    set_start(&cstat);
    cleanup(&cstat);
    DBFETCH(cstat.program)->sp.program.instances = 0;

#ifdef MODULAR_SUPPORT
	for (i = 0; i < DBFETCH(cstat.program)->sp.program.siz; i++) {
		if (DBFETCH(cstat.program)->sp.program.code[i].type == PROG_MODPRIM) {
			struct module *m = DBFETCH(cstat.program)->sp.program.code[i].data.modprim->mod;
			struct mod_proglist *prm = m->progs;

			DBFETCH(cstat.program)->sp.program.code[i].data.modprim->links++;

			while (prm) {
				if (prm->prog == cstat.program)
					break;
				
				prm = prm->next;
			}

			if (!prm) {
				prm = (struct mod_proglist *)malloc(sizeof(struct mod_proglist));
				prm->prog = cstat.program;
				prm->prev = NULL;
				prm->next = m->progs;
				if (m->progs)
					m->progs->prev = prm;
				m->progs = prm;
				log_status("MODULE: Added prog %d to module %s because of %s.\r\n", prm->prog, m->info->name, DBFETCH(cstat.program)->sp.program.code[i].data.modprim->name);
			}
		}
	}
#endif

    /* restart AUTOSTART program. */
    if ((FLAGS(cstat.program) & ABODE) && TMage(OWNER(cstat.program)))
        add_muf_queue_event(-1, OWNER(cstat.program), NOTHING, NOTHING,
                            cstat.program, "Startup", "Queued Event.", 0);

}

struct LABEL_LIST *
find_label(COMPSTATE *cstat, const char *name)
{
    struct LABEL_LIST *p;

    for (p = cstat->procs->labels; p; p = p->next)
        if (!string_compare(p->name, name))
            return p;
    return NULL;
}

#ifdef MODULAR_SUPPORT
int modprim(const char *token);
struct INTERMEDIATE *modprim_word(COMPSTATE *cstat, const char *token);
#endif /* MODULAR_SUPPORT */

struct INTERMEDIATE *
next_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *new_word;
    static char buf[BUFFER_LEN];

    if (!token)
        return 0;

    if (call(cstat, token))
        new_word = call_word(cstat, token);
    /* else if (jump(cstat, token))
       new_word = jump_word(cstat, token); */
    else if (scopedvar(cstat, token))
        new_word = svar_word(cstat, token);
    else if (localvar(cstat, token))
        new_word = lvar_word(cstat, token);
    else if (variable(cstat, token))
        new_word = var_word(cstat, token);
    else if (special(token))
        new_word = process_special(cstat, token);
#ifdef MODULAR_SUPPORT
	else if (modprim(token))
        new_word = modprim_word(cstat, token);
#endif
    else if (primitive(token))
        new_word = primitive_word(cstat, token);
    else if (string(token))
        new_word = string_word(cstat, token + 1);
    else if (number(token))
        new_word = number_word(cstat, token);
    else if (ifloat(token))
        new_word = float_word(cstat, token);
    else if (object(token))
        new_word = object_word(cstat, token);
    else if (quoted(cstat, token))
        new_word = quoted_word(cstat, token + 1);
    else if (cstat->procs) {
        if (quoted_label(cstat, token))
            new_word = quoted_label_word(cstat, token + 1);
        else if (quoted_note(cstat, token))
            new_word = quoted_note_word(cstat, token + 1);
        else if (label(token))
            new_word = label_word(cstat, token);
    } else {
        sprintf(buf, "Unrecognized word %s.", token);
        abort_compile(cstat, buf);
    }
    return new_word;
}



/* Little routine to do the line_copy handling right */
void
advance_line(COMPSTATE *cstat)
{
    cstat->curr_line = cstat->curr_line->next;
    cstat->lineno++;
    cstat->macrosubs = 0;
    if (cstat->line_copy) {
        free((void *) cstat->line_copy);
        cstat->line_copy = NULL;
    }
    if (cstat->curr_line)
        cstat->next_char = (cstat->line_copy =
                            alloc_string(cstat->curr_line->this_line));
    else
        cstat->next_char = (cstat->line_copy = NULL);
}

/* Skips comments, grabs strings, returns NULL when no more tokens to grab. */
const char *
next_token_raw(COMPSTATE *cstat)
{
    static char buf[BUFFER_LEN];
    int i;

    if (!cstat->curr_line)
        return (char *) 0;

    if (!cstat->next_char)
        return (char *) 0;

    /* skip white space */
    while (*cstat->next_char && isspace(*cstat->next_char))
        cstat->next_char++;

    if (!(*cstat->next_char)) {
        advance_line(cstat);
        return next_token_raw(cstat);
    }
    /* take care of comments */
    if (*cstat->next_char == BEGINCOMMENT) {
        if (FLAG2(cstat->program) & F2LIGHT)
            do_comment(cstat);
        else
            do_comment_new(cstat);
        return next_token_raw(cstat);
    }
    if (*cstat->next_char == BEGINSTRING)
        return do_string(cstat);

    for (i = 0; *cstat->next_char && !isspace(*cstat->next_char); i++) {
        buf[i] = *cstat->next_char;
        cstat->next_char++;
    }
    buf[i] = '\0';
    return alloc_string(buf);
}


const char *
next_token(COMPSTATE *cstat)
{
    char *expansion, *temp;
    int result = 0;

    temp = (char *) next_token_raw(cstat);
    if (!temp)
        return NULL;

    if (temp[0] == BEGINDIRECTIVE) {
        result = do_directive(cstat, temp);
        free(temp);
        if (!result) {          /* changed to abort compiling on directive errors. -Akari */
            cstat->compile_err = 1;
            return NULL;
        }
        return next_token(cstat);
    }
    if (temp[0] == BEGINESCAPE) {
        if (temp[1]) {
            expansion = temp;
            temp = (char *) malloc(strlen(expansion));
            strcpy(temp, (expansion + 1));
            free(expansion);
        }
        return (temp);
    }
    if ((expansion = expand_def(cstat, temp))) {
        free(temp);
        if (++cstat->macrosubs > SUBSTITUTIONS) {
            abort_compile(cstat, "Too many macro substitutions.");
        } else {
            temp =
                (char *) malloc(strlen(cstat->next_char) + strlen(expansion) +
                                21);
            strcpy(temp, expansion);
            strcat(temp, cstat->next_char);
            free((void *) expansion);
            if (cstat->line_copy) {
                free((void *) cstat->line_copy);
            }
            cstat->next_char = cstat->line_copy = temp;
            return next_token(cstat);
        }
    } else {
        return (temp);
    }
}



/* skip comments */
/* This is the traditional FB comment parser. Used by putting the
 * O flag on a program object.
 */
void
do_comment(COMPSTATE *cstat)
{
    int startLine = cstat->lineno;

    while (*cstat->next_char && *cstat->next_char != ENDCOMMENT)
        cstat->next_char++;
    if (!(*cstat->next_char)) {
        advance_line(cstat);
        if (!cstat->curr_line) {
            char tbuf[BUFFER_LEN];

            sprintf(tbuf, "Unterminated comment starting in line %d.",
                    startLine);
            v_abort_compile(cstat, tbuf);
        }
        do_comment(cstat);
    } else {
        cstat->next_char++;
        if (!(*cstat->next_char))
            advance_line(cstat);
    }
}

/* This is the new Proto comment parser that allows nested comments,
 * used by default.
 */

void
do_comment_new(COMPSTATE *cstat)
{                               /* ProtoMUCK Comment parser */
    int parCount = 1;
    int startLine = cstat->lineno;

    cstat->next_char++;
    do {
        while (*cstat->next_char) {
            if (*cstat->next_char == ENDCOMMENT)
                parCount--;
            else if (*cstat->next_char == BEGINCOMMENT)
                parCount++;
            if (parCount == 0) {
                cstat->next_char++;
                if (!(*cstat->next_char))
                    advance_line(cstat);
                return;
            }
            cstat->next_char++;
        }
        advance_line(cstat);
        if (!cstat->curr_line) {
            char tbuf[BUFFER_LEN];

            sprintf(tbuf, "Unterminated comment starting in line %d.",
                    startLine);
            v_abort_compile(cstat, tbuf);
        }
    } while (1);
}

/* This tells us if the token is a compile time conditional directive. */
int
is_preprocessor_conditional(const char *tmpptr)
{
    if (!string_compare(tmpptr, "$ifdef"))
        return 1;
    if (!string_compare(tmpptr, "$ifndef"))
        return 1;
    if (!string_compare(tmpptr, "$iflib"))
        return 1;
    if (!string_compare(tmpptr, "$ifver"))
        return 1;
    if (!string_compare(tmpptr, "$iflibver"))
        return 1;
    if (!string_compare(tmpptr, "$ifnver"))
        return 1;
    if (!string_compare(tmpptr, "$ifnlibver"))
        return 1;
    if (!string_compare(tmpptr, "$ifcancall"))
        return 1;
    if (!string_compare(tmpptr, "$ifncanncall"))
        return 1;
    if (!string_compare(tmpptr, "$ifauthor"))
        return 1;
    if (!string_compare(tmpptr, "$ifbeta"))
        return 1;
    if (!string_compare(tmpptr, "$ifalpha"))
        return 1;
    if (!string_compare(tmpptr, "$ifnauthor"))
        return 1;
    if (!string_compare(tmpptr, "$ifnbeta"))
        return 1;
    if (!string_compare(tmpptr, "$ifnalpha"))
        return 1;

    return 0;
}

/* handle compiler directives */
int
do_directive(COMPSTATE *cstat, char *direct)
{
    struct match_data md;
    char temp[BUFFER_LEN];
    char *tmpname, *tmpptr = NULL;
    int i = 0;
    int j;

    strcpy(temp, ++direct);

    if (!(temp[0])) {
        abort_compile(cstat, "I don't understand that compiler directive!");
    }
    if (!string_compare(temp, "define")) {
        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for $define name.");
        i = 0;
        while ((tmpptr = (char *) next_token_raw(cstat)) &&
               (string_compare(tmpptr, "$enddef"))) {
            char *cp;

            for (cp = tmpptr; i < (BUFFER_LEN / 2) && *cp;) {
                if (*tmpptr == BEGINSTRING && cp != tmpptr &&
                    (*cp == ENDSTRING || *cp == BEGINESCAPE)) {
                    temp[i++] = BEGINESCAPE;
                }
                temp[i++] = *cp++;
            }
            if (*tmpptr == BEGINSTRING)
                temp[i++] = ENDSTRING;
            temp[i++] = ' ';
            free(tmpptr);
            if (i > (BUFFER_LEN / 2))
                abort_compile(cstat, "$define definition too long.");
        }
        if (i)
            i--;
        temp[i] = '\0';
        if (!tmpptr)
            abort_compile(cstat,
                          "Unexpected end of file in $define definition.");
        free(tmpptr);
        (void) insert_def(cstat, tmpname, temp);
        free(tmpname);

    } else if (!string_compare(temp, "beta")) {
        int progver;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname || !*tmpname) {
            progver = 1;
        } else {
            progver = atoi(tmpname);
        }
        if (progver <= 0) {
            abort_compile(cstat,
                          "You must provide a valid beta version number of 1 or greater.");
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        add_property(cstat->program, "_Beta", NULL, progver);
        free(tmpname);

    } else if (!string_compare(temp, "alpha")) {
        int progver;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname || !*tmpname) {
            progver = 1;
        } else {
            progver = atoi(tmpname);
        }
        if (progver <= 0) {
            abort_compile(cstat,
                          "You must provide a valid alpha version number of 1 or greater.");
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        add_property(cstat->program, "_Alpha", NULL, progver);
        free(tmpname);

    } else if (!string_compare(temp, "log_status")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        tmpname = (char *) cstat->next_char;
        if (!(MLevel(OWNER(cstat->program)) >= LWIZ))
            abort_compile(cstat, "Permission denied for $log_status");
        if (tmpname && *tmpname) {
            log_status("%s", tmpname);
        } else {
            abort_compile(cstat,
                          "No data given for the status log and show to LOGWALL admin.");
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
    } else if (!string_compare(temp, "show_status")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        tmpname = (char *) cstat->next_char;
        if (!(MLevel(OWNER(cstat->program)) >= LWIZ))
            abort_compile(cstat, "Permission denied for $show_status");
        if (tmpname && *tmpname) {
            show_status("%s", tmpname);
        } else {
            abort_compile(cstat, "No data given to show to LOGWALL admin.");
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);

    } else if (!string_compare(temp, "abort")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        tmpname = (char *) cstat->next_char;
        if (tmpname && *tmpname) {
            abort_compile(cstat, tmpname);
        } else {
            abort_compile(cstat, "Forced abort for the compile.");
        }

    } else if (!string_compare(temp, "cleardefs")) {
        char nextToken[BUFFER_LEN];

        purge_defs(cstat);      /* Get rid of all defs first. */
        include_internal_defs(cstat); /* Always include internal defs. */
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        strcpy(nextToken, cstat->next_char);
        tmpname = nextToken;
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        if (!tmpname || !*tmpname || !(MLevel(OWNER(cstat->program)) >= LWIZ)) {
            include_defs(cstat, OWNER(cstat->program));
            include_defs(cstat, (dbref) 0);
        }

    } else if (!string_compare(temp, "nomacros")) {
        cstat->use_macros = 0;

    } else if (!string_compare(temp, "enddef")) {
        abort_compile(cstat, "$enddef without a previous matching $define.");

    } else if (!string_compare(temp, "def")) {
        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for $def name.");
        (void) insert_def(cstat, tmpname, cstat->next_char);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        free(tmpname);

    } else if (!string_compare(temp, "version")) {
        tmpname = (char *) next_token_raw(cstat);
        if (!ifloat(tmpname))
            abort_compile(cstat,
                          "Expected a floating point number for the version.");
        add_property(cstat->program, "_Version", tmpname, 0);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        free(tmpname);
    } else if (!string_compare(temp, "lib-version")) {
        tmpname = (char *) next_token_raw(cstat);
        if (!ifloat(tmpname))
            abort_compile(cstat,
                          "Expected a floating point number for the version.");
        while (*cstat->next_char)
            cstat->next_char++;
        add_property(cstat->program, "_Lib-Version", tmpname, 0);
        advance_line(cstat);
        free(tmpname);
    } else if (!string_compare(temp, "author")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        tmpname = (char *) cstat->next_char;
        add_property(cstat->program, "_Author", tmpname, 0);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
    } else if (!string_compare(temp, "note")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        tmpname = (char *) cstat->next_char;
        while (*cstat->next_char)
            cstat->next_char++; /* What does this accomplish ??? */
        add_property(cstat->program, "_Note", tmpname, 0);
        advance_line(cstat);
    } else if (!string_compare(temp, "wwwtype")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        tmpname = (char *) cstat->next_char;
        while (*cstat->next_char)
            cstat->next_char++; /* What does this accomplish ??? */
        add_property(cstat->program, "_type", tmpname, 0);
        advance_line(cstat);
    } else if (!string_compare(temp, "ifcancall")
               || !string_compare(temp, "ifncancall")) {
        struct match_data md;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat, "Unexpected end of file for ifcancall.");
        if (string_compare(tmpname, "this")) {
            char tempa[BUFFER_LEN], tempb[BUFFER_LEN];

            strcpy(tempa, match_args);
            strcpy(tempb, match_cmdname);
            init_match(cstat->descr, cstat->player, tmpname, NOTYPE, &md);
            match_registered(&md);
            match_absolute(&md);
            match_me(&md);
            i = (int) match_result(&md);
            strcpy(match_args, tempa);
            strcpy(match_cmdname, tempb);
        } else {
            i = cstat->program;
        }
        free(tmpname);
        if (!OkObj(i)
            || (Typeof(i) == TYPE_GARBAGE))
            abort_compile(cstat,
                          "I don't understand what program you want to check in ifcancall.");
        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname || !*tmpname) {
            free(tmpptr);
            abort_compile(cstat,
                          "I don't understand what function you want to check for.");
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        if (!(DBFETCH(i)->sp.program.code)) {
            struct line *tmpline;

            tmpline = DBFETCH(i)->sp.program.first;
            DBFETCH(i)->sp.program.first = ((struct line *) read_program(i));
            do_compile(cstat->descr, OWNER(i), i, 0);
            free_prog_text(DBFETCH(i)->sp.program.first);
            DBFETCH(i)->sp.program.first = tmpline;
        }
        j = 0;
        if (MLevel(OWNER(i)) > 0 &&
            (MLevel(OWNER(cstat->program)) >= 4
             || OWNER(i) == OWNER(cstat->program) || Linkable(i))
            ) {
            struct publics *pbs;

            pbs = DBFETCH(i)->sp.program.pubs;
            while (pbs) {
                if (!string_compare(tmpname, pbs->subname))
                    break;
                pbs = pbs->next;
            }
            if (pbs && MLevel(OWNER(cstat->program)) >= pbs->mlev)
                j = 1;
        }
        free(tmpname);
        if (!string_compare(temp, "ifncancall"))
            j = !j;
        if (!j) {
            i = 0;
            while ((tmpptr = (char *) next_token_raw(cstat)) &&
                   (i || ((string_compare(tmpptr, "$else"))
                          && (string_compare(tmpptr, "$endif"))))) {
                if (is_preprocessor_conditional(tmpptr))
                    i++;
                else if (!string_compare(tmpptr, "$endif"))
                    i--;
                free(tmpptr);
            }
            if (!tmpptr) {
                abort_compile(cstat,
                              "Unexpected end of file in $ifcancall clause.");
            }
            free(tmpptr);
        }

    } else if (!string_compare(temp, "ifauthor")
               || !string_compare(temp, "ifnauthor")) {
        int needFree = 0;
        struct match_data md;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file while doing $include.");
        if (string_compare(tmpname, "this")) {
            char tempa[BUFFER_LEN], tempb[BUFFER_LEN];

            strcpy(tempa, match_args);
            strcpy(tempb, match_cmdname);
            init_match(cstat->descr, cstat->player, tmpname, NOTYPE, &md);
            match_registered(&md);
            match_absolute(&md);
            match_me(&md);
            i = (int) match_result(&md);
            strcpy(match_args, tempa);
            strcpy(match_cmdname, tempb);
        } else {
            i = cstat->program;
        }
        free(tmpname);
        if (!OkObj(i)
            || (Typeof(i) == TYPE_GARBAGE))
            abort_compile(cstat,
                          "I don't understand what object you want to check with $ifauthor.");
        tmpptr = (char *) get_property_class(i, "_Author");
        if (!tmpptr || !*tmpptr) {
            tmpptr = (char *)malloc(8);
            strcpy(tmpptr, "Unknown");
            needFree = 1;
        }

        tmpname = (char *) cstat->next_char;
        if (!tmpname || !*tmpname) {
            free(tmpptr);
            abort_compile(cstat,
                          "I don't understand what author you are checking for with $ifauthor.");
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        if (needFree)
            free(tmpptr);
        j = equalstr(tmpptr, tmpname);
        if (!string_compare(temp, "ifnauthor"))
            j = !j;
        if (j) {
            i = 0;
            while ((tmpptr = (char *) next_token_raw(cstat)) &&
                   (i || ((string_compare(tmpptr, "$else"))
                          && (string_compare(tmpptr, "$endif"))))) {
                if (is_preprocessor_conditional(tmpptr))
                    i++;
                else if (!string_compare(tmpptr, "$endif"))
                    i--;
                free(tmpptr);
            }
            if (!tmpptr) {
                abort_compile(cstat,
                              "Unexpected end of file in $ifauthor clause.");
            }
            free(tmpptr);
        }

    } else if (!string_compare(temp, "ifver")
               || !string_compare(temp, "iflibver")
               || !string_compare(temp, "ifnver")
               || !string_compare(temp, "ifnlibver")) {
        struct match_data md;
        double verflt = 0;
        double checkflt = 0;
        int needFree = 0;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat, "Unexpected end of file while doing $ifver.");
        if (string_compare(tmpname, "this")) {
            char tempa[BUFFER_LEN], tempb[BUFFER_LEN];

            strcpy(tempa, match_args);
            strcpy(tempb, match_cmdname);
            init_match(cstat->descr, cstat->player, tmpname, NOTYPE, &md);
            match_registered(&md);
            match_absolute(&md);
            match_me(&md);
            i = (int) match_result(&md);
            strcpy(match_args, tempa);
            strcpy(match_cmdname, tempb);
        } else {
            i = cstat->program;
        }
        free(tmpname);
        if (!OkObj(i)
            || (Typeof(i) == TYPE_GARBAGE))
            abort_compile(cstat,
                          "I don't understand what object you want to check with $ifver.");
        if (!string_compare(temp, "ifver") || !string_compare(temp, "ifnver")) {
            tmpptr = (char *) get_property_class(i, "_Version");
        } else {
            tmpptr = (char *) get_property_class(i, "_Lib-Version");
        }
        if (!tmpptr || !*tmpptr) {
            tmpptr = (char *)malloc(4);
            strcpy(tmpptr, "0.0");
            needFree = 1;
        }

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname || !*tmpname) {
            free(tmpptr);
            free(tmpname);
            abort_compile(cstat,
                          "I don't understand what version you want to compare to with $ifver.");
        }
        if (!tmpptr || !ifloat(tmpptr)) {
            verflt = 0.0;
        } else {
            sscanf(tmpptr, "%lg", &verflt);
        }
        if (needFree)
            free(tmpptr);
        if (!tmpname || !ifloat(tmpname)) {
            checkflt = 0.0;
        } else {
            sscanf(tmpname, "%lg", &checkflt);
        }
        free(tmpname);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        j = checkflt <= verflt;
        if (!string_compare(temp, "ifnver")
            || !string_compare(temp, "ifnlibver"))
            j = !j;
        if (!j) {
            i = 0;
            while ((tmpptr = (char *) next_token_raw(cstat)) &&
                   (i || ((string_compare(tmpptr, "$else"))
                          && (string_compare(tmpptr, "$endif"))))) {
                if (is_preprocessor_conditional(tmpptr))
                    i++;
                else if (!string_compare(tmpptr, "$endif"))
                    i--;
                free(tmpptr);
            }
            if (!tmpptr) {
                abort_compile(cstat,
                              "Unexpected end of file in $ifver clause.");
            }
            free(tmpptr);
        }

    } else if (!string_compare(temp, "ifbeta")
               || !string_compare(temp, "ifalpha")
               || !string_compare(temp, "ifnbeta")
               || !string_compare(temp, "ifnalpha")) {
        struct match_data md;
        int vernum, checknum;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat, "Unexpected end of file while doing $ifver.");
        if (string_compare(tmpname, "this")) {
            char tempa[BUFFER_LEN], tempb[BUFFER_LEN];

            strcpy(tempa, match_args);
            strcpy(tempb, match_cmdname);
            init_match(cstat->descr, cstat->player, tmpname, NOTYPE, &md);
            match_registered(&md);
            match_absolute(&md);
            match_me(&md);
            i = (int) match_result(&md);
            strcpy(match_args, tempa);
            strcpy(match_cmdname, tempb);
        } else {
            i = cstat->program;
        }
        free(tmpname);
        if (!OkObj(i)
            || (Typeof(i) == TYPE_GARBAGE))
            abort_compile(cstat,
                          "I don't understand what object you want to check with $ifbeta or $ifalpha.");
        if (!string_compare(temp, "ifbeta") || !string_compare(temp, "ifnbeta")) {
            vernum = get_property_value(i, "_Beta");
        } else {
            vernum = get_property_value(i, "_Alpha");
        }
        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname || !*tmpname) {
            free(tmpptr);
            abort_compile(cstat,
                          "I don't understand what version you want to compare to with $alpha or $beta.");
        }
        if (vernum < 0)
            vernum = 0;
        if (!tmpname || !number(tmpname)) {
            checknum = 1;
        } else {
            checknum = atoi(tmpname);
            if (checknum <= 0)
                checknum = 0;
        }
        free(tmpname);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        j = (checknum <= vernum); /* check to see if versions are the same */
        if (!string_compare(temp, "ifnbeta")
            || !string_compare(temp, "ifnalpha"))
            j = !j;             /* ifnbeta or ifnalpha we want the opposite */
        if (!j) {
            i = 0;
            while ((tmpptr = (char *) next_token_raw(cstat)) &&
                   (i || ((string_compare(tmpptr, "$else"))
                          && (string_compare(tmpptr, "$endif"))))) {
                if (is_preprocessor_conditional(tmpptr))
                    i++;
                else if (!string_compare(tmpptr, "$endif"))
                    i--;
                free(tmpptr);
            }
            if (!tmpptr) {
                abort_compile(cstat,
                              "Unexpected end of file in $ifbeta or $ifalpha clause.");
            }
            free(tmpptr);
        }
    } else if (!string_compare(temp, "pubdef")) {
        char *holder = NULL;

        tmpname = (char *) next_token_raw(cstat);
        holder = tmpname;
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for $pubdef name.");
        if (string_compare(tmpname, ":")
            && (index(tmpname, '/') || index(tmpname, ':')
                || Prop_SeeOnly(tmpname) || Prop_Hidden(tmpname))) {
            free(tmpname);
            abort_compile(cstat,
                          "Invalid $pubdef name.  No /, :, @, nor ~ are allowed.");
        } else {
            if (!string_compare(tmpname, ":")) {
                remove_property(cstat->program, "/_Defs");
            } else {
                const char *defstr = NULL;
                char propname[BUFFER_LEN];
                int doitset = 1;

                while (*cstat->next_char && isspace(*cstat->next_char))
                    cstat->next_char++; /* eating leading spaces */
                defstr = cstat->next_char;

                if (*tmpname == '\\') {
                    char *temppropstr = NULL;

                    (void) *tmpname++;
                    sprintf(propname, "/_Defs/%s", tmpname);
                    temppropstr =
                        (char *) get_property_class(cstat->program, propname);
                    if (temppropstr) {
                        doitset = 0;
                    }
                } else {
                    sprintf(propname, "/_Defs/%s", tmpname);
                }

                if (doitset) {
                    if (defstr && *defstr) {
                        add_property(cstat->program, propname, defstr, 0);
                    } else {
                        remove_property(cstat->program, propname);
                    }
                }
            }

        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        free(holder);

    } else if (!string_compare(temp, "propdoc")) {
        char *holder = NULL;

        tmpname = (char *) next_token_raw(cstat);
        holder = tmpname;
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for $propdoc name.");
        if (!string_compare(tmpname, ":")) {
            remove_property(cstat->program, "/_propdoc/");
        } else {
            const char *defstr = NULL;
            char propname[BUFFER_LEN];
            int doitset = 1;

            while (*cstat->next_char && isspace(*cstat->next_char))
                cstat->next_char++; /* eating leading spaces */
            defstr = cstat->next_char;

            if (*tmpname == '\\') {
                char *temppropstr = NULL;

                (void) *tmpname++;
                sprintf(propname, "/_propdoc/%s", tmpname);
                temppropstr =
                    (char *) get_property_class(cstat->program, propname);
                if (temppropstr) {
                    doitset = 0;
                }
            } else {
                sprintf(propname, "/_propdoc/%s", tmpname);
            }

            if (doitset) {
                if (defstr && *defstr) {
                    add_property(cstat->program, propname, defstr, 0);
                } else {
                    remove_property(cstat->program, propname);
                }
            }
        }

        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        free(holder);

    } else if (!string_compare(temp, "iflib")
               || !string_compare(temp, "ifnlib")) {
        struct match_data md;

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file while doing $include.");
        {
            char tempa[BUFFER_LEN], tempb[BUFFER_LEN];

            strcpy(tempa, match_args);
            strcpy(tempb, match_cmdname);
            init_match(cstat->descr, cstat->player, tmpname, NOTYPE, &md);
            match_registered(&md);
            match_absolute(&md);
            match_me(&md);
            i = (int) match_result(&md);
            strcpy(match_args, tempa);
            strcpy(match_cmdname, tempb);
        }
        free(tmpname);
        if ((!OkObj(i)
             || (Typeof(i) == TYPE_GARBAGE))
            ? 0 : (Typeof(i) == TYPE_PROGRAM)) {
            j = 1;
        } else {
            j = 0;
        }
        if (!string_compare(temp, "ifnlib"))
            j = !j;
        if (!j) {
            i = 0;
            while ((tmpptr = (char *) next_token_raw(cstat)) &&
                   (i || ((string_compare(tmpptr, "$else"))
                          && (string_compare(tmpptr, "$endif"))))) {
                if (is_preprocessor_conditional(tmpptr))
                    i++;
                else if (!string_compare(tmpptr, "$endif"))
                    i--;
                free(tmpptr);
            }
            if (!tmpptr) {
                abort_compile(cstat,
                              "Unexpected end of file in $iflib clause.");
            }
            free(tmpptr);
        }

    } else if (!string_compare(temp, "ansi")) {
        /* char *holder = NULL; */

        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        if (((FLAGS(cstat->player) & INTERACTIVE) &&
             !(FLAGS(cstat->player) & READMODE)) || cstat->force_err_display)
            anotify_nolisten(cstat->player, cstat->next_char, 1);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
    } else if (!string_compare(temp, "libdef")) {
        char *holder = NULL;

        tmpname = (char *) next_token_raw(cstat);
        holder = tmpname;
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for $lib/def name.");

        if (index(tmpname, '/') ||
            index(tmpname, ':') ||
            Prop_SeeOnly(tmpname) || Prop_Hidden(tmpname)) {
            free(tmpname);
            abort_compile(cstat,
                          "Invalid $libdef name. No /, :, @, nor ~ allowed.");
        } else {                /* okay string */
            char propname[BUFFER_LEN];
            char defstr[BUFFER_LEN];
            int doitset = 1;

            while (*cstat->next_char && isspace(*cstat->next_char))
                cstat->next_char++; /* eat leading space */
            if (*tmpname == '\\') {
                char *temppropstr = NULL;

                (void) *tmpname++;
                sprintf(propname, "/_defs/%s", tmpname);
                temppropstr =
                    (char *) get_property_class(cstat->program, propname);
                if (temppropstr)
                    doitset = 0;
            } else {
                sprintf(propname, "/_defs/%s", tmpname);
            }

            snprintf(defstr, sizeof(defstr), "#%i \"%s\" call",
                     cstat->program, tmpname);

            if (doitset)
                add_property(cstat->program, propname, defstr, 0);
        }
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);
        free(holder);
    } else if (!string_compare(temp, "include")) {
        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file while doing $include.");
        if (!number(tmpname)) {
            {
                char tempa[BUFFER_LEN], tempb[BUFFER_LEN];

                strcpy(tempa, match_args);
                strcpy(tempb, match_cmdname);
                init_match(cstat->descr, cstat->player, tmpname, NOTYPE, &md);
                match_registered(&md);
                match_absolute(&md);
                match_me(&md);
                i = (int) match_result(&md);
                strcpy(match_args, tempa);
                strcpy(match_cmdname, tempb);
            }
            free(tmpname);
            if (!OkObj(i)
                || (Typeof(i) == TYPE_GARBAGE))
                abort_compile(cstat,
                              "I don't understand what object you want to $include.");
            include_defs(cstat, (dbref) i);
        } else {
            int inc_type;

            inc_type = atoi(tmpname);
            free(tmpname);
            if (inc_type == 0) {
                cstat->use_macros = 1;
                include_defs(cstat, OWNER(cstat->program));
                include_defs(cstat, (dbref) 0);
            } else if (inc_type == 1) {
                include_defs(cstat, OWNER(cstat->program));
                include_defs(cstat, (dbref) 0);
            } else if (inc_type == 2) {
                cstat->use_macros = 1;
            }
        }

    } else if (!string_compare(temp, "undef")) {
        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for name to $undef.");
        kill_def(cstat, tmpname);
        free(tmpname);

    } else if (!string_compare(temp, "echo")) {
        while (*cstat->next_char && isspace(*cstat->next_char))
            cstat->next_char++; /* eating leading spaces */
        if (((FLAGS(cstat->player) & INTERACTIVE) &&
             !(FLAGS(cstat->player) & READMODE)) || cstat->force_err_display)
            notify_nolisten(cstat->player, cstat->next_char, 1);
        while (*cstat->next_char)
            cstat->next_char++;
        advance_line(cstat);

    } else if (!string_compare(temp, "ifdef")
               || !string_compare(temp, "ifndef")) {
        char temp2[BUFFER_LEN];

        tmpname = (char *) next_token_raw(cstat);
        if (!tmpname)
            abort_compile(cstat,
                          "Unexpected end of file looking for $ifdef condition.");
        strcpy(temp2, tmpname);
        free(tmpname);
        for (i = 1; temp2[i] && (temp2[i] != '=') && (temp2[i] != '>')
             && (temp2[i] != '<'); i++) ;
        tmpname = &(temp2[i]);
        i = (temp2[i] ==
             '>') ? 1 : ((temp2[i] == '=') ? 0 : ((temp2[i] == '<') ? -1 : -2));
        *tmpname = '\0';
        tmpname++;
        tmpptr = (char *) expand_def(cstat, temp2);
        if (i == -2) {
            j = (!tmpptr);
            if (tmpptr)
                free(tmpptr);
        } else {
            if (!tmpptr) {
                j = 1;
            } else {
                j = string_compare(tmpptr, tmpname);
                j = !((!i && !j) || ((i * j) > 0));
                free(tmpptr);
            }
        }
        if (!string_compare(temp, "ifndef"))
            j = !j;
        if (j) {
            i = 0;
            while ((tmpptr = (char *) next_token_raw(cstat)) &&
                   (i || ((string_compare(tmpptr, "$else"))
                          && (string_compare(tmpptr, "$endif"))))) {
                if (is_preprocessor_conditional(tmpptr))
                    i++;
                else if (!string_compare(tmpptr, "$endif"))
                    i--;
                free(tmpptr);
            }
            if (!tmpptr) {
                abort_compile(cstat,
                              "Unexpected end of file in $ifdef clause.");
            }
            free(tmpptr);
        }

    } else if (!string_compare(temp, "else")) {
        i = 0;
        while ((tmpptr = (char *) next_token_raw(cstat)) &&
               (i || (string_compare(tmpptr, "$endif")))) {
            if (is_preprocessor_conditional(tmpptr))
                i++;
            else if (!string_compare(tmpptr, "$endif"))
                i--;
            free(tmpptr);
        }
        if (!tmpptr) {
            abort_compile(cstat, "Unexpected end of file in $else clause.");
        }
        free(tmpptr);

    } else if (!string_compare(temp, "endif")) {

    } else {
        abort_compile(cstat, "Unrecognized compiler directive.");
    }
    return 1;
}


/* return string */
const char *
do_string(COMPSTATE *cstat)
{
    static char buf[BUFFER_LEN];
    int i = 0, quoted = 0;

    buf[i] = *cstat->next_char;
    cstat->next_char++;
    i++;
    while ((quoted || *cstat->next_char != ENDSTRING) && *cstat->next_char) {
        if (*cstat->next_char == '\\' && !quoted) {
            quoted++;
            cstat->next_char++;
        } else if (*cstat->next_char == 'r' && quoted) {
            buf[i++] = '\r';
            cstat->next_char++;
            quoted = 0;
        } else if (*cstat->next_char == 'n' && quoted) {
            buf[i++] = '\n';
            cstat->next_char++;
            quoted = 0;
        } else if (*cstat->next_char == '[' && quoted) {
            buf[i++] = ESCAPE_CHAR;
            cstat->next_char++;
            quoted = 0;
        } else {
            buf[i] = *cstat->next_char;
            i++;
            cstat->next_char++;
            quoted = 0;
        }
    }
    if (!*cstat->next_char) {
        abort_compile(cstat, "Unterminated string found at end of line.");
    }
    cstat->next_char++;
    buf[i] = '\0';
    return alloc_string(buf);
}



/* process special.  Performs special processing.
   It sets up FOR and IF structures.  Remember --- for those,
   we've got to set aside an extra argument space.         */
struct INTERMEDIATE *
process_special(COMPSTATE *cstat, const char *token)
{
    static char buf[BUFFER_LEN];
    const char *tok;
    struct INTERMEDIATE *nw;

    if (!string_compare(token, ":")) {
        const char *proc_name;
        int argsflag = 0;

        if (cstat->curr_proc)
            abort_compile(cstat, "Definition within definition.");
        proc_name = next_token(cstat);
        if (!proc_name)
            abort_compile(cstat, "Unexpected end of file within procedure.");

        strcpy(buf, proc_name);
        if (proc_name)
            free((void *) proc_name);
        proc_name = buf;

        if (*proc_name && buf[strlen(buf) - 1] == '[') {
            argsflag = 1;
            buf[strlen(buf) - 1] = '\0';
            if (!*proc_name)
                abort_compile(cstat, "Bad procedure name.");
        }

        nw = new_inst(cstat);

        nw->no = cstat->nowords++;
        nw->in.type = PROG_FUNCTION;
        nw->in.line = cstat->lineno;
        nw->in.data.mufproc =
            (struct muf_proc_data *) malloc(sizeof(struct muf_proc_data));
        nw->in.data.mufproc->procname = string_dup(proc_name);
        nw->in.data.mufproc->vars = 0;

        nw->in.data.mufproc->args = 0;
        nw->in.data.mufproc->varnames = NULL;
        nw->in.data.mufproc->varnames = NULL;

        cstat->curr_proc = nw;

        if (argsflag) {
            const char *varspec;
            const char *varname;
            int argsdone = 0;
            int outflag = 0;

            do {
                varspec = next_token(cstat);
                if (!varspec)
                    abort_compile(cstat,
                                  "Unexpected end of file within procedure arguments declaration.");

                if (!strcmp(varspec, "]")) {
                    argsdone = 1;
                } else if (!strcmp(varspec, "--")) {
                    outflag = 1;
                } else if (!outflag) {
                    varname = index(varspec, ':');
                    if (varname) {
                        varname++;
                    } else {
                        varname = varspec;
                    }
                    if (*varname) {
                        if (add_scopedvar(cstat, varname, PROG_UNTYPED) < 0)
                            abort_compile(cstat, "Variable limit exceeded.");

                        nw->in.data.mufproc->vars ++;

                        nw->in.data.mufproc->args++;
                    }
                }
                if (varspec)
                    free((void *) varspec);
            } while (!argsdone);
        }

        add_proc(cstat, proc_name, nw, PROG_UNTYPED);

        return nw;
    } else if (!string_compare(token, ";")) {
        int i, varcnt;

        if (cstat->control_stack)
            abort_compile(cstat,
                          "Unexpected end of procedure definition. (Usually unterminated if-then or loops.)");
        if (!cstat->curr_proc)
            abort_compile(cstat, "Procedure end without body.");
        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_PRIMITIVE;
        nw->in.line = cstat->lineno;
        nw->in.data.number = IN_RET;
        varcnt = cstat->curr_proc->in.data.mufproc->vars;

        if (varcnt) {
            cstat->curr_proc->in.data.mufproc->varnames =
                (const char **) calloc(varcnt, sizeof(char *));
            for (i = 0; i < varcnt; ++i) {
                cstat->curr_proc->in.data.mufproc->varnames[i] =
                    cstat->scopedvars[i];
                cstat->scopedvars[i] = 0;
            }
        }

        if (cstat->procs->lnotes) {
            if (cstat->procs->labels) {
                struct LABEL_NOTE *n;
                struct LABEL_LIST *l, *tmpl;

                for (n = cstat->procs->lnotes; n; n = cstat->procs->lnotes) {
                    if (!(l = find_label(cstat, n->name))) {
                        sprintf(buf, "Unrecognized word '%s.", n->name);
                        abort_compile(cstat, buf);
                        break;  /* never gets here */
                    }

                    cstat->procs->lnotes = n->next;
                    n->code->in.data.number = get_address(cstat, l->code, 0);

                    free((void *) n->name);
                    free((void *) n);
                } cstat->procs->lnotes = NULL;

                for (l = cstat->procs->labels; l; l = tmpl) {
                    tmpl = l->next;
                    free((void *) l->name);
                    free((void *) l);
                } cstat->procs->labels = NULL;
            } else {
                sprintf(buf, "Unrecognized word '%s.",
                        cstat->procs->lnotes->name);
                abort_compile(cstat, buf);
            }
        }

        cstat->curr_proc = 0;
        return nw;
    } else if (!string_compare(token, "IF")) {
        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_IF;
        nw->in.line = cstat->lineno;
        nw->in.data.call = 0;
        add_control_structure(cstat, CTYPE_IF, nw);
        return nw;
    } else if (!string_compare(token, "ELSE")) {
        struct INTERMEDIATE *eef;
        int ctrltype = innermost_control_type(cstat);

        switch (ctrltype) {
            case CTYPE_IF:
                break;
            case CTYPE_TRY:
                abort_compile(cstat, "Unterminated TRY-CATCH block at ELSE.");
                break;
            case CTYPE_CATCH:
                abort_compile(cstat,
                              "Unterminated CATCH-ENDCATCH block at ELSE.");
                break;
            case CTYPE_FOR:
            case CTYPE_BEGIN:
                abort_compile(cstat, "Unterminated Loop at ELSE.");
                break;
            default:
                abort_compile(cstat, "ELSE without IF.");
                break;
        }

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_JMP;
        nw->in.line = cstat->lineno;
        nw->in.data.call = 0;

        eef = pop_control_structure(cstat, CTYPE_IF, 0);
        add_control_structure(cstat, CTYPE_ELSE, nw);
        eef->in.data.number = get_address(cstat, nw, 1);
        return nw;
    } else if (!string_compare(token, "THEN")) {
        /* can't use 'if' because it's a reserved word */
        struct INTERMEDIATE *eef;
        int ctrltype = innermost_control_type(cstat);

        switch (ctrltype) {
            case CTYPE_IF:
            case CTYPE_ELSE:
                break;
            case CTYPE_TRY:
                abort_compile(cstat, "Unterminated TRY-CATCH block at THEN.");
                break;
            case CTYPE_CATCH:
                abort_compile(cstat,
                              "Unterminated CATCH-ENDCATCH block at THEN.");
                break;
            case CTYPE_FOR:
            case CTYPE_BEGIN:
                abort_compile(cstat, "Unterminated Loop at THEN.");
                break;
            default:
                abort_compile(cstat, "THEN without IF.");
                break;
        }

        prealloc_inst(cstat);
        eef = pop_control_structure(cstat, CTYPE_IF, CTYPE_ELSE);
        eef->in.data.number = get_address(cstat, cstat->nextinst, 0);
        return NULL;
    } else if (!string_compare(token, "BEGIN")) {
        prealloc_inst(cstat);
        add_control_structure(cstat, CTYPE_BEGIN, cstat->nextinst);
        return NULL;
    } else if (!string_compare(token, "FOR")) {
        struct INTERMEDIATE *new2, *new3;

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_PRIMITIVE;
        nw->in.line = cstat->lineno;
        nw->in.data.number = IN_FOR;
        new2 = (nw->next = new_inst(cstat));
        new2->no = cstat->nowords++;
        new2->in.type = PROG_PRIMITIVE;
        new2->in.line = cstat->lineno;
        new2->in.data.number = IN_FORITER;
        new3 = (new2->next = new_inst(cstat));
        new3->no = cstat->nowords++;
        new3->in.line = cstat->lineno;
        new3->in.type = PROG_IF;
        new3->in.data.number = 0;

        add_control_structure(cstat, CTYPE_FOR, new2);
        cstat->nested_fors++;
        return nw;
    } else if (!string_compare(token, "FOREACH")) {
        struct INTERMEDIATE *new2, *new3;

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_PRIMITIVE;
        nw->in.line = cstat->lineno;
        nw->in.data.number = IN_FOREACH;
        new2 = (nw->next = new_inst(cstat));
        new2->no = cstat->nowords++;
        new2->in.type = PROG_PRIMITIVE;
        new2->in.line = cstat->lineno;
        new2->in.data.number = IN_FORITER;
        new3 = (new2->next = new_inst(cstat));
        new3->no = cstat->nowords++;
        new3->in.line = cstat->lineno;
        new3->in.type = PROG_IF;
        new3->in.data.number = 0;

        add_control_structure(cstat, CTYPE_FOR, new2);
        cstat->nested_fors++;
        return nw;
    } else if (!string_compare(token, "UNTIL")) {
        /* can't use 'if' because it's a reserved word */
        struct INTERMEDIATE *eef;
        struct INTERMEDIATE *curr;
        int ctrltype = innermost_control_type(cstat);

        switch (ctrltype) {
            case CTYPE_FOR:
                cstat->nested_fors--;
            case CTYPE_BEGIN:
                break;
            case CTYPE_TRY:
                abort_compile(cstat, "Unterminated TRY-CATCH block at UNTIL.");
                break;
            case CTYPE_CATCH:
                abort_compile(cstat,
                              "Unterminated CATCH-ENDCATCH block at UNTIL.");
                break;
            case CTYPE_IF:
            case CTYPE_ELSE:
                abort_compile(cstat, "Unterminated IF-THEN at UNTIL.");
                break;
            default:
                abort_compile(cstat, "Loop start not found for UNTIL.");
                break;
        }

        prealloc_inst(cstat);
        resolve_loop_addrs(cstat, get_address(cstat, cstat->nextinst, 1));
        eef = pop_control_structure(cstat, CTYPE_BEGIN, CTYPE_FOR);

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_IF;
        nw->in.line = cstat->lineno;
        nw->in.data.number = get_address(cstat, eef, 0);

        if (ctrltype == CTYPE_FOR) {
            curr = (nw->next = new_inst(cstat));
            curr->no = cstat->nowords++;
            curr->in.type = PROG_PRIMITIVE;
            curr->in.line = cstat->lineno;
            curr->in.data.number = IN_FORPOP;
        }
        return nw;
    } else if (!string_compare(token, "WHILE")) {
        struct INTERMEDIATE *curr;
        int trycount;

        if (!in_loop(cstat))
            abort_compile(cstat, "Can't have a WHILE outside of a loop.");

        trycount = count_trys_inside_loop(cstat);
        nw = curr = NULL;
        while (trycount-- > 0) {
            if (!nw) {
                nw = curr = new_inst(cstat);
            } else {
                nw = (nw->next = new_inst(cstat));
            }
            nw->no = cstat->nowords++;
            nw->in.type = PROG_PRIMITIVE;
            nw->in.line = cstat->lineno;
            nw->in.data.number = IN_TRYPOP;
        }
        if (nw) {
            nw = (nw->next = new_inst(cstat));
        } else {
            curr = nw = new_inst(cstat);
        }
        nw->no = cstat->nowords++;
        nw->in.type = PROG_IF;
        nw->in.line = cstat->lineno;
        nw->in.data.number = 0;

        add_loop_exit(cstat, nw);
        return curr;
    } else if (!string_compare(token, "BREAK")) {
        /* can't use 'if' because it's a reserved word */
        int trycount;
        struct INTERMEDIATE *curr;

        if (!in_loop(cstat))
            abort_compile(cstat, "Can't have a BREAK outside of a loop.");

        trycount = count_trys_inside_loop(cstat);
        nw = curr = NULL;
        while (trycount-- > 0) {
            if (!nw) {
                nw = curr = new_inst(cstat);
            } else {
                nw = (nw->next = new_inst(cstat));
            }
            nw->no = cstat->nowords++;
            nw->in.type = PROG_PRIMITIVE;
            nw->in.line = cstat->lineno;
            nw->in.data.number = IN_TRYPOP;
        }
        if (nw) {
            nw = (nw->next = new_inst(cstat));
        } else {
            curr = nw = new_inst(cstat);
        }
        nw->no = cstat->nowords++;
        nw->in.type = PROG_JMP;
        nw->in.line = cstat->lineno;
        nw->in.data.number = 0;

        add_loop_exit(cstat, nw);
        return curr;
    } else if (!string_compare(token, "CONTINUE")) {
        /* can't use 'if' because it's a reserved word */
        struct INTERMEDIATE *beef;
        struct INTERMEDIATE *curr;
        int trycount;

        if (!in_loop(cstat))
            abort_compile(cstat, "Can't CONTINUE outside of a loop.");

        beef = locate_control_structure(cstat, CTYPE_FOR, CTYPE_BEGIN);
        trycount = count_trys_inside_loop(cstat);
        nw = curr = NULL;
        while (trycount-- > 0) {
            if (!nw) {
                nw = curr = new_inst(cstat);
            } else {
                nw = (nw->next = new_inst(cstat));
            }
            nw->no = cstat->nowords++;
            nw->in.type = PROG_PRIMITIVE;
            nw->in.line = cstat->lineno;
            nw->in.data.number = IN_TRYPOP;
        }
        if (nw) {
            nw = (nw->next = new_inst(cstat));
        } else {
            curr = nw = new_inst(cstat);
        }
        nw->no = cstat->nowords++;
        nw->in.type = PROG_JMP;
        nw->in.line = cstat->lineno;
        nw->in.data.number = get_address(cstat, beef, 0);

        return curr;
    } else if (!string_compare(token, "REPEAT")) {
        /* can't use 'if' because it's a reserved word */
        struct INTERMEDIATE *eef;
        struct INTERMEDIATE *curr;
        int ctrltype = innermost_control_type(cstat);

        switch (ctrltype) {
            case CTYPE_FOR:
                cstat->nested_fors--;
            case CTYPE_BEGIN:
                break;
            case CTYPE_TRY:
                abort_compile(cstat, "Unterminated TRY-CATCH block at REPEAT.");
                break;
            case CTYPE_CATCH:
                abort_compile(cstat,
                              "Unterminated CATCH-ENDCATCH block at REPEAT.");
                break;
            case CTYPE_IF:
            case CTYPE_ELSE:
                abort_compile(cstat, "Unterminated IF-THEN at REPEAT.");
                break;
            default:
                abort_compile(cstat, "Loop start not found for REPEAT.");
                break;
        }

        prealloc_inst(cstat);
        resolve_loop_addrs(cstat, get_address(cstat, cstat->nextinst, 1));
        eef = pop_control_structure(cstat, CTYPE_BEGIN, CTYPE_FOR);

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_JMP;
        nw->in.line = cstat->lineno;
        nw->in.data.number = get_address(cstat, eef, 0);

        if (ctrltype == CTYPE_FOR) {
            curr = (nw->next = new_inst(cstat));
            curr->no = cstat->nowords++;
            curr->in.type = PROG_PRIMITIVE;
            curr->in.line = cstat->lineno;
            curr->in.data.number = IN_FORPOP;
        }

        return nw;
    } else if (!string_compare(token, "TRY")) {
        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_TRY;
        nw->in.line = cstat->lineno;
        nw->in.data.number = 0;

        add_control_structure(cstat, CTYPE_TRY, nw);
        cstat->nested_trys++;

        return nw;
    } else if (!string_compare(token, "CATCH")
               || !string_compare(token, "CATCH_DETAILED")) {
        /* can't use 'if' because it's a reserved word */
        struct INTERMEDIATE *eef;
        struct INTERMEDIATE *curr;
        struct INTERMEDIATE *jump;
        int ctrltype = innermost_control_type(cstat);

        switch (ctrltype) {
            case CTYPE_TRY:
                break;
            case CTYPE_FOR:
            case CTYPE_BEGIN:
                abort_compile(cstat, "Unterminated Loop at CATCH.");
                break;
            case CTYPE_IF:
            case CTYPE_ELSE:
                abort_compile(cstat, "Unterminated IF-THEN at CATCH.");
                break;
            case CTYPE_CATCH:
            default:
                abort_compile(cstat, "No TRY found for CATCH.");
                break;
        }

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_PRIMITIVE;
        nw->in.line = cstat->lineno;
        nw->in.data.number = IN_TRYPOP;

        jump = (nw->next = new_inst(cstat));
        jump->no = cstat->nowords++;
        jump->in.type = PROG_JMP;
        jump->in.line = cstat->lineno;
        jump->in.data.number = 0;

        curr = (jump->next = new_inst(cstat));
        curr->no = cstat->nowords++;
        curr->in.type = PROG_PRIMITIVE;
        curr->in.line = cstat->lineno;
        curr->in.data.number = IN_CATCH;
        if (!string_compare(token, "CATCH_DETAILED")) {
            curr->in.data.number = IN_CATCH_DETAILED;
        }

        eef = pop_control_structure(cstat, CTYPE_TRY, 0);
        cstat->nested_trys--;
        eef->in.data.number = get_address(cstat, curr, 0);
        add_control_structure(cstat, CTYPE_CATCH, jump);

        return nw;
    } else if (!string_compare(token, "ENDCATCH")) {
        /* can't use 'if' because it's a reserved word */
        struct INTERMEDIATE *eef;
        int ctrltype = innermost_control_type(cstat);

        switch (ctrltype) {
            case CTYPE_CATCH:
                break;
            case CTYPE_FOR:
            case CTYPE_BEGIN:
                abort_compile(cstat, "Unterminated Loop at ENDCATCH.");
                break;
            case CTYPE_IF:
            case CTYPE_ELSE:
                abort_compile(cstat, "Unterminated IF-THEN at ENDCATCH.");
                break;
            case CTYPE_TRY:
            default:
                abort_compile(cstat, "No CATCH found for ENDCATCH.");
                break;
        }

        prealloc_inst(cstat);
        eef = pop_control_structure(cstat, CTYPE_CATCH, 0);
        eef->in.data.number = get_address(cstat, cstat->nextinst, 0);
        return NULL;
    } else if (!string_compare(token, "CALL")) {
        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_PRIMITIVE;
        nw->in.line = cstat->lineno;
        nw->in.data.number = IN_CALL;
        return nw;
    } else if (!string_compare(token, "WIZCALL")
               || !string_compare(token, "PUBLIC")
               || !string_compare(token, "SELFCALL")
               || !string_compare(token, "SAFECALL")
               || !string_compare(token, "ARCHCALL")
               || !string_compare(token, "MAGECALL")
               || !string_compare(token, "BOYCALL")) {
        struct PROC_LIST *p;
        struct publics *pub;
        int wizflag = 0;
    int selfflag = 0;
        int wizlevel = LMAGE;

        if (!string_compare(token, "MAGECALL")) {
            wizflag = 1;
            wizlevel = LMAGE;
        } else if (!string_compare(token, "WIZCALL")) {
            wizflag = 1;
            wizlevel = LWIZ;
        } else if (!string_compare(token, "ARCHCALL")) {
            wizflag = 1;
            wizlevel = LARCH;
        } else if (!string_compare(token, "BOYCALL")) {
            wizflag = 1;
            wizlevel = LBOY;
        } else if (!string_compare(token, "SELFCALL") || !string_compare(token, "SAFECALL")) {
        selfflag = 1;
    }
        if (cstat->curr_proc)
            abort_compile(cstat,
                          "PUBLIC or WIZCALL declaration within procedure.");
        tok = next_token(cstat);
        if ((!tok) || !call(cstat, tok))
            abort_compile(cstat,
                          "Subroutine unknown in PUBLIC or WIZCALL declaration.");
        for (p = cstat->procs; p; p = p->next)
            if (!string_compare(p->name, tok))
                break;
        if (!p)
            abort_compile(cstat,
                          "Subroutine unknown in PUBLIC or WIZCALL declaration.");
        if (!cstat->currpubs) {
            cstat->currpubs = (struct publics *) malloc(sizeof(struct publics));

            cstat->currpubs->next = NULL;
            cstat->currpubs->subname = (char *) string_dup(tok);
            if (tok)
                free((void *) tok);
            cstat->currpubs->addr.no = get_address(cstat, p->code, 0);
            cstat->currpubs->mlev = wizflag ? wizlevel : 1;
            cstat->currpubs->self = selfflag;
			cstat->currpubs->usecount = 0;
            return 0;
        }
        for (pub = cstat->currpubs; pub;) {
            if (!string_compare(tok, pub->subname)) {
                abort_compile(cstat, "Function already declared public.");
            } else {
                if (pub->next) {
                    pub = pub->next;
                } else {
                    pub->next =
                        (struct publics *) malloc(sizeof(struct publics));

                    pub = pub->next;
                    pub->next = NULL;
                    pub->subname = (char *) string_dup(tok);
                    if (tok)
                        free((void *) tok);
                    pub->addr.no = get_address(cstat, p->code, 0);
                    pub->mlev = wizflag ? wizlevel : 1;
            pub->self = selfflag;
                    pub = NULL;
                }
            }
        }
        return 0;
    } else if (!string_compare(token, "VAR")) {
        if (cstat->curr_proc) {
            tok = next_token(cstat);
            if (!tok)
                abort_compile(cstat, "Unexpected end of program.");
            if (add_scopedvar(cstat, tok, PROG_UNTYPED) < 0)
                abort_compile(cstat, "Variable limit exceeded.");
            if (tok)
                free((void *) tok);
            cstat->curr_proc->in.data.mufproc->vars ++;
        } else {
            tok = next_token(cstat);
            if (!tok)
                abort_compile(cstat, "Unexpected end of program.");
            if (!add_variable(cstat, tok, PROG_UNTYPED))
                abort_compile(cstat, "Variable limit exceeded.");
            if (tok)
                free((void *) tok);
        }
        return 0;
    } else if (!string_compare(token, "VAR!")) {
        if (cstat->curr_proc) {
            struct INTERMEDIATE *nw;

            tok = next_token(cstat);
            if (!tok)
                abort_compile(cstat, "Unexpected end of program.");
            if (add_scopedvar(cstat, tok, PROG_UNTYPED) < 0)
                abort_compile(cstat, "Variable limit exceeded.");
            if (tok)
                free((void *) tok);

            nw = new_inst(cstat);
            nw->no = cstat->nowords++;
            nw->in.type = PROG_SVAR_BANG;
            nw->in.line = cstat->lineno;
            nw->in.data.number = cstat->curr_proc->in.data.mufproc->vars ++;

            return nw;
        } else {
            abort_compile(cstat, "VAR! used outside of procedure.");
        }
        return 0;
    } else if (!string_compare(token, "LVAR")) {
        if (cstat->curr_proc)
            abort_compile(cstat, "Local variable declared within procedure.");
        tok = next_token(cstat);
        if (!tok || (add_localvar(cstat, tok, PROG_UNTYPED) == -1))
            abort_compile(cstat, "Local variable limit exceeded.");
        if (tok)
            free((void *) tok);
        return 0;
    } else {
        sprintf(buf, "Unrecognized special form %s found. (%d)", token,
                cstat->lineno);
        abort_compile(cstat, buf);
    }
}

#ifdef MODULAR_SUPPORT

extern struct module *modules;

int
modprim(const char *token)
{
	struct mod_primlist *mpr = NULL;
	struct module *m;
	int i;

	m = modules;
	while (m) {
		if (m->prims) {
			for (i = 0; m->prims[i].name; i++) {
				if (!string_compare(m->prims[i].name, token)) {
					mpr = &m->prims[i]; 
					break;
				}
			}

			if (mpr)
				break;
		}

		m = m->next;
	}
	
	if (mpr)
		return 1;
	else
		return 0;
}

/* return modular prim word. */
struct INTERMEDIATE *
modprim_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw, *cur;
    struct mod_primlist *mpr = NULL;
	struct module *m;
	int i;

    cur = nw = new_inst(cstat);

	m = modules;
	while (m) {
		if (m->prims) {
			for (i = 0; m->prims[i].name; i++) {
				if (!string_compare(m->prims[i].name, token)) {
					mpr = &m->prims[i]; 
					break;
				}
			}

			if (mpr)
				break;
		}

		m = m->next;
	}  

    cur->no = cstat->nowords++;
    cur->in.type = PROG_MODPRIM;
    cur->in.line = cstat->lineno;
    cur->in.data.modprim = mpr;

    return nw;
}

#endif /* MODULAR_SUPPORT */

/* return primitive word. */
struct INTERMEDIATE *
primitive_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw, *cur;
    int pnum, loop;

    pnum = get_primitive(token);
    cur = nw = new_inst(cstat);
    if (pnum == IN_RET || pnum == IN_JMP) {
        for (loop = 0; loop < cstat->nested_trys; loop++) {
            cur->no = cstat->nowords++;
            cur->in.type = PROG_PRIMITIVE;
            cur->in.line = cstat->lineno;
            cur->in.data.number = IN_TRYPOP;
            cur->next = new_inst(cstat);
            cur = cur->next;
        }
        for (loop = 0; loop < cstat->nested_fors; loop++) {
            cur->no = cstat->nowords++;
            cur->in.type = PROG_PRIMITIVE;
            cur->in.line = cstat->lineno;
            cur->in.data.number = IN_FORPOP;
            cur->next = new_inst(cstat);
            cur = cur->next;
        }
    }

    cur->no = cstat->nowords++;
    cur->in.type = PROG_PRIMITIVE;
    cur->in.line = cstat->lineno;
    cur->in.data.number = pnum;

    return nw;
}

/* return self pushing word (string) */
struct INTERMEDIATE *
string_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_STRING;
    nw->in.line = cstat->lineno;
    nw->in.data.string = alloc_prog_string(token);
    return nw;
}
/* return self pushing word (float) */ struct INTERMEDIATE *
float_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_FLOAT;
    nw->in.line = cstat->lineno;
#ifdef CYGWIN
    /* What does God neeed with a starship? */
    nw->in.data.fnumber = 0.0;
    if (!string_compare(token, "inf")) {
        nw->in.data.fnumber = (float) INF;
    }
    if (!string_compare(token, "-inf")) {
        nw->in.data.fnumber = (float) NINF;
    }
    if (!string_compare(token, "nan")) {
        nw->in.data.fnumber = (float) NAN;
    }
    if (!nw->in.data.fnumber) {
        sscanf(token, "%lg", &(nw->in.data.fnumber));
    }
#else
    sscanf(token, "%lg", &(nw->in.data.fnumber));
#endif
    return nw;
}
/* return self pushing word (number) */ struct INTERMEDIATE *
number_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_INTEGER;
    nw->in.line = cstat->lineno;
    nw->in.data.number = atoi(token);
    return nw;
}

/* do a subroutine call --- push address onto stack, then make a primitive
   CALL.
   */ struct INTERMEDIATE *
call_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    struct PROC_LIST *p;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_EXEC;
    nw->in.line = cstat->lineno;
    for (p = cstat->procs; p; p = p->next)
        if (!string_compare(p->name, token))
            break;

    nw->in.data.number = get_address(cstat, p->code, 0);
    return nw;
}

struct INTERMEDIATE *
jump_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    struct LABEL_LIST *p = find_label(cstat, token);

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_JMP;
    nw->in.line = cstat->lineno;

    nw->in.data.number = get_address(cstat, p->code, 0);
    return nw;
}

struct INTERMEDIATE *
label_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    char buf[BUFFER_LEN];

    if (cstat->curr_proc) {
        strcpy(buf, token);
        buf[strlen(token) - 1] = '\0';

        if (find_label(cstat, buf))
            abort_compile(cstat, "Cannot define the same label twice.");

        nw = new_inst(cstat);
        nw->no = cstat->nowords++;
        nw->in.type = PROG_LABEL;
        nw->in.line = cstat->lineno;
        nw->in.data.labelname = string_dup(buf);
        add_label(cstat, buf, nw);

        return nw;
    } else {
        abort_compile(cstat, "Cannot define label outside of procedure.");
    }
}

struct INTERMEDIATE *
quoted_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    struct PROC_LIST *p;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_ADD;
    nw->in.line = cstat->lineno;
    for (p = cstat->procs; p; p = p->next)
        if (!string_compare(p->name, token))
            break;

    nw->in.data.number = get_address(cstat, p->code, 0);
    return nw;
}

struct INTERMEDIATE *
quoted_label_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    struct LABEL_LIST *p;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_ADD;
    nw->in.line = cstat->lineno;

    for (p = cstat->procs->labels; p; p = p->next)
        if (!string_compare(p->name, token))
            break;

    nw->in.data.number = get_address(cstat, p->code, 0);
    return nw;
}

/* This one's a little strange.  Since you need to be able to */
/*  define labels AFTER they're used, I make note of any address */
/*  retreival attempts that've failed, and then check again after */
/*  the end of the function.  */

struct INTERMEDIATE *
quoted_note_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    struct LABEL_NOTE *n;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_ADD;
    nw->in.line = cstat->lineno;
    nw->in.data.number = 0;     /* If it can't find anything it'll abort anyway. */

    n = (struct LABEL_NOTE *) malloc(sizeof(struct LABEL_NOTE));
    n->name = alloc_string(token);
    n->code = nw;
    n->next = cstat->procs->lnotes;
    cstat->procs->lnotes = n;

    return nw;
}

/* returns number corresponding to variable number.
   We assume that it DOES exist */ struct INTERMEDIATE *
var_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    int i, var_no;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_VAR;
    nw->in.line = cstat->lineno;
    for (var_no = i = 0; i < MAX_VAR; i++) {
        if (!cstat->variables[i])
            break;
        if (!string_compare(token, cstat->variables[i]))
            var_no = i;
    }
    nw->in.data.number = var_no;

    return nw;
}

struct INTERMEDIATE *
svar_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    int i, var_no;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_SVAR;
    nw->in.line = cstat->lineno;
    for (i = var_no = 0; i < MAX_VAR; i++) {
        if (!cstat->scopedvars[i])
            break;
        if (!string_compare(token, cstat->scopedvars[i]))
            var_no = i;
    }
    nw->in.data.number = var_no;

    return nw;
}

struct INTERMEDIATE *
lvar_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    int i, var_no;

    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_LVAR;
    nw->in.line = cstat->lineno;
    for (i = var_no = 0; i < MAX_VAR; i++) {
        if (!cstat->localvars[i])
            break;
        if (!string_compare(token, cstat->localvars[i]))
            var_no = i;
    }
    nw->in.data.number = var_no;

    return nw;
}

/* check if object is in database before putting it in */
struct INTERMEDIATE *
object_word(COMPSTATE *cstat, const char *token)
{
    struct INTERMEDIATE *nw;
    int objno;

    objno = atol(token + 1);
    nw = new_inst(cstat);
    nw->no = cstat->nowords++;
    nw->in.type = PROG_OBJECT;
    nw->in.line = cstat->lineno;
    nw->in.data.objref = objno;
    return nw;
}

/* support routines for internal data structures. */

/* add procedure to procedures list */
void
add_proc(COMPSTATE *cstat, const char *proc_name,
         struct INTERMEDIATE *place, int rettype)
{
    struct PROC_LIST *nw;

    nw = (struct PROC_LIST *) malloc(sizeof(struct PROC_LIST));

    nw->name = alloc_string(proc_name);
    nw->returntype = rettype;
    nw->code = place;
    nw->labels = NULL;
    nw->lnotes = NULL;
    nw->next = cstat->procs;
    cstat->procs = nw;
}

/* add a label to the list */
void
add_label(COMPSTATE *cstat, const char *label_name, struct INTERMEDIATE *place)
{
    struct LABEL_LIST *nw;

    nw = (struct LABEL_LIST *) malloc(sizeof(struct LABEL_LIST));

    nw->name = alloc_string(label_name);
    nw->code = place;
    nw->next = cstat->procs->labels;
    cstat->procs->labels = nw;
}

/* add if to control stack */ void
add_control_structure(COMPSTATE *cstat, int typ, struct INTERMEDIATE *place)
{
    struct CONTROL_STACK *nu;

    nu = (struct CONTROL_STACK *) malloc(sizeof(struct CONTROL_STACK));

    nu->place = place;
    nu->type = typ;
    nu->next = cstat->control_stack;
    nu->extra = 0;
    cstat->control_stack = nu;
}
/* add while to current loop's list of exits remaining to be resolved. */ void
add_loop_exit(COMPSTATE *cstat, struct INTERMEDIATE *place)
{
    struct CONTROL_STACK *nu;
    struct CONTROL_STACK *loop;

    loop = cstat->control_stack;

    while (loop && loop->type != CTYPE_BEGIN && loop->type != CTYPE_FOR) {
        loop = loop->next;
    }
    if (!loop)
        return;

    nu = (struct CONTROL_STACK *) malloc(sizeof(struct CONTROL_STACK));

    nu->place = place;
    nu->type = CTYPE_WHILE;
    nu->next = 0;
    nu->extra = loop->extra;
    loop->extra = nu;
}

/* Returns true if a loop start is in the control structure stack. */
int
in_loop(COMPSTATE *cstat)
{
    struct CONTROL_STACK *loop;

    loop = cstat->control_stack;
    while (loop && loop->type != CTYPE_BEGIN && loop->type != CTYPE_FOR) {
        loop = loop->next;
    }
    return (loop != NULL);
}

/* Returns the type of the innermost nested control structure. */
int
innermost_control_type(COMPSTATE *cstat)
{
    struct CONTROL_STACK *ctrl;

    ctrl = cstat->control_stack;
    if (!ctrl)
        return 0;

    return ctrl->type;
}
/* Returns number of TRYs before topmost Loop */ int
count_trys_inside_loop(COMPSTATE *cstat)
{
    struct CONTROL_STACK *loop;
    int count = 0;

    loop = cstat->control_stack;

    while (loop) {
        if (loop->type == CTYPE_FOR || loop->type == CTYPE_BEGIN) {
            break;
        }
        if (loop->type == CTYPE_TRY) {
            count++;
        }
        loop = loop->next;
    }

    return count;
}

/* returns topmost begin or for off the stack */
struct INTERMEDIATE *
locate_control_structure(COMPSTATE *cstat, int type1, int type2)
{
    struct CONTROL_STACK *loop;

    loop = cstat->control_stack;

    while (loop) {
        if (loop->type == type1 || loop->type == type2) {
            return loop->place;
        }
        loop = loop->next;
    }

    return 0;
}

/* checks if topmost loop stack item is a for */
struct INTERMEDIATE *
innermost_control_place(COMPSTATE *cstat, int type1)
{
    struct CONTROL_STACK *ctrl;

    ctrl = cstat->control_stack;
    if (!ctrl)
        return 0;
    if (ctrl->type != type1)
        return 0;

    return ctrl->place;
}
/* Pops off the innermost control structure and returns the place. */ struct INTERMEDIATE *
pop_control_structure(COMPSTATE *cstat, int type1, int type2)
{
    struct CONTROL_STACK *ctrl;
    struct INTERMEDIATE *place;

    ctrl = cstat->control_stack;
    if (!ctrl)
        return NULL;
    if (ctrl->type != type1 && ctrl->type != type2)
        return NULL;

    place = ctrl->place;
    cstat->control_stack = ctrl->next;
    free(ctrl);

    return place;
}
/* pops first while off the innermost control structure, if it's a loop. */ struct INTERMEDIATE *
pop_loop_exit(COMPSTATE *cstat)
{
    struct INTERMEDIATE *temp;
    struct CONTROL_STACK *tofree;
    struct CONTROL_STACK *parent;

    parent = cstat->control_stack;

    if (!parent)
        return 0;
    if (parent->type != CTYPE_BEGIN && parent->type != CTYPE_FOR)
        return 0;
    if (!parent->extra)
        return 0;
    if (parent->extra->type != CTYPE_WHILE)
        return 0;

    temp = parent->extra->place;
    tofree = parent->extra;
    parent->extra = parent->extra->extra;
    free((void *) tofree);
    return temp;
}

void
resolve_loop_addrs(COMPSTATE *cstat, int where)
{
    struct INTERMEDIATE *eef;

    while ((eef = pop_loop_exit(cstat)))
        eef->in.data.number = where;
    eef = innermost_control_place(cstat, CTYPE_FOR);
    if (eef) {
        eef->next->in.data.number = where;
    }
}
/* adds variable.  Return 0 if no space left */ int
add_variable(COMPSTATE *cstat, const char *varname, int valtype)
{
    int i;

    for (i = RES_VAR; i < MAX_VAR; i++)
        if (!cstat->variables[i])
            break;

    if (i == MAX_VAR)
        return 0;

    cstat->variables[i] = alloc_string(varname);
    cstat->variabletypes[i] = valtype;
    return i;
}
/* adds local variable.  Return 0 if no space left */ int
add_scopedvar(COMPSTATE *cstat, const char *varname, int valtype)
{
    int i;

    for (i = 0; i < MAX_VAR; i++)
        if (!cstat->scopedvars[i])
            break;

    if (i == MAX_VAR)
        return -1;

    cstat->scopedvars[i] = alloc_string(varname);
    cstat->scopedvartypes[i] = valtype;
    return i;
}
/* adds local variable.  Return 0 if no space left */ int
add_localvar(COMPSTATE *cstat, const char *varname, int valtype)
{
    int i;

    for (i = 0; i < MAX_VAR; i++)
        if (!cstat->localvars[i])
            break;

    if (i == MAX_VAR)
        return -1;

    cstat->localvars[i] = alloc_string(varname);
    cstat->localvartypes[i] = valtype;
    return i;
}
/* predicates for procedure calls */ int
special(const char *token)
{
    return (token && !(string_compare(token, ":")
                       && string_compare(token, ";")
                       && string_compare(token, "IF")
                       && string_compare(token, "ELSE")
                       && string_compare(token, "THEN")
                       && string_compare(token, "BEGIN")
                       && string_compare(token, "FOR")
                       && string_compare(token, "FOREACH")
                       && string_compare(token, "UNTIL")
                       && string_compare(token, "WHILE")
                       && string_compare(token, "BREAK")
                       && string_compare(token, "CONTINUE")
                       && string_compare(token, "REPEAT")
                       && string_compare(token, "TRY")
                       && string_compare(token, "CATCH")
                       && string_compare(token, "CATCH_DETAILED")
                       && string_compare(token, "ENDCATCH")
                       && string_compare(token, "CALL")
                       && string_compare(token, "PUBLIC")
                       && string_compare(token, "MAGECALL")
                       && string_compare(token, "WIZCALL")
                       && string_compare(token, "ARCHCALL")
                       && string_compare(token, "BOYCALL")
                       && string_compare(token, "SELFCALL")
                       && string_compare(token, "SAFECALL")
                       && string_compare(token, "LVAR")
                       && string_compare(token, "VAR!")
                       && string_compare(token, "VAR")));
}

/* see if procedure call */ int
call(COMPSTATE *cstat, const char *token)
{
    struct PROC_LIST *i;

    for (i = cstat->procs; i; i = i->next)
        if (!string_compare(i->name, token))
            return 1;

    return 0;
}

/* see if it's a jumpable label */ int
jump(COMPSTATE *cstat, const char *token)
{
    return (find_label(cstat, token) != NULL);
}

/* see if it's a quoted procedure name */ int
quoted(COMPSTATE *cstat, const char *token)
{
    return (cstat->procs && *token == '\'' && call(cstat, token + 1));
}

/* see if it's a quoted label name */ int
quoted_label(COMPSTATE *cstat, const char *token)
{
    return (cstat->procs && *token == '\'' && jump(cstat, token + 1));
}

/* see if it's a quoted label name */ int
quoted_note(COMPSTATE *cstat, const char *token)
{
    return (cstat->procs && *token == '\'');
}

/* see if it's an object # */
int
object(const char *token)
{
    if (*token == NUMBER_TOKEN && number(token + 1))
        return 1;
    else
        return 0;
}

/* see if it's label definition */
int
label(const char *token)
{
    int i = strlen(token);

    return (i > 1 && token[i - 1] == ':');
}

/* see if string */ int
string(const char *token)
{
    return (token[0] == '"');
}

int
variable(COMPSTATE *cstat, const char *token)
{
    int i;

    for (i = 0; i < MAX_VAR && cstat->variables[i]; i++)
        if (!string_compare(token, cstat->variables[i]))
            return 1;

    return 0;
}

int
scopedvar(COMPSTATE *cstat, const char *token)
{
    int i;

    for (i = 0; i < MAX_VAR && cstat->scopedvars[i]; i++)
        if (!string_compare(token, cstat->scopedvars[i]))
            return 1;

    return 0;
}

int
localvar(COMPSTATE *cstat, const char *token)
{
    int i;

    for (i = 0; i < MAX_VAR && cstat->localvars[i]; i++)
        if (!string_compare(token, cstat->localvars[i]))
            return 1;

    return 0;
}
/* see if token is primitive */ int
primitive(const char *token)
{
    int primnum;

    primnum = get_primitive(token);
    return (primnum && primnum <= BASE_MAX - PRIMS_INTERNAL_CNT);
}
/* return primitive instruction */ int
get_primitive(const char *token)
{
    hash_data *hd;

    if ((hd = find_hash(token, primitive_list, COMP_HASH_SIZE)) == NULL)
        return 0;
    else {
        return (hd->ival);
    }
}
/* clean up as nicely as we can. */ void
cleanpubs(struct publics *mypub)
{
    struct publics *tmppub;

    while (mypub) {
        tmppub = mypub->next;
        free(mypub->subname);
        free(mypub);
        mypub = tmppub;
    }
}

#ifdef MCP_SUPPORT
void
clean_mcpbinds(struct mcp_binding *mypub)
{
    struct mcp_binding *tmppub;

    while (mypub) {
        tmppub = mypub->next;
        free(mypub->pkgname);
        free(mypub->msgname);
        free(mypub);
        mypub = tmppub;
    }
}
#endif
void

append_intermediate_chain(struct INTERMEDIATE *chain, struct INTERMEDIATE *add)
{
    while (chain->next)
        chain = chain->next;
    chain->next = add;
}
/* free resources used by INTERMEDIATE struct */ void
free_intermediate_node(struct INTERMEDIATE *wd)
{
    int varcnt, j;

    if (wd->in.type == PROG_STRING) {
        if (wd->in.data.string)
            free((void *) wd->in.data.string);
    }

    if (wd->in.type == PROG_FUNCTION) {
        free((void *) wd->in.data.mufproc->procname);
        varcnt = wd->in.data.mufproc->vars;

        if (wd->in.data.mufproc->varnames) {
            for (j = 0; j < varcnt; ++j)
                free((void *) wd->in.data.mufproc->varnames[j]);
            free((void *) wd->in.data.mufproc->varnames);
        }
        free((void *) wd->in.data.mufproc);
    }
    free((void *) wd);
}

/* free chain of intermediate nodes during compiling */
void
free_intermediate_chain(struct INTERMEDIATE *wd)
{
    struct INTERMEDIATE *tempword;

    while (wd) {
        tempword = wd->next;
        free_intermediate_node(wd);
        wd = tempword;
    }
}
void
cleanup(COMPSTATE *cstat)
{
/*  struct INTERMEDIATE *wd, *tempword; */
    struct CONTROL_STACK *eef, *tempif;
    struct PROC_LIST *p, *tempp;
    int i;

    free_intermediate_chain(cstat->first_word);
    cstat->first_word = 0;

    for (eef = cstat->control_stack; eef; eef = tempif) {
        tempif = eef->next;
        free((void *) eef);
    } cstat->control_stack = 0;

    for (p = cstat->procs; p; p = tempp) {
        struct LABEL_LIST *l = p->labels, *tmpl;
        struct LABEL_NOTE *n = p->lnotes, *tmpn;

        tempp = p->next;

        for (; l; l = tmpl) {
            tmpl = l->next;
            free((void *) l->name);
            free((void *) l);
        }

        for (; n; n = tmpn) {
            tmpn = n->next;
            free((void *) n->name);
            free((void *) n);
        }

        free((void *) p->name);
        free((void *) p);
    } cstat->procs = 0;

    /* for (l = cstat->labels; l; l = templ) {
       templ = l->next;
       free((void *) l->name);
       free((void *) l);
       } cstat->labels = 0; */

    purge_defs(cstat);
    free_addresses(cstat);

    for (i = RES_VAR; i < MAX_VAR && cstat->variables[i]; i++) {
        free((void *) cstat->variables[i]);
        cstat->variables[i] = 0;
    }

    for (i = 0; i < MAX_VAR && cstat->scopedvars[i]; i++) {
        free((void *) cstat->scopedvars[i]);
        cstat->scopedvars[i] = 0;
    }

    for (i = 0; i < MAX_VAR && cstat->localvars[i]; i++) {
        free((void *) cstat->localvars[i]);
        cstat->localvars[i] = 0;
    }
}



/* copy program to an array */
void
copy_program(COMPSTATE *cstat)
{
    /*
     * Everything should be peachy keen now, so we don't do any error checking
     */
    struct INTERMEDIATE *curr;
    struct inst *code;
    int i, j, varcnt;

    if (!cstat->first_word)
        v_abort_compile(cstat, "Nothing to compile.");

    code = (struct inst *) malloc(sizeof(struct inst) * (cstat->nowords + 1));

    i = 0;
    for (curr = cstat->first_word; curr; curr = curr->next) {
        code[i].type = curr->in.type;
        code[i].line = curr->in.line;
        switch (code[i].type) {
            case PROG_PRIMITIVE:
            case PROG_INTEGER:
            case PROG_SVAR:
            case PROG_SVAR_AT:
            case PROG_SVAR_AT_CLEAR:
            case PROG_SVAR_BANG:
            case PROG_LVAR:
            case PROG_LVAR_AT:
            case PROG_LVAR_AT_CLEAR:
            case PROG_LVAR_BANG:
            case PROG_VAR:
                code[i].data.number = curr->in.data.number;
                break;
            case PROG_FLOAT:
                code[i].data.fnumber = curr->in.data.fnumber;
                break;
            case PROG_STRING:
                code[i].data.string = curr->in.data.string ?
                    alloc_prog_string(curr->in.data.string->data) : 0;
                break;
            case PROG_FUNCTION:
                code[i].data.mufproc = (struct muf_proc_data *)
                    malloc(sizeof(struct muf_proc_data));
                code[i].data.mufproc->procname =
                    string_dup(curr->in.data.mufproc->procname);
                code[i].data.mufproc->vars = varcnt =
                    curr->in.data.mufproc->vars;

                code[i].data.mufproc->args = curr->in.data.mufproc->args;
                if (varcnt) {
                    if (curr->in.data.mufproc->varnames) {
                        code[i].data.mufproc->varnames =
                            (const char **) calloc(varcnt, sizeof(char *));
                        for (j = 0; j < varcnt; ++j)
                            code[i].data.mufproc->varnames[j] =
                                string_dup(curr->in.data.mufproc->varnames[j]);
                    } else {
                        code[i].data.mufproc->varnames = NULL;
                    }
                } else {
                    code[i].data.mufproc->varnames = NULL;
                }
                break;
            case PROG_OBJECT:
                code[i].data.objref = curr->in.data.objref;
                break;
            case PROG_ADD:
                code[i].data.addr =
                    alloc_addr(cstat, curr->in.data.number, code);
                break;
            case PROG_IF:
            case PROG_JMP:
            case PROG_EXEC:
            case PROG_TRY:
                code[i].data.call = code + curr->in.data.number;
                break;
            case PROG_LABEL:
                code[i].data.labelname = curr->in.data.labelname;
                break;
#ifdef MODULAR_SUPPORT
			case PROG_MODPRIM:
				code[i].data.modprim = curr->in.data.modprim;
				break;
#endif
            default:
                v_abort_compile(cstat,
                                "Unknown type compile!  Internal error.");
                break;
        }
        i++;
    }
    DBFETCH(cstat->program)->sp.program.code = code;
}

void
set_start(COMPSTATE *cstat)
{
    DBFETCH(cstat->program)->sp.program.siz = cstat->nowords;

    /* address instr no is resolved before this gets called. */
    DBFETCH(cstat->program)->sp.program.start =
        (DBFETCH(cstat->program)->sp.program.code + cstat->procs->code->no);
}
/* allocate and initialize data linked structure. */ struct INTERMEDIATE *
alloc_inst(void)
{
    struct INTERMEDIATE *nw;

    nw = (struct INTERMEDIATE *) malloc(sizeof(struct INTERMEDIATE));

    nw->next = 0;
    nw->no = 0;
    nw->in.type = 0;
    nw->in.line = 0;
    nw->in.data.number = 0;
    return nw;
}
struct INTERMEDIATE *
prealloc_inst(COMPSTATE *cstat)
{
    struct INTERMEDIATE *ptr;
    struct INTERMEDIATE *nw;

    /* only allocate at most one extra instr */
    if (cstat->nextinst)
        return NULL;

    nw = alloc_inst();

    if (!cstat->nextinst) {
        cstat->nextinst = nw;
    } else {
        for (ptr = cstat->nextinst; ptr->next; ptr = ptr->next) ;
        ptr->next = nw;
    }

    nw->no = cstat->nowords;

    return nw;
}

struct INTERMEDIATE *
new_inst(COMPSTATE *cstat)
{
    struct INTERMEDIATE *nw;

    nw = cstat->nextinst;
    if (!nw) {
        nw = alloc_inst();
    }
    cstat->nextinst = nw->next;

    nw->next = NULL;

    return nw;
}

/* allocate an address */
struct prog_addr *
alloc_addr(COMPSTATE *cstat, int offset, struct inst *codestart)
{
    struct prog_addr *nw;

    nw = (struct prog_addr *) malloc(sizeof(struct prog_addr));

    nw->links = 1;
    nw->progref = cstat->program;
    nw->data = codestart + offset;
    return nw;
}

void
free_prog(dbref prog)
{
    int i;
	struct funcprof *fpr;
    struct inst *c = DBFETCH(prog)->sp.program.code;
    int siz = DBFETCH(prog)->sp.program.siz;

    if (c) {
        if (DBFETCH(prog)->sp.program.instances) {
            fprintf(stderr, "Freeing program %s with %d instances reported\n",
                    unparse_object(MAN, prog),
                    DBFETCH(prog)->sp.program.instances);
        }
        i = scan_instances(prog);

        if (i) {
            fprintf(stderr, "Freeing program %s with %d instances found\n",
                    unparse_object(GOD, prog), i);
        }
        for (i = 0; i < siz; i++) {
            if (c[i].type == PROG_ADD) {
                if (c[i].data.addr->links != 1) {
                    fprintf(stderr,
                            "Freeing address in %s with link count %d\n",
                            unparse_object(MAN, prog), c[i].data.addr->links);
                }
                free(c[i].data.addr);
#ifdef MODULAR_SUPPORT
			} else if (c[i].type == PROG_MODPRIM) {
				struct module *m = c[i].data.modprim->mod;
				struct mod_proglist *prm = m->progs;

				c[i].data.modprim->links--;

				while (prm) {
					if (prm->prog == prog) {
						if (prm->next)
						   prm->next->prev = prm->prev;
						if (prm->prev)
							prm->prev->next = prm->next;
						if (prm == m->progs)
							m->progs = m->progs->next;
							log_status("MODULE: Removed prog %d from module %s because of %s.\r\n", prog, m->info->name,c[i].data.modprim->name);
						free((void *)prm);
						break;
					}
					
					prm = prm->next;
				}
#endif /* MODULAR_SUPPORT */
            } else {
                CLEAR(c + i);
            }
        }
        free((void *) c);
    }
    DBFETCH(prog)->sp.program.code = 0;
    DBFETCH(prog)->sp.program.siz = 0;
    DBFETCH(prog)->sp.program.start = 0;

	while (DBFETCH(prog)->sp.program.fprofile) {
		fpr = DBFETCH(prog)->sp.program.fprofile->next;
		free((void *)DBFETCH(prog)->sp.program.fprofile->funcname);
		free((void *)DBFETCH(prog)->sp.program.fprofile);
		DBFETCH(prog)->sp.program.fprofile = fpr;
	}	
}

long
size_prog(dbref prog)
{
    struct inst *c;
    long i, j, varcnt, siz, byts = 0;

    c = DBFETCH(prog)->sp.program.code;
    if (!c)
        return 0;
    siz = DBFETCH(prog)->sp.program.siz;
    for (i = 0L; i < siz; i++) {
        byts += sizeof(*c);
        if (c[i].type == PROG_FUNCTION) {
            byts += strlen(c[i].data.mufproc->procname) + 1;
            varcnt = c[i].data.mufproc->vars;

            if (c[i].data.mufproc->varnames) {
                for (j = 0; j < varcnt; ++j)
                    byts += strlen(c[i].data.mufproc->varnames[j])
                        + 1;
                byts += sizeof(char **) * varcnt;
            }
            byts += sizeof(struct muf_proc_data);
        } else if (c[i].type == PROG_STRING && c[i].data.string) {
            byts += strlen(c[i].data.string->data) + 1;
            byts += sizeof(struct shared_string);
        } else if ((c[i].type == PROG_ADD))
            byts += sizeof(struct prog_addr);
    }
    byts += size_pubs(DBFETCH(prog)->sp.program.pubs);
    return byts;
}

static void
add_primitive(int val)
{
    hash_data hd;

    hd.ival = val;
    if (add_hash(base_inst[val - BASE_MIN], hd, primitive_list, COMP_HASH_SIZE)
        == NULL)
        panic("Out of memory");
    else
        return;
}

void
clear_primitives(void)
{
    kill_hash(primitive_list, COMP_HASH_SIZE, 0);
    return;
}

void
init_primitives(void)
{
    int i;

    clear_primitives();
    for (i = BASE_MIN; i <= BASE_MAX; i++) {
        add_primitive(i);
    }
    IN_FORPOP = get_primitive(" FORPOP");

    IN_FORITER = get_primitive(" FORITER");
    IN_FOR = get_primitive(" FOR");
    IN_FOREACH = get_primitive(" FOREACH");
    IN_TRYPOP = get_primitive(" TRYPOP");
}
