#include "copyright.h"
#include "config.h"
#include "params.h"
#ifdef JSON_SUPPORT
#include <jansson.h>
#include "db.h"
#include "tune.h"
#include "props.h"
#include "inst.h"
#include "externs.h"
#include "match.h"
#include "interface.h"
#include "strings.h"
#include "interp.h"
#include "json.h"
#include "p_json.h"

extern struct inst *oper1, *oper2, *oper3, *oper4, *oper5, *oper6;
extern struct inst temp1;

void
prim_array_jencode(PRIM_PROTOTYPE) {
    char *jsontext;
    int flags;
    stk_array *jsonarr;

    CHECKOP(2);
    oper1 = POP();
    oper2 = POP();

    if (oper1->type != PROG_INTEGER) {
        abort_interp("Argument not an integer. (1)");
    }
    if (oper1->data.number < 0) {
        abort_interp("Argument is a negative number. (1)");
    }
    if (oper2->type != PROG_ARRAY) {
        abort_interp("Argument not an array. (2)");
    }

    jsonarr = oper2->data.array;
    flags = oper1->data.number;

    if (!(jsontext = array_jencode(jsonarr, flags, player))) {
        /* Hardcode error. User shouldn't be able to break the parser. */
        abort_interp("Internal error during encoding operation.");
    }
    
    CLEAR(oper1);
    CLEAR(oper2);
   
    if (jsontext) { 
        PushString(jsontext);
        free(jsontext);
    }
}

void
prim_array_jdecode(PRIM_PROTOTYPE) {
    stk_array *strarr, *jsonarr, *errarr;
    int flags;

    CHECKOP(2);
    oper1 = POP();
    oper2 = POP();

    if (oper1->type != PROG_INTEGER) {
        abort_interp("Argument not an integer. (1)");
    }
    if (oper1->data.number < 0) {
        abort_interp("Argument is a negative number. (1)");
    }
    if (oper2->type != PROG_ARRAY) {
        abort_interp("Argument not an array of strings. (2)");
    }
    if (!array_is_homogenous(oper2->data.array, PROG_STRING)) {
        abort_interp("Argument not an array of strings. (2)");
    }

    flags = oper1->data.number;
    strarr = oper2->data.array;

    jsonarr = array_jdecode(strarr, flags, &errarr);

    CLEAR(oper1);
    CLEAR(oper2);
    PushArrayRaw(jsonarr);
    PushArrayRaw(errarr);
}




#ifdef FILE_PRIMS

/* decode a JSON encoded file to an array (based on prim_fread) */
void
prim_jdecode_file(PRIM_PROTOTYPE)
{
    char *filename;
    int flags;
    stk_array *jsonarr, *errarr;

    CHECKOP(2);
    oper1 = POP();   /* int: JSON decoding options */
    oper2 = POP();   /* str: path to file */

#ifndef PROTO_AS_ROOT
        if (getuid() == 0) {
                    abort_interp("Muck is running under root privs, file prims disabled.");
        }
#endif
    if (mlev < LBOY) {
        abort_interp("BOY primitive only.");
    }
    if (oper1->type != PROG_INTEGER) {
        abort_interp("Argument not an integer. (1)");
    }
    if (oper1->data.number < 0) {
        abort_interp("Argument is a negative number. (1)");
    }
    if (oper2->type != PROG_STRING) {
        abort_interp("Argument not a string. (2)");
    }
    if (!oper2->data.string) {
        abort_interp("Argument is a null string. (2)");
    }
    flags = oper1->data.number;
    filename = oper2->data.string->data;
#ifdef SECURE_FILE_PRIMS
    if (!(valid_name(filename))) {
        abort_interp("Invalid file name.");
    }
    if (strchr(filename, '$') == NULL) {
        filename = set_directory(filename);
    } else {
        filename = parse_token(filename);
    }
    if (filename == NULL) {
        abort_interp("Invalid shortcut used.");
    }
#endif

    jsonarr = jdecode_file(filename, flags, &errarr);

    CLEAR(oper1);
    CLEAR(oper2);
    PushArrayRaw(jsonarr);
    PushArrayRaw(errarr);
}
    
/* encode an array as JSON and write to a file */
void
prim_jencode_file(PRIM_PROTOTYPE)
{
    char *filename;
    int flags, success;

    CHECKOP(3);
    oper1 = POP();  /* int: JSON decoding options */ 
    oper2 = POP();  /* str: path to file */ 
    oper3 = POP();  /* arr: array to encode */ 


#ifndef PROTO_AS_ROOT
        if (getuid() == 0) {
                    abort_interp("Muck is running under root privs, file prims disabled.");
        }
#endif
    if (mlev < LBOY) {
        abort_interp("BOY primitive only.");
    }
    if (oper1->type != PROG_INTEGER) {
        abort_interp("Argument not an integer. (1)");
    }
    if (oper1->data.number < 0) {
        abort_interp("Argument is a negative number. (1)");
    }
    if (oper2->type != PROG_STRING) {
        abort_interp("Argument not a string. (2)");
    }
    if (!oper2->data.string) {
        abort_interp("Argument is a null string. (2)");
    }
    if (oper3->type != PROG_ARRAY) {
        abort_interp("Argument not an array. (3)");
    }
    /* type of array doesn't matter */

    flags = oper1->data.number;
    filename = oper2->data.string->data;
#ifdef SECURE_FILE_PRIMS
    if (!(valid_name(filename))) {
        abort_interp("Invalid file name.");
    }
    if (strchr(filename, '$') == NULL) {
        filename = set_directory(filename);
    } else {
        filename = parse_token(filename);
    }
    if (filename == NULL) {
        abort_interp("Invalid shortcut used.");
    }
#endif
    success = jencode_file(oper3->data.array, filename, flags, player);

    temp1.type = PROG_INTEGER;
    if (success == 0) {
        temp1.data.number = 1;
    } else {
        temp1.data.number = 0;
    }

    CLEAR(oper1);
    CLEAR(oper2);
    CLEAR(oper3);
    PushInt(temp1);
}

#endif /* FILE_PRIMS */

#endif /* JSON_SUPPORT */
