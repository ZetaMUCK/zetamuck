

#include "copyright.h"
#include "config.h"

#include "db.h"
#include "props.h"
#include "params.h"
#include "tune.h"
#include "interface.h"
#include "externs.h"
#include "strings.h"

struct object *db = 0;
dbref db_top = 0;
dbref recyclable = NOTHING;
int db_load_format = 0;
bool db_hash_passwords = 0;
int db_hash_ver = 0;

#ifndef DB_INITIAL_SIZE
#define DB_INITIAL_SIZE 10000
#endif /* DB_INITIAL_SIZE */

#ifdef DB_DOUBLING

dbref db_size = DB_INITIAL_SIZE;

#endif /* DB_DOUBLING */

struct macrotable *macrotop;

#ifndef MALLOC_PROFILING
extern char *alloc_string(const char *);
#endif

int number(const char *s);
int ifloat(const char *s);
void putproperties(FILE * f, int obj);
void getproperties(FILE * f, int obj);

#ifdef DBDEBUG
/* This function is a total quickhack, mostly because of how */
/*  DBFETCH works.  -Hinoserm.                               */
short
dbcheck(const char *file, int line, dbref item)
{
    if (!OkObj(item)) {
        log_status
            ("DB FATAL ERROR! Attempt to access a bad object at %s:%d. Object was %d.\n",
             file, line, item);
        abort();
    }

    return 0;
}
#endif /* DBDEBUG */

dbref
getparent(dbref obj)
{
    int limit = 88;

    if (!OkObj(obj))
        return GLOBAL_ENVIRONMENT;
    do {
        if (Typeof(obj) == TYPE_THING && (FLAGS(obj) & VEHICLE) && limit-- > 0) {
            obj = DBFETCH(obj)->sp.thing.home;
            if (obj == NIL)
                obj = GLOBAL_ENVIRONMENT;
            if (obj != NOTHING && Typeof(obj) == TYPE_PLAYER)
                obj = DBFETCH(obj)->sp.player.home;
        } else {
            obj = getloc(obj);
        }
    } while (obj != NOTHING && Typeof(obj) == TYPE_THING);
    if (!limit)
        return GLOBAL_ENVIRONMENT;
    return obj;
}


void
free_line(struct line *l)
{
    if (l->this_line)
        free((void *) l->this_line);
    free((void *) l);
}

void
free_prog_text(struct line *l)
{
    struct line *next;

    while (l) {
        next = l->next;
        free_line(l);
        l = next;
    }
}

#ifdef DB_DOUBLING

static void
db_grow(dbref newtop)
{
    struct object *newdb;

    if (newtop > db_top) {
        db_top = newtop;
        if (!db) {
            /* make the initial one */
            db_size = DB_INITIAL_SIZE;
            while (db_top > db_size)
                db_size += 1000;
            if ((db = (struct object *)
                 malloc(db_size * sizeof(struct object))) == 0) {
                fprintf(stderr, "PANIC: Unable to allocate new object.\n");
                abort();
            }
            // uninit check: temporarily init the name of #0 to NULL -davin
            db[0].name = NULL;
        }
        /* maybe grow it */
        if (db_top > db_size) {
            /* make sure it's big enough */
            while (db_top > db_size)
                db_size += 1000;
            if ((newdb = (struct object *)
                 realloc((void *) db, db_size * sizeof(struct object))) == 0) {
                fprintf(stderr, "PANIC: Unable to reallocate object.\n");
                abort();
            }
            db = newdb;
        }
    }
}

#else /* DB_DOUBLING */

static void
db_grow(dbref newtop)
{
    struct object *newdb;

    if (newtop > db_top) {
        db_top = newtop + (newtop / 10); /*CrT */
        if (db) {
            if ((newdb = (struct object *)
                 realloc((void *) db, db_top * sizeof(struct object))) == 0) {
                fprintf(stderr, "PANIC: Unable to reallocate object.\n");
                abort();
            }
            db = newdb;
        } else {
            /* make the initial one */
            int startsize = (newtop >= DB_INITIAL_SIZE) ?
                newtop : DB_INITIAL_SIZE;
            if ((db = (struct object *)
                 malloc(startsize * sizeof(struct object))) == 0) {
                fprintf(stderr, "PANIC: Unable to allocate new object.\n");
                abort();
            }
        }
    }
}

#endif /* DB_DOUBLING */

void
db_clear_object(dbref player, dbref i)
{
    struct object *o = DBFETCH(i);

    bzero(o, sizeof(struct object));
    NAME(i) = 0;
    ts_newobject(player, o);
    o->location = NOTHING;
    o->contents = NOTHING;
    o->exits = NOTHING;
    o->next = NOTHING;
    o->properties = 0;

#ifdef DISKBASE
    o->propsfpos = 0;
    o->propstime = 0;
    o->propsmode = PROPS_UNLOADED;
    o->nextold = NOTHING;
    o->prevold = NOTHING;
#endif

    /* DBDIRTY(i); */
    /* flags you must initialize yourself */
    /* type-specific fields you must also initialize */
}

dbref
new_object(dbref player)
{
    dbref newobj;

    if (recyclable != NOTHING) {
        newobj = recyclable;
        recyclable = DBFETCH(newobj)->next;
        db_free_object(newobj);
    } else {
        newobj = db_top;
        db_grow(db_top + 1);
    }

    /* clear it out */
    db_clear_object(player, newobj);
    DBDIRTY(newobj);
    return newobj;
}


dbref
new_program(dbref player, const char *name)
{
    unsigned char mlvl;
    dbref newprog;
    char buf[BUFFER_LEN];

    newprog = new_object(player);
    player = OWNER(player);

    NAME(newprog) = alloc_string(name);
    sprintf(buf, "A scroll containing a spell called %s", name);
    SETDESC(newprog, buf);
    DBFETCH(newprog)->location = player;
    FLAGS(newprog) = TYPE_PROGRAM;

    mlvl = MLevel(player);
    if (mlvl < 1)
        mlvl = 2;
    else if (mlvl > 3)
        mlvl = 3;
    SetMLevel(newprog, mlvl);

    OWNER(newprog) = player;
    DBFETCH(newprog)->sp.program.first = 0;
    DBFETCH(newprog)->sp.program.curr_line = 0;
    DBFETCH(newprog)->sp.program.siz = 0;
    DBFETCH(newprog)->sp.program.code = 0;
    DBFETCH(newprog)->sp.program.start = 0;
    DBFETCH(newprog)->sp.program.pubs = 0;
    DBFETCH(newprog)->sp.program.fprofile = NULL;
#ifdef MCP_SUPPORT
    DBFETCH(newprog)->sp.program.mcpbinds = 0;
#endif
    DBFETCH(newprog)->sp.program.proftime.tv_sec = 0;
    DBFETCH(newprog)->sp.program.proftime.tv_usec = 0;
    DBFETCH(newprog)->sp.program.profstart = 0;
    DBFETCH(newprog)->sp.program.profuses = 0;
    DBFETCH(newprog)->sp.program.instances = 0;
    PUSH(newprog, DBFETCH(player)->contents);
    DBDIRTY(newprog);
    DBDIRTY(player);

    return newprog;
}


void
putref(FILE * f, dbref ref)
{
    if (fprintf(f, "%d\n", ref) < 0) {
        fprintf(stderr, "PANIC: Unable to write to db file.\n");
        abort();
    }
}

void
putfref(FILE * f, dbref ref, dbref ref2, dbref ref3, dbref ref4, dbref pow1,
        dbref pow2)
{
    if (fprintf(f, "%d %d %d %d %d %d\n", ref, ref2, ref3, ref4, pow1, pow2) ==
        EOF) {
        fprintf(stderr, "PANIC: Unable write to db file.\n");
        abort();
    }
}

void
puttimestampEx(FILE * f, int ref, dbref ref2)
{
    if (fprintf(f, "%d %d\n", ref, ref2) == EOF) {
        fprintf(stderr, "PANIC: Unable write to db file.\n");
        abort();
    }
}


static void
putstring(FILE * f, const char *s)
{
    if (s) {
        if (fputs(s, f) == EOF) {
            fprintf(stderr, "PANIC: Unable to write to db file.\n");
            abort();
        }
    }
    if (putc('\n', f) == EOF) {
        fprintf(stderr, "PANIC: Unable to write to db file.\n");
        abort();
    }
}

void
putproperties_rec(FILE * f, const char *dir, dbref obj)
{
    PropPtr pref;
    PropPtr p, pptr;
    char buf[BUFFER_LEN];
    char name[BUFFER_LEN];

    pref = first_prop_nofetch(obj, dir, &pptr, name);
    while (pref) {
        p = pref;
        db_putprop(f, dir, p);
        strcat(strcpy(buf, dir), name);
        if (PropDir(p))
            putproperties_rec(f, strcat(buf, "/"), obj);
        pref = next_prop(pptr, pref, name);
    }
}

/*** CHANGED:
was: void putproperties(FILE *f, PropPtr p)
 is: void putproperties(FILE *f, dbref obj)
***/
void
putproperties(FILE * f, dbref obj)
{
    putstring(f, "*Props*");
    db_dump_props(f, obj);
    /* putproperties_rec(f, "/", obj); */
    putstring(f, "*End*");
}

extern FILE *input_file;
extern FILE *delta_infile;
extern FILE *delta_outfile;

#ifdef DISKBASE

int
fetch_propvals(dbref obj, const char *dir)
{
    PropPtr p, pptr;

    int cnt = 0;

    char buf[BUFFER_LEN];

    char name[BUFFER_LEN];

    p = first_prop_nofetch(obj, dir, &pptr, name);
    while (p > 0) {
        cnt = (cnt || propfetch(obj, p));
        if (PropDir(p)) {
            strcpy(buf, dir);
            strcat(buf, name);
            strcat(buf, "/");
            fetch_propvals(obj, buf);
        }
        p = next_prop(pptr, p, name);
    }
    return cnt;
}

void
putprops_copy(FILE * f, dbref obj)
{
    char buf[BUFFER_LEN * 3];

    char *ptr;

    FILE *g;

    if (DBFETCH(obj)->propsmode != PROPS_UNLOADED) {
        if (fetch_propvals(obj, "/")) {
            fseek(f, 0L, 2);
        }
        putproperties(f, obj);
        return;
    }
    if (db_load_format < 7 || db_conversion_flag) {
        if (fetchprops_priority(obj, 1) || fetch_propvals(obj, "/")) {
            fseek(f, 0L, 2);
        }
        putproperties(f, obj);
        return;
    }
    if (FLAGS(obj) & SAVED_DELTA) {
        g = delta_infile;
    } else {
        g = input_file;
    }
    putstring(f, "*Props*");
    if (DBFETCH(obj)->propsfpos) {
        fseek(g, DBFETCH(obj)->propsfpos, 0);
        ptr = fgets(buf, sizeof(buf), g);
        if (!ptr) {
            fprintf(stderr, "PANIC: Error reading prop from db file.\n");
            abort();
        }
        for (;;) {
            ptr = fgets(buf, sizeof(buf), g);
            if (!ptr) {
                fprintf(stderr, "PANIC: Error reading propvalue from db.\n");
                abort();
            }
            if (!string_compare(ptr, "*End*\n"))
                break;
            fputs(buf, f);
        }
    }
    putstring(f, "*End*");
}

#endif /* DISKBASE */


void
macrodump(struct macrotable *node, FILE * f)
{
    if (!node)
        return;

    macrodump(node->left, f);
    putstring(f, node->name);
    putstring(f, node->definition);
    putref(f, node->implementor);
    macrodump(node->right, f);
}

char *
file_line(FILE * f)
{
    char buf[BUFFER_LEN];

    if (!fgets(buf, BUFFER_LEN, f))
        return NULL;
    buf[strlen(buf) - 1] = '\0';
    return alloc_string(buf);
}

void
foldtree(struct macrotable *center)
{
    int count = 0;
    struct macrotable *nextcent = center;

    for (; nextcent; nextcent = nextcent->left)
        count++;
    if (count > 1) {
        for (nextcent = center, count /= 2; count--;
             nextcent = nextcent->left) ;
        if (center->left)
            center->left->right = NULL;
        center->left = nextcent;
        foldtree(center->left);
    }
    for (count = 0, nextcent = center; nextcent; nextcent = nextcent->right)
        count++;
    if (count > 1) {
        for (nextcent = center, count /= 2; count--;
             nextcent = nextcent->right) ;
        if (center->right)
            center->right->left = NULL;
        foldtree(center->right);
    }
}

int
macrochain(struct macrotable *lastnode, FILE * f)
{
    char *line, *line2;
    struct macrotable *newmacro;

    if (!(line = file_line(f)))
        return 0;
    line2 = file_line(f);

    newmacro = (struct macrotable *) new_macro(line, line2, getref(f));
    free(line);
    free(line2);

    if (!macrotop)
        macrotop = (struct macrotable *) newmacro;
    else {
        newmacro->left = lastnode;
        lastnode->right = newmacro;
    }
    return (1 + macrochain(newmacro, f));
}

void
macroload(FILE * f)
{
    int count = 0;

    macrotop = NULL;
    count = macrochain(macrotop, f);
    for (count /= 2; count--; macrotop = macrotop->right) ;
    foldtree(macrotop);
    return;
}

void
log_program_text(struct line *first, dbref player, dbref i)
{
    FILE *f;
    char fname[BUFFER_LEN], buf1[BUFFER_LEN], buf2[BUFFER_LEN];
    time_t lt = current_systime;

#ifndef SANITY
    strcpy(fname, PROGRAM_LOG);
    f = fopen(fname, "ab");
    if (!f) {
        log_status("Couldn't open file %s!\n", fname);
        return;
    }

    fprintf(f,
            "{{{ PROGRAM %s, SAVED AT %s BY %s\n",
            strcpy(buf1, unparse_object(player, i)),
            ctime(&lt), strcpy(buf2, unparse_object(player, player))
        );

    while (first) {
        if (!first->this_line)
            continue;
        fputs(first->this_line, f);
        fputc('\n', f);
        first = first->next;
    }
    fputs("\n\n\n", f);
    fclose(f);
#endif
}

void
write_program(struct line *first, dbref i)
{
    FILE *f;
    char fname[BUFFER_LEN];

    sprintf(fname, "muf/%d.m", (int) i);
    f = fopen(fname, "wb");
    if (!f) {
        log_status("Couldn't open file %s!\n", fname);
        return;
    }
    while (first) {
        if (!first->this_line)
            continue;
        if (fputs(first->this_line, f) == EOF) {
            fprintf(stderr, "PANIC: Unable to write to db file.\n");
            abort();
        }
        if (fputc('\n', f) == EOF) {
            fprintf(stderr, "PANIC: Unable to write to db file.\n");
            abort();
        }
        first = first->next;
    }
    fclose(f);
}

int
db_write_object(FILE * f, dbref i)
{
    struct object *o = DBFETCH(i);
    int j;

#ifdef DISKBASE
    int tmppos;
#endif

    putstring(f, NAME(i));
    putref(f, o->location);
    putref(f, o->contents);
    putref(f, o->next);
    /* write non-internal flags */
    if (Typeof(i) != TYPE_GARBAGE)
        putfref(f, (FLAGS(i) & ~DUMP_MASK), (FLAG2(i) & ~DUM2_MASK),
                (FLAG3(i) & ~DUM3_MASK), (FLAG4(i) & ~DUM4_MASK),
                (POWERS(i) & ~POWERS_DUMP_MASK),
                (POWER2(i) & ~POWER2_DUMP_MASK));
    else
        putfref(f, (FLAGS(i) & ~DUMP_MASK), (FLAG2(i) & ~DUM2_MASK),
                (FLAG3(i) & ~DUM3_MASK), (FLAG4(i) & ~DUM4_MASK), 0, 0);

    puttimestampEx(f, (int) o->ts.created, o->ts.dcreated);
    puttimestampEx(f, (int) o->ts.lastused, o->ts.dlastused);
    putref(f, o->ts.usecount);
    puttimestampEx(f, (int) o->ts.modified, o->ts.dmodified);


#ifdef DISKBASE

    tmppos = ftell(f) + 1;
    putprops_copy(f, i);
    o->propsfpos = tmppos;
    undirtyprops(i);

#else /* !DISKBASE */
    putproperties(f, i);
#endif /* DISKBASE */

    switch (Typeof(i)) {
        case TYPE_THING:
            putref(f, o->sp.thing.home);
            putref(f, o->exits);
            putref(f, OWNER(i));
            putref(f, o->sp.thing.value);
            break;

        case TYPE_ROOM:
            putref(f, o->sp.room.dropto);
            putref(f, o->exits);
            putref(f, OWNER(i));
            break;

        case TYPE_EXIT:
            putref(f, o->sp.exit.ndest);
            for (j = 0; j < o->sp.exit.ndest; j++) {
                putref(f, (o->sp.exit.dest)[j]);
            }
            putref(f, OWNER(i));
            break;

        case TYPE_PLAYER:
            putref(f, o->sp.player.home);
            putref(f, o->exits);
            putref(f, o->sp.player.pennies);
            putstring(f, o->sp.player.password);
            break;

        case TYPE_PROGRAM:
            putref(f, OWNER(i));
            break;
    }

    return 0;
}

int deltas_count = 0;

#ifndef CLUMP_LOAD_SIZE
#define CLUMP_LOAD_SIZE 20
#endif


/* mode == 1 for dumping all objects.  mode == 0 for deltas only.  */

void
db_write_list(FILE * f, int mode)
{
    dbref i;

    for (i = db_top; i-- > 0;) {
        if (mode == 1 || (FLAGS(i) & OBJECT_CHANGED)) {
            if (fprintf(f, "#%d\n", i) < 0) {
                fprintf(stderr, "PANIC: Error writing changed objects.\n");
                abort();
            }
            db_write_object(f, i);
#ifdef DISKBASE
            if (mode == 1) {
                FLAGS(i) &= ~SAVED_DELTA; /* clear delta flag */
            } else {
                FLAGS(i) |= SAVED_DELTA; /* set delta flag */
                deltas_count++;
            }
#endif


            FLAGS(i) &= ~OBJECT_CHANGED; /* clear changed flag */
        }
    }
}


dbref
db_write(FILE * f)
{
    putstring(f, "***NeonMuck V2 DUMP Format***");

    putref(f, db_top);
    putref(f, DB_PARMSINFO
#ifdef COMPRESS
           + (db_decompression_flag ? 0 : DB_COMPRESSED)
#endif
           + (db_hash_passwords ? DB_NEWPASSES : 0)
           +
           (db_hash_passwords ? ((HVER_CURRENT << HVER_SHIFT) & HVER_MASK) : 0)
        );
    putref(f, tune_count_parms());
    tune_save_parms_to_file(f);

#ifdef COMPRESS
    if (!db_decompression_flag) {
        save_compress_words_to_file(f);
    }
#endif

    db_write_list(f, 1);

    fseek(f, 0L, 2);
    putstring(f, "***END OF DUMP***");

    fflush(f);
    deltas_count = 0;
    return (db_top);
}



dbref
db_write_deltas(FILE * f)
{
    fseek(f, 0L, 2);            /* seek end of file */
    putstring(f, "***NeonMuck V2 Deltas Dump Format***");
    db_write_list(f, 0);

    fseek(f, 0L, 2);
    putstring(f, "***END OF DUMP***");
    fflush(f);
    return (db_top);
}



dbref
parse_dbref(const char *s)
{
    const char *p;
    int x;

    x = atol(s);
    if (x > 0) {
        return x;
    } else if (x == 0) {
        /* check for 0 */
        for (p = s; *p; p++) {
            if (*p == '0')
                return 0;
            if (!isspace(*p))
                break;
        }
    }
    /* else x < 0 or s != 0 */
    return NOTHING;
}

static int
do_peek(FILE * f)
{
    int peekch;

    ungetc((peekch = getc(f)), f);

    return (peekch);
}

dbref
getref(FILE * f)
{
    char buf[BUFFER_LEN];
    int peekch;

    /*
     * Compiled in with or without timestamps, Sep 1, 1990 by Fuzzy, added to
     * Muck by Kinomon.  Thanks Kino!
     */
    if ((peekch = do_peek(f)) == NUMBER_TOKEN || peekch == LOOKUP_TOKEN) {
        return (0);
    }
    fgets(buf, sizeof(buf), f);
    return (atol(buf));
}


dbref
getfref(FILE * f, dbref *f2, dbref *f3, dbref *f4, dbref *p1, dbref *p2)
{
    char buf[BUFFER_LEN];
    dbref f1;
    int got, peekch;

    if ((peekch = do_peek(f)) == NUMBER_TOKEN || peekch == LOOKUP_TOKEN) {
        return (0);
    }
    fgets(buf, sizeof(buf), f);

    got = sscanf(buf, "%d %d %d %d %d %d", &f1, f2, f3, f4, p1, p2);

    if (got < 6)
        (*p2) = 0;
    if (got < 5)
        (*p1) = 0;
    if (got < 4)
        (*f4) = 0;
    if (got < 3)
        (*f3) = 0;
    if (got < 2)
        (*f2) = 0;
    if (got < 1) {
        fprintf(stderr, "getfref: scanf failed\n");
        return 0;
    }
    return (f1);
}

dbref
gettimestampEx(FILE * f, dbref *f2)
{
    char buf[BUFFER_LEN];
    dbref f1;
    int got, peekch;

    if ((peekch = do_peek(f)) == NUMBER_TOKEN || peekch == LOOKUP_TOKEN) {
        return (0);
    }
    fgets(buf, sizeof(buf), f);

    got = sscanf(buf, "%d %d", &f1, f2);

    if (got < 2)
        (*f2) = -1;
    if (got < 1) {
        fprintf(stderr, "getfref: scanf failed\n");
        return 0;
    }
    return (f1);
}


static char xyzzybuf[BUFFER_LEN];

static const char *
getstring_noalloc(FILE * f)
{
    char *p;

    char c;

    if (fgets(xyzzybuf, sizeof(xyzzybuf), f) == NULL) {
        xyzzybuf[0] = '\0';
        return xyzzybuf;
    }

    if (strlen(xyzzybuf) == BUFFER_LEN - 1) {
        /* ignore whatever comes after */
        if (xyzzybuf[BUFFER_LEN - 2] != '\n')
            while ((c = fgetc(f)) != '\n') ;
    }
    for (p = xyzzybuf; *p; p++) {
        if (*p == '\n') {
            *p = '\0';
            break;
        }
    }

    return xyzzybuf;
}

#define getstring(x) alloc_string(getstring_noalloc(x))

#ifdef COMPRESS
extern const char *pcompress(const char *);

#ifdef ARCHAIC_DATABASES
extern const char *old_uncompress(const char *);
#endif /* ARCHAIC_DATABASES */

#define alloc_compressed(x) alloc_string(pcompress(x))
#else
#define alloc_compressed(x) alloc_string(x)
#endif /* COMPRESS */

/* returns true for numbers of form [ + | - ] <series of digits> */
int
number(const char *s)
{
    if (!s)
        return 0;
    while (isspace(*s))
        s++;
    if (*s == '+' || *s == '-')
        s++;
    if (!*s)
        return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

/* returns true for floats of form  [+|-]<digits>.<digits>[E[+|-]<digits>] */
int
ifloat(const char *s)
{
    const char *hold = NULL;
    int decFound = 0;           /* bool to indicate if a decimal is found yet */
    int expFound = 0;           /* bool to indicate if exponent is found yet */

    if (!s)
        return 0;               /* no string at all */
    while (isspace(*s))
        s++;                    /* remove leading spaces */
    if (*s == '+' || *s == '-')
        s++;
    /* inf or nan */
    if (*s == 'i' || *s == 'n' || *s == 'I' || *s == 'N') {
        s++;
        if (*s == 'n' || *s == 'a' || *s == 'N' || *s == 'A') {
            s++;
            if (*s == 'f' || *s == 'n' || *s == 'F' || *s == 'N') {
                s++;
                if (!*s) {
                    return 1;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }
    if (*s == '.') {
        decFound = 1;
        s++;                    /* valid format = .#e# and .# */
    }
    hold = s;
    while ((*s) && (*s >= '0' && *s <= '9'))
        s++;
    if (s == hold)              /* Blank or non-numbers at start. Boo */
        return 0;
    if (!*s)                    /* means it was a # or a .# number */
        return 1;
    if (*s == '.' && decFound)
        return 0;               /* prevent 2 decimal marks */
    if (*s == '.')
        s++;                    /* skip valid decimal point */
    if (*s == 'e' || *s == 'E') {
        expFound = 1;
        s++;                    /* could be #.#e# or .#e# */
    }
    if (expFound && (*s == '+' || *s == '-'))
        s++;                    /* skip + or - for exponent */
    hold = s;
    while ((*s) && (*s >= '0' && *s <= '9'))
        s++;                    /* eat more numbers */
    if (s == hold)
        return 0;               /* no numbers or non-numbers after token */
    if (!*s)
        return 1;               /* normal ending at this point */
    if (expFound)
        return 0;               /* more characters following exponent already */
    if (*s != 'e' && *s != 'E')
        return 0;               /* Invalid token following numbers */
    s++;                        /* skip 'e' or 'E' */
    if (*s == '+' || *s == '-')
        s++;                    /* Alynna: skip + or - for this exponent */
    hold = s;
    while ((*s) && (*s >= '0' && *s <= '9'))
        s++;                    /* eat final numbers */
    if (s == hold)
        return 0;               /* no numbers after token */
    if (*s)
        return 0;               /* more characters after numbers = bad */
    return 1;
}

/*** CHANGED:
was: PropPtr getproperties(FILE *f)
now: void getproperties(FILE *f, dbref obj)
***/
void
getproperties(FILE * f, dbref obj)
{
    char buf[BUFFER_LEN], *p;
    int datalen;

#ifdef DISKBASE
    /* if no props, then don't bother looking. */
    if (!DBFETCH(obj)->propsfpos)
        return;

    /* seek to the proper file position. */
    fseek(f, DBFETCH(obj)->propsfpos, 0);
#endif

    /* get rid of first line */
    fgets(buf, sizeof(buf), f);

    if (strcmp(buf, "Props*\n")) {
        /* initialize first line stuff */
        fgets(buf, sizeof(buf), f);
        while (1) {
            /* fgets reads in \n too! */
            if (!strcmp(buf, "***Property list end ***\n") ||
                !strcmp(buf, "*End*\n"))
                break;
            p = index(buf, PROP_DELIMITER);
            *(p++) = '\0';
            datalen = strlen(p);
            p[datalen - 1] = '\0';

            if ((p - buf) >= BUFFER_LEN)
                buf[BUFFER_LEN - 1] = '\0';
            if (datalen >= BUFFER_LEN)
                p[BUFFER_LEN - 1] = '\0';

            if ((*p == '^') && (number(p + 1))) {
                add_prop_nofetch(obj, buf, NULL, atol(p + 1));
            } else {
                if (*buf) {
                    add_prop_nofetch(obj, buf, p, 0);
                }
            }
            fgets(buf, sizeof(buf), f);
        }
    } else {
        db_getprops(f, obj);
    }
}

#ifdef DISKBASE
void
skipproperties(FILE * f, dbref obj)
{
    char buf[BUFFER_LEN * 3];
    int islisten = 0;
    int iscommand = 0;

    /* get rid of first line */
    fgets(buf, sizeof(buf), f);

    fgets(buf, sizeof(buf), f);
    while (strcmp(buf, "***Property list end ***\n") && strcmp(buf, "*End*\n")) {
        if (!islisten) {
            if (string_prefix(buf, "_listen"))
                islisten = 1;
            if (string_prefix(buf, "_olisten"))
                islisten = 1;
            if (string_prefix(buf, "~listen"))
                islisten = 1;
            if (string_prefix(buf, "~olisten"))
                islisten = 1;
            if (string_prefix(buf, "@listen"))
                islisten = 1;
            if (string_prefix(buf, "@olisten"))
                islisten = 1;
        }
        if (!iscommand) {
            if (string_prefix(buf, "_command"))
                iscommand = 1;
            if (string_prefix(buf, "_ocommand"))
                iscommand = 1;
            if (string_prefix(buf, "~command"))
                iscommand = 1;
            if (string_prefix(buf, "~ocommand"))
                iscommand = 1;
            if (string_prefix(buf, "@command"))
                iscommand = 1;
            if (string_prefix(buf, "@ocommand"))
                iscommand = 1;
        }
        fgets(buf, sizeof(buf), f);
    }
    if (islisten) {
        FLAGS(obj) |= LISTENER;
    } else {
        FLAGS(obj) &= ~LISTENER;
    }
    if (iscommand) {
        FLAG2(obj) |= F2COMMAND;
    } else {
        FLAG2(obj) &= ~F2COMMAND;
    }
}

#endif



void
db_free_object(dbref i)
{
    struct object *o;

    o = DBFETCH(i);
    if (NAME(i) && Typeof(i) != TYPE_GARBAGE)
        free((void *) NAME(i));

#ifdef DISKBASE
    unloadprops_with_prejudice(i);
#else
    if (o->properties) {
        delete_proplist(o->properties);
    }
#endif

    if (Typeof(i) == TYPE_EXIT && o->sp.exit.dest) {
        free((void *) o->sp.exit.dest);
    } else if (Typeof(i) == TYPE_PLAYER) {
        if (o->sp.player.password) {
            free((void *) o->sp.player.password);
        }
        if (o->sp.player.descrs) {
            free((void *) o->sp.player.descrs);
            o->sp.player.descrs = NULL;
            o->sp.player.descr_count = 0;
        }
    }
#ifndef SANITY
    if (Typeof(i) == TYPE_PROGRAM) {
        uncompile_program(i);
    }
#endif
    /* DBDIRTY(i); */
}

void
db_free(void)
{
    dbref i;

    if (db) {
        for (i = 0; i < db_top; i++)
            db_free_object(i);
        free((void *) db);
        db = 0;
        db_top = 0;
    }

    clear_players();
    clear_primitives();
    recyclable = NOTHING;
}


struct line *
get_new_line(void)
{
    struct line *nw;

    nw = (struct line *) malloc(sizeof(struct line));

    if (!nw) {
        fprintf(stderr, "get_new_line(): Out of memory!\n");
        abort();
    }
    nw->this_line = NULL;
    nw->next = NULL;
    nw->prev = NULL;

    return nw;
}

struct line *
read_program(dbref i)
{
    char buf[BUFFER_LEN];
    struct line *first;
    struct line *prev = NULL;
    struct line *nw;
    FILE *f;
    int len;

    first = NULL;
    sprintf(buf, "muf/%d.m", (int) i);
    f = fopen(buf, "rb");
    if (!f)
        return 0;

    while (fgets(buf, BUFFER_LEN, f)) {
        nw = get_new_line();
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        if (!*buf)
            strcpy(buf, " ");
        nw->this_line = alloc_string(buf);
        if (!first) {
            prev = nw;
            first = nw;
        } else {
            prev->next = nw;
            nw->prev = prev;
            prev = nw;
        }
    }

    fclose(f);
    return first;
}

#ifdef ARCHAIC_DATABASES

#ifdef COMPRESS
# define getstring_oldcomp_noalloc(foo) old_uncompress(getstring_noalloc(foo))
#else
# define getstring_oldcomp_noalloc(foo) getstring_noalloc(foo)
#endif

void
db_read_object_old(FILE * f, struct object *o, dbref objno)
{
    dbref exits, f2, f3, f4, p1, p2;
    int pennies;
    const char *password;

    db_clear_object(-1, objno);
    FLAGS(objno) = 0;
    FLAG2(objno) = 0;
    FLAG3(objno) = 0;
    FLAG4(objno) = 0;
    POWERSDB(objno) = 0;
    POWER2DB(objno) = 0;
    NAME(objno) = getstring(f);
    LOADDESC(objno, getstring_oldcomp_noalloc(f));
    o->location = getref(f);
    o->contents = getref(f);
    exits = getref(f);
    o->next = getref(f);
    LOADLOCK(objno, getboolexp(f));
    LOADFAIL(objno, getstring_oldcomp_noalloc(f));
    LOADSUCC(objno, getstring_oldcomp_noalloc(f));
    LOADOFAIL(objno, getstring_oldcomp_noalloc(f));
    LOADOSUCC(objno, getstring_oldcomp_noalloc(f));
    OWNER(objno) = getref(f);
    pennies = getref(f);

    /* timestamps mods */
    o->ts.created = current_systime;
    o->ts.lastused = current_systime;
    o->ts.usecount = 0;
    o->ts.modified = current_systime;
    o->ts.dcreated = -1;
    o->ts.dlastused = -1;
    o->ts.dmodified = -1;


    FLAGS(objno) |= getfref(f, &f2, &f3, &f4, &p1, &p2);
    FLAG2(objno) |= f2;
    FLAG3(objno) |= f3;
    FLAG4(objno) |= f4;
    POWERSDB(objno) |= p1;
    POWER2DB(objno) |= p2;
    /*
     * flags have to be checked for conflict --- if they happen to coincide
     * with chown_ok flags and jump_ok flags, we bump them up to the
     * corresponding HAVEN and ABODE flags
     */
    if (FLAGS(objno) & CHOWN_OK) {
        FLAGS(objno) &= ~CHOWN_OK;
        FLAGS(objno) |= HAVEN;
    }
    if (FLAGS(objno) & JUMP_OK) {
        FLAGS(objno) &= ~JUMP_OK;
        FLAGS(objno) |= ABODE;
    }
    password = getstring(f);
    /* convert GENDER flag to property */
    switch ((FLAGS(objno) & GENDER_MASK) >> GENDER_SHIFT) {
        case GENDER_NEUTER:
            add_property(objno, tp_sex_prop, "neuter", 0);
            break;
        case GENDER_FEMALE:
            add_property(objno, tp_sex_prop, "female", 0);
            break;
        case GENDER_MALE:
            add_property(objno, tp_sex_prop, "male", 0);
            break;
        default:
            break;
    }
    FLAGS(objno) &= ~GENDER_MASK;
    /* For downward compatibility with databases using the */
    /* obsolete ANTILOCK flag. */
    if (FLAGS(objno) & ANTILOCK) {
        LOADLOCK(objno, negate_boolexp(copy_bool(GETLOCK(objno))))
            FLAGS(objno) &= ~ANTILOCK;
    }
    switch (FLAGS(objno) & TYPE_MASK) {
        case TYPE_THING:
            o->sp.thing.home = exits;
            o->exits = NOTHING;
            o->sp.thing.value = pennies;
            break;
        case TYPE_ROOM:
            o->sp.room.dropto = o->location;
            o->location = NOTHING;
            o->exits = exits;
            break;
        case TYPE_EXIT:
            if (o->location == NOTHING) {
                o->sp.exit.ndest = 0;
                o->sp.exit.dest = NULL;
            } else {
                o->sp.exit.ndest = 1;
                o->sp.exit.dest = (dbref *) malloc(sizeof(dbref));
                (o->sp.exit.dest)[0] = o->location;
            }
            o->location = NOTHING;
            break;
        case TYPE_PLAYER:
            o->sp.player.home = exits;
            o->exits = NOTHING;
            o->sp.player.pennies = pennies;
            o->sp.player.password = password;
            o->sp.player.curr_prog = NOTHING;
            o->sp.player.insert_mode = 0;
            o->sp.player.descrs = NULL;
            o->sp.player.descr_count = 0;
            break;
        case TYPE_GARBAGE:
            OWNER(objno) = NOTHING;
            o->next = recyclable;
            recyclable = objno;

#ifdef DISKBASE
            dirtyprops(objno);
#endif

            free((void *) NAME(objno));
            NAME(objno) = "<garbage>";
            SETDESC(objno, "<recyclable>");
            break;
    }
}

void
db_read_object_new(FILE * f, struct object *o, dbref objno)
{
    dbref f2, f3, f4, p1, p2;

    int j;

    db_clear_object(-1, objno);
    FLAGS(objno) = 0;
    FLAG2(objno) = 0;
    FLAG3(objno) = 0;
    FLAG4(objno) = 0;
    POWERSDB(objno) = 0;
    POWER2DB(objno) = 0;
    NAME(objno) = getstring(f);
    LOADDESC(objno, getstring_noalloc(f));
    o->location = getref(f);
    o->contents = getref(f);
    o->next = getref(f);
    LOADLOCK(objno, getboolexp(f));
    LOADFAIL(objno, getstring_oldcomp_noalloc(f));
    LOADSUCC(objno, getstring_oldcomp_noalloc(f));
    LOADOFAIL(objno, getstring_oldcomp_noalloc(f));
    LOADOSUCC(objno, getstring_oldcomp_noalloc(f));

    /* timestamps mods */
    o->ts.created = current_systime;
    o->ts.lastused = current_systime;
    o->ts.usecount = 0;
    o->ts.modified = current_systime;
    o->ts.dcreated = -1;
    o->ts.dlastused = -1;
    o->ts.dmodified = -1;

    /* OWNER(objno) = getref(f); */
    /* o->pennies = getref(f); */
    FLAGS(objno) |= getfref(f, &f2, &f3, &f4, &p1, &p2);
    FLAG2(objno) |= f2;
    FLAG3(objno) |= f3;
    FLAG4(objno) |= f4;
    POWERSDB(objno) |= p1;
    POWER2DB(objno) |= p2;
    /*
     * flags have to be checked for conflict --- if they happen to coincide
     * with chown_ok flags and jump_ok flags, we bump them up to the
     * corresponding HAVEN and ABODE flags
     */
    if (FLAGS(objno) & CHOWN_OK) {
        FLAGS(objno) &= ~CHOWN_OK;
        FLAGS(objno) |= HAVEN;
    }
    if (FLAGS(objno) & JUMP_OK) {
        FLAGS(objno) &= ~JUMP_OK;
        FLAGS(objno) |= ABODE;
    }
    /* convert GENDER flag to property */
    switch ((FLAGS(objno) & GENDER_MASK) >> GENDER_SHIFT) {
        case GENDER_NEUTER:
            add_property(objno, tp_sex_prop, "neuter", 0);
            break;
        case GENDER_FEMALE:
            add_property(objno, tp_sex_prop, "female", 0);
            break;
        case GENDER_MALE:
            add_property(objno, tp_sex_prop, "male", 0);
            break;
        default:
            break;
    }
    FLAGS(objno) &= ~GENDER_MASK;

    /* o->password = getstring(f); */
    /* For downward compatibility with databases using the */
    /* obsolete ANTILOCK flag. */
    if (FLAGS(objno) & ANTILOCK) {
        LOADLOCK(objno, negate_boolexp(copy_bool(GETLOCK(objno))))
            FLAGS(objno) &= ~ANTILOCK;
    }
    switch (FLAGS(objno) & TYPE_MASK) {
        case TYPE_THING:
            o->sp.thing.home = getref(f);
            o->exits = getref(f);
            OWNER(objno) = getref(f);
            o->sp.thing.value = getref(f);
            break;
        case TYPE_ROOM:
            o->sp.room.dropto = getref(f);
            o->exits = getref(f);
            OWNER(objno) = getref(f);
            break;
        case TYPE_EXIT:
            o->sp.exit.ndest = getref(f);
            o->sp.exit.dest = (dbref *) malloc(sizeof(dbref)
                                               * o->sp.exit.ndest);
            for (j = 0; j < o->sp.exit.ndest; j++) {
                (o->sp.exit.dest)[j] = getref(f);
            }
            OWNER(objno) = getref(f);
            break;
        case TYPE_PLAYER:
            o->sp.player.home = getref(f);
            o->exits = getref(f);
            o->sp.player.pennies = getref(f);
            o->sp.player.password = getstring(f);
            o->sp.player.curr_prog = NOTHING;
            o->sp.player.insert_mode = 0;
            o->sp.player.descrs = NULL;
            o->sp.player.descr_count = 0;
            break;
    }
}

#endif /* ARCHAIC_DATABASES */

/* Reads in Foxen, Foxen[234], WhiteFire, Mage or Lachesis DB Formats */
void
db_read_object_foxen(FILE * f, struct object *o, dbref objno,
                     int dtype, int read_before)
{
    dbref f2, f3, f4, p1, p2;

    int tmp, c, prop_flag = 0;

    int j = 0;

    if (read_before) {
        db_free_object(objno);
    }
    db_clear_object(-1, objno);

    FLAGS(objno) = 0;
    FLAG2(objno) = 0;
    FLAG3(objno) = 0;
    FLAG4(objno) = 0;
    POWERSDB(objno) = 0;
    POWER2DB(objno) = 0;

    if (verboseload)
        fprintf(stderr, "#%d [object_info] ", objno);

    NAME(objno) = getstring(f);
#ifdef ARCHAIC_DATABASES
    if (dtype <= 3)
        LOADDESC(objno, getstring_oldcomp_noalloc(f));
#endif /* ARCHAIC_DATABASES */

    o->location = getref(f);
    o->contents = getref(f);
    o->next = getref(f);

#ifdef ARCHAIC_DATABASES
    if (dtype < 6)
        LOADLOCK(objno, getboolexp(f));

    if (dtype == 3) {
        if (verboseload)
            fprintf(stderr, "[timestamps v3] ");
        /* Mage timestamps */
        o->ts.created = getref(f);
        o->ts.modified = getref(f);
        o->ts.lastused = getref(f);
        o->ts.usecount = 0;
    }

    if (dtype <= 3) {
        /* Lachesis, WhiteFire, and Mage messages */
        LOADFAIL(objno, getstring_oldcomp_noalloc(f));
        LOADSUCC(objno, getstring_oldcomp_noalloc(f));
        LOADDROP(objno, getstring_oldcomp_noalloc(f));
        LOADOFAIL(objno, getstring_oldcomp_noalloc(f));
        LOADOSUCC(objno, getstring_oldcomp_noalloc(f));
        LOADODROP(objno, getstring_oldcomp_noalloc(f));
    }
#endif /* ARCHAIC_DATABASES */

    if (verboseload)
        fprintf(stderr, "[flags] ");

    tmp = getfref(f, &f2, &f3, &f4, &p1, &p2);

    if (dtype >= 4) {
        tmp &= ~DUMP_MASK;
        f2 &= ~DUM2_MASK;
        f3 &= ~DUM3_MASK;
        f4 &= ~DUM4_MASK;
        p1 &= ~POWERS_DUMP_MASK;
        p2 &= ~POWER2_DUMP_MASK;
    }

    FLAGS(objno) |= tmp;
    FLAG2(objno) |= f2;
    FLAG3(objno) |= f3;
    FLAG4(objno) |= f4;
    POWERSDB(objno) |= p1;
    POWER2DB(objno) |= p2;

    FLAGS(objno) &= ~SAVED_DELTA;

    if (dtype != 3) {
        if (verboseload)
            fprintf(stderr, "[timestamps v4] ");
        /* Foxen and WhiteFire timestamps */
        o->ts.created = gettimestampEx(f, &f2);
        o->ts.dcreated = f2;
        o->ts.lastused = gettimestampEx(f, &f2);
        o->ts.dlastused = f2;
        o->ts.usecount = getref(f);
        o->ts.modified = gettimestampEx(f, &f2);
        o->ts.dmodified = f2;
    }

    c = getc(f);
    if (c == '*') {
        if (verboseload)
            fprintf(stderr, "[properties] ");
#ifdef DISKBASE
        o->propsfpos = ftell(f);
        if (o->propsmode == PROPS_CHANGED) {
            getproperties(f, objno);
        } else {
            skipproperties(f, objno);
        }
#else
        getproperties(f, objno);
#endif

        prop_flag++;
    } else {
        /* do our own getref */
        int sign = 0;

        char buf[BUFFER_LEN];

        int i = 0;

        if (c == '-')
            sign = 1;
        else if (c != '+') {
            buf[i] = c;
            i++;
        }
        while ((c = getc(f)) != '\n') {
            buf[i] = c;
            i++;
        }
        buf[i] = '\0';
        j = atol(buf);
        if (sign)
            j = -j;

        /* set gender stuff */
        /* convert GENDER flag to property */
        switch ((FLAGS(objno) & GENDER_MASK) >> GENDER_SHIFT) {
            case GENDER_NEUTER:
                add_property(objno, tp_sex_prop, "neuter", 0);
                break;
            case GENDER_FEMALE:
                add_property(objno, tp_sex_prop, "female", 0);
                break;
            case GENDER_MALE:
                add_property(objno, tp_sex_prop, "male", 0);
                break;
            default:
                break;
        }
    }
    FLAGS(objno) &= ~GENDER_MASK;

    /* o->password = getstring(f); */
    /* For downward compatibility with databases using the */
    /* obsolete ANTILOCK flag. */
    if (FLAGS(objno) & ANTILOCK) {
        LOADLOCK(objno, negate_boolexp(copy_bool(GETLOCK(objno))))
            FLAGS(objno) &= ~ANTILOCK;
    }

    switch (FLAGS(objno) & TYPE_MASK) {
        case TYPE_THING:
            if (verboseload)
                fprintf(stderr, "[type: THING] ");
            o->sp.thing.home = prop_flag ? getref(f) : j;
            o->exits = getref(f);
            OWNER(objno) = getref(f);
            o->sp.thing.value = getref(f);
            break;
        case TYPE_ROOM:
            if (verboseload)
                fprintf(stderr, "[type: ROOM] ");
            o->sp.room.dropto = prop_flag ? getref(f) : j;
            o->exits = getref(f);
            OWNER(objno) = getref(f);
            break;
        case TYPE_EXIT:
            if (verboseload)
                fprintf(stderr, "[type: EXIT] ");
            o->sp.exit.ndest = prop_flag ? getref(f) : j;
            if (o->sp.exit.ndest) /* only allocate space for linked exits */
                o->sp.exit.dest =
                    (dbref *) malloc(sizeof(dbref) * (o->sp.exit.ndest));
            for (j = 0; j < o->sp.exit.ndest; j++) {
                (o->sp.exit.dest)[j] = getref(f);
            }
            OWNER(objno) = getref(f);
            break;
        case TYPE_PLAYER:
            if (verboseload)
                fprintf(stderr, "[type: PLAYER] ");
            o->sp.player.home = prop_flag ? getref(f) : j;
            o->exits = getref(f);
            o->sp.player.pennies = getref(f);
            if (db_hash_passwords) {
                if (db_hash_convert) {
                    // Update legacy untagged raw plaintext to new tagged hex encoded best algorithm
                    char hashbuf[BUFFER_LEN];

                    hashbuf[0] = '\0';
                    const char *p = getstring_noalloc(f);

                    if (!p || !*p) {
                        // Convert blank legacy untagged raw plaintext password to new tagged NONE indicator
                        db_hash_password(HTYPE_NONE, hashbuf, NULL, NULL);
                    } else {
                        // Convert legacy untagged raw plaintext password to new tagged hex encoded best algorithm
                        db_hash_password(HTYPE_CURRENT, hashbuf, p, NULL);
                    }
                    o->sp.player.password = alloc_string(hashbuf);
                } else {
                    if (db_hash_ver == HVER_NONE) {
                        // Update legacy untagged base64 encoded md5 to new tagged hex encoded unsalted MD5 algorithm
                        char hashbuf[BUFFER_LEN];

                        hashbuf[0] = '\0';
                        const char *p = getstring_noalloc(f);

                        db_hash_oldconvert(hashbuf, p);
                        o->sp.player.password = alloc_string(hashbuf);
                    } else {
                        // Handle new tagged methods
                        const char *p = getstring_noalloc(f);

                        if (db_hash_tagtoval(p) == HTYPE_PLAIN) {
                            // Update new tagged plaintext to new tagged hex encoded best algorithm
                            char hashbuf[BUFFER_LEN];

                            hashbuf[0] = '\0';
                            db_hash_split(p, NULL, hashbuf, NULL);
                            db_hash_password(HTYPE_CURRENT, hashbuf, hashbuf,
                                             NULL);
                            o->sp.player.password = alloc_string(hashbuf);
                        } else {
                            // Preserve new tagged methods
                            o->sp.player.password = alloc_string(p);
                        }
                    }
                }
            } else {
                if (db_hash_convert) { // This section doesn't need to be here, but is included for robustness
                    // Update legacy untagged raw plaintext to new tagged hex encoded best algorithm
                    char hashbuf[BUFFER_LEN];

                    hashbuf[0] = '\0';
                    const char *p = getstring_noalloc(f);

                    if (!p || !*p) {
                        // Convert blank legacy untagged raw plaintext password to new tagged NONE indicator
                        db_hash_password(HTYPE_NONE, hashbuf, NULL, NULL);
                    } else {
                        // Convert legacy untagged raw plaintext password to new tagged hex encoded best algorithm
                        db_hash_password(HTYPE_CURRENT, hashbuf, p, NULL);
                    }
                    o->sp.player.password = alloc_string(hashbuf);
                } else {
                    // Preserve legacy untagged raw plaintext
                    o->sp.player.password = getstring(f);
                }
            }
            o->sp.player.curr_prog = NOTHING;
            o->sp.player.insert_mode = 0;
            o->sp.player.descrs = NULL;
            o->sp.player.descr_count = 0;
#ifdef IGNORE_SUPPORT
            o->sp.player.ignoretime = 0;
#endif /* IGNORE_SUPPORT */
            break;
        case TYPE_PROGRAM:
            if (verboseload)
                fprintf(stderr, "[type: PROGRAM] ");
            OWNER(objno) = getref(f);
            FLAGS(objno) &= ~INTERNAL;
            o->sp.program.curr_line = 0;
            o->sp.program.first = 0;
            o->sp.program.code = 0;
            o->sp.program.siz = 0;
            o->sp.program.start = 0;
            o->sp.program.pubs = 0;
#ifdef MCP_SUPPORT
            o->sp.program.mcpbinds = 0;
#endif /* MCP_SUPPORT */
            o->sp.program.proftime.tv_sec = 0;
            o->sp.program.proftime.tv_usec = 0;
            o->sp.program.profstart = 0;
            o->sp.program.profuses = 0;
            o->sp.program.fprofile = NULL;
            o->sp.program.instances = 0;

#ifdef ARCHAIC_DATABASES
            if (dtype < 5 && MLevel(objno) == 0)
                SetMLevel(objno, 2);
#endif /* ARCHAIC_DATABASES */
            break;
        case TYPE_GARBAGE:
            free((void *) NAME(objno));
            NAME(objno) = "<garbage>";

            if (verboseload)
                fprintf(stderr, "[type: GARBAGE] ");
            break;
    }
    if (verboseload)
        fprintf(stderr, "OK\n");
}

void
autostart_progs(void)
{
    dbref i;
    struct object *o;
    struct line *tmp;

    if (db_conversion_flag) {
        return;
    }

    for (i = 0; i < db_top; i++) {
        if (Typeof(i) == TYPE_PROGRAM) {
            if ((FLAGS(i) & ABODE) && TMage(OWNER(i))) {
                /* pre-compile AUTOSTART programs. */
                /* They queue up when they finish compiling. */
                o = DBFETCH(i);
                tmp = o->sp.program.first;
                o->sp.program.first = (struct line *) read_program(i);
                do_compile(-1, OWNER(i), i, 0);
                free_prog_text(o->sp.program.first);
                o->sp.program.first = tmp;
            }
        }
    }
}

dbref
db_read(FILE * f)
{
    dbref i, thisref;
    struct object *o;
    const char *special;
    int doing_deltas = 0;
    int main_db_format = 0;
    int parmcnt;
    int dbflags = 0;
    char c;

    db_load_format = 0;

    if ((c = getc(f)) == '*') {
        special = getstring(f);
#ifdef ARCHAIC_DATABASES
        if (!strcmp(special, "**TinyMUCK DUMP Format***")) {
            db_load_format = 1;
        } else if (!strcmp(special, "**Lachesis TinyMUCK DUMP Format***") ||
                   !strcmp(special, "**WhiteFire TinyMUCK DUMP Format***")) {
            db_load_format = 2;
        } else if (!strcmp(special, "**Mage TinyMUCK DUMP Format***")) {
            db_load_format = 3;
        } else if (!strcmp(special, "**Foxen TinyMUCK DUMP Format***")) {
            db_load_format = 4;
        } else if (!strcmp(special, "**Foxen2 TinyMUCK DUMP Format***")) {
            db_load_format = 5;
        } else if (!strcmp(special, "**Foxen3 TinyMUCK DUMP Format***")) {
            db_load_format = 6;
        } else if (!strcmp(special, "**Foxen4 TinyMUCK DUMP Format***")) {
            db_load_format = 6;
            i = getref(f);
            db_grow(i);
        } else
#endif /* ARCHAIC_DATABASES */
        if (!strcmp(special, "**Foxen5 TinyMUCK DUMP Format***") ||
                !strcmp(special, "**Foxen6 TinyMUCK DUMP Format***") ||
                !strcmp(special, "**Foxen7 TinyMUCK DUMP Format***") ||
                !strcmp(special, "**NeonMuck V2 DUMP Format***")) {
            db_load_format =
                !strcmp(special, "**Foxen7 TinyMUCK DUMP Format***") ? 8 : 7;
            i = getref(f);
            dbflags = getref(f);
            if (dbflags & DB_PARMSINFO) {
                parmcnt = getref(f);
                tune_load_parms_from_file(f, NOTHING, parmcnt);
            }
            if (dbflags & DB_COMPRESSED) {
#ifdef COMPRESS
                init_compress_from_file(f);
#else
                fprintf(stderr,
                        "This server is not compiled to read compressed databases.\n");
                return -1;
#endif
            }

            if ((db_hash_passwords =
                 (dbflags & DB_NEWPASSES || db_load_format == 8)))
                db_hash_convert = 0;
            else if (db_hash_convert)
                db_hash_passwords = 1;
            db_hash_ver = db_hash_passwords ?
                ((dbflags & HVER_MASK) >> HVER_SHIFT) : HVER_NONE;

            db_grow(i);
#ifdef ARCHAIC_DATABASES
        } else if (!strcmp(special, "***Foxen Deltas Dump Extention***")) {
            db_load_format = 4;
            doing_deltas = 1;
        } else if (!strcmp(special, "***Foxen2 Deltas Dump Extention***")) {
            db_load_format = 5;
            doing_deltas = 1;
        } else if (!strcmp(special, "***Foxen4 Deltas Dump Extention***")) {
            db_load_format = 6;
            doing_deltas = 1;
#endif /* ARCHAIC_DATABASES */
        } else if (!strcmp(special, "***Foxen5 Deltas Dump Extention***") ||
                   !strcmp(special, "***Foxen6 Deltas Dump Extention***") ||
                   !strcmp(special, "***Foxen7 Deltas Dump Extention***") ||
                   !strcmp(special, "***NeonMuck V2 Deltas Dump Format***")) {
            db_load_format =
                !strcmp(special, "***Foxen7 Deltas Dump Extention***") ? 8 : 7;
            doing_deltas = 1;
        }
        if (doing_deltas && !db) {
            fprintf(stderr, "Can't read a deltas file without a dbfile.\n");
            return -1;
        }
        free((void *) special);
        if (!doing_deltas)
            main_db_format = db_load_format;
        c = getc(f);            /* get next char */
    }

    for (i = 0;; i++) {
        switch (c) {
            case NUMBER_TOKEN:
                /* another entry, yawn */
                thisref = getref(f);

#ifndef SANITY
                if (thisref < db_top) {
                    if (doing_deltas && Typeof(thisref) == TYPE_PLAYER) {
                        delete_player(thisref);
                    }
                }
#endif

                /* make space */
                db_grow(thisref + 1);

                /* read it in */
                o = DBFETCH(thisref);
#ifdef ARCHAIC_DATABASES
                switch (db_load_format) {
                    case 0:
                        db_read_object_old(f, o, thisref);
                        break;
                    case 1:
                        db_read_object_new(f, o, thisref);
                        break;
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                        db_read_object_foxen(f, o, thisref,
                                             db_load_format, doing_deltas);
                        break;
                }
#else /* !ARCHAIC_DATABASES */
                db_read_object_foxen(f, o, thisref, db_load_format,
                                     doing_deltas);
#endif /* !ARCHAIC_DATABASES */

                if (Typeof(thisref) == TYPE_PLAYER) {
                    OWNER(thisref) = thisref;
                    add_player(thisref);
                }
                break;
            case LOOKUP_TOKEN:
                special = getstring(f);
                if (strcmp(special, "**END OF DUMP***")) {
                    free((void *) special);
                    return -1;
                } else {
                    free((void *) special);
                    special = getstring(f);
#ifdef ARCHAIC_DATABASES
                    if (!special || strcmp(special,
                                           "***Foxen Deltas Dump Extention***"))
                    {
                        if (!special
                            || strcmp(special,
                                      "***Foxen2 Deltas Dump Extention***")) {
                            if (!special
                                || strcmp(special,
                                          "***Foxen4 Deltas Dump Extention***"))
                            {
#endif /* ARCHAIC_DATABASES */
                                if (!special
                                    || strcmp(special,
                                              "***Foxen5 Deltas Dump Extention***")
                                    || strcmp(special,
                                              "***Foxen6 Deltas Dump Extention***")
                                    || strcmp(special,
                                              "***Foxen7 Deltas Dump Extention***")
                                    || strcmp(special,
                                              "***NeonMuck V2 Deltas Dump Format***"))
                                {
                                    if (special)
                                        free((void *) special);
                                    if ((main_db_format == 7
                                         || main_db_format == 8)
                                        && (dbflags & DB_PARMSINFO)) {
                                        rewind(f);
                                        free((void *) getstring(f));
                                        getref(f);
                                        getref(f);
                                        parmcnt = getref(f);
                                        tune_load_parms_from_file(f, NOTHING,
                                                                  parmcnt);
                                    }
                                    for (i = 0; i < db_top; i++) {
                                        if (Typeof(i) == TYPE_GARBAGE) {
                                            DBFETCH(i)->next = recyclable;
                                            recyclable = i;
                                        }
                                    }
                                    if (db_hash_passwords)
                                        db_hash_ver = HVER_CURRENT;
                                    else
                                        db_hash_ver = HVER_NONE;
                                    autostart_progs();
                                    return db_top;
                                } else {
                                    free((void *) special);
                                    db_load_format =
                                        !strcmp(special,
                                                "***Foxen7 Deltas Dump Extention***")
                                        ? 8 : 7;
                                    doing_deltas = 1;
                                }
#ifdef ARCHAIC_DATABASES
                            } else {
                                free((void *) special);
                                db_load_format = 6;
                                doing_deltas = 1;
                            }
                        } else {
                            free((void *) special);
                            db_load_format = 5;
                            doing_deltas = 1;
                        }
                    } else {
                        free((void *) special);
                        db_load_format = 4;
                        doing_deltas = 1;
                    }
#endif /* ARCHAIC_DATABASES */
                }
                break;
            default:
                return -1;
                /* break; */
        }
        c = getc(f);
    }                           /* for */
}                               /* db_read */

int
RawMWLevel(dbref thing, const char *file, int line)
{
    if (!OkObj(thing))
        return 0;

    switch (CheckMWLevel(thing)) {
        case LBOY:
            return (tp_multi_wizlevels ? LBOY : LARCH);
        case LWIZ:
            return (tp_multi_wizlevels ? LWIZ : LARCH);
        case LMAGE:
            return (tp_multi_wizlevels ? LMAGE : LM3);
        default:
            return CheckMWLevel(thing);
    }
}

int
WLevel(dbref player)
{
    int mlev = MLevel(player);

    return mlev >= LMAGE ? mlev : 0;
}

char *
db_hash_valtotag(int type)
{
    switch (type) {
        case HTYPE_SHA1SALT:
            return "SHA1SALTED";
        case HTYPE_MD5:
            return "MD5";
        case HTYPE_NONE:
            return "NONE";
        case HTYPE_DISABLED:
            return "DISABLED";
        case HTYPE_PLAIN:
            return "PLAIN";
        case HTYPE_SHA1:
            return "SHA1";
        case HTYPE_MD5SALT:
            return "MD5SALTED";
        case HTYPE_INVALID:
            return NULL;
        default:
            return NULL;
    }
}

int
db_hash_tagtoval(const char *tag)
{
    char buf[BUFFER_LEN];
    int i = 0;

    if (!tag)
        return HTYPE_INVALID;

    for (i = 0; (i < BUFFER_LEN - 1); i++) {
        if (tag[i] == '\0' || tag[i] == ':')
            break;
        buf[i] = (char) toupper((int) tag[i]);
    }

    buf[i++] = '\0';

    if (!strcmp(buf, "SHA1SALTED"))
        return HTYPE_SHA1SALT;
    if (!strcmp(buf, "MD5"))
        return HTYPE_MD5;
    if (!strcmp(buf, "NONE"))
        return HTYPE_NONE;
    if (!strcmp(buf, "DISABLED"))
        return HTYPE_DISABLED;
    if (!strcmp(buf, "PLAIN"))
        return HTYPE_PLAIN;
    if (!strcmp(buf, "SHA1"))
        return HTYPE_SHA1;
    if (!strcmp(buf, "MD5SALTED"))
        return HTYPE_MD5SALT;

    return HTYPE_INVALID;
}

int
db_hash_password(int type, char *out, const char *password, const char *saltin)
{
    char buf[BUFFER_LEN];
    char sbuf[17];
    char salt[9];
    int i = 0;

    if (!out)
        return 0;
    if (!password || !*password) {
        sprintf(out, "%s", db_hash_valtotag(HTYPE_NONE));
        return 1;
    }
    if (!saltin || !*saltin) {
        for (i = 0; i < 8; i++) {
            salt[i] = (unsigned char) (RANDOM() & 0xFF) + 1;
            // Don't include null bytes in the salt.
            while (!salt[i]) {
                salt[i] = (unsigned char) (RANDOM() & 0xFF) + 1;
            }
        }
        salt[8] = '\0';
    } else {
        memcpy(salt, saltin, 8);
        salt[8] = '\0';
    }

    strtohex(sbuf, 17, salt, 8);

    switch (type) {
        case HTYPE_SHA1SALT:
            /* glibc <1.7 breaks using sprintf on random input bytes when using
             * a UTF8 locale. This is a genuine glibc bug, but is still present
             * in many distros:
             *
             *  https://code.google.com/p/zetamuck/issues/detail?id=33 */
            //sprintf(buf, "%.8s%s", salt, password);
            memcpy(buf, salt, 8);
            buf[8] = ':';
            strcpy(buf + 9, password);
            SHA1hex(buf, buf, strlen(password) + 8);
            sprintf(out, "%s:%s:%s", db_hash_valtotag(type), buf, sbuf);
            break;
        case HTYPE_MD5:
            MD5hex(buf, password, strlen(password));
            sprintf(out, "%s:%s", db_hash_valtotag(type), buf);
            break;
        case HTYPE_NONE:
            sprintf(out, "%s", db_hash_valtotag(type));
            break;
        case HTYPE_DISABLED:
            sprintf(out, "%s", db_hash_valtotag(type));
            break;
        case HTYPE_PLAIN:
            sprintf(buf, "%s", password);
            sprintf(out, "%s:%s", db_hash_valtotag(type), buf);
            break;
        case HTYPE_SHA1:
            SHA1hex(buf, password, strlen(password));
            sprintf(out, "%s:%s", db_hash_valtotag(type), buf);
            break;
        case HTYPE_MD5SALT:
            sprintf(buf, "%.8s%s", salt, password);
            MD5hex(buf, buf, strlen(password) + 8);
            sprintf(out, "%s:%s:%s", db_hash_valtotag(type), buf, sbuf);
            break;
        case HTYPE_INVALID:
            *out = '\0';
            return 0;
        default:
            *out = '\0';
            return 0;
    }
    return 1;
}

int
db_hash_split(const char *hashin, int *tagout, char *hashout, char *saltout)
{
    int i = 0, k = 0, mode = 0;
    int j[3];

    if (!hashin)
        return 0;

    if (hashin[i] == '\0')
        return 0;

    mode = 1;

    for (i = 0; (i < BUFFER_LEN - 1) && (mode < 4); i++) {
        if (hashin[i] == ':') {
            j[mode - 1] = i;
            mode++;
        }
        if (hashin[i] == '\0') {
            j[mode - 1] = i;
            break;
        }
    }

    switch (mode) {
        case 4:
            mode--;
        case 3:
            for (i = j[1] + 1, k = 0; i < j[2]; i++, k++) {
                if (saltout)
                    saltout[k] = hashin[i];
            }
            if (saltout)
                saltout[k++] = '\0';
        case 2:
            for (i = j[0] + 1, k = 0; i < j[1]; i++, k++) {
                if (hashout)
                    hashout[k] = hashin[i];
            }
            if (hashout)
                hashout[k++] = '\0';
        case 1:
            if (tagout)
                *tagout = db_hash_tagtoval(hashin);
            break;
        default:
            return 0;
    }

    return mode;
}

int
db_hash_compare(const char *hash, const char *password)
{
    char buf[BUFFER_LEN];
    char hbuf[BUFFER_LEN];
    char sbuf[BUFFER_LEN];
	char salt[9];
	int res = 0, tag = 0, i = 0;

    sbuf[0] = '\0';
    salt[0] = '\0';

    if (!hash)
        return 1;
    for (i = 0; hash[i] != 0 && i < BUFFER_LEN - 1; i++)
        buf[i] = toupper(hash[i]);
    buf[i] = '\0';
    res = db_hash_split(buf, &tag, NULL, sbuf);
    if (res == 0)
        return 0;
    if (tag == HTYPE_DISABLED)
        return 0;
    if (tag == HTYPE_NONE)
        return 1;
    if (!password || !*password)
        return 0;
    if (res == 3) {
        hextostr(salt, 9, sbuf, 16);
        if (!db_hash_password(tag, hbuf, password, salt))
            return 0;
    } else {
        if (!db_hash_password(tag, hbuf, password, NULL))
            return 0;
    }
    return !strcmp(buf, hbuf);
}

int
db_hash_oldconvert(char *out, const char *hash)
{
    char buf[BUFFER_LEN];

    if (!hash || !*hash) {
        sprintf(out, "%s", db_hash_valtotag(HTYPE_NONE));
        return 1;
    }

    if (!base64tohex(buf, BUFFER_LEN, hash, strlen(hash)))
        return 0;

    sprintf(out, "%s:%s", db_hash_valtotag(HTYPE_MD5), buf);
    return 1;
}
