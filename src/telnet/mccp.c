#include <params.h>
#include <interface.h>
#include <telnet.h>
#include <externs.h>
#ifdef MCCP_ENABLED

void
do_compress(dbref player, int descr, const char *arg)
{
    struct descriptor_data *d = descrdata_by_descr(descr);   

    if (!strcmp(arg, "off")) {
        if (d->mccp) {
            mccp_end(d);
            queue_ansi(d, "MCCP Compression Ended.\r\n");
        } else
            queue_ansi(d, "You do not have MCCP turned on!\r\n");
    } else if (!strcmp(arg, "on")) {
        if (d->mccp) {
            queue_ansi(d, "You already have MCCP turned on!\r\n");
        } else if (!d->telopt.mccp) {
            queue_ansi(d, "Your client does not appear to support MCCP.\r\n");
        } else {
            mccp_start(d, d->telopt.mccp);
            queue_ansi(d, "MCCP Compression Started.\r\n");
        }                      
    } else
        queue_ansi(d, "Compression Help Goes Here\r\n");
}


bool
mccp_process_compressed(struct descriptor_data *d)
{
    int length;

    if (!d->mccp)
        return TRUE;
    
    // Try to write out some data..
    length = COMPRESS_BUF_SIZE - d->mccp->z->avail_out;
    if (length > 0) {
        int nWrite = 0;
      
        if ((nWrite = writesocket(d->descriptor, (const char *)d->mccp->buf, UMIN(length, 4096))) < 0) {
            if (errnosocket == EWOULDBLOCK)
                nWrite = 0;
            else
                return 0;
        }

        if (nWrite) {
            if (nWrite == length) {
                d->mccp->z->next_out = d->mccp->buf;
                d->mccp->z->avail_out = COMPRESS_BUF_SIZE;
            } else {
                memmove(d->mccp->buf, d->mccp->buf + nWrite, (length-nWrite));
                d->mccp->z->next_out = d->mccp->buf + (length-nWrite);
                d->mccp->z->avail_out += nWrite;
            }
        }
    }

    return 1;
}

void
mccp_start(struct descriptor_data *d, int version)
{
    z_stream *s;
    struct mccp *m;
    int opt = 0;
    struct shared_string *termtype;

#ifdef USE_SSL
    if (d->type == CT_SSL)
        return; 
#endif
   
    m = (struct mccp *) malloc(sizeof(struct mccp));
    m->z = NULL;
    m->buf = NULL;
    m->version = 0;

    termtype = d->telopt.termtypes->data.packed[0].data.string;
    
    /* log_status("MCCP_START(%d)\r\n", d->descriptor); */
    opt = (termtype != termtype_init_state &&
            !string_compare(termtype->data, "simplemu"));
    if (opt)
        setsockopt(d->descriptor, IPPROTO_TCP, TCP_NODELAY, (char *) &opt, sizeof(opt));

        if (d->type != CT_HTTP) {
	    if (version == 1)
                sockwrite(d, "\377\372\125\373\360", 5); /* IAC SB COMPRESS WILL SE (MCCP v1) */
	    else if (version == 2) 
                sockwrite(d, "\377\372\126\377\360", 5); /* IAC SB COMPRESS2 IAC SE (MCCP v2) */ //ff fa 56 ff f0

        }
    if (opt)  {
        //This is a temporary fix for a bug related to SimpleMU.
#ifdef WIN32
        Sleep(20); //20ms
#else
        fsync(d->descriptor);
        usleep(20000); //20ms
#endif
    }


    s = (z_stream *) malloc(sizeof(z_stream));
    m->buf = (unsigned char *) malloc(sizeof(unsigned char) * COMPRESS_BUF_SIZE);

    s->next_in = NULL;
    s->avail_in = 0;

    s->next_out = m->buf;
    s->avail_out = COMPRESS_BUF_SIZE;

    s->zalloc = NULL;
    s->zfree  = NULL;
    s->opaque = NULL;

    if (d->type == CT_HTTP) {
        deflateInit2(s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15+16, 8, Z_DEFAULT_STRATEGY);
    } else {
        if (deflateInit(s, 9) != Z_OK) {
            free((void *)m->buf);
            free((void *)m);
            free((void *)s);
            d->mccp = NULL;

            return;
        }
    }

    m->version = version;
    m->z = s;
    d->mccp = m;

    DR_RAW_ADD_FLAGS(d, DF_COMPRESS);

    return;
}


void
mccp_end(struct descriptor_data *d)
{
    unsigned char dummy[1];

    /* log_status("MCCP_END(%d)\n", d->descriptor); */

    if (!d->mccp)
        return;

    d->mccp->z->avail_in = 0;
    d->mccp->z->next_in = dummy;

    /* Blob says I should handle this differently.  I'll look into it later, since it works currently.  -Hinoserm */
    if (deflate(d->mccp->z, Z_FINISH) == Z_STREAM_END)
        mccp_process_compressed(d);

    deflateEnd(d->mccp->z);
    free((void *)d->mccp->buf);
    free((void *)d->mccp->z);
    free((void *)d->mccp);

    d->mccp = NULL;

    DR_RAW_REM_FLAGS(d, DF_COMPRESS);

    return;

}

#endif

