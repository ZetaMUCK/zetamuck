#include <params.h>
#include <interface.h>
#include <telnet.h>
#include <externs.h>
#include <interp.h>

void
termtypes_init(struct telopt *t)
{
    if (!termtype_init_state) {
        termtype_init_state = alloc_prog_string("<unknown>");
        /* By not de-incrementing the link count here, termtype_init will always
         * be a minimum of 1. That's actually makes things simpler. */
    }
    if (t->termtypes) {
        array_free(t->termtypes);
    }
    t->termtypes = new_array_packed(1);
    t->termtypes->data.packed[0].type = PROG_STRING;
    t->termtypes->data.packed[0].data.string = termtype_init_state;
    termtype_init_state->links++;

    t->termtypes_cnt = 0;
}

void
telopt_sb_termtype(struct descriptor_data *d) {
    char termtype[TELOPT_MAX_BUF_LEN];
    char *last_termtype;
    struct inst temp1;
    struct inst temp2;

    if (d->telopt.sb_buf_len < 2) /* At this point, a valid request would have at least two bytes in it. */
        return;

    if (d->telopt.sb_buf[1] == 1) {
        /* 
           The client requested our termtype.  Send it.
           It would not be proper to send our version number here.
        */
        queue_write(d, "\xFF\xFA\x00ProtoMUCK\xFF\xF0", 14);
    } else if (!d->telopt.sb_buf[1]) {

        /* Move the data into the termtype string, making sure to add a null at the end. */
        //memcpy(d->telopt.termtype, d->telopt.sb_buf + 2, d->telopt.sb_buf_len-2);
        //d->telopt.termtype[d->telopt.sb_buf_len-2] = '\0';
        if (d->telopt.sb_buf_len >= 3) {
            memcpy(termtype, d->telopt.sb_buf + 2, d->telopt.sb_buf_len-2);
            termtype[d->telopt.sb_buf_len-2] = '\0';
        } else {
            // Blank termtype.
            termtype[0] = '\0';
        }

        if (d->telopt.termtypes_cnt == 0) {
            /* First seen termtype: the primary, i.e. name of client.
             * We need to be aware of the fact that this might be a second
             * attempt to cycle through termtypes, and reset our termtypes
             * array if that's the case. */
            if (d->telopt.termtypes->items > 1) {
                termtypes_init(&d->telopt);
            }

            d->telopt.termtypes_cnt++;
        } else {
            // Check to see if cycling has begun...
            temp1.type = PROG_INTEGER;
            temp1.data.number = d->telopt.termtypes_cnt - 1;

            temp2 = *(array_getitem(d->telopt.termtypes, &temp1));
            last_termtype = temp2.data.string->data;

            //last_termtype = d->termtypes->data.packed[d->termtypes->items].data.string;
            if ((!*termtype && last_termtype == termtype_init_state->data) ||
                    (!string_compare(termtype, last_termtype))) {
                /* String matched last reported termtype. We're done.
                 * This sets the count back to zero, which prevents us
                 * from requesting further termtypes and starts us over
                 * from scratch in the event that the client sends us
                 * another TERMTYPE unsolicited. */
                d->telopt.termtypes_cnt = 0;
            }


            if (*termtype && d->telopt.termtypes_cnt == 1) {
                // Second seen termtype...actual "type".
                //
                if (string_prefix(termtype, "DUMB")) {
                    // ragequit
                } else if (has_suffix(termtype, "-256COLOR")) {
                    DR_RAW_ADD_FLAGS(d, DF_COLOR);
                    DR_RAW_ADD_FLAGS(d, DF_256COLOR);
                } else if (string_prefix(termtype, "ANSI") ||
                               string_prefix(termtype, "VT100") ||
                               string_prefix(termtype, "SCREEN")) {
                    DR_RAW_ADD_FLAGS(d, DF_COLOR);
                } else if (string_prefix(termtype, "XTERM")) {
                    DR_RAW_ADD_FLAGS(d, DF_COLOR);
                    DR_RAW_ADD_FLAGS(d, DF_256COLOR);
                }
                d->telopt.termtypes_cnt++;
            } else if (*termtype && d->telopt.termtypes_cnt == 2) {
                // Third termtype...candidate for MTTS.
                if (string_prefix(termtype, "MTTS ")) {
                    char *tailptr;
                    d->telopt.mtts = strtol(termtype+5, &tailptr, 10);
                    if (*tailptr != '\0') {
                        // bogus MTTS value, reset it.
                        d->telopt.mtts = 0;
                    } else {
                        /* Got MTTS...detect capabilities.
                         * 
                         * Not implemented:
                         *   2 = VT100
                         *  16 = Mouse Tracking
                         *  32 = OSC Color Palette
                         *  64 = Screen Reader
                         * 128 = Proxy */
                        if (d->telopt.mtts & 1) // ANSI COLOR
                            DR_RAW_ADD_FLAGS(d, DF_COLOR);
                        if (d->telopt.mtts & 4) // UTF-8
                            d->encoding = ENC_UTF8;
                        if (d->telopt.mtts & 8) // 256 COLOR
                            DR_RAW_ADD_FLAGS(d, DF_256COLOR);
                    }

                }
                d->telopt.termtypes_cnt++;
            } else if (d->telopt.termtypes_cnt != 0) {
                // No special processing here, just keep going.
                d->telopt.termtypes_cnt++;
            }
        }
        if (d->telopt.termtypes_cnt) {

            if (d->telopt.termtypes_cnt == 1 && *termtype) {
                /* We only need to set termtype on first pass if it
                 * is non-null, termtypes_init handled it otherwise. */
                temp1.type = PROG_STRING;
                temp1.data.string = alloc_prog_string(termtype);
                temp2.type = PROG_INTEGER;
                temp2.data.number = 0;
                array_setitem(&d->telopt.termtypes, &temp2, &temp1);
                CLEAR(&temp1);

                if (!string_compare(termtype, "Pueblo")) {
                    /* Functionality test, DF_PUEBLO doesn't actually do anything yet */
                    DR_RAW_ADD_FLAGS(d, DF_PUEBLO);
                }
            } else if (d->telopt.termtypes_cnt > 1)  {
                if (!*termtype) {
                    temp1.type = PROG_STRING;
                    temp1.data.string = termtype_init_state;
                    array_appenditem(&d->telopt.termtypes, &temp1);
                } else {
                    temp1.type = PROG_STRING;
                    temp1.data.string = alloc_prog_string(termtype);
                    array_appenditem(&d->telopt.termtypes, &temp1);
                    CLEAR(&temp1);
                }
            }


            if (!DR_RAW_FLAGS(d, DF_COMPRESS) && d->telopt.termtypes_cnt == 1) {
#ifdef MCCP_ENABLED
                /* Due to SimpleMU, we need to determine the termtype before we can enable MCCP. */
                if (d->telopt.mccp && !d->mccp)
                    mccp_start(d, d->telopt.mccp);
#endif
            }

            /* Request the next: IAC SB TERMTYPE SEND IAC SE */
            queue_write(d, "\xFF\xFA\x18\x01\xFF\xF0", 6);
        }
    }
}

