#include <params.h>
#include <interface.h>
#include <telnet.h>
#include <reg.h>
#include <externs.h>

void
telopt_sb_charset(struct descriptor_data *d) {
    char buf2[MAX_COMMAND_LEN];
    int spos, dpos, accepted_charsets = 0;
    char sep;

    //log_status("DBUG: (%d) IAC SB CHARSET REQUEST %x %x %s -- len: %d\n",
    //    d->descriptor,d->telopt.sb_buf[1],d->telopt.sb_buf[2],d->telopt.sb_buf+3,d->telopt.sb_buf_len);
    if (d->telopt.sb_buf[1] == TELOPT_CHARSET_ACCEPTED) {
        if (!string_compare((char *)d->telopt.sb_buf + 2, "ISO_8859-1:1987")) {
            d->encoding = ENC_LATIN1;
#ifdef UTF8_SUPPORT
        } else if (!string_compare((char *)d->telopt.sb_buf + 2, "UTF-8")) {
            d->encoding = ENC_UTF8;
        } else if (!string_compare((char *)d->telopt.sb_buf + 2, "IBM437")) {
            d->encoding = ENC_IBM437;
#endif
        } else { // US-ASCII or unsolicited.
            d->encoding = ENC_ASCII;
        }

    } else if (d->telopt.sb_buf[1] == TELOPT_CHARSET_REQUEST) {
        if (d->telopt.sb_buf_len < 3) /* Need more than CHARSET REQUEST SEP */
            return;
        if (d->telopt.sb_buf[2]) /* store the requested charset delimiter */
            sep = d->telopt.sb_buf[2];
        else
            return;
        //log_status("DBUG: (%d) CHARSET REQUEST, buffer valid, sep is 0x%x\n",d->descriptor,sep);
        spos = 3;
        dpos = 4;
        strncpy(buf2,"\xFF\xFA\x2A\x02",dpos);  /* IAC SB CHARSET ACCEPTED */

        while (d->telopt.sb_buf[spos]) {
            while (!(d->telopt.sb_buf[spos]==sep)) {
                buf2[dpos] = d->telopt.sb_buf[spos];
                dpos++;
                spos++;      
                if (!d->telopt.sb_buf[spos])
                    return;
            }
            /* should now be IAC SB CHARSET ACCEPTED <charset> IAC SE */
            buf2[dpos] = '\xFF';
            buf2[dpos+1] = '\xF0';
            buf2[dpos+2] = '\0';
            dpos++;
            dpos++;
            //log_status("DBUG: (%d) Found charset, created buffer %s\n",d->descriptor,buf2);
            if (strcasestr2(buf2, "US-ASCII") || strcasestr2(buf2, "us") ||
                    strcasestr2(buf2, "ANSI_X3.4-1968")) {
                d->encoding = ENC_ASCII;
                accepted_charsets = 1;
            } else if (strcasestr2(buf2, "UTF-8")) {
                d->encoding = ENC_UTF8;
                accepted_charsets = 1;
            } else if (strcasestr2(buf2, "ISO-8859-1") ||
                    strcasestr2(buf2, "ISO_8859-1:1987") ||
                    strcasestr2(buf2, "latin1")) {
                d->encoding = ENC_LATIN1;
                accepted_charsets = 1;
            } else if (strcasestr2(buf2, "IBM437") ||
                    strcasestr2(buf2, "cp437")) {
                d->encoding = ENC_IBM437;
                accepted_charsets = 1;
            }
            queue_write(d, buf2, dpos+1);
            //log_status("DBUG: (%d) IAC SB CHARSET ACCEPTED %s IAC SE, accepted: %d\n",
            //    d->descriptor,buf2,accepted_charsets);
            spos++;
            dpos = 4; // Reuse this buffer  
        }
        if (!accepted_charsets) {
            /* IAC SB CHARSET REJECTED IAC SE */
            queue_write(d, "\xFF\xFA\x2A\x03\xFF\xF0", 6);
            //log_status("DBUG: (%d) IAC SB CHARSET REJECTED IAC SE, accepted: %d\n",
            //    d->descriptor,accepted_charsets);
        }
        return;
    } else {
        // REJECTED or unsolicited...possibly do things here later.
    }
}

void
charset_send(struct descriptor_data *d)
{
    queue_write(d, "\xFF\xFA\x2A\x01\x20", 5); // IAC SB CHARSET REQUEST SEPERATOR:" "
#ifdef UTF8_SUPPORT
    queue_write(d, "UTF-8 ", 6);
    queue_write(d, "IBM437 ", 7);
#endif
    queue_write(d, "ISO_8859-1:1987 ", 16); // Latin1
    queue_write(d, "US-ASCII", 8);

    queue_write(d, "\xFF\xF0", 2);
}

