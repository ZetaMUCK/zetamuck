#include <params.h>
#include <interface.h>
#include <telnet.h>

char *iacbyte[255];

char *teopt[255];


/* Advertise telnet options. This probably shouldn't be called more than once
 * per client connection. */
void
telopt_advertise(struct descriptor_data *d)
{
    queue_write(d, "\xFF\xFB\x46",       3); /* IAC WILL MSSP */
    queue_write(d, "\xFF\xFB\x56\x0A",   4); /* IAC WILL TELOPT_COMPRESS2 (MCCP v2) */
    queue_write(d, "\xFF\xFB\x2A",       3); /* IAC WILL CHARSET */
#ifdef UTF8_SUPPORT
    queue_write(d, "\xFF\xFB\x5B",       3); /* IAC WILL MXP */
#endif
    queue_write(d, "\xFF\xFD\x18",       3); /* IAC DO TERMTYPE */
    queue_write(d, "\xFF\xFD\x1F",       3); /* IAC DO NAWS */
    /* Can't IAC DO CHARSET due to Potato:
     * https://code.google.com/p/potatomushclient/issues/detail?id=343 */
    //queue_write(d, "\xFF\xFD\x2A",       3); /* IAC DO CHARSET */
}

void
telopt_init(struct telopt *t)
{

    t->mccp = 0;
    t->sb_buf = NULL;
    t->sb_buf_len = 0;
    t->width = 0;
    t->height = 0;
    t->mtts = 0;
    t->termtypes = NULL;
    t->termtypes_cnt = 0;

    termtypes_init(t);
}

void
telopt_clean(struct telopt *t)
{
    t->mccp = 0;
    if (t->sb_buf)
        free((void *)t->sb_buf);
    t->sb_buf = NULL;
    t->sb_buf_len = 0;
    t->width = 0;
    t->height = 0;
    t->mtts = 0;
    t->termtypes_cnt = 0;

    // termtypes may persist in memory if link count >0, this is expected.
    array_free(t->termtypes);
    t->termtypes = NULL;
}

/* Called by process_input to handle a telnet IAC sequence from a client.
 * "q" is the current position in the input queue, and "p" is the current
 * position in the put buffer. (command currently being stored)
 *
 * Returning 1 "consumes" the character stored in *q, while returning 0 allows
 * process_input() to evaluate whether the character should be stored in the put
 * buffer. */
bool
process_telnet_IAC(struct descriptor_data *d, char *q, char *p, char *pend)
{
    if (d->inIAC == TELOPT_IAC) {
        // Client sent us IAC; assume IAC+NOP is safe and enable keepalives.
        if (!DR_RAW_FLAGS(d, DF_KEEPALIVE)) {
            DR_RAW_ADD_FLAGS(d, DF_KEEPALIVE);
        }
        switch (*q) {
            case TELOPT_BRK:   /* Break */
            case TELOPT_IP:   /* Interrupt Processes */
                save_command(d, BREAK_COMMAND, sizeof(BREAK_COMMAND), -2);
                d->inIAC = 0;
                break;
            case TELOPT_AYT:{  /* AYT: Are You There? */
                queue_write(d, "[Yes]\r\n", 7);
                d->inIAC = 0;
                break;
            }
            case TELOPT_EC:   /* Erase character */
                if (p > d->raw_input)
                    --p;
                d->inIAC = 0;
                break;
            case TELOPT_EL:   /* Erase line */
                p = d->raw_input;
                d->inIAC = 0;
                break;
            case TELOPT_SB:   /* SB */ /* Go ahead. Treat as NOP */
                if (d->telopt.sb_buf)
                    free((void *)d->telopt.sb_buf);
                d->telopt.sb_buf = (unsigned char *)malloc(TELOPT_MAX_BUF_LEN);
                d->telopt.sb_buf_len = 0;

                d->inIAC = TELOPT_SB;
                break;
            case TELOPT_WILL:
                d->inIAC = TELOPT_WILL;
                break;
            case TELOPT_WONT:
                d->inIAC = TELOPT_WONT;
                break;
            case TELOPT_DO:
                d->inIAC = TELOPT_DO;
                break;
            case TELOPT_DONT:
                d->inIAC = TELOPT_DONT;
                break;
            case TELOPT_IAC:   /* IAC a second time */
                if (d->encoding == ENC_LATIN1) {
                    // escaped ÿ
#ifndef UTF8_SUPPORT
                    *p++ = TELOPT_IAC;
#else
                    if ((pend - p) >= 3) {
                        *p++ = '\xC3';
                        *p++ = '\xBF';
                        d->raw_input_mblength++;
                    }
                } else if (d->encoding == ENC_IBM437 && (pend - p) >= 3) {
                    // escaped NBSP
                    *p++ = '\xC2';
                    *p++ = '\xA0';
                    d->raw_input_mblength++;
#endif
                }
                d->inIAC = 0;
                break;
            case TELOPT_NOP:
            case TELOPT_AO:  /* Abort Output */
            default:
                d->inIAC = 0;
                break;
        }
        return 1;
    } else if (d->inIAC == TELOPT_SB) {
        if (*(q-1) == TELOPT_IAC && *q == TELOPT_SE) {
            d->telopt.sb_buf[d->telopt.sb_buf_len-1] = '\0';

            /* Begin processing the TELOPT data buffer */
            if (d->telopt.sb_buf_len) {
                switch (d->telopt.sb_buf[0]) {
                    case TELOPT_NAWS:
                        if (d->telopt.sb_buf_len != 6)
                            break;

                        d->telopt.width  =  (d->telopt.sb_buf[1] <<8) | d->telopt.sb_buf[2];
                        d->telopt.height =  (d->telopt.sb_buf[3] <<8) | d->telopt.sb_buf[4];
                        
                        /* that was easy */
                        break;
                    case TELOPT_TERMTYPE:
                        telopt_sb_termtype(d);
                        break;
                    case TELOPT_CHARSET:
                        telopt_sb_charset(d);
                        break;
                    default:
                        break;
                }
            }
            free((void *)d->telopt.sb_buf); /* don't need the buffer anymore */
            d->telopt.sb_buf = NULL;
            d->telopt.sb_buf_len = 0;

            d->inIAC = 0;
        } else {
            if (d->telopt.sb_buf_len < TELOPT_MAX_BUF_LEN)
                d->telopt.sb_buf[d->telopt.sb_buf_len++] = *q;
            else
                d->inIAC = 0;
        }
        return 1;
    } else if (d->inIAC == TELOPT_WILL) {
        if (*q == TELOPT_TERMTYPE) { /* TERMTYPE */
            queue_write(d, "\xFF\xFA\x18\x01\xFF\xF0", 6); /* IAC SB TERMTYPE SEND IAC SE */
        } else if (*q == TELOPT_MSSP) {
            mssp_send(d);
        } else if (*q == TELOPT_NAWS) {
            /* queue_write(d, "\xFF\xFD\x1F", 3); */ /* Oops, infinite loop */
        } else if (*q == TELOPT_CHARSET) {
            //log_status("DBUG: (%d) IAC WILL CHARSET\n",d->descriptor);
            /* Just have to wait for the DO now. */
        } else {
            /* send back DONT option in all other cases */
            queue_write(d, "\xFF\xFE", 2);
            queue_write(d, q, 1);
        }
        d->inIAC = 0;
        return 1;
    } else if (d->inIAC == TELOPT_DO) {
#ifdef MCCP_ENABLED
        if (*q == TELOPT_MCCP2) {        /* TELOPT_COMPRESS2 */
            d->telopt.mccp = 2;
                
            /* Thanks to SimpleMU, we're required to know the termtype before we can enable MCCP. */
            if (d->telopt.termtypes_cnt && !d->mccp)
                mccp_start(d, d->telopt.mccp);
        } else if (*q == TELOPT_MSSP) {
            mssp_send(d);
#ifdef UTF8_SUPPORT
        } else if (*q == TELOPT_MXP) {
            DR_RAW_ADD_FLAGS(d, DF_MXP);
            // enable MXP
            queue_write(d, "\xFF\xFA\x5B\xFF\xF0", 5); /* IAC SB MXP IAC SE */
            // lock secure mode
            queue_write(d, "\x1B[6z", 4); /* ESC [ 6 z */
#endif
        } else if (*q == TELOPT_NAWS) {
        } else if (*q == TELOPT_CHARSET) {
            charset_send(d);
        } else {
            /* Send back WONT in all cases */
            queue_write(d, "\xFF\xFC", 2);
            queue_write(d, q, 1);
        }
#else
        queue_write(d, "\377\374", 2);
        queue_write(d, q, 1);
#endif

        d->inIAC = 0;
        return 1;
    } else if (d->inIAC == TELOPT_DONT) {
#ifdef MCCP_ENABLED
        if (*q == TELOPT_MCCP2) {        /* TELOPT_COMPRESS2 */
            d->telopt.mccp = 0;
            mccp_end(d);
        } else if (*q == TELOPT_MSSP) {
        } else if (*q == TELOPT_NAWS) {
        } else {
            /* Send back WONT in all cases */
            queue_write(d, "\377\xFC", 2);
            queue_write(d, q, 1);
        }
#else
        queue_write(d, "\377\374", 2); 
        queue_write(d, q, 1);
#endif
        d->inIAC = 0;
        return 1;
    } else if (d->inIAC == TELOPT_WONT) {
        d->inIAC = 0;
        return 1;
    } else if (*q == TELOPT_IAC) {
        /* Got TELNET IAC, store for next byte */
        d->inIAC = TELOPT_IAC;
        return 1;
    }

    return 0;
}
