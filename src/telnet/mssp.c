#include <params.h>
#include <interface.h>
#include <telnet.h>
#include <externs.h>
#include <props.h>
#include <interp.h>
#include <tune.h>
    
void
mssp_send(struct descriptor_data *d)
{
    char buf[BUFFER_LEN];
    char mssp_var[BUFFER_LEN];
    char mssp_val[BUFFER_LEN];
    char propname[BUFFER_LEN];
    char *p;
    PropPtr propadr, pptr;
    PropPtr prptr;

    const char *dir = "/~mssp";
    const dbref ref = (dbref)0;

    int sent_name = 0;
    int sent_players = 0;
    int sent_uptime = 0;
    int sent_hostname = 0;
    int sent_port = 0;
    int sent_codebase = 0;
    int sent_family = 0;

    queue_write(d, "\xFF\xFA\x46", 3);

    propadr = first_prop(ref, dir, &pptr, propname);
    while (propadr) {
        strcpy(mssp_var, propname);
        sprintf(buf, "%s%c%s", dir, PROPDIR_DELIMITER, propname);
        prptr = get_property(ref, buf);
        if (prptr) {
#ifdef DISKBASE
            propfetch(ref, prptr);
#endif
            switch (PropType(prptr)) {
                case PROP_STRTYP:
                    strcpy(mssp_val, PropDataUNCStr(prptr));
                    break;
                case PROP_INTTYP:
                    sprintf(mssp_val, "%d", PropDataVal(prptr));
                    break;
                case PROP_FLTTYP:
                    sprintf(mssp_val, "%#.15g", PropDataFVal(prptr));
                    break;
                default:
                    mssp_val[0] = '\0';
                    break;
            }
        }
        propadr = next_prop(pptr, propadr, propname);

        for (p = mssp_var; *p; p++)
            *p = UPCASE(*p);

        if (*mssp_var && *mssp_val) {
            if (!strcmp("NAME", mssp_var))
                sent_name = 1;
            else if (!strcmp("PLAYERS", mssp_var))
                sent_players = 1;
            else if (!strcmp("UPTIME", mssp_var))
                sent_uptime = 1;
            else if (!strcmp("HOSTNAME", mssp_var))
                sent_hostname = 1;
            else if (!strcmp("PORT", mssp_var))
                sent_port = 1;
            else if (!strcmp("CODEBASE", mssp_var))
                sent_codebase = 1;
            else if (!strcmp("FAMILY", mssp_var))
                sent_family = 1;

            sprintf(buf, "\x01%s\x02%s", mssp_var, mssp_val);
            queue_write(d, buf, strlen(buf));
        }
    }

    if (!sent_name) {
        sprintf(buf, "\x01%s\x02%s", "NAME", tp_muckname);
        queue_write(d, buf, strlen(buf));
    }

    if (!sent_players) {
        sprintf(buf, "\x01%s\x02%d", "PLAYERS", pcount());
        queue_write(d, buf, strlen(buf));
    }

    if (!sent_uptime) {
        sprintf(buf, "\x01%s\x02%d", "UPTIME", (int)startup_systime);
        queue_write(d, buf, strlen(buf));
    }

    // Sending localhost (the default) is pointless.
    if (!sent_hostname && strcmp("localhost", tp_servername)) {
        sprintf(buf, "\x01%s\x02%s", "HOSTNAME", tp_servername);
        queue_write(d, buf, strlen(buf));
    }

    if (!sent_port) {
        sprintf(buf, "\x01%s\x02%d", "PORT", tp_textport);
        queue_write(d, buf, strlen(buf));
    }

    if (!sent_codebase) {
        sprintf(buf, "\x01%s\x02%s", "CODEBASE", "ZetaMUCK");
        queue_write(d, buf, strlen(buf));
    }

    if (!sent_family) {
        sprintf(buf, "\x01%s\x02%s", "FAMILY", "TinyMUD");
        queue_write(d, buf, strlen(buf));
    }

    // The reported protocols cannot be overwridden by props on #0.

    sprintf(buf, "\x01%s\x02%d", "ANSI", 1);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "GMCP", 0);
    queue_write(d, buf, strlen(buf));

#ifdef MCCP_ENABLED
    sprintf(buf, "\x01%s\x02%d", "MCCP", 1);  
#else
    sprintf(buf, "\x01%s\x02%d", "MCCP", 0);
#endif
    queue_write(d, buf, strlen(buf));

    // send 0 for now, until MCP code is re-audited
    sprintf(buf, "\x01%s\x02%d", "MCP", 0);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "MSDP", 0);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "MSP", 0);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "MXP", 0);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "PUEBLO", 1);
    queue_write(d, buf, strlen(buf));

    // send 0 for now, until re-implemented
    sprintf(buf, "\x01%s\x02%d", "UTF-8", 0);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "VT100", 0);
    queue_write(d, buf, strlen(buf));

    sprintf(buf, "\x01%s\x02%d", "XTERM 256 COLORS", 1);
    queue_write(d, buf, strlen(buf));


    queue_write(d, "\xFF\xF0", 2);
}

