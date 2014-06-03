/* Primitives package */

#include "copyright.h"
#include "config.h"

#ifdef NEWHTTPD

#define SECURE_FILE_PRIMS

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
#include "newhttp.h"

extern struct inst *oper1, *oper2, *oper3, *oper4, *oper5, *oper6;
extern struct inst temp1, temp2, temp3;
extern int tmp, result;
extern dbref ref;
extern char buf[BUFFER_LEN];

#if defined(FILE_PRIMS) && defined(SECURE_FILE_PRIMS)
extern char *set_directory(char *filename);
extern int valid_name(char *filename);
extern char *parse_token(char *filename);
#endif

void
prim_descr_safeboot(PRIM_PROTOTYPE)
{
    struct descriptor_data *d;

    CHECKOP(1);
    oper1 = POP();

    if (oper1->type != PROG_INTEGER)
        abort_interp("Descriptor integer expected.");
    if (mlev < LARCH)
        abort_interp("W3 prim.");
    if (!(d = descrdata_by_descr(oper1->data.number)))
        abort_interp("Invalid descriptor.");

    d->booted = BOOT_SAFE;

    CLEAR(oper1);
}

void
prim_body_getchar(PRIM_PROTOTYPE)
{
    struct descriptor_data *d;

    CHECKOP(2);
    oper1 = POP();              /* (i) char  */
    oper2 = POP();              /* (i) descr */

    if (oper1->type != PROG_INTEGER)
        abort_interp("Integer argument expected. (2)");
    if (oper2->type != PROG_INTEGER)
        abort_interp("Descriptor integer expected. (1)");
    if (mlev < LM2)
        abort_interp("M2 prim.");
    if (!(d = descrdata_by_descr(oper2->data.number)))
        abort_interp("Invalid descriptor.");
    if (d->type != CT_HTTP)
        abort_interp("Non-HTTP connection.");

    if (!d->http->body.data || d->http->body.len < 1) {
        result = -2;
    } else if (oper1->data.number < 0 || oper1->data.number > d->http->body.len) {
        result = -1;
    } else {
        result = d->http->body.data[oper1->data.number];
    }

    CLEAR(oper1);
    CLEAR(oper2);
    PushInt(result);
}

void
prim_body_nextchar(PRIM_PROTOTYPE)
{
    struct descriptor_data *d;

    CHECKOP(1);
    oper1 = POP();              /* (i) descr */

    if (oper1->type != PROG_INTEGER)
        abort_interp("Descriptor integer expected. (1)");
    if (mlev < LM2)
        abort_interp("M2 prim.");
    if (!(d = descrdata_by_descr(oper1->data.number)))
        abort_interp("Invalid descriptor.");
    if (d->type != CT_HTTP)
        abort_interp("Non-HTTP connection.");

    if (!d->http->body.data || d->http->body.len < 1) {
        result = -2;
    } else {
        tmp = d->http->body.curr;
        if (tmp < 0 || tmp > d->http->body.len) {
            result = -1;
        } else {
            result = d->http->body.data[tmp];
            d->http->body.curr = tmp + 1;
        }
    }

    CLEAR(oper1);
    PushInt(result);
}

void
prim_body_prevchar(PRIM_PROTOTYPE)
{
    struct descriptor_data *d;

    CHECKOP(1);
    oper1 = POP();              /* (i) descr */

    if (oper1->type != PROG_INTEGER)
        abort_interp("Descriptor integer expected. (1)");
    if (mlev < LM2)
        abort_interp("M2 prim.");
    if (!(d = descrdata_by_descr(oper1->data.number)))
        abort_interp("Invalid descriptor.");
    if (d->type != CT_HTTP)
        abort_interp("Non-HTTP connection.");

    if (!d->http->body.data || d->http->body.len < 1) {
        result = -2;
    } else {
        tmp = d->http->body.curr - 1;
        if (tmp < 0 || tmp > d->http->body.len) {
            result = -1;
        } else {
            result = d->http->body.data[tmp];
            d->http->body.curr = tmp;
        }
    }

    CLEAR(oper1);
    PushInt(result);
}

void
prim_httpdata(PRIM_PROTOTYPE)
{
    struct descriptor_data *d;
    stk_array *nw;

    CHECKOP(1);
    oper1 = POP();

    if (oper1->type != PROG_INTEGER)
        abort_interp("Descriptor integer expected.");
    if (mlev < LMAGE)
        abort_interp("MAGE prim.");
    if (!(d = descrdata_by_descr(oper1->data.number)))
        abort_interp("Invalid descriptor.");
    if (d->type != CT_HTTP)
        abort_interp("Non-HTTP connection.");

    nw = http_makearray(d);

    CLEAR(oper1);
    PushArrayRaw(nw);
}

void
prim_httpsendheader(PRIM_PROTOTYPE)
{
    struct descriptor_data *d;

    // ( iDescr iStatusCode sContentType iContentLength bTerminateHeaders bClose -- )
    CHECKOP(5);
    oper1 = POP();
    oper2 = POP();
    oper3 = POP();
    oper4 = POP();
    oper5 = POP();
    oper6 = POP();

    if (oper1->type != PROG_INTEGER)
        abort_interp("Connection close preference integer expected. (6)");
    if (oper2->type != PROG_INTEGER)
        abort_interp("Header termination integer expected. (5)");
    if (oper2->data.number != 1 && oper1->data.number != 0)
        abort_interp("Header termination integer must be 1 or 0. (5)");
    if (oper3->type != PROG_INTEGER)
        abort_interp("Content length integer expected. (4)");
    if (oper3->data.number < 0)
        abort_interp("Content length must be >= 0. (4)");
    if (oper4->type != PROG_STRING)
        abort_interp("Content type string expected. (3)");
    // string can be anything, even null.
    if (oper5->type != PROG_INTEGER)
        abort_interp("Status code integer expected. (2)");
    if (oper6->type != PROG_INTEGER)
        abort_interp("Descriptor integer expected. (1)");
    if (mlev < LMAGE)
        abort_interp("MAGE prim.");
    if (!(d = descrdata_by_descr(oper6->data.number)))
        abort_interp("Invalid descriptor. (1)");
    if (d->type != CT_HTTP)
        abort_interp("Non-HTTP connection. (1)");

    http_sendheader(d, oper5->data.number, 
                    DoNullInd(oper4->data.string), oper3->data.number,
                    oper2->data.number, oper1->data.number);

    CLEAR(oper1);
    CLEAR(oper2);
    CLEAR(oper3);
    CLEAR(oper4);
    CLEAR(oper5);
    CLEAR(oper6);
}


#endif /* NEWHTTPD */
