/* Primitives package */

#include "copyright.h"
#include "config.h"

#include "db.h"
#include "inst.h"
#include "externs.h"
#include "match.h"
#include "interface.h"
#include "params.h"
#include "tune.h"
#include "strings.h"
#include "interp.h"
#include "cgi.h"

extern struct inst *oper1, *oper2, *oper3, *oper4, *oper5, *oper6;
extern struct inst temp1, temp2, temp3;
extern int tmp, result;
extern dbref ref;
extern char buf[BUFFER_LEN];

/* struct tm *time_tm; */
/* extern struct descriptor_data *descriptor_list; */

char *
p_commandtext(dbref player, char *command, char *text)
{
    static char buf[BUFFER_LEN];

    strcpy(buf, "");

    if (FLAG2(player) & F2PUEBLO) {
        sprintf(buf, "<a xch_cmd=\"%s\">%s</a>", command, text);
    }

    return (buf);
}

char *
p_playmidi(dbref player, char *musicurl, char *volume)
{
    static char buf[BUFFER_LEN];

    strcpy(buf, "");

    if (FLAG2(player) & F2PUEBLO) {
        sprintf(buf, "<img xch_sound=play href=\"%s\" xch_volume=%s>",
                musicurl, volume);
    }

    return (buf);
}

char *
p_stopmidi(dbref player)
{
    static char buf[BUFFER_LEN];

    strcpy(buf, "");
    if (FLAG2(player) & F2PUEBLO) {
        sprintf(buf, "<img xch_sound=stop device=midi>");
    }
    return (buf);
}

void
prim_stopmidi(PRIM_PROTOTYPE)
{
    CHECKOP(1);
    oper1 = POP();
    if (!valid_object(oper1))
        abort_interp("Invalid object argument.");
    CHECKOFLOW(1);
    strcpy(buf, p_stopmidi(oper1->data.objref));
    CLEAR(oper1);
    PushString(buf);
}

void
prim_playmidi(PRIM_PROTOTYPE)
{
    char buf2[BUFFER_LEN];
    char buf3[5];

    CHECKOP(3);
    oper1 = POP();
    oper2 = POP();
    oper3 = POP();
    if (oper1->type != PROG_INTEGER)
        abort_interp("Integer value expected (3)");
    if (oper2->type != PROG_STRING)
        abort_interp("String value expected (2)");
    if (!valid_object(oper3))
        abort_interp("Invalid object argument (1)");
    CHECKOFLOW(1);
    strcpy(buf2, oper2->data.string->data);
    sprintf(buf3, "%d", oper1->data.number);
    strcpy(buf, p_playmidi(oper3->data.objref, buf2, buf3));
    CLEAR(oper1);
    CLEAR(oper2);
    CLEAR(oper3);
    PushString(buf);
}

void
prim_commandtext(PRIM_PROTOTYPE)
{
    char buf2[BUFFER_LEN];
    char buf3[BUFFER_LEN];

    CHECKOP(3);
    oper1 = POP();
    oper2 = POP();
    oper3 = POP();

    if (oper1->type != PROG_STRING)
        abort_interp("String argument expected (3)");
    if (oper2->type != PROG_STRING)
        abort_interp("String argument expected (2)");
    if (!valid_object(oper3))
        abort_interp("Invalid object reference (1)");
    strcpy(buf2, oper2->data.string->data);
    strcpy(buf3, oper1->data.string->data);
    strcpy(buf, p_commandtext(oper3->data.objref, buf2, buf3));
    CLEAR(oper1);
    CLEAR(oper2);
    CLEAR(oper3);
    PushString(buf);
}

void
prim_unescape_url(PRIM_PROTOTYPE)
{
    CHECKOP(1);
    oper1 = POP();
    if (oper1->type != PROG_STRING)
        abort_interp("Non-string argument. (1)");

    if (oper1->data.string) {
        strcpy(buf, oper1->data.string->data);
        unescape_url(buf);      /* Found in cgi.c */

        CLEAR(oper1);
        PushString(buf);
    } else {
        CLEAR(oper1);
        PushNullStr;
    }
}

void
prim_escape_url(PRIM_PROTOTYPE)
{
    CHECKOP(1);
    oper1 = POP();
    if (oper1->type != PROG_STRING)
        abort_interp("Non-string argument. (1)");

    if (oper1->data.string) {
        escape_url(buf, oper1->data.string->data); /* Found in cgi.c */

        CLEAR(oper1);
        PushString(buf);
    } else {
        CLEAR(oper1);
        PushNullStr;
    }
}
