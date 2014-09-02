/* ProtoMUCK's main interface.c */
/*  int main() is in this file  */
#include "copyright.h"
#include "config.h"

#include "cgi.h"
#include "defaults.h"
#include "db.h"
#include "interface.h"
#include "params.h"
#include "tune.h"
#include "props.h"
#include "match.h"
#include "reg.h"
#ifdef MCP_SUPPORT
# include "mcp.h"
# include "mcpgui.h"
#endif
#include "mpi.h"
#include "reg.h"
#include "externs.h"
#include "mufevent.h"
#include "interp.h"
#include "newhttp.h"            /* hinoserm */
#include "netresolve.h"         /* hinoserm */
#include "telnet.h"

/* Cynbe stuff added to help decode corefiles:
#define CRT_LAST_COMMAND_MAX 65535
char crt_last_command[ CRT_LAST_COMMAND_MAX ];
int  crt_last_len;
int  crt_last_player; */
int crt_connect_count = 0;

char restart_message[BUFFER_LEN];
char shutdown_message[BUFFER_LEN];
int shutdown_flag = 0;
int restart_flag = 0;
int total_loggedin_connects = 0;

time_t delayed_shutdown = 0;

int pending_welcomes = 0;
time_t next_welcome = 0;

int event_needs_delay = 0;

int do_keepalive = 0;

#ifdef MODULAR_SUPPORT
struct module *modules = NULL;
#endif

#ifdef USE_PS

#ifndef PS_CLOBBER_ARGV
/* all but one options need a buffer to write their ps line in */
static char ps_buffer[PS_BUFFER_SIZE];
static const size_t ps_buffer_size = PS_BUFFER_SIZE;
#else /* !PS_CLOBBER_ARGV */
static char *ps_buffer;         /* will point to argv area */
static size_t ps_buffer_size;   /* space determined at run time */
#endif /* PS_CLOBBER_ARGV */

static size_t ps_buffer_fixed_size; /* size of the constant prefix */

/* save the original argv[] location here */
static int save_argc;
static char **save_argv;
extern char **environ;
extern struct frame* aForceFrameStack[9];
#endif /* USE_PS */

static const char *connect_fail = "Incorrect login.\r\n";
static const char *flushed_message = "<Flushed>\r\n";

unsigned int bytesIn = 0;       /* Total bytes sent TO the muck */
unsigned int bytesOut = 0;      /* Total bytes sent FROM the muck */
unsigned int commandTotal = 0;  /* Total commands entered by players */

struct descriptor_data *descriptor_list = 0;

#ifdef IPV6
#define MAX_LISTEN_SOCKS 32
static int is_ipv6[MAX_LISTEN_SOCKS];
#else
#define MAX_LISTEN_SOCKS 16
#endif
static int listener_port[MAX_LISTEN_SOCKS];
static int sock[MAX_LISTEN_SOCKS];
static int ndescriptors = 0;
static int numsocks = 0;

#ifdef UTF8_SUPPORT
/* This is the global iconv handle. It is available to all functions as a
 * convenience, but it must /never/ be shared between conversions. (i.e. if you
 * aren't going to complete your conversion in one scheduling pass, you need to
 * use a descriptor based handle instead) */
iconv_t iconv_global = NULL;
#endif

int maxd = 0;                   /* Moved from shovechars since needed in p_socket.c */

#ifdef UDP_SOCKETS
struct udp_frame udp_sockets[34];
int udp_count;
#endif

struct shared_string *termtype_init_state = NULL;

void parse_connect(const char *msg, char *command, char *user, char *pass);
void check_connect(struct descriptor_data *d, const char *msg);
void dump_users(struct descriptor_data *d, char *user);
void announce_disconnect(struct descriptor_data *);
void announce_disclogin(struct descriptor_data *);
void forget_descriptor(struct descriptor_data *);
void announce_unidle(struct descriptor_data *d);
void announce_login(struct descriptor_data *d);
void announce_idle(struct descriptor_data *d);
void remember_player_descr(dbref player, int);
void welcome_user(struct descriptor_data *d);
void forget_player_descr(dbref player, int);
void help_user(struct descriptor_data *d);
void mssp_send(struct descriptor_data *d);
void charset_send(struct descriptor_data *d);
void announce_connect(int descr, dbref);
void freeqs(struct descriptor_data *d);
void update_desc_count_table(void);
void init_descr_count_lookup(void);
void init_descriptor_lookup(void);
void process_commands(void);
void shovechars(void);


#if defined(DESCRFILE_SUPPORT) || defined(NEWHTTPD)
long descr_sendfile(struct descriptor_data *d, int start, int stop,
                    const char *filename, int pid);
void descr_sendfileblock(struct descriptor_data *d);
void descr_fsenddisc(struct descriptor_data *d);
#endif /* DESCRFILE_SUPPORT */

char *time_format_1(time_t);

struct descriptor_data *new_connection(int port, int sock);
struct descriptor_data *new_connection6(int port, int sock);
int queue_ansi(struct descriptor_data *d, const char *msg);
int do_command(struct descriptor_data *d, struct text_block *t);
int is_interface_command(const char *cmd);
int remember_descriptor(struct descriptor_data *);
int process_output(struct descriptor_data *d);
int process_input(struct descriptor_data *d);
int get_ctype(int port);
int make_socket(int);
int make_socket6(int);

#define MALLOC(result, type, number) do {   \
				       if (!((result) = (type *) malloc ((number) * sizeof (type)))) \
				       panic("Out of memory");                             \
				     } while (0)

#define FREE(x) (free((void *) x))

#ifndef BOOLEXP_DEBUGGING

#ifdef DISKBASE
extern FILE *input_file;
#endif
#ifdef DELTADUMPS
extern FILE *delta_infile;
extern FILE *delta_outfile;
#endif

#define MAX_SOCKETS 800

#ifdef USE_SSL
static int ssl_listener_port;
static int ssl_numsocks;
SSL_CTX *ssl_ctx;
SSL_CTX *ssl_ctx_client;
static int ssl_sock;
#ifdef IPV6
static int ssl_sock6;
#endif
#endif

bool db_conversion_flag = 0;
bool db_decompression_flag = 0;
bool db_hash_convert = 0;
bool wizonly_mode = 0;
bool verboseload = 0;

/* binding support */
int bind_to;
#ifdef IPV6
struct in6_addr bind6 = IN6ADDR_ANY_INIT;
#endif

time_t sel_prof_start_time;
long sel_prof_idle_sec;
long sel_prof_idle_usec;
unsigned long sel_prof_idle_use;

#ifdef USE_PS
void
save_ps_display_args(int argc, char *argv[])
{
    save_argc = argc;
    save_argv = argv;

#ifdef PS_CLOBBER_ARGV
    /* If we're going to overwrite the argv area, count the available
     * space.  Also move the environment to make additional room.  */
    {
        char *end_of_area = NULL;
        char **new_environ;
        int i;

        /* check for contiguous argv strings */
        for (i = 0; i < argc; i++) {
            if (i == 0 || end_of_area + 1 == argv[i])
                end_of_area = argv[i] + strlen(argv[i]);
        }

        if (end_of_area == NULL) { /* probably can't happen? */
            ps_buffer = NULL;
            ps_buffer_size = 0;
            return;
        }

        /* check for contiguous environ strings following argv */
        for (i = 0; environ[i] != NULL; i++) {
            if (end_of_area + 1 == environ[i])
                end_of_area = environ[i] + strlen(environ[i]);
        }

        ps_buffer = argv[0];
        ps_buffer_size = end_of_area - argv[0];

        /* move the environment out of the way */
        new_environ = (char **)malloc(sizeof(char *) * (i + 1));
        for (i = 0; environ[i] != NULL; i++)
            new_environ[i] = strdup(environ[i]);
        new_environ[i] = NULL;
        environ = new_environ;
    }
#endif /* PS_USE_CLOBBER_ARGV */
}

/*
 * Call this once during subprocess startup to set the identification
 * values.	At this point, the original argv[] array may be overwritten.
 */
void
init_ps_display(void)
{
#ifdef PS_CHANGE_ARGV
    save_argv[0] = ps_buffer;
    save_argv[1] = NULL;
#endif /* PS_CHANGE_ARGV */

#ifdef PS_CLOBBER_ARGV
    {
        int i;

        /* make extra argv slots point at end_of_area (a NULL) */
        for (i = 1; i < save_argc; i++)
            save_argv[i] = ps_buffer + ps_buffer_size;
    }
#endif /* PS_CLOBBER_ARGV */

#ifdef HAVE_SETPROCTITLE
    strncpy(ps_buffer, PROTOBASE ": ", ps_buffer_size);
#else /* !HAVE_SETPROCTITLE */
    strncpy(ps_buffer, "protomuck " PROTOBASE ": ", ps_buffer_size);
#endif /* HAVE_SETPROCTITLE */
    ps_buffer_fixed_size = strlen(ps_buffer);
}

/*
 * Call this to update the ps status display to a fixed prefix plus an
 * indication of what you're currently doing passed in the argument.
 */
void
set_ps_display(const char *status)
{
    /* Update ps_buffer to contain both fixed part and activity */
    strncpy(ps_buffer + ps_buffer_fixed_size, status,
            ps_buffer_size - ps_buffer_fixed_size);

    /* Transmit new setting to kernel, if necessary */

#ifdef HAVE_SETPROCTITLE
    setproctitle("%s", ps_buffer);
#endif

#ifdef PS_CLOBBER_ARGV
    {
        int buflen;

        /* pad unused memory */
        buflen = strlen(ps_buffer);
        memset(ps_buffer + buflen, PS_PADDING, ps_buffer_size - buflen);
    }
#endif /* PS_CLOBBER_ARGV */
}

#endif /* USE_PS */


void
show_program_usage(char *prog)
{
    fprintf(stderr,
            "Usage: %s [<options>] [infile [dumpfile [portnum [portnum ...]]]]\n",
            prog);
    fprintf(stderr, "   Arguments:\n");
    fprintf(stderr,
            "       infile            db loaded at startup. optional with -dbin.\n");
    fprintf(stderr,
            "       outfile           output db to save to. optional with -dbout.\n");
    fprintf(stderr,
            "       portnum           port num to listen for conns on. (%d ports max)\n",
            MAX_LISTEN_SOCKS);
    fprintf(stderr, "   Options:\n");
    fprintf(stderr,
            "       -dbin INFILE      uses INFILE as the database to load at startup.\n");
    fprintf(stderr,
            "       -dbout OUTFILE    uses OUTFILE as the output database to save to.\n");
    fprintf(stderr,
            "       -port NUMBER      sets the port number to listen for connections on.\n");
    fprintf(stderr,
            "       -bind IP          bind ProtoMUCK to a specific IP\n");
#ifdef IPV6
    fprintf(stderr,
            "       -bind6 IP         Enable ipv6 on this IP address\n");
#endif
    fprintf(stderr,
            "       -gamedir PATH     changes directory to PATH before starting up.\n");
    fprintf(stderr,
            "       -convert          load db, save in current format, and quit.\n");
    fprintf(stderr,
            "       -pwconvert        convert passwords to hashed format on next save.\n");
    fprintf(stderr,
            "       -decompress       when saving db, save in uncompressed format.\n");
    fprintf(stderr,
            "       -nosanity         don't do db sanity checks at startup time.\n");
    fprintf(stderr,
            "       -insanity         load db, then enter interactive sanity editor.\n");
    fprintf(stderr,
            "       -sanfix           attempt to auto-fix a corrupt db after loading.\n");
    fprintf(stderr, "       -wizonly          only allow wizards to login.\n");
    fprintf(stderr, "       -godpasswd PASS   reset God(#1)'s password to PASS.  Implies -convert\n");
	fprintf(stderr,
            "       -version          display this server's version.\n");
    fprintf(stderr, "       -verboseload      show messages while loading.\n");
    fprintf(stderr, "       -help             display this message.\n");
    exit(1);
}


extern int sanity_violated;

#ifdef USE_SSL
int
pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
    const char *pw = (const char *) userdata;
    int pwlen = strlen(pw);

    strncpy(buf, pw, size);
    return ((pwlen > size) ? size : pwlen);
}
#endif

#ifdef MALLOC_PROFILING
extern void free_old_macros(void);
extern void purge_all_free_frames(void);
extern void purge_mfns(void);
extern void cleanup_game(void);
extern void tune_freeparms(void);

extern void free_compress_dictionary(void);
#endif /* MALLOC_PROFILING */

int
main(int argc, char **argv)
{
    FILE *ffd;
    char *infile_name = NULL;
    char *outfile_name = NULL;
    char *num_one_new_passwd = NULL;
    int i, nomore_options;
    int sanity_skip;
    int sanity_interactive;
    int sanity_autofix;
    int val;
    int initsock = 0;
    int resolver_myport = 0;
    int nCurPtr = 0;

#ifdef DETACH
#ifndef __CYGWIN__
    int fd;
#endif
#endif

#ifdef WIN_VC
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
        perror("WSAStartup failed.");
        exit(0);
        return 0;
    }
#endif

#if defined(MALLOC_PROFILING) && defined(HAVE_PTHREAD_H)
	/* initializing the mutex lock for CrT */
	CrT_pthread_init();
#endif

    strcpy(restart_message, "\r\nServer restarting, be back in a few!\r\n");
    strcpy(shutdown_message, "\r\nServer shutting down, be back in a few!\r\n");
    time(&current_systime);
    init_descriptor_lookup();
    init_descr_count_lookup();
    init_color_hash();

    nomore_options = 0;
    sanity_skip = 0;
    sanity_interactive = 0;
    sanity_autofix = 0;
    bind_to = INADDR_ANY;
#ifdef USE_PS
    save_ps_display_args(argc, argv);
#endif /* USE_PS */
    for (i = 1; i < argc; i++) {
        if (!nomore_options && argv[i][0] == '-') {
            if (!strcmp(argv[i], "-convert")) {
                db_conversion_flag = 1;
            } else if (!strcmp(argv[i], "-decompress")) {
                db_decompression_flag = 1;
            } else if (!strcmp(argv[i], "-nosanity")) {
                sanity_skip = 1;
            } else if (!strcmp(argv[i], "-verboseload")) {
                verboseload = 1;
            } else if (!strcmp(argv[i], "-insanity")) {
                sanity_interactive = 1;
            } else if (!strcmp(argv[i], "-wizonly")) {
                wizonly_mode = 1;
            } else if (!strcmp(argv[i], "-sanfix")) {
                sanity_autofix = 1;
            } else if (!strcmp(argv[i], "-pwconvert")) {
                db_hash_convert = 1;
            } else if (!strcmp(argv[i], "-version")) {
                printf("ProtoMUCK %s (%s -- %s)\n", PROTOBASE, TINYVERSION,
                       NEONVER);
                exit(0);
            } else if (!strcmp(argv[i], "-dbin")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                infile_name = argv[++i];

            } else if (!strcmp(argv[i], "-dbout")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                outfile_name = argv[++i];

            } else if (!strcmp(argv[i], "-godpasswd")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                num_one_new_passwd = argv[++i];
                if (!ok_password(num_one_new_passwd)) {
                    fprintf(stderr, "Bad -godpasswd password.\n");
                    exit(1);
                }
                db_conversion_flag = 1;

            } else if (!strcmp(argv[i], "-port")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                resolver_myport = atoi(argv[++i]);
            } else if (!strcmp(argv[i], "-bind")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                bind_to = str2ip(argv[++i]);
                if (bind_to == -1) {
                    bind_to = INADDR_ANY;
                } else {
                    bind_to = ntohl(bind_to);
                }
#ifdef IPV6
            } else if (!strcmp(argv[i], "-bind6")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                bind6 = str2ip6(argv[++i]);
#endif
#ifndef WIN_VC
            } else if (!strcmp(argv[i], "-gamedir")) {
                if (i + 1 >= argc) {
                    show_program_usage(*argv);
                }
                if (chdir(argv[++i])) {
                    perror("cd to gamedir");
                    exit(4);
                }
#endif
            } else if (!strcmp(argv[i], "--")) {
                nomore_options = 1;
            } else {
                show_program_usage(*argv);
            }
        } else {
            if (!infile_name) {
                infile_name = argv[i];
            } else if (!outfile_name) {
                outfile_name = argv[i];
            } else {
                val = atoi(argv[i]);
                if (!((val <= 1) || (val >= 65535))) {
                    if (initsock) {
                        if (numsocks < MAX_LISTEN_SOCKS) {
                            listener_port[numsocks++] = val;
                        }
                    } else {
                        initsock = 1;
                    }
                }
            }
        }
    }

    if (!infile_name || !outfile_name) {
        show_program_usage(*argv);
    }
#ifdef DISKBASE
    if (!strcmp(infile_name, outfile_name)) {
        fprintf(stderr, "Output file must be different from the input file.");
        exit(3);
    }
#endif


    if (!sanity_interactive) {

#ifndef WIN_VC
#ifdef UTF8_SUPPORT
//    if (!setlocale(LC_CTYPE, "en_US.UTF-8")) {
//        fprintf(stdout, "Unable to change locale to UTF-8, aborting.\n");
//        fprintf(stdout, "Make sure your OS supports it!\n");
//        exit(1);
//    }
#ifndef LEGACY_COLLATION
    /* Setting LC_COLLATE to UTF-8 mode will confuse some people, i.e. "foobar"
     * will sort before ".foobar" and ".foobar" will sort before "foobaz". This
     * only effects the sorting of the standard library, it doesn't change any
     * of the manual sorting that we do. (i.e. prop ordering in examine) */
//    if (!setlocale(LC_COLLATE, "en_US.UTF-8")) {
//        fprintf(stdout, "Unable to change collation order to en_US.UTF-8!.\n");
//        fprintf(stdout, "Changing LC_CTYPE to en_US.UTF-8 succeeded though, weird.\n");
//        fprintf(stdout, "E-mail this error to davin@protomuck.org.\n");
//        exit(1);
//    }
#endif /* LEGACY_COLLATION */
#endif /* UTF8_SUPPORT */


#ifdef DETACH
#if defined(__CYGWIN__) || defined(WIN32) || defined(WIN_VC)
# ifdef __CYGWIN__
        fprintf(stdout, "ProtoMUCK-Cygwin %s now detaching from console.\n",
                PROTOBASE);
# else
        fprintf(stdout, "ProtoMUCK-Win32 %s now detaching from console.\n",
                PROTOBASE);
# endif
#endif
        /* Go into the background unless improper mode to */
        if (!sanity_interactive && !db_conversion_flag) {
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);
            if (fork() != 0)
                exit(0);
        }
#endif
#endif

        /* save the PID for future use */
        if ((ffd = fopen(PID_FILE, "w")) != NULL) {
            fprintf(ffd, "%d\n", getpid());
            fclose(ffd);
        }

        log_status_nowall("INIT: ProtoMUCK %s starting as pid %d.\n", PROTOBASE,
                          getpid());

#ifdef WIN32
        fprintf(stderr, "ProtoMUCK-Win32 %s(%s-compat)\n", PROTOBASE, TINYVERSION);
        fprintf(stderr, "----------------------------------------------\n");
        fprintf(stderr, "NOTE: The Windows port of Proto is considered\n");
        fprintf(stderr, " to be EXPERIMENTAL.  I will try to provide as\n");
        fprintf(stderr, " much support as I can for it, but recognize that\n");
        fprintf(stderr, " ProtoMuck as a whole is a spare-time project.\n");
        fprintf(stderr, "   --Moose/Van (contikimoose@hotmail.com)\n");
        fprintf(stderr, "   --Akari     (Nakoruru08@hotmail.com)\n");
        fprintf(stderr, "----------------------------------------------\n");
#endif


#ifdef DETACH
/*    fprintf(stdout,"Console messages to '%s', Error messages to '%s'.", LOG_FILE, LOG_ERR_FILE); */
        if (!sanity_interactive && !db_conversion_flag) {
            /* Detach from the TTY, log whatever output we have... */
            freopen(LOG_ERR_FILE, "a", stderr);
            setbuf(stderr, NULL);
            freopen(LOG_FILE, "a", stdout);
            setbuf(stdout, NULL);

/* Disassociate from Process Group */

/* CYGWIN hack - Cygwin is SYS_POSIX but doesnt come as such. */

#if defined(__CYGWIN__) || defined(SYS_POSIX) || defined (USE_SID)
            setsid();
#elif defined(SYSV) || defined(USE_SYSVPGRP)
            setpgrp();          /* System V's way */
#elif defined(BSD) || defined(USE_BSDPGRP)
            setpgrp(0, getpid()); /* BSDism */
#elif !defined(WIN_VC) /* anyone else, default to POSIX (WinNT, Linux) */
            setsid();
#endif
# ifdef  TIOCNOTTY              /* we can force this, POSIX / BSD */
            if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
                ioctl(fd, TIOCNOTTY, (char *) 0); /* lose controll TTY */
                close(fd);
            }
# endif /* TIOCNOTTY */
        }
#endif /* DETACH */
    }


#ifdef USE_PS
    /* Initialize the array of pointers used for tracking
     * PIDs during FORCE recursion */
    for ( ; nCurPtr < 9; ++nCurPtr )
    {
        aForceFrameStack[nCurPtr] = NULL;
    }
#endif

    /* Initialize MCP and some packages. */
#ifdef MCP_SUPPORT
    mcp_initialize();
    gui_initialize();
#endif

    /* initialize udp sockets */
#ifdef UDP_SOCKETS
    udp_count = 0;
    memset(udp_sockets, 0, sizeof(udp_sockets));
#endif


    sel_prof_start_time = current_systime; /* Set useful starting time */
    sel_prof_idle_sec = 0;
    sel_prof_idle_usec = 0;
    sel_prof_idle_use = 0;

    if (init_game(infile_name, outfile_name) < 0) {
        fprintf(stderr, "Couldn't load %s!\n", infile_name);
        exit(2);
    }

    if (num_one_new_passwd != NULL) {
        set_password(GOD, num_one_new_passwd);
    }

    if ((resolver_myport > 1) && (resolver_myport < 65536))
        tp_textport = resolver_myport;
    if ((tp_textport > 1) && (tp_textport < 65536)) {
        listener_port[numsocks++] = tp_textport;
#ifdef IPV6
	is_ipv6[numsocks-1] = 0;
        listener_port[numsocks++] = tp_textport;
	is_ipv6[numsocks-1] = 1;
#endif
    }
/* Only open a web port if support was #defined in config.h */
#ifdef NEWHTTPD                 /* hinoserm */
    if ((tp_wwwport > 1) && (tp_wwwport < 65536)) { /* hinoserm */
        listener_port[numsocks++] = tp_wwwport; /* hinoserm */
#ifdef IPV6
	is_ipv6[numsocks-1] = 0;
        listener_port[numsocks++] = tp_wwwport;
	is_ipv6[numsocks-1] = 1;
#endif
    }
#endif  /* NEWHTTPD */               /* hinoserm */
    if ((tp_puebloport > 1) && (tp_puebloport < 65536)) {
        listener_port[numsocks++] = tp_puebloport;
#ifdef IPV6
	is_ipv6[numsocks-1] = 0;
        listener_port[numsocks++] = tp_puebloport;
	is_ipv6[numsocks-1] = 1;
#endif
    }
#ifdef USE_SSL
    if ((tp_sslport > 1) && (tp_sslport < 65536)) { /* Alynna */
        ssl_listener_port = tp_sslport;
        ssl_numsocks = 1;
#ifdef IPV6
        ssl_numsocks++;
#endif
    }
#endif
    if (!numsocks) {
        listener_port[numsocks++] = TINYPORT;
#ifdef IPV6
	is_ipv6[numsocks-1] = 0;
        listener_port[numsocks++] = TINYPORT;
	is_ipv6[numsocks-1] = 1;
#endif
    }

#ifdef USE_PS
    {
        char buf[BUFFER_LEN];
        char tbuf[8];

        snprintf(buf, BUFFER_LEN, "%s: ", tp_muckname);
        for (i = 0; i < numsocks; i++) {
            if (get_ctype(listener_port[i]) == CT_MUCK) {
                snprintf(tbuf, 8, "%d ", listener_port[i]);
                strncat(buf, tbuf, BUFFER_LEN);
            }
        }

        init_ps_display();
        set_ps_display(buf);
    }
#endif /* USE_PS */


    if (!sanity_interactive && !db_conversion_flag) {
#if !defined(WIN_VC) || !defined(DEBUG)
        set_signals();
#endif

#if defined(USE_RESLVD) && defined(SPAWN_HOST_RESOLVER)
        if (reslvd_open() != 1)
            spawn_resolver();
#elif defined(USE_RESLVD)
        reslvd_open();
#elif defined(SPAWN_HOST_RESOLVER)
        spawn_resolver();
#endif

#ifdef MUD_ID
        do_setuid(MUD_ID);
#endif /* MUD_ID */

        host_init();

        if (!sanity_skip) {
            sanity(AMBIGUOUS);
            if (sanity_violated) {
                wizonly_mode = 1;
                if (sanity_autofix) {
                    sanfix(AMBIGUOUS);
                }
            }
        }

        /* go do it */
        shovechars();

        if (restart_flag) {
            close_sockets(restart_message);
        } else {
            close_sockets(shutdown_message);
        }

        do_dequeue(-1, (dbref) 1, "all");

        host_shutdown();
    }

    if (sanity_interactive) {
        san_main();
    } else {
        dump_database(0);
        tune_save_parmsfile();

#ifdef SPAWN_HOST_RESOLVER
        kill_resolver();
#endif
#ifdef USE_RESLVD
        reslvd_close();
#endif

#ifdef WIN_VC
        WSACleanup();
#endif

#ifdef MALLOC_PROFILING
        db_free();
        free_old_macros();
        purge_all_free_frames();
        purge_timenode_free_pool();
        purge_for_pool();
        purge_for_pool();       /* 2nd time is needed to completely purge */
        purge_try_pool();       /* 3rd time is needed to... oh... wait... */
        purge_try_pool();       /* 2nd time is needed to completely purge */
        purge_mfns();
        cleanup_game();
        tune_freeparms();
        free_compress_dictionary();
#endif /* MALLOC_PROFILING */

#ifdef DISKBASE
        fclose(input_file);
#endif
#ifdef DELTADUMPS
        fclose(delta_infile);
        fclose(delta_outfile);
        (void) unlink(DELTAFILE_NAME);
#endif

#ifdef MALLOC_PROFILING
        CrT_summarize_to_file("malloc_log", "Shutdown");
#endif

        if (restart_flag) {
            char portlist[BUFFER_LEN];
            char numbuf[16];

            portlist[0] = '\0';
            for (i = 0; i < numsocks; i++) {
                if ((listener_port[i] != tp_textport)
#ifdef NEWHTTPD                 /* hinoserm */
                    && (listener_port[i] != tp_wwwport) /* hinoserm */
#endif /* hinoserm */
                    && (listener_port[i] != tp_puebloport)) {
                    sprintf(numbuf, "%d", listener_port[i]);
                    if (*portlist) {
                        strcat(portlist, " ");
                    }
                    strcat(portlist, numbuf);
                }
            }
            execl("restart", "restart", portlist, (char *) 0);
        }
    }


    exit(0);

    return (0);
}

#endif /* BOOLEXP_DEBUGGING */

/* CGI decoder */

void
notify_descriptor(int descr, const char *msg)
{
    char *ptr1;
    const char *ptr2;
    char buf[BUFFER_LEN + 2];
    struct descriptor_data *d;

    if (!msg || !*msg)
        return;
    for (d = descriptor_list; d && (d->descriptor != descr); d = d->next) ;
    if (!d || d->descriptor != descr)
        return;

    ptr2 = msg;
    while (ptr2 && *ptr2) {
        ptr1 = buf;
        while ((ptr2) && (*ptr2) && (*ptr2 != '\r') && (*ptr2 != '\n'))
            *(ptr1++) = *(ptr2++);
        *(ptr1++) = '\r';
        *(ptr1++) = '\n';
        *(ptr1++) = '\0';
        while ((*ptr2 == '\r') || (*ptr2 == '\n'))
            ptr2++;
        queue_ansi(d, buf);
        process_output(d);
    }
}


/* Just like notify_descriptor, except no changes are made to the buffer
 * at all. This is for the purpos of sending unaltered binaries. 
 * Still experimental at this point. */
void
notify_descriptor_raw(int descr, const char *msg, int length)
{
    struct descriptor_data *d;

    if (!msg || !*msg)
        return;
    for (d = descriptor_list; d && (d->descriptor != descr); d = d->next) ;
    if (!d || d->descriptor != descr)
        return;
    //add_to_queue(&d->output, msg, length);
    add_to_queue(&d->output, msg, length, -2);
    d->output_size += length;
}

/* To go with the descriptor_notify_char prim, this has a singular
 * purpose in an effort to write binary pass through support into
 * a MUF based webserver. It will probably come into play down
 * the line if we write an in-server version of the MUF one in 
 * progress.
 */
void
notify_descriptor_char(int descr, char c)
{
    struct descriptor_data *d;
    char cbuf[1];

    for (d = descriptor_list; d && (d->descriptor != descr); d = d->next) ;
    if (!d || d->descriptor != descr)
        return;
    cbuf[0] = c;
    queue_write(d, cbuf, 1);
}

void
anotify_descriptor(int descr, const char *msg)
{
    /* Just like notify descriptor. But leaves ANSI in for 
     * connections not connected to a player that have a 
     * DR_COLOR flag set.
     */
    char buf3[BUFFER_LEN + 2];

    parse_ansi(NOTHING, buf3, msg, ANSINORMAL);
    notify_descriptor(descr, buf3);

}


char *
html_escape(const char *msg)
{
    char *tempstr;
    static char buf[BUFFER_LEN];
    char buff[BUFFER_LEN];

    buff[0] = '\0';
    tempstr = buff;
    while (*msg) {
        switch (*msg) {
            case '&':{
                *tempstr++ = '&';
                *tempstr++ = 'a';
                *tempstr++ = 'm';
                *tempstr++ = 'p';
                *tempstr++ = ';';
                break;
            }
            case '\"':{
                *tempstr++ = '&';
                *tempstr++ = 'q';
                *tempstr++ = 'u';
                *tempstr++ = 'o';
                *tempstr++ = 't';
                *tempstr++ = ';';
                break;
            }
            case '<':{
                *tempstr++ = '&';
                *tempstr++ = 'l';
                *tempstr++ = 't';
                *tempstr++ = ';';
                break;
            }
            case '>':{
                *tempstr++ = '&';
                *tempstr++ = 'g';
                *tempstr++ = 't';
                *tempstr++ = ';';
                break;
            }
/*			case ' ': {
				*tempstr++ = '&';
				*tempstr++ = '#';
				*tempstr++ = '3';
				*tempstr++ = '2';
				*tempstr++ = ';';
				break;
			} */
            case '\n':{
                break;
            }
            default:
                *tempstr++ = *msg;
        }
        (void) msg++;
    }
    *tempstr = '\0';
/*	strcpy(buf, "<TT><PRE>"); */
    strcpy(buf, buff);
/*	strcat(buf, "</TT></PRE>"); */
    return buf;
}

char *
html_escape2(char *msg, int addbr)
{
    char buf2[BUFFER_LEN];
    static char buff[BUFFER_LEN];

    strcpy(buf2, msg);
    if (strlen(buf2) >= (BUFFER_LEN - 18))
        buf2[BUFFER_LEN - 18] = '\0';
    strcpy(buff, "<CODE>");
    strcat(buff, html_escape(buf2));
    strcat(buff, "</CODE>");
    if (addbr) {
        strcat(buff, "<BR>");
    }
    return buff;
}

char *
grab_html(int nothtml, char *title)
{
    static char buf[BUFFER_LEN];

    buf[0] = '\0';

    if (!title || !*title) {
        return NULL;
    }
    if (!string_compare(title, "BR")) {
        strcpy(buf, "\r\n");
    }
    if (!string_compare(title, "P")) {
        if (nothtml) {
            strcpy(buf, "\r\n");
        } else {
            strcpy(buf, "\r\n      ");
        }
    }
    if (!string_compare(title, "LI")) {
        strcpy(buf, "\r\n* ");
    }
    return buf;
}

char *
html2text(char *msg)
{
    char *htmltitle, *tempstr;
    static char buf[BUFFER_LEN];
    char titlebuf[BUFFER_LEN];
    int nothtml = 0;

    while (*msg && isspace(*msg))
        (void) msg++;
    if (*msg == '/') {
        nothtml = 1;
        (void) msg++;
    }
    titlebuf[0] = '\0';
    htmltitle = titlebuf;
    while (*msg && !isspace(*msg))
        *htmltitle++ = *msg++;
    *htmltitle = '\0';

    tempstr = grab_html(nothtml, titlebuf);
    buf[0] = '\0';
    strcpy(buf, tempstr);
    return buf;
}

char
grab_html_char(char *htmlstr)
{
    static char c;
    int num = 0;

    if (*htmlstr == '#') {
        htmlstr++;
        if (number(htmlstr)) {
            num = atoi(htmlstr);
            if (!isprint((char) num) || ((char) num == '\n')) {
                notify(1, "&#unprintable;");
                c = 0;
            } else {
                notify(1, "&#??;");
                c = (char) num;
            }
        } else {
            notify(1, "&#unk;");
            c = 0;
        }
    } else {
        if (!string_compare(htmlstr, "lt")) {
            notify(1, "&lt;");
            c = '<';
        } else if (!string_compare(htmlstr, "gt")) {
            notify(1, "&gt;");
            c = '>';
        } else if (!string_compare(htmlstr, "quot")) {
            notify(1, "&quot;");
            c = '\"';
        } else if (!string_compare(htmlstr, "amp")) {
            notify(1, "&amp;");
            c = '&';
        } else {
            notify(1, "&unknown;");
            c = 0;
        }
    }
    return c;
}

char *
parse_html(char *msg)
{
    char c;
    char *tempstr, *tempstr2, *htmlstr;
    static char buf[BUFFER_LEN];
    char htmlbuf[BUFFER_LEN], buff[BUFFER_LEN];

    tempstr = buf;
    buf[0] = '\0';
    while (*msg) {
        if (strlen(tempstr) >= BUFFER_LEN - 10) {
            break;
        }
        if (*msg == '<') {
            (void) msg++;
            htmlbuf[0] = '\0';
            htmlstr = htmlbuf;
            while (*msg && (*msg != '>'))
                *htmlstr++ = *msg++;
            *htmlstr = '\0';
            if (*msg)
                (void) msg++;
            tempstr2 = strcpy(buff, html2text(htmlbuf));
            notify(1, "AFTER HTML2TEXT():");
            notify(1, htmlbuf);
            notify(1, buff);
            notify(1, tempstr2);
            while (*tempstr2)
                *tempstr++ = *tempstr2++;
        } else {
            if (*msg == '&') {
                (void) msg++;
                htmlbuf[0] = '\0';
                htmlstr = htmlbuf;
                while (*msg && (*msg != ';'))
                    *htmlstr++ = *msg++;
                *htmlstr = '\0';
                if (*msg)
                    (void) msg++;
                c = grab_html_char(htmlstr);
                notify(1, "AFTER GRAB_HTML_CHAR:");
                notify(1, htmlstr);
                notify(1, &c);
                if (c) {
                    *tempstr++ = c;
                }
            } else {
                *tempstr++ = *msg++;
            }
        }
    }
    *tempstr = '\0';
    return buf;
}

int
queue_ansi(struct descriptor_data *d, const char *msg)
{
    char buf[BUFFER_LEN + 5];
    char buf2[BUFFER_LEN + 5];

    if (!msg || !*msg)
        return 0;

    if (d->connected) {
        if (DR_RAW_FLAGS(d, DF_256COLOR) && (FLAGS(d->player) & CHOWN_OK)) {
            strip_bad_ansi(buf, msg);
        } else if (FLAGS(d->player) & CHOWN_OK) {
            strip_256_ansi(buf2, msg);
            strip_bad_ansi(buf, buf2);
        } else {
            strip_ansi(buf, msg);
        }
    } else {
        if (DR_RAW_FLAGS(d, DF_256COLOR) && DR_RAW_FLAGS(d, DF_COLOR)) {
            strip_bad_ansi(buf, msg);
        } else if (DR_RAW_FLAGS(d, DF_COLOR)) {
            strip_256_ansi(buf2, msg);
            strip_bad_ansi(buf, buf2);
        } else {
            strip_ansi(buf, msg);
        }
    }
#ifdef MCP_SUPPORT
    mcp_frame_output_inband(&d->mcpframe, buf);
#else
    queue_string(d, buf);
#endif
    return strlen(buf);
}

int
queue_html(struct descriptor_data *d, char *msg)
{
    char buf[BUFFER_LEN];

    if ((d->connected
         && OkObj(d->player)) ? (Html(d->player)) : (d->type == CT_PUEBLO)) {
        strncpy(buf, msg, BUFFER_LEN);
        return queue_ansi(d, buf);
    } else {
/*		strcpy(buf, parse_html(msg)); */
        return 0;
    }
}

int
queue_unhtml(struct descriptor_data *d, char *msg)
{
    char buf[BUFFER_LEN];

    if ((d->connected
         && OkObj(d->player)) ? (Html(d->player)) : (d->type == CT_PUEBLO)) {
/*		if(strlen(msg) >= (BUFFER_LEN/6)) { */
        strncpy(buf, html_escape2(msg, 0), BUFFER_LEN);
/*		} else {
			strcpy(buf, html_escape(msg));
			if (d->type == CT_HTTP) {
				strcat(buf, "<BR>");
			}
			strcpy(buf, html_escape2(msg, 0));
		} */
    } else {
        return queue_ansi(d, msg);
        //strncpy(buf, msg, BUFFER_LEN);
    }
    return queue_ansi(d, buf);
}

int
notify_nolisten(dbref player, const char *msg, int isprivate)
{
    struct descriptor_data *d;
    int retval = 0;
    char buf[BUFFER_LEN + 2];
    char buf2[BUFFER_LEN + 2];
    int firstpass = 1;
    char *ptr1;
    const char *ptr2;
    dbref ref;
    int *darr;
    int di;
    int dcount;
    int bufspace = 0;

    if (player < 0)
        return retval;          /* no one to notify */
    ptr2 = msg;
    while (ptr2 && *ptr2) {
        ptr1 = buf;
        while ((ptr2) && (*ptr2) && (*ptr2 != '\r') && (*ptr2 != '\r'))
            *(ptr1++) = *(ptr2++);
        *(ptr1++) = '\r';
        *(ptr1++) = '\n';
        *(ptr1++) = '\0';
        while ((*ptr2 == '\r') || (*ptr2 == '\n'))
            ptr2++;

        darr = get_player_descrs(player, &dcount);

        for (di = 0; di < dcount; di++) {
            d = descrdata_by_index(darr[di]);
            if (d->connected && d->player == player) {
                if ((d->linelen > 0) && !(FLAGS(player) & CHOWN_OK)) {
                    if (d)
                        queue_unhtml(d, buf);
                } else if (d)
                    queue_unhtml(d, buf);

                if (firstpass)
                    retval++;
            }
        }
        if (tp_zombies) {
            if ((Typeof(player) == TYPE_THING) && (FLAGS(player) & ZOMBIE) &&
                !(FLAGS(OWNER(player)) & ZOMBIE) && !(FLAGS(player) & QUELL)) {
                ref = getloc(player);
                if (Mage(OWNER(player)) || ref == NOTHING ||
                    Typeof(ref) != TYPE_ROOM || !(FLAGS(ref) & ZOMBIE)) {
                    if (isprivate || getloc(player) != getloc(OWNER(player))) {
                        char pbuf[BUFFER_LEN];
                        const char *prefix;

                        prefix = GETPECHO(player);

                        if (prefix && *prefix) {
                            char ch = *match_args;

                            *match_args = '\0';
                            prefix = do_parse_mesg(-1, player, player, prefix,
                                                   "(@Pecho)", pbuf,
                                                   MPI_ISPRIVATE);
                            *match_args = ch;
                        }
                        if (!prefix || !*prefix) {
                            prefix = NAME(player);
                            bufspace = (int) (BUFFER_LEN - (strlen(prefix) + 3));
                            SNPRINTF(buf2, BUFFER_LEN, "%s> %.*s", "%U> %.*U",
                                    prefix, bufspace, buf);
                        } else {
                            bufspace = (int) (BUFFER_LEN - (strlen(prefix) + 2));
                            SNPRINTF(buf2, BUFFER_LEN, "%s %.*s", "%U %.*U",
                                    prefix, bufspace, buf);
                        }
                        darr = get_player_descrs(OWNER(player), &dcount);

                        for (di = 0; di < dcount; di++) {
                            d = descrdata_by_index(darr[di]);
                            if (Html(OWNER(player)) && d)
                                queue_ansi(d, html_escape(buf2));
                            else if (d)
                                queue_unhtml(d, buf2);
                            if (firstpass)
                                retval++;
                        }
                    }
                }
            }
        }
        firstpass = 0;
    }
    return retval;
}


int
notify_html_nolisten(dbref player, const char *msg, int isprivate)
{
    struct descriptor_data *d;
    int retval = 0;
    char buf[BUFFER_LEN + 2];
    char buf2[BUFFER_LEN + 2];
    int firstpass = 1;
    char *ptr1;
    const char *ptr2;
    dbref ref;
    int *darr;
    int di;
    int dcount;
    int bufspace = 0;

    if (player < 0)
        return retval;          /* no one to notify */
    ptr2 = msg;
    while (ptr2 && *ptr2) {
        ptr1 = buf;
        while ((ptr2) && (*ptr2) && (*ptr2 != '\r') && (*ptr2 != '\n'))
            *(ptr1++) = *(ptr2++);
        if ((*ptr2 == '\r') || (*ptr2 == '\n')) {
            while ((*ptr2 == '\r') || (*ptr2 == '\n'))
                ptr2++;
            *(ptr1++) = '\r';
            *(ptr1++) = '\n';
        }
        *(ptr1++) = '\0';

        darr = get_player_descrs(player, &dcount);

        for (di = 0; di < dcount; di++) {
            d = descrdata_by_index(darr[di]);
            if ((d->linelen > 0) && !(FLAGS(player) & CHOWN_OK)) {
                if (d)
                    queue_html(d, buf);
            } else {
                if (d)
                    queue_html(d, buf);
                if (NHtml(d->player) && d)
                    queue_html(d, "<BR>");
            }
            if (firstpass)
                retval++;
        }
        if (tp_zombies) {
            if ((Typeof(player) == TYPE_THING) && (FLAGS(player) & ZOMBIE) &&
                !(FLAGS(OWNER(player)) & ZOMBIE) && !(FLAGS(player) & QUELL)) {
                ref = getloc(player);
                if (Mage(OWNER(player)) || ref == NOTHING ||
                    Typeof(ref) != TYPE_ROOM || !(FLAGS(ref) & ZOMBIE)) {
                    if (isprivate || getloc(player) != getloc(OWNER(player))) {
                        char pbuf[BUFFER_LEN];
                        const char *prefix;

                        prefix = GETPECHO(player);

                        if (prefix && *prefix) {
                            char ch = *match_args;

                            *match_args = '\0';
                            prefix = do_parse_mesg(-1, player, player, prefix,
                                                   "(@Pecho)", pbuf,
                                                   MPI_ISPRIVATE);
                            *match_args = ch;
                        }
                        if (!prefix || !*prefix) {
                            prefix = NAME(player);
                            bufspace = (int) (BUFFER_LEN - (strlen(prefix) + 3));
                            SNPRINTF(buf2, BUFFER_LEN, "%s> %.*s", "%U> %.*U",
                                    prefix, bufspace, buf);
                        } else {
                            bufspace = (int) (BUFFER_LEN - (strlen(prefix) + 2));
                            SNPRINTF(buf2, BUFFER_LEN, "%s %.*s", "%U %.*U",
                                    prefix, bufspace, buf);
                        }
                        darr = get_player_descrs(OWNER(player), &dcount);

                        for (di = 0; di < dcount; di++) {
                            d = descrdata_by_index(darr[di]);
                            if (d)
                                queue_html(d, buf2);
                            if (firstpass)
                                retval++;
                        }
                    }
                }
            }
        }
        firstpass = 0;
    }
    return retval;
}


int
notify_from_echo(dbref from, dbref player, const char *msg, int isprivate)
{
#ifdef IGNORE_SUPPORT
    if (ignorance(from, player))
        return 0;
#endif
    return notify_listeners(dbref_first_descr(from), from, NOTHING, player,
                            getloc(from), msg, isprivate);
}

int
notify_html_from_echo(dbref from, dbref player, const char *msg, int isprivate)
{
#ifdef IGNORE_SUPPORT
    if (ignorance(from, player))
        return 0;
#endif
    return notify_html_listeners(dbref_first_descr(from), from, NOTHING,
                                 player, getloc(from), msg, isprivate);
}

int
notify_from(dbref from, dbref player, const char *msg)
{
    return notify_from_echo(from, player, msg, 1);
}

int
notify_html_from(dbref from, dbref player, const char *msg)
{
    return notify_html_from_echo(from, player, msg, 1);
}


int
notify(dbref player, const char *msg)
{
    return notify_from_echo(player, player, msg, 1);
}

int
notify_html(dbref player, const char *msg)
{
    return notify_html_from_echo(player, player, msg, 1);
}

int
anotify_nolisten(dbref player, const char *msg, int isprivate)
{
    char buf[BUFFER_LEN + 2];

    if (!OkObj(player))
        return 0;

    if ((FLAGS(OWNER(player)) & CHOWN_OK) && !(FLAG2(OWNER(player)) & F2HTML)) {
        parse_ansi(player, buf, msg, ANSINORMAL);
    } else {
        unparse_ansi(buf, msg);
    }
    return notify_nolisten(player, buf, isprivate);
}

int
anotify_nolisten2(dbref player, const char *msg)
{
    return anotify_nolisten(player, msg, 1);
}

int
anotify_from_echo(dbref from, dbref player, const char *msg, int isprivate)
{
#ifdef IGNORE_SUPPORT
    if (ignorance(from, player))
        return 0;
#endif
    return ansi_notify_listeners(dbref_first_descr(from), from, NOTHING,
                                 player, getloc(from), msg, isprivate);
}


int
anotify_from(dbref from, dbref player, const char *msg)
{
    return anotify_from_echo(from, player, msg, 1);
}


int
anotify(dbref player, const char *msg)
{
    return anotify_from_echo(player, player, msg, 1);
}

struct timeval
timeval_sub(struct timeval now, struct timeval then)
{
    now.tv_sec -= then.tv_sec;
    now.tv_usec -= then.tv_usec;
    if (now.tv_usec < 0) {
        now.tv_usec += 1000000;
        now.tv_sec--;
    }
    return now;
}

int
msec_diff(struct timeval now, struct timeval then)
{
    return ((now.tv_sec - then.tv_sec) * 1000
            + (now.tv_usec - then.tv_usec) / 1000);
}

struct timeval
msec_add(struct timeval t, int x)
{
    t.tv_sec += x / 1000;
    t.tv_usec += (x % 1000) * 1000;
    if (t.tv_usec >= 1000000) {
        t.tv_sec += t.tv_usec / 1000000;
        t.tv_usec = t.tv_usec % 1000000;
    }
    return t;
}

struct timeval
update_quotas(struct timeval last, struct timeval current)
{
    int nslices;
    int cmds_per_time;
    struct descriptor_data *d;

    nslices = msec_diff(current, last) / tp_command_time_msec;

    if (nslices > 0) {
        for (d = descriptor_list; d; d = d->next) {
            if (d->connected) {
                cmds_per_time = ((FLAGS(d->player) & INTERACTIVE)
                                 ? (tp_commands_per_time *
                                    8) : tp_commands_per_time);
            } else {
                cmds_per_time = tp_commands_per_time;
            }
            d->quota += cmds_per_time * nslices;
            if (d->quota > tp_command_burst_size)
                d->quota = tp_command_burst_size;
        }
    }
    return msec_add(last, nslices * tp_command_time_msec);
}

/*
 * long max_open_files()
 *
 * This returns the max number of files you may have open
 * as a long, and if it can use setrlimit() to increase it,
 * it will do so.
 *
 * Becuse there is no way to just "know" if get/setrlimit is
 * around, since its defs are in <sys/resource.h>, you need to
 * define USE_RLIMIT in config.h to attempt it.
 *
 * Otherwise it trys to use sysconf() (POSIX.1) or getdtablesize()
 * to get what is avalible to you.
 */
#ifdef HAVE_RESOURCE_H
# include <sys/resource.h>
#endif

#if defined(RLIMIT_NOFILE) || defined(RLIMIT_OFILE)
# define USE_RLIMIT
#endif

long
max_open_files(void)
{
#if defined(_SC_OPEN_MAX) && !defined(USE_RLIMIT) /* Use POSIX.1 method, sysconf() */
/*
 * POSIX.1 code.
 */
    return sysconf(_SC_OPEN_MAX);
#else /* !POSIX */
# if defined(USE_RLIMIT) && (defined(RLIMIT_NOFILE) || defined(RLIMIT_OFILE))
#  ifndef RLIMIT_NOFILE
#   define RLIMIT_NOFILE RLIMIT_OFILE /* We Be BSD! */
#  endif /* !RLIMIT_NOFILE */
/*
 * get/setrlimit() code.
 */
    struct rlimit file_limit;

    getrlimit(RLIMIT_NOFILE, &file_limit); /* Whats the limit? */

    if (file_limit.rlim_cur < file_limit.rlim_max) { /* if not at max... */
        file_limit.rlim_cur = file_limit.rlim_max; /* ...set to max. */
        setrlimit(RLIMIT_NOFILE, &file_limit);

        getrlimit(RLIMIT_NOFILE, &file_limit); /* See what we got. */
    }

    return (long) file_limit.rlim_cur;

# else /* !RLIMIT */
/*
 * Don't know what else to do, try getdtablesize().
 * email other bright ideas to me. :) (whitefire)
 */
    return (long) getdtablesize();
# endif /* !RLIMIT */
#endif /* !POSIX */
}

int
sockwrite(struct descriptor_data *d, const char *str, int len)
{

    /* 
    WARNING: It is not acceptable to use sockwrite() directly except in extremely specific cases,
             where buffering may result in unexpected results.  Please use queue_write(), which
             has the same function parameters as sockwrite().  -Hinoserm
    */
#ifdef MCCP_ENABLED
    if (d->mccp) {
        d->mccp->z->next_in = (unsigned char *)str;
        d->mccp->z->avail_in = len;

        //while (d->out_compress->avail_in)
        //{
			d->mccp->z->avail_out = COMPRESS_BUF_SIZE - (d->mccp->z->next_out - d->mccp->buf);

            if (d->mccp->z->avail_out)

            {

                if (deflate(d->mccp->z, Z_SYNC_FLUSH) != Z_OK)

                    return -1;

            }

            mccp_process_compressed(d);
        //}

        return len - d->mccp->z->avail_in;

    } else
#endif /* MCCP_ENABLED */
#ifdef USE_SSL
        if (d->ssl_session)
            return SSL_write(d->ssl_session, str, len);
        else
#endif
            return writesocket(d->descriptor, str, len);
}

void
goodbye_user(struct descriptor_data *d)
{
    anotify_descriptor(d->descriptor, "\r\n");
    anotify_descriptor(d->descriptor, tp_leave_message);
    anotify_descriptor(d->descriptor, "\r\n\r\n");
    /*
    queue_write(d, "\r\n", 2);
    queue_write(d, tp_leave_message, strlen(tp_leave_message));
    queue_write(d, "\r\n\r\n", 4);
    */
}

void
idleboot_user(struct descriptor_data *d)
{
    queue_write(d, "\r\n", 2);
    queue_write(d, tp_idleboot_msg, strlen(tp_idleboot_msg));
    queue_write(d, "\r\n\r\n", 4);
    d->booted = BOOT_DROP;
}

static int con_players_max = 0; /* one of Cynbe's good ideas. */
static int con_players_curr = 0; /* for playermax checks. */
extern void purge_free_frames(void);

time_t current_systime = 0;
time_t startup_systime = 0;

#ifdef EXPERIMENTAL_THREADING

#ifdef WIN_VC
DWORD WINAPI threaded_output_handler(void *ptr)
#else
void *threaded_output_handler(void *ptr)
#endif
{
	fd_set output_set;
    struct timeval timeout;
	struct descriptor_data *d, *dnext;
    struct descriptor_data *newd;
    int avail_descriptors;

    while (shutdown_flag == 0) { /* Game Loop */
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        FD_ZERO(&output_set);

        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;
			if (d->output.head) /* Prepare write pool */
				FD_SET(d->descriptor, &output_set);
#ifdef MCCP_ENABLED /* If MCCP is enabled, and data is waiting to be sent out of the compression buffer */
			else if (d->mccp && COMPRESS_BUF_SIZE - d->mccp->z->avail_out)
				FD_SET(d->descriptor, &output_set);
#endif
			
#ifdef USE_SSL
			if (d->ssl_session) {
				/* SSL may want to write even if the output queue is empty */
				if (!SSL_is_init_finished(d->ssl_session))
					FD_CLR(d->descriptor, &output_set);
				if (SSL_want_write(d->ssl_session))
					FD_SET(d->descriptor, &output_set);
			}
#endif
		}

		if (select(maxd, (fd_set *) 0, &output_set, (fd_set *) 0, &timeout) < 0) {
            if (errnosocket != EINTR) { /* select() returned crit error */
                perror("select");
                //return;
            }
        } else {                /* select returned >= 0 */
			for (d = descriptor_list; d; d = dnext) {
                dnext = d->next;
                if (FD_ISSET(d->descriptor, &output_set)) {
                    if (!process_output(d)) /* send text */
                        d->booted = BOOT_DROP; /* connection lost */
#ifdef MCCP_ENABLED
                    else if (!mccp_process_compressed(d))
                        d->booted = BOOT_DROP;
#endif
                }
			}
		}
	}
#ifdef WIN_VC
# ifndef __cplusplus
	/* Supposedly, when using C++ and WINAPI, you just return from the thread function. */
	ExitThread(0);
# endif
#else
    pthread_exit(NULL);
#endif
	return NULL;
}
#endif /* EXPERIMENTAL_THREADING */


void
shovechars(void)
{
    fd_set input_set, output_set;
    time_t now, tmptq;
    struct timeval last_slice, current_time;
    struct timeval next_slice;
    struct timeval timeout, slice_timeout;
    int cnt;
    struct descriptor_data *d, *dnext;
    struct descriptor_data *newd;
    int avail_descriptors;
    struct timeval sel_in, sel_out;
    int openfiles_max;
    int i;
    char buf[2048], buf2[256];
#ifdef USE_SSL
    int ssl_status_ok = 1;
#endif
#ifdef MUF_SOCKETS
    extern struct muf_socket_queue *socket_list;
    struct muf_socket_queue *curr = socket_list;
#endif
#ifdef EXPERIMENTAL_THREADING
# ifndef WIN_VC
    int result;
    pthread_t thread1;
    pthread_attr_t attr;
# endif
#endif

    time(&startup_systime);

#ifdef USE_SSL
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    ssl_ctx_client = SSL_CTX_new(SSLv23_client_method());

    if (!SSL_CTX_use_certificate_file(ssl_ctx, SSL_CERT_FILE, SSL_FILETYPE_PEM)) {
        log_status("SSLX: Could not load certificate file %s\n", SSL_CERT_FILE);
        ssl_status_ok = 0;
    }
    if (ssl_status_ok) {
        SSL_CTX_set_default_passwd_cb(ssl_ctx, pem_passwd_cb);
        SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx,
                                               (void *) tp_ssl_keyfile_passwd);

        if (!SSL_CTX_use_PrivateKey_file
            (ssl_ctx, SSL_KEY_FILE, SSL_FILETYPE_PEM)) {
            log_status("SSLX: Could not load private key file %s\n",
                       SSL_KEY_FILE);
            ssl_status_ok = 0;
        }
    }
    if (ssl_status_ok) {
        if (!SSL_CTX_check_private_key(ssl_ctx)) {
            log_status
                ("SSLX: Private key does not check out and appears to be invalid.\n");
            ssl_status_ok = 0;
        }
    }

    if (ssl_status_ok) {
        ssl_sock = make_socket(tp_sslport);
        log_status("SSLX: SSL initialized OK\n");
        ssl_numsocks++;
#ifdef IPV6
        ssl_sock6 = make_socket6(tp_sslport);
        log_status("SSLX: SSL ipv6 initialized OK\n");
        ssl_numsocks++;
#endif
    } else {
        ssl_numsocks = 0;
    }
#endif

    for (i = 0; i < numsocks; i++) {
#ifdef IPV6
	if (is_ipv6[i])
            sock[i] = make_socket6(listener_port[i]);
	else
#endif
            sock[i] = make_socket(listener_port[i]); /* fixes the socket blocking problem at startup. */
    }

    gettimeofday(&last_slice, (struct timezone *) 0);

    openfiles_max = max_open_files();
    printf("Max FDs = %d\n", openfiles_max);

    printf("\nListening on: ");
    for (i = 0; i < numsocks; i++) {
        if (get_ctype(listener_port[i]) == CT_MUCK) {
            printf("%d ", listener_port[i]);
        }
    }
    printf("\n");

    avail_descriptors = max_open_files() - 10;
#ifdef EXPERIMENTAL_THREADING
# ifdef WIN_VC
    CreateThread(NULL, 0, threaded_output_handler, NULL, 0, NULL);
# else
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    result = pthread_create(&thread1, &attr, threaded_output_handler, NULL);
 
    pthread_attr_destroy(&attr);

    if (result) {
		perror("Unable to start output thread");
    }
# endif
#endif /* EXPERIMENTAL_THREADING */


    while (shutdown_flag == 0) { /* Game Loop */
        gettimeofday(&current_time, (struct timezone *) 0);

        last_slice = update_quotas(last_slice, current_time);

        next_muckevent();       /* Process things in event.c */
        process_commands();     /* Process player commands in game.c */
        muf_event_process();    /* Process MUF events in mufevents.c */

        purge_free_frames();
        untouchprops_incremental(1);

        if (delayed_shutdown && current_systime >= delayed_shutdown ) {
            log_status("Countdown reached, shutting down.\n");
            shutdown_flag = 1;
        }

        if (shutdown_flag)
            break;              /* Get out of game loop */

        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        next_slice = msec_add(last_slice, tp_command_time_msec);
        slice_timeout = timeval_sub(next_slice, current_time);

        FD_ZERO(&output_set);
        FD_ZERO(&input_set);


        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;

            if (!d->booted && DR_RAW_FLAGS(d, DF_WELCOMING)) {
                /* This is where we render the welcome screen for DF_TELNET
                 * connections if tp_delayed_welcomes is enabled.
                 * We use this approach to give telopt negotiation an
                 * opportunity to negotiate capabilities and
                 * enable DF_COLOR, DF_COLOR256, and DF_PUEBLO. */
                if (current_systime >= d->connected_at + 1) {
                    DR_RAW_REM_FLAGS(d, DF_WELCOMING);
                    announce_login(d);
                    welcome_user(d);
                    pending_welcomes--;
                }
            }
#if defined(DESCRFILE_SUPPORT) || defined(NEWHTTPD)
            if (d->dfile)
                descr_sendfileblock(d);
#endif /* DESCRFILE_SUPPORT */
            /* This was a tremendous eyesore before. I've tweaked the formatting
             * to make it more readable, and have created #defines for the
             * various d->booted settings. -davin */

            if (d->booted) {
                // BOOT_DROP and BOOT_QUIT are unconditional punts.

                if (// boot BOOT_SAFE if...
                    (d->booted == BOOT_SAFE &&
                        // ...no tasks queued, and no output queued...
                        !(descr_running_queue(d->descriptor) || d->output.lines) &&
                        // ...and the webserver doesn't have something in the timequeue
                        !(d->type == CT_HTTP && in_timequeue(d->http->pid))) ||
                    // ignore BOOT_DEFERRED and BOOT_SAFE unless...
                    (d->booted != BOOT_DEFERRED && d->booted != BOOT_SAFE) ||
                    // ...type is CF_MUF and the running queue is empty.
                    (d->type == CT_MUF && !descr_running_queue(d->descriptor))
                    // this check used to be here, but was never evaluated.
                    //(d->type == CT_INBOUND && d->booted == BOOT_DROP)
                   ) {
#ifdef NEWHTTPD
                    if (!(d->flags & DF_HALFCLOSE)) {
#endif /* NEWHTTPD */
                        process_output(d); /* send the queued notifies */
                        if (d->booted == BOOT_QUIT)
                            goodbye_user(d);
                        process_output(d); /* send QUIT related notifies */
#ifdef NEWHTTPD
                    }
                    if (d->type == CT_HTTP && d->booted != BOOT_SAFE) {
                        // d->booted is about to be reset, so this must be set now.
                        d->http->close = 1;
                    } 
#endif /* NEWHTTPD */
                    d->booted = 0;
#ifdef NEWHTTPD
                    if (d->type == CT_HTTP) {
                        struct frame *tmpfr;

                        if ((tmpfr = timequeue_pid_frame(d->http->pid))
                            && tmpfr->descr == d->descriptor)
                            dequeue_process(d->http->pid);
                        dequeue_prog_descr(d->descriptor, 2);
                    }
                    if (!d->connected && (d->type != CT_HTTP)) /* quit from login screen (hinoserm changed) */
#else /* !NEWHTTPD */
                    if (!d->connected)
#endif /* NEWHTTPD */
                        announce_disclogin(d);
#ifdef NEWHTTPD
                    if ((d->type == CT_HTTP) && !(d->flags & DF_HALFCLOSE)) {
                        if (!d->http->close) {
                            // HTTP/1.1 persistent connections -davin
                            http_deinitstruct(d);
                            DR_RAW_REM_FLAGS(d, DF_KEEPALIVE);
                            http_initstruct(d);
                        } else {
                            d->flags |= DF_HALFCLOSE;
                            shutdown(d->descriptor, 1);
                        }
                    } else
#endif /* NEWHTTP */
                    if (d->type != CT_INBOUND) { /* Don't touch MUF sockets */
                        shutdownsock(d);
                        continue; /* this is important, it keeps the FD_SET() code below from breaking. */
                    }
                }               /* if d->booted != BOOT_DEFERRED */
            } /* if (d->booted) */

            if (do_keepalive && DR_RAW_FLAGS(d, DF_TELNET) &&
                    DR_RAW_FLAGS(d, DF_KEEPALIVE) && !d->inIAC) {
                buf[0] = TELOPT_IAC;
                buf[1] = TELOPT_NOP;
                queue_write(d, buf, 2);
            }

            if (d->input.lines > 100)
                timeout = slice_timeout;
            else
                FD_SET(d->descriptor, &input_set);

#ifndef EXPERIMENTAL_THREADING
            if (d->output.head) /* Prepare write pool */
                FD_SET(d->descriptor, &output_set);
# ifdef MCCP_ENABLED /* If MCCP is enabled, and data is waiting to be sent out of the compression buffer */
            else if (d->mccp && COMPRESS_BUF_SIZE - d->mccp->z->avail_out)
                FD_SET(d->descriptor, &output_set);
# endif
#endif /* EXPERIMENTAL_THREADING */
#ifdef USE_SSL
            if (d->ssl_session) {
                /* SSL may want to write even if the output queue is empty */
                if (!SSL_is_init_finished(d->ssl_session)) {
                    FD_CLR(d->descriptor, &output_set);
                    FD_SET(d->descriptor, &input_set);
                }
                if (SSL_want_write(d->ssl_session))
                    FD_SET(d->descriptor, &output_set);
            }
#endif
        }                       /* for (d) */

        do_keepalive = 0;

        if (ndescriptors < avail_descriptors) /* prepare read pool */
            for (i = 0; i < numsocks; i++)
                FD_SET(sock[i], &input_set);

#ifdef USE_SSL
        if (ssl_numsocks > 0)
            FD_SET(ssl_sock, &input_set);
#ifdef IPV6
        if (ssl_numsocks > 0)
            FD_SET(ssl_sock6, &input_set);
#endif
#endif

#ifdef SPAWN_HOST_RESOLVER
        if (resolverpid)
            FD_SET(resolver_sock[1], &input_set);
#endif

#ifdef USE_RESLVD
        if (reslvd_connected)
            FD_SET(reslvd_sock, &input_set);
#endif

#ifdef MUF_SOCKETS
        for (curr = socket_list; curr; curr = curr->next) {
            if (!curr->theSock->readWaiting && curr->theSock->connected == 1)
                FD_SET(curr->theSock->socknum, &input_set);
            else if (!curr->theSock->connected)
                FD_SET(curr->theSock->socknum, &output_set);
        }
#endif

#ifdef UDP_SOCKETS
        /* Add UDP sockets to the input set */
        if (udp_count)
            for (i = 0; i < udp_count; i++) {
                FD_SET(udp_sockets[i].socket, &input_set);
#ifdef IPV6
                FD_SET(udp_sockets[i].socket6, &input_set);
#endif
	}
#endif



        tmptq = next_muckevent_time();
        if ((tmptq >= 0L) && (timeout.tv_sec > tmptq)) {
            timeout.tv_sec = (long)tmptq + (event_needs_delay ? (tp_pause_min / 1000) : 0);
            timeout.tv_usec = (event_needs_delay ? (tp_pause_min % 1000) * 1000L : 0);
        }
        event_needs_delay = 0;

        gettimeofday(&sel_in, NULL);
        if (select(maxd, &input_set, &output_set, (fd_set *) 0, &timeout) < 0) {
            if (errnosocket != EINTR) { /* select() returned crit error */
                perror("select");
                return;
            }
        } else {                /* select returned >= 0 */
            time(&current_systime);
            gettimeofday(&sel_out, NULL);
            if (sel_out.tv_usec < sel_in.tv_usec) {
                sel_out.tv_usec += 1000000;
                sel_out.tv_sec -= 1;
            }
            sel_out.tv_usec -= sel_in.tv_usec;
            sel_out.tv_sec -= sel_in.tv_sec;
            sel_prof_idle_sec += sel_out.tv_sec;
            sel_prof_idle_usec += sel_out.tv_usec;
            if (sel_prof_idle_usec >= 1000000) {
                sel_prof_idle_usec -= 1000000;
                sel_prof_idle_sec += 1;
            }
            sel_prof_idle_use++;
            now = current_systime;

            for (i = 0; i < numsocks; i++) { /* check for new connects */
                if (FD_ISSET(sock[i], &input_set)) { /* new connect */
#ifdef IPV6
		if (is_ipv6[i]) {
		    newd = new_connection6(listener_port[i], sock[i]);
		    if (newd) newd->flags += DF_IPV6;
		} else
#endif
		    newd = new_connection(listener_port[i], sock[i]);
                    if (!newd) { /* connection error */
                        if (errnosocket
#if !defined(WIN32) && !defined(WIN_VC)
                            && errno != EINTR
                            && errno != EMFILE && errno != ENFILE
#endif
                            ) {
#if defined(WIN32) || defined(WIN_VC)
                            log_status("new_connection: error #%d\n",
                                       errnosocket);
#else
                            log_status("new_connection: %s\n",
                                       strerror(errnosocket));
#endif
                        }
                    }
                }               /* if FD_ISET... */
            }                   /* for i < numsocks... */

#ifdef SPAWN_HOST_RESOLVER
            if (resolverpid && FD_ISSET(resolver_sock[1], &input_set))
                resolve_hostnames();
#endif

#ifdef USE_RESLVD
            if (reslvd_connected && FD_ISSET(reslvd_sock, &input_set))
                reslvd_input();
#endif

#ifdef USE_SSL
            if (FD_ISSET(ssl_sock, &input_set)) {
                if (!(newd = new_connection(ssl_listener_port, ssl_sock))) {
#ifndef WIN32
                    if (errno
                        && errno != EINTR
                        && errno != EMFILE && errno != ENFILE) {
                        perror("new_connection");
                        /* return; */
                    }
#else
                    if (WSAGetLastError() != WSAEINTR
                        && WSAGetLastError() != EMFILE) {
                        perror("new_connection");
                        /* return; */
                    }
#endif
                } else {
                    newd->ssl_session = SSL_new(ssl_ctx);
                    SSL_set_fd(newd->ssl_session, newd->descriptor);
                    cnt = SSL_accept(newd->ssl_session);
                    newd->type = CT_SSL;
                    newd->flags += DF_SSL;
                }
            }
#ifdef IPV6
            if (FD_ISSET(ssl_sock6, &input_set)) {
                if (!(newd = new_connection6(ssl_listener_port, ssl_sock6))) {
#ifndef WIN32
                    if (errno
                        && errno != EINTR
                        && errno != EMFILE && errno != ENFILE) {
                        perror("new_connection");
                        /* return; */
                    }
#else
                    if (WSAGetLastError() != WSAEINTR
                        && WSAGetLastError() != EMFILE) {
                        perror("new_connection");
                        /* return; */
                    }
#endif
                } else {
                    newd->ssl_session = SSL_new(ssl_ctx);
                    SSL_set_fd(newd->ssl_session, newd->descriptor);
                    cnt = SSL_accept(newd->ssl_session);
                    newd->type = CT_SSL;
                    newd->flags += DF_SSL;
		    newd->flags += DF_IPV6;
                }
            }

#endif
#endif

#ifdef MUF_SOCKETS
            for (curr = socket_list; curr; curr = curr->next) {
                if (FD_ISSET(curr->theSock->socknum, &input_set)
                    || (!curr->theSock->connected
                        && FD_ISSET(curr->theSock->socknum, &output_set))) {
#if defined(SSL_SOCKETS) && defined(USE_SSL)
                    /* Don't fire an event during SSL (re)negotiation. */
                    if (curr->theSock->ssl_session) {
                        if (SSL_is_init_finished(curr->theSock->ssl_session)
                            && SSL_want_nothing(curr->theSock->ssl_session)) {
                            muf_socket_sendevent(curr);
                        }
                    }
                    else
#endif
                        muf_socket_sendevent(curr);
                }
#if defined(SSL_SOCKETS) && defined(USE_SSL)
                else
                    /* This feeds in buffered SSL data, without requiring socket
                     * activity to trigger the event. */
                    if (curr->theSock->ssl_session
                        && SSL_pending(curr->theSock->ssl_session) > 0
                        && SSL_want_nothing(curr->theSock->ssl_session)) {
                        muf_socket_sendevent(curr);
                    }
#endif
            }
#endif


#ifdef UDP_SOCKETS
            /* Alynna:
               Process all packets on UDP ports, and make events from anything
               that actually came in.
             */
            /* And which ones have input? */
            for (i = 0; i < udp_count; i++) {
                if (FD_ISSET(udp_sockets[i].socket, &input_set)) {
                    struct inst tmpi;
                    stk_array *nw = new_array_dictionary();
                    struct sockaddr_in tmpaddr;
                    socklen_t tmpsz = sizeof(tmpaddr);

                    /* Get the data waiting */
                    memset(buf, 0, sizeof(buf));
                    recvfrom(udp_sockets[i].socket, buf, sizeof(buf), 0,
                                 (struct sockaddr *) &tmpaddr, &tmpsz);
                    /* And dispatch the UDP event */
                    strncpy(buf2, inet_ntoa(tmpaddr.sin_addr), 16);
                    if (tp_log_sockets)
                        log2filetime("logs/sockets",
                                     "UDP.event: pid %d, from %s, port %d\n'",
                                     udp_sockets[i].fr->pid, buf2,
                                     udp_sockets[i].portnum);
                    array_set_strkey_intval(&nw, "pid", udp_sockets[i].fr->pid);
                    array_set_strkey_strval(&nw, "from", buf2);
                    array_set_strkey_intval(&nw, "port",
                                            udp_sockets[i].portnum);
                    array_set_strkey_strval(&nw, "data", buf);
#ifdef IPV6
                    array_set_strkey_intval(&nw, "ipv6", 0);
#endif
                    tmpi.type = PROG_ARRAY;
                    tmpi.data.array = nw;
                    sprintf(buf2, "UDP.%d", udp_sockets[i].portnum);
                    muf_event_add(udp_sockets[i].fr, buf2, &tmpi, 0);
                    CLEAR(&tmpi);
                }
            }
#ifdef IPV6
            for (i = 0; i < udp_count; i++) {
                if (FD_ISSET(udp_sockets[i].socket6, &input_set)) {
		    log_status("UDP6 on %d", udp_sockets[i].socket6);
                    struct inst tmpi6;
                    stk_array *nw6 = new_array_dictionary();
                    struct sockaddr_in6 tmpaddr6;
                    socklen_t tmpsz6 = sizeof(tmpaddr6);

                    /* Get the data waiting */
                    memset(buf, 0, sizeof(buf));
                    recvfrom(udp_sockets[i].socket6, buf, sizeof(buf), 0,
                                 (struct sockaddr *) &tmpaddr6, &tmpsz6);
                    /* And dispatch the UDP event */
                    strncpy(buf2, ip_address(tmpaddr6.sin6_addr.s6_addr), 64);
                    if (tp_log_sockets)
                        log2filetime("logs/sockets",
                                     "UDP6.event: pid %d, from %s, port %d\n'",
                                     udp_sockets[i].fr->pid, buf2,
                                     udp_sockets[i].portnum);
                    array_set_strkey_intval(&nw6, "pid", udp_sockets[i].fr->pid);
                    array_set_strkey_strval(&nw6, "from", buf2);
                    array_set_strkey_intval(&nw6, "port", udp_sockets[i].portnum);
                    array_set_strkey_strval(&nw6, "data", buf);
                    array_set_strkey_intval(&nw6, "ipv6", 1);
                    tmpi6.type = PROG_ARRAY;
                    tmpi6.data.array = nw6;
                    sprintf(buf2, "UDP.%d", udp_sockets[i].portnum);
                    muf_event_add(udp_sockets[i].fr, buf2, &tmpi6, 0);
                    CLEAR(&tmpi6);
                }
            }

#endif
#endif

            /* This is the loop that handles input */
            for (cnt = 0, d = descriptor_list; d; d = dnext) {
                dnext = d->next;
                if (FD_ISSET(d->descriptor, &input_set)) {
                    if (!process_input(d)) { /* handle the input */
                        d->booted = BOOT_DROP; /* read error */
                    } else {    /* There was input, manage idle stuff */
                        if (OkObj(d->player) ?
                            Typeof(d->player) == TYPE_PLAYER : 0) {
                            if (FLAG2(d->player) & F2TRUEIDLE)
                                announce_unidle(d); /* really idle */
                            if (FLAG2(d->player) & F2IDLE)
                                FLAG2(d->player) &= ~F2IDLE; /*remove idle */
                            DBFETCH(d->player)->sp.player.last_descr = d->descriptor; /* least idle */
                        } else { /* not a player */
                            if (DR_RAW_FLAGS(d, DF_TRUEIDLE))
                                announce_unidle(d);
                        }
                        if (DR_RAW_FLAGS(d, DF_TRUEIDLE))
                            DR_RAW_REM_FLAGS(d, DF_TRUEIDLE);
                        if (DR_RAW_FLAGS(d, DF_IDLE))
                            DR_RAW_REM_FLAGS(d, DF_IDLE);
                    }           /* else */
                }
#ifdef NEWHTTPD
                else if (d->flags & DF_HALFCLOSE) {
                    d->booted = BOOT_DROP;
                }
#endif /* NEWHTTPD */
                if (FD_ISSET(d->descriptor, &output_set)) {
                    if (!process_output(d)) /* send text */
                        d->booted = BOOT_DROP; /* connection lost */
#ifdef MCCP_ENABLED
                    else if (!mccp_process_compressed(d))
                        d->booted = BOOT_DROP;
#endif
                }
                if (d->connected && OkObj(d->player)) { /* begin the idle FLAG/boots management */
                    int leastIdle = 0;
                    time_t dr_idletime = 0;
                    struct descriptor_data *tempd;

                    leastIdle = least_idle_player_descr(d->player);
                    tempd = get_descr(pdescr(leastIdle), NOTHING);
                    dr_idletime = now - tempd->last_time;
                    cnt++;
                    /* Check idle boot */
                    if (tp_idleboot && (dr_idletime > tp_maxidle) &&
                        !TMage(d->player) && !(POWERS(d->player) & POW_IDLE)) {
                        idleboot_user(d);
                    }
                    if (FLAG2(d->player) & F2IDLE) { /* update DF_IDLE flag */
                        if (!(DR_RAW_FLAGS(d, DF_IDLE)))
                            DR_RAW_ADD_FLAGS(d, DF_IDLE);
                    }
                    if (DBFETCH(d->player)->sp.player.last_descr == d->descriptor) { /* check for idling */
                        int idx_idletime = get_property_value(d->player,
                                                              "/_Prefs/IdleTime")
                            * 60;

                        if (idx_idletime < 120)
                            idx_idletime = (int)tp_idletime;
                        if (d->idletime_set != idx_idletime)
                            pset_idletime(d->player, idx_idletime);
                        if ((dr_idletime >= tp_idletime)
                            && !(FLAG2(d->player) & F2TRUEIDLE)) {
                            announce_idle(d); /* _idle/ propqueues */
                        }
                    } else {    /* update DF_TRUEIDLE flag */
                        if (FLAG2(d->player) & F2TRUEIDLE) {
                            if (!(DR_RAW_FLAGS(d, DF_TRUEIDLE)))
                                DR_RAW_ADD_FLAGS(d, DF_TRUEIDLE);
                        } else {
                            if ((DR_RAW_FLAGS(d, DF_TRUEIDLE)))
                                DR_RAW_REM_FLAGS(d, DF_TRUEIDLE);
                        }
                    }
                    if (!(FLAG2(d->player) & F2IDLE)) { /* check to set IDLE */
                        int idx_idletime = 0;

                        if (!idx_idletime)
                            idx_idletime = (int)tp_idletime;
                        if (dr_idletime >= d->idletime_set) {
                            FLAG2(d->player) |= F2IDLE;
                            if (!(DR_RAW_FLAGS(d, DF_IDLE)))
                                DR_RAW_ADD_FLAGS(d, DF_IDLE);
                        } else {
                            if ((DR_RAW_FLAGS(d, DF_IDLE)))
                                DR_RAW_REM_FLAGS(d, DF_IDLE);
                        }
                    } else {    /* check to set DF_IDLE */
                        if (!(DR_RAW_FLAGS(d, DF_IDLE)))
                            DR_RAW_ADD_FLAGS(d, DF_IDLE);
                    }
                } else {        /* !d->connected */
                    time_t dr_idletime = (now - d->last_time);

                    if ((dr_idletime >= tp_idletime)) {
                        if (!(DR_RAW_FLAGS(d, DF_IDLE))) /* descriptor idle */
                            DR_RAW_ADD_FLAGS(d, DF_IDLE);
                        announce_idle(d);
                    }
#ifdef NEWHTTPD
                    /* 20 second idle window for HTTP sessions; behaviors
                     * modeled after what Apache httpd does. HTTP sessions with
                     * a DF_KEEPALIVE flag are immune to booting, which is useful
                     * if the client is waiting on us. (i.e. long polls)
                     *                                           -davin */
                    if (d->type == CT_HTTP && dr_idletime >= 20 &&
                            !(d->flags & DF_KEEPALIVE) && !(d->booted)) {
                        d->http->close = 1;
                        if (d->http->method) {
                            // wave goodbye if they were behaving so far...
                            http_senderror(d, 408, "Server timeout waiting for the HTTP request from the client.");
                        } else {
                            // no method, no love -- punt!
                            d->booted = BOOT_DROP;
                        }
                    }
#endif /* NEWHTTPD */

                    /* check connidle times for normal and pueblo login */
                    if (!(
#ifdef NEWHTTPD
                             d->type == CT_HTTP ||
#endif /* NEWHTTPD */
                             d->type == CT_MUF)) {
                        int curidle;
                        char buff[BUFFER_LEN];

                        sprintf(buff, "@Ports/%d/Idle", d->cport);
                        if (get_property((dbref) 0, buff)) {
                            curidle = get_property_value((dbref) 0, buff);
                        } else {
                            curidle = (int)tp_connidle;
                        }
                        if (curidle < 30)
                            curidle = 9999999;
                        if ((now - d->connected_at) > curidle &&
                            !(descr_running_queue(d->descriptor)) &&
                            d->type != CT_INBOUND) {
                            d->booted = BOOT_DROP; /* don't drop if running program */
                        }
                    } 
                }               /* if d->connected... */
            }                   /* for input processing */
            if (cnt > con_players_max) {
                add_property((dbref) 0, "~sys/max_connects", NULL, cnt);
                if (tp_allow_old_trigs) {
                    add_property((dbref) 0, "_sys/max_connects", NULL, cnt);
                }
                con_players_max = cnt;
            }
            con_players_curr = cnt;
        }                       /* end of select returned > 0 */
    }                           /* end of main game loop */
    /* update dump props due to shutdown or restart */
    now = current_systime;
    add_property((dbref) 0, "~sys/lastdumptime", NULL, (int) now);
    add_property((dbref) 0, "~sys/shutdowntime", NULL, (int) now);
    if (tp_allow_old_trigs) {
        add_property((dbref) 0, "_sys/lastdumptime", NULL, (int) now);
        add_property((dbref) 0, "_sys/shutdowntime", NULL, (int) now);
    }
}

#if defined(DESCRFILE_SUPPORT) || defined(NEWHTTPD)
void
descr_sendfileblock(struct descriptor_data *d)
{
    char buf[MAX_COMMAND_LEN + 2];
    int x, r;

    if (!d->dfile)
        return;

    if (d->dfile->size < 0) {
        fclose(d->dfile->fp);
        free((void *) d->dfile);
        return;
    }

    for (x = 0; x < MAX_COMMAND_LEN; x++) {
        if (d->dfile->sent + x >= d->dfile->size)
            break;
        if ((r = fgetc(d->dfile->fp)) == EOF)
            break;
        buf[x] = (char) r;
    }

    if (x) {
        d->dfile->sent += x;
        add_to_queue(&d->output, buf, x, -2);
        d->output_size += x;
    }

    if (d->dfile->sent >= d->dfile->size) {
        fclose(d->dfile->fp);
        if (d->dfile->pid == -1) {
#ifdef NEWHTTPD
            httpfcount--;       /* This is in newhttp.c/newhttp.h */
#endif
            d->booted = BOOT_SAFE;
        } else if (d->dfile->pid > 0 && in_timequeue(d->dfile->pid)) {
            struct inst temp1;
            struct frame *destfr = timequeue_pid_frame(d->dfile->pid);

            if (destfr) {
                temp1.type = PROG_INTEGER;
                temp1.data.number = d->dfile->sent;
                muf_event_add(destfr, "FILE.COMPLETE", &temp1, 0);
                CLEAR(&temp1);
            }
        }
        free((void *) d->dfile);
        d->dfile = NULL;
    }
}

long
descr_sendfile(struct descriptor_data *d, int start, int stop,
               const char *filename, int pid)
{
    FILE *fp;
    long i, x;

    if (d->dfile) {
        return -2;
    } else if ((fp = fopen(filename, "r"))) {
        fseek(fp, 0, SEEK_END);
        if ((x = start) < 0)
            x = 0;
        i = (int) ftell(fp);
        if (stop >= 0 && stop < i)
            i = stop;
        if ((i -= x) < 0)
            i = 0;

        d->dfile = (struct dfile_struct *) malloc(sizeof(struct dfile_struct));
        d->dfile->size = i;
        d->dfile->pid = pid;
        d->dfile->sent = 0;
        d->dfile->fp = fp;
        fseek(fp, x, SEEK_SET);
        return i;
    } else {
        return -1;
    }
}

/* descr_fsenddisc():                                                 */
/*  This is called whenever a descr with a d->dfile is disconnected.  */
/*   It does cleanup of the d->dfile struct and closes any files that */
/*   might still be open.                                             */
void
descr_fsenddisc(struct descriptor_data *d)
{
    if (!d->dfile)
        return;

    if (d->dfile->pid > -1 && in_timequeue(d->dfile->pid)) {
        struct inst temp1;
        struct frame *destfr = timequeue_pid_frame(d->dfile->pid);

        if (destfr) {
            temp1.type = PROG_INTEGER;
            temp1.data.number = d->dfile->sent;
            muf_event_add(destfr, "FILE.INTERRUPT", &temp1, 0);
            CLEAR(&temp1);
        }
#ifdef NEWHTTPD
    } else if (d->dfile->pid == -1)
        httpfcount--;           /* This is in newhttp.c/newhttp.h */
#else
	}
#endif

    fclose(d->dfile->fp);
    free((void *) d->dfile);
}

#endif /* DESCRFILE_SUPPORT */


void
wall_and_flush(const char *msg)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN + 2];
    char abuf[BUFFER_LEN + 2];

    if (!msg || !*msg)
        return;
    strcpy(buf, msg);
    strcat(buf, "\r\n");

    for (d = descriptor_list; d; d = d->next) {
        if (d->player != NOTHING) {
            parse_ansi(d->player, abuf, buf, ANSINORMAL);
            queue_ansi(d, abuf);
        } else {
            queue_ansi(d, buf);
        }
        if (!process_output(d)) {
            d->booted = BOOT_DROP;
        }
    }
}

void
wall_logwizards(const char *msg)
{
    struct descriptor_data *d, *dnext;
    char buf[BUFFER_LEN + 2];
    int pos = 0;

    sprintf(buf, "LOG> %s", msg);

    while (buf[pos]) {
        if ((buf[pos] == '\n') || !isprint(buf[pos])) {
            buf[pos] = '\0';
            break;
        }
        pos++;
    }
    strcat(buf, "\r\n");

    for (d = descriptor_list; d; d = dnext) {
        dnext = d->next;
        if (d->connected && (FLAG2(d->player) & F2LOGWALL)) {
            queue_unhtml(d, buf);
            if (!process_output(d))
                d->booted = BOOT_DROP;
        }
    }
}

void
wall_arches(const char *msg)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN + 2];

    strcpy(buf, msg);
    strcat(buf, "\r\n");

    for (d = descriptor_list; d; d = d->next) {
        if (d->connected && /* (d->player >= 0) && */ TArch(d->player)) {
            queue_ansi(d, buf);
            if (!process_output(d)) {
                d->booted = BOOT_DROP;
            }
        }
    }
}


void
wall_wizards(const char *msg)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN + 2];

    strcpy(buf, msg);
    strcat(buf, "\r\n");

    for (d = descriptor_list; d; d = d->next) {
        if (d->connected && /* (d->player >= 0) && */ TMage(d->player)) {
            queue_ansi(d, buf);
            if (!process_output(d)) {
                d->booted = BOOT_DROP;
            }
        }
    }
}


void
ansi_wall_wizards(const char *msg)
{
    struct descriptor_data *d, *dnext;
    char buf[BUFFER_LEN + 2], buf2[BUFFER_LEN + 2];

    strcpy(buf, msg);
    strcat(buf, "\r\n");

    for (d = descriptor_list; d; d = dnext) {
        dnext = d->next;
        if (d->connected && Mage(d->player)) {
            if (FLAGS(d->player) & CHOWN_OK) {
                parse_ansi(d->player, buf2, buf, ANSINORMAL);
            } else {
                unparse_ansi(buf2, buf);
            }
            queue_ansi(d, buf2);
            if (!process_output(d))
                d->booted = BOOT_DROP;
        }
    }
}


void
show_wizards(dbref player)
{
    struct descriptor_data *d;

    anotify(player, CINFO "WizChatters:");
    for (d = descriptor_list; d; d = d->next)
        if (d->connected && /* (d->player >= 0) && */ Mage(d->player))
            notify(player, NAME(d->player));
}



void
flush_user_output(dbref player)
{
    int di;
    int *darr;
    int dcount;
    struct descriptor_data *d;

    darr = get_player_descrs(OWNER(player), &dcount);
    for (di = 0; di < dcount; di++) {
        d = descrdata_by_index(darr[di]);
        if (d && !process_output(d)) {
            d->booted = BOOT_DROP;
        }
    }
}


void
wall_all(const char *msg)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN + 2];

    strcpy(buf, msg);
    strcat(buf, "\r\n");

    for (d = descriptor_list; d; d = d->next) {
        queue_unhtml(d, buf);
        if (!process_output(d))
            d->booted = BOOT_DROP;
    }
}

const char *
host_as_hex(                    /* CrT */
               unsigned addr)
{
    static char buf[32];

    sprintf(buf,
            "%d.%d.%d.%d",
            (addr >> 24) & 0xff,
            (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);

    return buf;
}

#ifdef IPV6
struct in6_addr 
str2ip6(const char *ipstr)
{
    struct in6_addr x;
    inet_pton(AF_INET6, ipstr, &x);
    return x; 
}
#endif
int
str2ip(const char *ipstr)
{
    int ip1, ip2, ip3, ip4;

    if (sscanf(ipstr, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) != 4)
        return -1;

    if (ip1 < 0 || ip2 < 0 || ip3 < 0 || ip4 < 0)
        return -1;
    if (ip1 > 255 || ip2 > 255 || ip3 > 255 || ip4 > 255)
        return -1;

    return ((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | ip4);
    /* return( htonl((ip1 << 24) | (ip2 << 16) | (ip3 << 8) | ip4) ); */
}
int
get_ctype(int port)
{
    char buf[BUFFER_LEN];
    short ctype = 0;

    sprintf(buf, "@Ports/%d/Type", port);
    if (get_property((dbref) 0, buf)) {
        ctype = get_property_value((dbref) 0, buf);
        switch (ctype) {
            case 0:
                ctype = CT_MUCK;
                break;
#ifdef NEWHTTPD
            case 1:
                ctype = CT_HTTP;
                break;
#endif /* NEWHTTPD */
            case 2:
                ctype = CT_MUCK;
                break;
            case 3:
                ctype = CT_MUF;
                break;
#ifdef USE_SSL
            case 4:
                ctype = CT_SSL;
                break;
#endif
            default:
                ctype = CT_MUCK;
                break;
        }
    } else {
        if (port == tp_puebloport) {
            ctype = CT_PUEBLO;
#ifdef NEWHTTPD
        } else if (port == tp_wwwport) { /* hinoserm */
            ctype = CT_HTTP;    /* hinoserm */
#endif /* NEWHTTPD */
#ifdef USE_SSL
        } else if (port == tp_sslport) { /* alynna */
            ctype = CT_SSL;
#endif /* USE_SSL */
        } else
            ctype = CT_MUCK;
    }

    return ctype;
}

#ifdef IPV6
struct descriptor_data *
new_connection6(int port, int sock)
{
    int newsock;
    struct sockaddr_in6 addr;
    int addr_len;
    int ctype;
    int result;
    struct huinfo *hu;
    
    addr_len = sizeof(addr);
    newsock = accept(sock, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
    ctype = get_ctype(port);
    if (newsock < 0) {
        return 0;
    } else {
        hu = host_getinfo6(addr.sin6_addr, port, addr.sin6_port);
	/* log_status("Resolved to: %s\n",hu->h->name); */
	/* Alynna: Fix this soon!
        if (reg_site_is_blocked(ntohl(addr.sin6_addr.s6_addr)) == TRUE) {
            log_status("*BLK6: %2d %s %s C#%d P#%d\n", newsock,
                       hu->h->name, ntohs(addr.sin6_port),
                       ++crt_connect_count, port);
            shutdown(newsock, 2);
            closesocket(newsock);
            return 0;
        }
	*/
#ifdef NEWHTTPD
        if (ctype != CT_HTTP) { /* hinoserm */
#endif /* NEWHTTPD */
            show_status("ACT6: %2d %s %d C#%d P#%d %s\n", newsock,
                        hu->h->name, addr.sin6_port,
                        ++crt_connect_count, port,
                        ctype == CT_MUCK ? "TEXT" :
                        (ctype == CT_PUEBLO ? "PUEBLO" :
                         (ctype == CT_MUF ? "MUF" :
#ifdef USE_SSL
                          (ctype == CT_SSL ? "SSL" :
#endif /* USE_SSL */
                           ("UNKNOWN")
#ifdef USE_SSL
                          )
#endif /* USE_SSL */
                         ))
                );
            if (tp_log_connects)
                log2filetime(CONNECT_LOG, "ACT6: %2d %s %d C#%d P#%d %s\n",
                             newsock, hu->h->name, addr.sin6_port,
                             ++crt_connect_count, port,
                             ctype == CT_MUCK ? "TEXT" :
                             (ctype == CT_PUEBLO ? "PUEBLO" :
                              (ctype == CT_MUF ? "MUF" :
#ifdef USE_SSL
                               (ctype == CT_SSL ? "SSL" :
#endif /* USE_SSL */
                                ("UNKNOWN")
#ifdef USE_SSL
                               )
#endif /* USE_SSL */
                              ))
                    );
#ifdef NEWHTTPD
        }
#endif /* NEWHTTPD */
        result = get_property_value((dbref) 0, "~sys/concount");
        result++;
        add_property((dbref) 0, "~sys/concount", NULL, result);
        if (tp_allow_old_trigs)
            add_property((dbref) 0, "_sys/concount", NULL, result);
        check_maxd(newsock);
        return initializesock(newsock, hu, ctype, port, 1);
    }
}
#endif

struct descriptor_data *
new_connection(int port, int sock)
{
    int newsock;
    struct sockaddr_in addr;
    int addr_len;
    int ctype;
    int result;
    struct huinfo *hu;
    
    addr_len = sizeof(addr);
    newsock = accept(sock, (struct sockaddr *) &addr, (socklen_t *) &addr_len);
    ctype = get_ctype(port);
    if (newsock < 0) {
        return 0;
    } else {
        hu = host_getinfo(addr.sin_addr.s_addr, port, addr.sin_port);

        if (reg_site_is_blocked(ntohl(addr.sin_addr.s_addr)) == TRUE) {
            log_status("*BLK: %2d %s(%d) %s C#%d P#%d\n", newsock,
                       hu->h->name, ntohs(addr.sin_port),
                       host_as_hex(ntohl(addr.sin_addr.s_addr)),
                       ++crt_connect_count, port);
            shutdown(newsock, 2);
            closesocket(newsock);
            return 0;
        }
#ifdef NEWHTTPD
        if (ctype != CT_HTTP) { /* hinoserm */
#endif /* NEWHTTPD */
            show_status("ACPT: %2d %s(%d) %s C#%d P#%d %s\n", newsock,
                        hu->h->name, ntohs(addr.sin_port),
                        host_as_hex(ntohl(addr.sin_addr.s_addr)),
                        ++crt_connect_count, port,
                        ctype == CT_MUCK ? "TEXT" :
                        (ctype == CT_PUEBLO ? "PUEBLO" :
                         (ctype == CT_MUF ? "MUF" :
#ifdef USE_SSL
                          (ctype == CT_SSL ? "SSL" :
#endif /* USE_SSL */
                           ("UNKNOWN")
#ifdef USE_SSL
                          )
#endif /* USE_SSL */
                         ))
                );
            if (tp_log_connects)
                log2filetime(CONNECT_LOG, "ACPT: %2d %s(%d) %s C#%d P#%d %s\n",
                             newsock, hu->h->name, ntohs(addr.sin_port),
                             host_as_hex(ntohl(addr.sin_addr.s_addr)),
                             ++crt_connect_count, port,
                             ctype == CT_MUCK ? "TEXT" :
                             (ctype == CT_PUEBLO ? "PUEBLO" :
                              (ctype == CT_MUF ? "MUF" :
#ifdef USE_SSL
                               (ctype == CT_SSL ? "SSL" :
#endif /* USE_SSL */
                                ("UNKNOWN")
#ifdef USE_SSL
                               )
#endif /* USE_SSL */
                              ))
                    );
#ifdef NEWHTTPD
        }
#endif /* NEWHTTPD */
        result = get_property_value((dbref) 0, "~sys/concount");
        result++;
        add_property((dbref) 0, "~sys/concount", NULL, result);
        if (tp_allow_old_trigs)
            add_property((dbref) 0, "_sys/concount", NULL, result);
        check_maxd(newsock);
        return initializesock(newsock, hu, ctype, port, 1);
    }
}

void
clearstrings(struct descriptor_data *d)
{
    if (d->output_prefix) {
        FREE(d->output_prefix);
        d->output_prefix = 0;
    }
    if (d->output_suffix) {
        FREE(d->output_suffix);
        d->output_suffix = 0;
    }
}

void
shutdownsock(struct descriptor_data *d)
{
#ifdef NEWHTTPD
    if (d->type != CT_HTTP) {   /* hinoserm */
#endif /* NEWHTTPD */
        if (d->connected) {
            if (tp_log_connects)
                log2filetime(CONNECT_LOG,
                             "DISC: %2d %s %s(%s) %s, %d cmds P#%d\n",
                             d->descriptor, unparse_object(d->player,
                                                           d->player),
                             d->hu->h->name, d->hu->u->user,
                             host_as_hex(d->hu->h->a), d->commands, d->cport);
            show_status("DISC: %2d %s %s(%s) %s, %d cmds P#%d\n", d->descriptor,
                        unparse_object(d->player, d->player), d->hu->h->name,
                        d->hu->u->user, host_as_hex(d->hu->h->a), d->commands,
                        d->cport);
            announce_disconnect(d);
        } else if (d->type != CT_INBOUND) {
            show_status("DISC: %2d %s(%s) %s, %d cmds P#%d (never connected)\n",
                        d->descriptor, d->hu->h->name, d->hu->u->user,
                        host_as_hex(d->hu->h->a), d->commands, d->cport);
            if (tp_log_connects)
                log2filetime(CONNECT_LOG,
                             "DISC: %2d %s(%s) %s, %d cmds P#%d (never connected)\n",
                             d->descriptor, d->hu->h->name, d->hu->u->user,
                             host_as_hex(d->hu->h->a), d->commands, d->cport);
        }
#ifdef NEWHTTPD
    }
#endif /* NEWHTTPD */

#ifdef MCCP_ENABLED
	mccp_end(d);
#endif /* MCCP_ENABLED */
    bytesIn += d->input_len;
    bytesOut += d->output_len;
    commandTotal += d->commands;

    if (d->connected && OkObj(d->player) && (Typeof(d->player) == TYPE_PLAYER)) {
        forget_player_descr(d->player, d->descriptor);
        d->connected = 0;
        d->player = NOTHING;
        update_desc_count_table();
    }
    clearstrings(d);

#ifdef USE_SSL
    if (d->ssl_session) {
        if (SSL_shutdown(d->ssl_session) < 1)
            SSL_shutdown(d->ssl_session);
        SSL_free(d->ssl_session);
    }
#endif /* USE_SSL */
    shutdown(d->descriptor, 2);
    closesocket(d->descriptor);
    forget_descriptor(d);
    freeqs(d);
    *d->prev = d->next;
    if (d->next)
        d->next->prev = d->prev;
#ifdef MCP_SUPPORT
    mcp_frame_clear(&d->mcpframe);
#endif
#ifdef NEWHTTPD
    if (d->http)
        http_deinitstruct(d);
#endif /* NEWHTTPD */
#if defined(DESCRFILE_SUPPORT) || defined(NEWHTTPD)
    if (d->dfile)
        descr_fsenddisc(d);
#endif /* DESCRFILE_SUPPORT */
    host_delete(d->hu);

	telopt_clean(&d->telopt);

    FREE(d);
    ndescriptors--;
}

#ifdef MCP_SUPPORT

void
SendText(McpFrame *mfr, const char *text)
{
    queue_string((struct descriptor_data *) mfr->descriptor, text);
}

void
FlushText(McpFrame *mfr)
{
    struct descriptor_data *d = (struct descriptor_data *) mfr->descriptor;

    if (d && !process_output(d)) {
        d->booted = BOOT_DROP;
    }
}


int
mcpframe_to_descr(McpFrame *ptr)
{
    return ((struct descriptor_data *) ptr->descriptor)->descriptor;
}

int
mcpframe_to_user(McpFrame *ptr)
{
    return ((struct descriptor_data *) ptr->descriptor)->player;
}

#endif

struct descriptor_data *
initializesock(int s, struct huinfo *hu, int ctype, int cport, int welcome)
{
    struct descriptor_data *d;
    char buf[BUFFER_LEN];
    short is_telnet = 0;

    ndescriptors++;
    MALLOC(d, struct descriptor_data, 1);

#ifdef MCCP_ENABLED
    d->mccp = NULL;
#endif /* MCCP_ENABLED */

    telopt_init(&d->telopt);

    d->descriptor = s;
    d->connected = 0;
    d->did_connect = 0;
    d->player = NOTHING;
    d->booted = 0;
    d->fails = 0;
#ifdef USE_SSL
    d->ssl_session = NULL;
#endif
    d->con_number = 0;
    d->connected_at = current_systime;
    make_nonblocking(s);
    d->output_len = 0;
    d->input_len = 0;
    d->output_prefix = 0;
    d->output_suffix = 0;
    d->output_size = 0;
    d->output.lines = 0;
    d->output.head = 0;
    d->output.tail = &d->output.head;
    d->input.lines = 0;
    d->input.head = 0;
    d->input.tail = &d->input.head;
    d->raw_input = 0;
    d->raw_input_at = 0;
    d->bsescape = 0;
#ifdef UTF8_SUPPORT
    d->raw_input_mblength = 0;
#endif
    d->inIAC = 0;
    d->truncate = 0;
    d->quota = tp_command_burst_size;
    d->commands = 0;
    d->last_time = d->connected_at;
    d->type = ctype;
    d->cport = cport;
#ifdef MCP_SUPPORT
    mcp_frame_init(&d->mcpframe, d);
#endif
    d->hu = hu;
    d->linelen = 0;
    d->flags = 0;
    d->prog = NULL;
    d->block = 0;
    d->interactive = 0;
    d->idletime_set = (int)tp_idletime;
#ifdef NEWHTTPD
    d->http = NULL;
#endif /* NEWHTTPD */
#if defined(DESCRFILE_SUPPORT) || defined(NEWHTTPD)
    d->dfile = NULL;
#endif /* DESCRFILE_SUPPORT */
    d->encoding = ENC_RAW;

    if (descriptor_list)
        descriptor_list->prev = &d->next;
    d->next = descriptor_list;
    d->prev = &descriptor_list;
    descriptor_list = d;
    if (remember_descriptor(d) < 0)
        d->booted = BOOT_DROP;          /* Drop the connection ASAP */

    if ((ctype == CT_MUCK ||
#ifdef USE_SSL
         ctype == CT_SSL ||
#endif
         ctype == CT_PUEBLO)) {
        is_telnet = 1;
    } else if (ctype == CT_MUF) {
        sprintf(buf, "@Ports/%d/Telopts", cport);
        get_property((dbref) 0, buf);
        is_telnet = get_property_value((dbref) 0, buf);
    }

    if (is_telnet) {
        if (tp_ascii_descrs) {
            d->encoding = ENC_ASCII;
        } else {
            d->encoding = ENC_LATIN1;
        }
        DR_RAW_ADD_FLAGS(d, DF_TELNET);
        telopt_advertise(d);
    }

#ifdef NEWHTTPD
    if (ctype == CT_HTTP) {
        d->encoding = ENC_ASCII;
        http_initstruct(d);     /* hinoserm */
    }
#endif /* NEWHTTPD */

    if (welcome
#ifdef NEWHTTPD
        && ctype != CT_HTTP
#endif /* NEWHTTPD */
        ) {
        if (tp_delay_welcome && is_telnet) {
            // delay the welcome, handle it in shovechars
            DR_RAW_ADD_FLAGS(d, DF_WELCOMING);
            next_welcome = current_systime + 1;
            pending_welcomes++;
        } else {
            announce_login(d); 
            welcome_user(d);
        }
    }
    return d;
}

int
make_socket(int port)
{
    int s;
    struct sockaddr_in server;
    int opt;

    log_status("Opening port: %d\n", port); /* changed from fprintf */
    s = socket(AF_INET, SOCK_STREAM, 0);
    log_status("        Sock: %d\n", s); /* changed from fprintf */

    if (s < 0) {
        perror("creating stream socket");
        exit(3);
    }

    opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = bind_to;
    server.sin_port = (int) htons(port);
    if (bind(s, (struct sockaddr *) &server, sizeof(server))) {
        perror("binding stream socket");
        closesocket(s);
        exit(4);
    }

    check_maxd(s);
    listen(s, 5);
    return s;
}

#ifdef IPV6
int
make_socket6(int port)
{
    int s;
    struct sockaddr_in6 server;
    int opt;
    char ip[INET6_ADDRSTRLEN];

    inet_ntop(AF_INET6, bind6.s6_addr, ip, INET_ADDRSTRLEN);

    log_status("IPv6 address: %.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x\n", 
		bind6.s6_addr[0], bind6.s6_addr[1], bind6.s6_addr[2], bind6.s6_addr[3],
		bind6.s6_addr[4], bind6.s6_addr[5], bind6.s6_addr[6], bind6.s6_addr[7],
		bind6.s6_addr[8], bind6.s6_addr[9], bind6.s6_addr[10], bind6.s6_addr[11],
		bind6.s6_addr[12], bind6.s6_addr[13], bind6.s6_addr[14], bind6.s6_addr[15]);
    log_status("Opening port: %s %d(ipv6)\n", ip, port); /* changed from fprintf */
    s = socket(AF_INET6, SOCK_STREAM, 0);
    log_status("        Sock: %d\n", s); /* changed from fprintf */

    if (s < 0) {
        perror("creating stream socket");
        exit(3);
    }

    opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }
    if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    server.sin6_family = AF_INET6;
    server.sin6_addr = bind6;
    server.sin6_port = (int) htons(port);
    if (bind(s, (struct sockaddr *) &server, sizeof(server))) {
        perror("binding stream socket (ipv6)");
        closesocket(s);
        exit(4);
    }
    check_maxd(s);
    listen(s, 5);
    return s;
}
#endif

struct text_block *
make_text_block(const char *s, int len, int mblength)
{
    struct text_block *p;

    MALLOC(p, struct text_block, 1);
    MALLOC(p->buf, char, len);

    bcopy(s, p->buf, len);
    p->nchars = len;
#ifdef UTF8_SUPPORT
    p->nwchars = mblength;
#endif
    p->start = p->buf;
    p->nxt = 0;
    return p;
}

void
free_text_block(struct text_block *t)
{
    FREE(t->buf);
    FREE((char *) t);
}

void
add_to_queue(struct text_queue *q, const char *b, int len, int mblength)
{
    struct text_block *p;

    if (len == 0)
        return;

    p = make_text_block(b, len, mblength);
    p->nxt = 0;
    *q->tail = p;
    q->tail = &p->nxt;
    q->lines++;
}

int
flush_queue(struct text_queue *q, int n)
{
    struct text_block *p;
    int really_flushed = 0;

    n += strlen(flushed_message);

    while (n > 0 && (p = q->head)) {
        n -= p->nchars;
        really_flushed += p->nchars;
        q->head = p->nxt;
        q->lines--;
        free_text_block(p);
    }
    p = make_text_block(flushed_message, strlen(flushed_message), -2);
    p->nxt = q->head;
    q->head = p;
    q->lines++;
    if (!p->nxt)
        q->tail = &p->nxt;
    really_flushed -= p->nchars;
    return really_flushed;
}

int
queue_write(struct descriptor_data *d, const char *b, int n)
{
    int space;

    space = tp_max_output - d->output_size - n;
    if (space < 0)
        d->output_size -= flush_queue(&d->output, -space);
    add_to_queue(&d->output, b, n, -2);
    d->output_size += n;
    return n;
}

int
queue_string(struct descriptor_data *d, const char *s)
{
    int result, len = strlen(s);
    const char *sp;
    char *filtered, *fp;
    const char *send = s + len - 1;
    // char *fend;
#ifdef UTF8_SUPPORT
    char unsigned remap;
    int mblength;
    ucs4_t uchar;
    bool ucn_escaping = 0;
    bool mxp_open = 0;
    bool in_paragraph = 0;
#endif


    if (!*s || d->encoding == ENC_RAW) {
        result = queue_write(d, s, len);
        return result;
    } else {
#ifdef UTF8_SUPPORT
        if (d->encoding == ENC_UTF8) {
            /* Each invalid byte of s potentially maps to three UTF-8 bytes. */
            filtered = (char *) malloc( (len*3) + 1);
        } else {
            /* Each UCN escaped byte of s potentially maps to eight ASCII bytes. */
            filtered = (char *) malloc((len*8) + 1);
        }
#else
        filtered = (char *) malloc(len + 1);
#endif
        fp = filtered;
        //fend = filtered + len;
        
        for (sp = s; s < send && *sp != '\0'; sp++) {
            // MXP check
            if (DR_RAW_FLAGS(d, DF_MXP)) {
                switch (*sp) {
                    case '<':
                        memcpy(fp, "&lt;", 4);
                        fp += 4;
                        continue;
                    case '>':
                        memcpy(fp, "&gt;", 4);
                        fp += 4;
                        continue;
                    case '&':
                        memcpy(fp, "&amp;", 5);
                        fp += 5;
                        continue;
                    case '"':
                        memcpy(fp, "&quot;", 6);
                        fp += 6;
                        continue;
                }
            }
#ifdef UTF8_SUPPORT
            mblength = u8_mbtoucr(&uchar, (uint8_t *)sp, send - sp);

            if (mblength < 0) {
                /* Malformed character, probably truncated by a sprintf
                 * somewhere. Convert and keep going. */
                *fp++ = ASCII_REPLACEMENT_CHAR;
                continue;
            }

            if (DR_RAW_FLAGS(d, DF_MXP)) {
                switch (uchar) {
                    case 0x00A0: // NBSP -> &nbsp;
                        memcpy(fp, "&nbsp;", 6);
                        fp += 6;
                        continue;
                    case 0x2028: // Line Separator -> <br>
                        memcpy(fp, "<br>", 4);
                        fp += 4;
                        continue;
                    case 0x2029: // Paragraph Separator -> <p>
                        if (in_paragraph) {
                            in_paragraph = 0;
                            memcpy(fp, "</p>", 4);
                            fp += 4;
                        } else {
                            in_paragraph = 1;
                            memcpy(fp, "<p>", 3);
                            fp += 3;
                        }
                        continue;
                }
            } else if (mxp_open && uchar != NC_MXPCLOSE) {
                // Inside HTML tag, discard.
                continue;
            }

            /* Noncharacter handling. Either it's an escape we define inside of
             * the interface.h header, or we drop the sequence. */
            if (isnc(uchar)) {
                switch (uchar) {
                    case NC_UCNSTART:
                        ucn_escaping = 1;
                        break;
                    case NC_UCNSTOP:
                        ucn_escaping = 0;
                        break;
                    case NC_MXPLINE:
                        // Drop this line if we're not a MXP session.
                        if (!DR_RAW_FLAGS(d, DF_MXP)) {
                            free(filtered);
                            return 0;
                        }
                        break;
                    case NC_MXPOPEN:
                        mxp_open = 1;
                        if (DR_RAW_FLAGS(d, DF_MXP)) {
                            *fp++ = '<';
                        }
                        break;
                    case NC_MXPCLOSE:
                        mxp_open = 0;
                        if (DR_RAW_FLAGS(d, DF_MXP)) {
                            *fp++ = '>';
                        }
                        break;
                    case NC_MXPAMP:
                        *fp++ = '&';
                        break;
                    case NC_MXPQUOT:
                        *fp++ = '"';
                        break;
                }
                sp += mblength - 1;
                continue;
            }

            if (d->encoding == ENC_UTF8) {
                if (mblength == 1 || mblength == 0 ) {
                    *fp++ = *sp;
                    continue;
                } else if (mblength > 1) {
                    if (!isnc(uchar)) {
                        /* no check for overrun, we can hold the max size. */
                        memcpy(fp, sp, mblength);
                        fp += mblength;
                    }
                    /* the loop itself adds 1 */
                    sp += mblength - 1;
                    continue;
                } else if (mblength == -1) {
                    /* invalid byte, insert U+FFFD */
                    *fp++ = '\xef';
                    *fp++ = '\xbf';
                    *fp++ = '\xbd';
                    continue;
                } else {
                    assert(0 && "mbtowc returned <-1!");
                }
                // Everything past here is for single-byte encodings.
            } else if (mblength > 1 && ucn_escaping) {
                /* This is an escaped literal character sequence. Its meaning
                 * must be preserved in case it is pasted back into the game. */
                if (uchar > 0xFFFF) {
                    fp += sprintf(fp, "\\U%06lx", (unsigned long)uchar);
                } else {
                    fp += sprintf(fp, "\\u%04lx", (unsigned long)uchar);
                }
                sp += mblength - 1;
                continue;
            } else if (mblength > 1 && (remap = utf8_sbc_remap(d->encoding, uchar))) {
                // prefer our custom remappings
                if (remap == 255 && DR_RAW_FLAGS(d, DF_TELNET)) {
                    // Character 255 must be escaped as IAC+IAC.
                    *fp++ = TELOPT_IAC;
                }
                sp += mblength - 1;

                // post-remap MXP check
                if (DR_RAW_FLAGS(d, DF_MXP)) {
                    switch (remap) {
                        case '<':
                            memcpy(fp, "&lt;", 4);
                            fp += 4;
                            continue;
                        case '>':
                            memcpy(fp, "&gt;", 4);
                            fp += 4;
                            continue;
                        case '&':
                            memcpy(fp, "&amp;", 5);
                            fp += 5;
                            continue;
                        case '"':
                            memcpy(fp, "&quot;", 6);
                            fp += 6;
                            continue;
                    }
                }

                *fp++ = remap;
                continue;
            }

#endif

            // Must be ASCII at this point.
            if ((unsigned char)*sp < 0x80) {
                *fp++ = *sp;
                continue;
            } else {
                *fp++ = ASCII_REPLACEMENT_CHAR;
            }

            continue;
        }
    }

    *fp = '\0';

    result = queue_write(d, filtered, fp - filtered);

    free(filtered);

    return result;
}

int
process_output(struct descriptor_data *d)
{
    struct text_block **qp, *cur;
    int cnt;

    /* drastic, but this may give us crash test data */
    if (!d || !d->descriptor) {
        log_status("process_output: bad descriptor or connect struct!\n"); /* hinoserm */
        return 0;               /* hinoserm */
    }
#ifdef NEWHTTPD
    if (d->flags & DF_HALFCLOSE) /* hinoserm */
        return 1;               /* hinoserm */
#endif /* NEWHTTPD */

    if (d->output.lines == 0) {
        return 1;
    }

    for (qp = &d->output.head; (cur = *qp);) {
        cnt = sockwrite(d, cur->start, cur->nchars);

        if (cnt < 0) {
#ifdef DEBUGPROCESS
            fprintf(stderr, "process_output: write failed errno %d %s\n", errnosocket,
                    strerror(errnosocket));
#endif
            if (errnosocket == EWOULDBLOCK)
                return 1;
            return 0;
        }
        d->output_len += cnt;
        d->output_size -= cnt;
        if (cnt == cur->nchars) {
            d->output.lines--;
            if (!cur->nxt) {
                d->output.tail = qp;
                d->output.lines = 0;
            }
            *qp = cur->nxt;
            free_text_block(cur);
            continue;           /* do not adv ptr */
        }
        cur->nchars -= cnt;
        cur->start += cnt;
        break;
    }

    return 1;
}

void
make_blocking(int s)
{
#if !defined(O_NONBLOCK) || defined(ULTRIX) /* POSIX ME HARDER */
# ifdef FNDELAY                 /* SUN OS */
#  define O_NONBLOCK FNDELAY
# else
#  ifdef O_NDELAY               /* SyseVil */
#   define O_NONBLOCK O_NDELAY
#  endif /* O_NDELAY */
# endif /* FNDELAY */
#endif

#ifdef WIN_VC
    u_long turnon = 0;

    if (ioctl(s, FIONBIO, &turnon) != 0) {
#else
    if (fcntl(s, F_SETFL, 0) == -1) {
#endif
        perror("make_blocking: fcntl");
        panic("O_NONBLOCK fcntl failed");
    }
}

void
make_nonblocking(int s)
{
#if !defined(O_NONBLOCK) || defined(ULTRIX) /* POSIX ME HARDER */
# ifdef FNDELAY                 /* SUN OS */
#  define O_NONBLOCK FNDELAY
# else
#  ifdef O_NDELAY               /* SyseVil */
#   define O_NONBLOCK O_NDELAY
#  endif /* O_NDELAY */
# endif /* FNDELAY */
#endif

#ifdef WIN_VC
    u_long turnon = 1;

    if (ioctl(s, FIONBIO, &turnon) != 0) {
#else
    if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
#endif
        perror("make_nonblocking: fcntl");
        panic("O_NONBLOCK fcntl failed");
    }
}

void
freeqs(struct descriptor_data *d)
{
    struct text_block *cur, *next;

    cur = d->output.head;
    while (cur) {
        next = cur->nxt;
        free_text_block(cur);
        cur = next;
    }
    d->output.lines = 0;
    d->output.head = 0;
    d->output.tail = &d->output.head;

    cur = d->input.head;
    while (cur) {
        next = cur->nxt;
        free_text_block(cur);
        cur = next;
    }
    d->input.lines = 0;
    d->input.head = 0;
    d->input.tail = &d->input.head;

    if (d->raw_input)
        FREE(d->raw_input);
    d->raw_input = 0;
    d->raw_input_at = 0;
    d->bsescape = 0;
#ifdef UTF8_SUPPORT
    d->raw_input_mblength = 0;
#endif
}

/* Returns -1 if the @tune UNIDLE word is used. */
/* At the moment, the UNIDLE word won't get queued in the input 
 * queue, though this might change in the future.
 * Returns 1 if the input is anything other than the unidle word. */
int
save_command(struct descriptor_data *d, char *command, int len, int mblength)
{
#ifdef UTF8_SUPPORT
    static char iconvbuf[MAX_COMMAND_LEN];
    char *inbuf_p = command;
    char *outbuf_p = iconvbuf;
    size_t norm_chars = 0;
    int iconvreturn = 0;

    *iconvbuf = '\0';

    size_t inbytes = len;
    size_t outbytes = MAX_COMMAND_LEN - 1;

    // Changing *command to non-const for iconv is stupid. Blame the standard:
    // https://sourceware.org/bugzilla/show_bug.cgi?id=2962

    if (d->encoding == ENC_LATIN1 || d->encoding == ENC_IBM437) {
        if (d->encoding == ENC_LATIN1) {
            iconv_global = iconv_open("UTF-8", "ISO-8859-1");
        } else if (d->encoding == ENC_IBM437) {
            iconv_global = iconv_open("UTF-8", "IBM437");
        }
        iconvreturn = iconv(iconv_global, &inbuf_p, &inbytes, &outbuf_p, &outbytes);
        assert(iconvreturn >= 0 && "iconv returned error status.");
        iconv_close(iconv_global);
        iconvbuf[MAX_COMMAND_LEN - outbytes] = '\0';
    } else {
        if (len != mblength) {
            // multi-byte characters present, normalization pass required
            command = (char *)u8_normalize(UNINORM_NFC, (uint8_t *)command, (size_t)len + 1,
                    NULL, &norm_chars);
            len = norm_chars - 1;
            mblength = u8_mbsnlen((uint8_t *)command, norm_chars);
            command[len] = '\0';
        }

    }

#endif
/*
    if (d->connected &&
        !string_compare((char *) command, BREAK_COMMAND) &&
        ((MLevel(d->player) >= tp_min_progbreak_lev) || (Wiz(d->player)))) {
        if (dequeue_prog(d->player, 2))
            anotify(d->player, CINFO "Foreground program aborted.");
        DBFETCH(d->player)->sp.player.block = 0;
        if (d->player == NOTHING)
            d->block = 0;
        if (!(FLAGS(d->player) & INTERACTIVE))
            return 1;
    }
#ifdef MCP_SUPPORT
    if (d->connected && (DBFETCH(d->player)->sp.player.block)) {
        char cmdbuf[BUFFER_LEN];

        if (!mcp_frame_process_input(&d->mcpframe, command,
                                     cmdbuf, sizeof(cmdbuf))) {
            return 1;
        }
    }
#endif
*/
    if (tp_allow_unidle) {      /* check for unidle word */
        if (!string_compare((char *) command, tp_unidle_command)) {
            if (strlen(tp_unidle_command_msg) > 0)
                notify_descriptor(d->descriptor, tp_unidle_command_msg);
            return -1;
        }
    }

#ifdef UTF8_SUPPORT
    if (*iconvbuf) {
        add_to_queue(&d->input, (const char *)iconvbuf,
                MAX_COMMAND_LEN - outbytes + 1, iconvreturn + 1);
        return 1;
    }
#endif

    /* If mblength is < 0, the meaning is special. Don't add 1. */
    add_to_queue(&d->input, command, len + 1,
            mblength < 0 ? mblength : mblength + 1);
    //add_to_queue(&d->input, command, strlen(command) + 1);
    return 1;
}

/* Note that the char ** args are writable. This is so that we can rewind. */
int
process_backslash_escape(struct descriptor_data *d,
        char **qptr, char *qend,
        char **pptr, char *pend,
        int *mblength_in)
{
    char *q = *qptr; // current string from the input queue
    char *p = *pptr; // current location in the d->raw_input put buffer
    char ucnbuf[9];  // holds "nnnnnnnn" + '\0'
#ifdef UTF8_SUPPORT
    int utf8len = 0; // number of UTF-8 bytes represented by a UCN escape
    int ucnlen = 0;  // number of bytes stored in ucnbuf
    ucs4_t uchar;   // the character the UCN escape maps to
    int ucn_i;
#endif

    if (*q == 92) { // backslash
        if (d->bsescape < 2) {
            // first or second
            d->bsescape++;
        } else {
            // reset count
            d->bsescape = 0;
        }
        /* Consecutive backslashes beyond the second aren't worth noting. 
         * Just keep the value of 2 until something more interesting happens. */

        return 0;
    }
    if (!d->bsescape) {
        // Not inside of an escape, boring.
        return 0;
    } else if (d->bsescape == 2) {
        // Escaped escape, ignore it.
        d->bsescape = 9;
        return 0;
    } else {
        // Now we're getting somewhere. Do we want this letter?
        switch (*q) {
#ifdef UTF8_SUPPORT
            case 'u': // UCN escape \unnnn, basic multilingual plane (<=0xFFFF)
                d->bsescape = 0;
                ucnlen = 4;

                break;
            case 'U': // UCN escape \Unnnnnnnn, extended plane
                d->bsescape = 0;
                ucnlen = 8;

                break;
#endif
            default: // Nope. Reset state.
                d->bsescape = 0;

                return 0;
        }
#ifdef UTF8_SUPPORT
        if (ucnlen) {
            if (ucnlen && q + ucnlen < qend) {
                for (ucn_i=0; ucn_i != ucnlen; ucn_i++) {
                    if (isxdigit(*(q + 1 + ucn_i))) {
                        ucnbuf[ucn_i] = *(q + 1 + ucn_i);
                    } else {
                        // invalid, ignore it
                        return 0;
                    }
                }
            } else {
                // not enough space for this escape, ignore it
                return 0;
            }
            // Read a full character. Advance the input queue.
            *qptr = q + ucnlen;

            ucnbuf[ucn_i] = '\0';
            // ucnbuf now contains the digits, convert to long
            uchar = strtoul(ucnbuf, NULL, 16);

            // convert this integer to UTF-8 bytes, reuse ucnbuf
            utf8len = u8_uctomb((uint8_t *)ucnbuf, uchar, sizeof ucnbuf);

            if (uchar < 0x00A0) {
                anotify_descriptor(d->descriptor, CFAIL "Input: Escapes \\u and \\U must not be used to encode ASCII. (< 00A0 ignored)\n");

            }
            if (uchar >= 0xD800 && uchar <= 0xDFFF) { // surrogate pairs
                anotify_descriptor(d->descriptor, CFAIL "Input: Escape \\u must not encode a surrogate. Use \\U instead.\n");
            }
            

            /* Do we have room for utf8len worth of integers + \n? 
               We've already rewound over the backslash. */
            if ((p - 1 + utf8len) > pend) {
                // Nope.
                return 0;
            } else {
                // Yes!
                memcpy(p - 1, ucnbuf, utf8len);
                *pptr = p + utf8len - 1;
                return 1;
            }

        }
#endif
    }

    return 0;
}


int
process_input(struct descriptor_data *d)
{
    char buf[MAX_COMMAND_LEN * 2];
	char *p, *pend, *q, *qend;
	int got;
#ifdef UTF8_SUPPORT
    ucs4_t wctmp[1];   /* stores a single decoded wide character */
    int mblength;          /* size in bytes of current wide character */
#endif
    int wcbuflen = -2; /* this never changes if UTF8_SUPPORT is disabled */

    if (d->type == CT_INBOUND)
        return -1;
#ifdef USE_SSL
    if (d->ssl_session)
        got = SSL_read(d->ssl_session, buf, sizeof buf);
    else
#endif
        got = readsocket(d->descriptor, buf, sizeof buf);

#ifndef WIN32
# ifdef USE_SSL
    if ((got <= 0) && errno != EWOULDBLOCK)
# else
    if (got <= 0)
# endif
#else
# ifdef USE_SSL
    if ((got <= 0) && WSAGetLastError() != EWOULDBLOCK)
# else
    if (got <= 0)
# endif
#endif
    {
#ifdef DEBUGPROCESS
        fprintf(stderr, "process_input: read failed errno %d %s\n", errnosocket,
                strerror(errnosocket));
#endif
        return 0;
    }
    d->input_len += got;
    if (!d->raw_input) {
        MALLOC(d->raw_input, char, MAX_COMMAND_LEN);

        d->raw_input_at = d->raw_input;
#ifdef UTF8_SUPPORT
        if (d->encoding == ENC_RAW) {
            wcbuflen = -1; /* invalid UTF-8 */
        } else {
            wcbuflen = 0;
        }
    } else {
        wcbuflen = d->raw_input_mblength;
#endif
    }
    p = d->raw_input_at;
    pend = d->raw_input + MAX_COMMAND_LEN - 1;
    for (q = buf, qend = buf + got; q < qend; q++) {
#ifdef NEWHTTPD
        /* Begin messy webserver content handling junk! -hinoserm */
        if (d->type == CT_HTTP) {
            if (d->booted)
                break;          /* There's a reason for this. */
            /* Do content stuff. */
            if (d->http->body.elen) {
                if (http_processcontent(d, *q))
                    break;      /* Finished. */

                continue;       /* Keep going! */
            }
        }
        /* End messy webserver junk! -hinoserm */
#endif /* NEWHTTPD */
        if (*q == '\n') {
            *p = '\0';
            d->truncate = 0;
            if (p > d->raw_input) {
#ifdef NEWHTTPD
                if (d->type == CT_HTTP) { /* hinoserm */
/* There's a reason why I don't do this with  */ /* hinoserm */
/* the pre-existing queuing stuff. If I did,  */ /* hinoserm */
/* the content data stuff wouldn't work. This */ /* hinoserm */
/* might change if it becomes a problem.      */ /* hinoserm */
                    http_process_input(d, d->raw_input); /* hinoserm */
                } else
#endif /* NEWHTTPD */
                if (save_command(d, d->raw_input, p - d->raw_input, wcbuflen) != -1)
                    d->last_time = time(NULL);
            } else {
                /* BARE NEWLINE! */
#ifdef NEWHTTPD
                if (d->type == CT_HTTP) { /* hinoserm */
                    http_process_input(d, ""); /* hinoserm */
                } else
#endif /* NEWHTTPD */
                if (p == d->raw_input) {
                    /* Blank lines get sent on to be caught by MUF */
                    save_command(d, d->raw_input, 0, 0);
                }
            }
            p = d->raw_input;
        } else if (DR_RAW_FLAGS(d, DF_TELNET) && process_telnet_IAC(d, q, p, pend)) {
            /* This is where the telopt processing code used to be. It has been
             * moved to process_telnet_IAC to keep process_input cleaner.
             * It will return 0 if we should keep going.
             *
             * Any sort of processing for multi-character sequences (UTF-8,
             * custom escape sequences, etc.) need to happen after this point as
             * an IAC sequence must never be interrupted. */
        } else if (p < pend && !d->truncate) {
            if (process_backslash_escape(d, &q, qend, &p, pend, &wcbuflen)) {
                /* Similar to process_telnet_IAC(). Return true if the character
                 * should not be written to the buffer. This function will end
                 * up overwriting the initial backslash it allows into the
                 * buffer if a complete escape sequence is detected.
                 *
                 * q, p, len, and mblength may be overwritten by this function call. */
            } else if ((*q == '\t') && DR_RAW_FLAGS(d, DF_TELNET)) {
                *p++ = ' ';
#ifdef UTF8_SUPPORT
                wcbuflen++;
#endif
            } else if ((*q == 8 || *q == 127) && DR_RAW_FLAGS(d, DF_TELNET)) {
                /* if BS or DEL, delete last character */
                if (p > d->raw_input) {
                    p--;
#ifdef UTF8_SUPPORT
                    /* For UTF-8 encoded descriptors, we can't assume that
                     * backspacing a single byte is a legal operation. If the
                     * last byte of input is ASCII, it's safe. Otherwise we need
                     * to loop and eliminate all bytes we encounter in the
                     * "continuation" range of the 8th bit until we arrive
                     * at one that isn't. This byte will be the first byte of
                     * the UTF-8 character (length specifier), which will be
                     * indiscriminately overwritten since it was already
                     * validated when we accepted the input. */

                    if (!isascii(*p) && d->encoding == ENC_UTF8) {
                        do {
                            p--;
                        } while ( p > d->raw_input && (*p >= '\x80') && (*p <= '\xbf') );
                    }
                    wcbuflen--;
#endif
                }
            } else if (*q != 13) {
                if (d->encoding == ENC_ASCII) {
                    if (isascii(*q)) {
                        *p++ = *q;
#ifdef UTF8_SUPPORT
                        wcbuflen++; /* valid ASCII is valid UTF-8 */
#endif

                    }
#ifdef UTF8_SUPPORT
                } else if (d->encoding == ENC_UTF8) {
                    mblength = u8_mbtouc(wctmp, (uint8_t *)q, qend - q);

                    /* TODO: Consider filtering private use and block specifier
                     * (annotations, bidi) codes from user input... */

                    if (mblength > 0) {
                        /* check for overrun */
                        if (p + (mblength-1) < pend) {
                            /* copy the multi-byte character to the buffer */
                            memcpy(p, q, mblength);
                            p = p + mblength;
                            /* advance q by mblength-1 bytes. we subtract 1 because
                             * the loop itself already advances it by 1. */
                            q = q + (mblength - 1);
                            wcbuflen++;
                        } else {
                            /* d->raw_input can't hold the next UTF-8 character.
                             * Deleting characters during conversion when we
                             * have buffer space violates TR36 (3.5), so we need
                             * to replace it with at least one character. We'll
                             * set mblength to -1 and let the error handling code
                             * take over. */
                            mblength = -1;
                        }
                    }
                    if (mblength == -1) {
                        /* Invalid byte, insert U+FFFD (unknown) if we have the
                         * buffer space. */
                        if (p + 2 < pend) {
                            *p++ = '\xef';
                            *p++ = '\xbf';
                            *p++ = '\xbd';
                            wcbuflen++;
                        } else {
                            /* Not enough buffer space for U+FFFD, truncate. */
                            d->truncate = 1;
                        }
                    }
#endif
                } else { /* RAW */
                    /* any text from a raw source goes straight into the game,
                     * but is not considered for UTF-8 candidacy. */
                    *p++ = *q;

                }

            }
        }
    }
    if (p > d->raw_input) {
        d->raw_input_at = p;
#ifdef UTF8_SUPPORT
        d->raw_input_mblength = wcbuflen;
#endif
    } else {
        FREE(d->raw_input);
        d->raw_input = 0;
        d->raw_input_at = 0;
#ifdef UTF8_SUPPORT
        d->raw_input_mblength = 0;
#endif
    }
    return 1;
}

void
set_userstring(char **userstring, const char *command)
{
    if (*userstring) {
        FREE(*userstring);
        *userstring = 0;
    }

    while (*command && isascii(*command) && isspace(*command))
        command++;
    if (*command)
        *userstring = string_dup(command);
}

void
process_commands(void)
{
    int nprocessed;
    struct descriptor_data *d, *dnext;
    struct text_block *t;
    char buf[BUFFER_LEN];
    dbref mufprog;

    do {
        nprocessed = 0;
        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;

            if (DR_RAW_FLAGS(d, DF_WELCOMING)) {
                // skip command processing until welcome screen is rendered
                continue;
            }

            if (d->type == CT_MUF) {
                sprintf(buf, "@Ports/%d/MUF", d->cport);
                mufprog = get_property_dbref((dbref) 0, buf);
                if (OkObj(mufprog) && (!descr_running_queue(d->descriptor))
                    && (d->booted != BOOT_DEFERRED)) {
                    struct frame *tmpfr;

                    strcpy(match_args, "MUF");
                    strcpy(match_cmdname, "Queued Event.");
                    tmpfr = interp(d->descriptor, NOTHING, NOTHING, mufprog,
                                   (dbref) 0, FOREGROUND, STD_HARDUID, 0);
                    if (tmpfr) {
                        interp_loop(NOTHING, mufprog, tmpfr, 1);
                    }
                    // bypass the usual d->booted descriptor loop
                    d->booted = BOOT_DEFERRED;
                    continue;
                }
            }
            if (d->quota > 0 && (t = d->input.head)) {
                /* Added in the is_interface_command to seperate out checking
                 * for things like @q, WHO, QUIT, etc. -Akari */
                if ((d->connected && DBFETCH(d->player)->sp.player.block &&
                     !is_interface_command(t->start))
                    || (!d->connected && d->block)) {
                    char *tmp = t->start;

#ifdef MCP_SUPPORT
                    /* dequote MCP quoting. */
                    if (!strncmp(tmp, "#%\"", 3)) {
                        tmp += 3;
                    }
#endif

                    /* If read_event_notify returns 0, it didn't handle
                     * the input. If tmp is an empty string, then we'll
                     * remove the empty line here. -Akari
                     */
                    if (!read_event_notify(d->descriptor, d->player, tmp) &&
                        !*tmp) {
                        ++nprocessed;
                        d->input.head = t->nxt;
                        d->input.lines--;
                        if (!d->input.head) {
                            d->input.tail = &d->input.head;
                            d->input.lines = 0;
                        }
                        free_text_block(t);
                    }
                } else {
#ifdef MCP_SUPPORT
                    if (strncmp(t->start, "#$#", 3)) {
                        d->quota--; /* Only count non-MCP messages for quota */
                    }
#else
                    d->quota--;
#endif
                    nprocessed++;
                    if (!do_command(d, t)) {
                        if (valid_obj(tp_quit_prog)
                            && Typeof(tp_quit_prog) == TYPE_PROGRAM) {
                            char *full_command, xbuf[BUFFER_LEN];
                            char buf[BUFFER_LEN], *msg, *command;
                            struct frame *tmpfr;

                            command = QUIT_COMMAND;
                            strcpy(match_cmdname, QUIT_COMMAND);
                            strcpy(buf, command + sizeof(QUIT_COMMAND) - 1);
                            msg = buf;
                            full_command = strcpy(xbuf, msg);
                            for (; *full_command && !isspace(*full_command);
                                 full_command++) ;
                            if (*full_command)
                                full_command++;
                            strcpy(match_args, full_command);
                            tmpfr = interp(d->descriptor, d->player,
                                           DBFETCH(d->player)->location,
                                           tp_quit_prog, (dbref) -5, FOREGROUND,
                                           STD_REGUID, 0);
                            if (tmpfr) {
                                interp_loop(d->player, tp_quit_prog, tmpfr, 0);
                            }
                        } else {
                            d->booted = BOOT_QUIT;
                        }
                    }

                    d->input.head = t->nxt;
                    d->input.lines--;
                    if (!d->input.head) {
                        d->input.tail = &d->input.head;
                        d->input.lines = 0;
                    }
                    free_text_block(t);
                }
            }
        }
    } while (nprocessed > 0);
}

/* This returns 1 if the command is one of the inherent
 * interface commands, like QUIT, PREFIX_COMMAND, @Q, etc.
 * -Akari
 */
int
is_interface_command(const char *cmd)
{
    const char *tmp = cmd;

#ifdef MCP_SUPPORT
    if (!strncmp(tmp, "#$\"", 3)) {
        /* Dequote MCP quoting */
        tmp += 3;
    }
    if (!strncmp(cmd, "#$#", 3)) { /* MCP Message */
        return 1;
    }
#endif

    if (!string_compare(tmp, BREAK_COMMAND))
        return 1;
    if (!string_compare(tmp, QUIT_COMMAND))
        return 1;
    if (!strncmp(tmp, WHO_COMMAND, strlen(WHO_COMMAND)))
        return 1;
    if (!strncmp(tmp, PREFIX_COMMAND, strlen(PREFIX_COMMAND)))
        return 1;
    if (!strncmp(tmp, SUFFIX_COMMAND, strlen(SUFFIX_COMMAND)))
        return 1;
    return 0;
}

int
do_command(struct descriptor_data *d, struct text_block *t)
{

    struct frame *tmpfr;
    char cmdbuf[BUFFER_LEN];

    int len = t->nchars;
#ifdef UTF8_SUPPORT
    int mblength = t->nwchars;
#else
    int mblength = -1;
#endif
    char *command = t->start;

#ifdef NEWHTTPD
    if (d->type == CT_HTTP)     /* hinoserm */
        return 1;               /* hinoserm */
#endif /* NEWHTTPD */

#ifdef MCP_SUPPORT
    if (!mcp_frame_process_input(&d->mcpframe, command, cmdbuf, sizeof(cmdbuf))) {
        d->quota++;
        return 1;
    }
#else
    strcpy(cmdbuf, command);
#endif
    command = cmdbuf;

    if (tp_use_self_on_command && d->connected)
        ts_lastuseobject(d->player, d->player);

    if (!string_compare(command, BREAK_COMMAND)) {
        if (!d->connected || !OkObj(d->player)) {
            return 0;           /* don't bother dealing with #-1 READ programs, just QUIT */
        } else if (Wiz(d->player) || MLevel(d->player) >= tp_min_progbreak_lev) {
            if (dequeue_prog(d->player, 2)) {
                if (d->output_prefix) {
                    queue_ansi(d, d->output_prefix);
                    queue_write(d, "\r\n", 2);
                }
                anotify(d->player, CINFO "Foreground program aborted.");
                if ((FLAGS(d->player) & INTERACTIVE))
                    if ((FLAGS(d->player) & READMODE))
                        process_command(d->descriptor, d->player, command, len, mblength);
                if (d->output_suffix) {
                    queue_ansi(d, d->output_suffix);
                    queue_write(d, "\r\n", 2);
                }
                DBFETCH(d->player)->sp.player.block = 0;
            }
        }
    } else if (!strcmp(command, QUIT_COMMAND)) {
        return 0;
    } else if (!strncmp(command, PUEBLO_COMMAND, sizeof(PUEBLO_COMMAND) - 1)) {
        queue_ansi(d, "</xch_mudtext><img xch_mode=html>");
        d->type = CT_PUEBLO;
    } else if (!strncmp(command, "!WHO", sizeof("!WHO") - 1)) {
        if (!d->connected && (reg_site_is_barred(d->hu->h->a) == TRUE)) {
            queue_ansi(d, "Connect and find out!\r\n");
        } else if (tp_secure_who && (!d->connected || !TMage(d->player))) {
            queue_ansi(d, "Connect and find out!\r\n");
        } else {
            dump_users(d, command + sizeof("!WHO") - 1);
        }
    } else if (!strncmp(command, WHO_COMMAND, sizeof(WHO_COMMAND) - 1)) {
        char buf[BUFFER_LEN];

        if (d->output_prefix) {
            queue_ansi(d, d->output_prefix);
            queue_write(d, "\r\n", 2);
        }
        strcpy(buf, "@");
        strcat(buf, WHO_COMMAND);
        strcat(buf, " ");
        strcat(buf, command + sizeof(WHO_COMMAND) - 1);

        if (!d->connected) {
            if (tp_secure_who || (reg_site_is_barred(d->hu->h->a) == TRUE)) {
                queue_ansi(d, "Login and find out!\r\n");
            } else {
                if (OkObj(tp_player_start) && OkObj(tp_login_who_prog)
                    && Typeof(tp_login_who_prog) == TYPE_PROGRAM) {
                    char *full_command, xbuf[BUFFER_LEN], *msg;

                    strcpy(match_cmdname, WHO_COMMAND);
                    strcpy(buf, command + sizeof(WHO_COMMAND) - 1);
                    msg = buf;
                    full_command = strcpy(xbuf, msg);
                    for (; *full_command && !isspace(*full_command);
                         full_command++) ;
                    if (*full_command)
                        full_command++;
                    strcpy(match_args, full_command);
                    tmpfr = interp(d->descriptor, -1, -1, tp_login_who_prog,
                                   (dbref) -5, FOREGROUND, STD_REGUID, 0);
                    if (tmpfr) {
                        interp_loop(-1, tp_login_who_prog, tmpfr, 0);
                    }
                } else {
                    dump_users(d, command + sizeof(WHO_COMMAND) - 1);
                }
            }
        } else {
            if (can_move(d->descriptor, d->player, buf, 5)) {
                do_move(d->descriptor, d->player, buf, 5);
            } else {
                dump_users(d, command + sizeof(WHO_COMMAND) - 1);
            }
        }
        if (d->output_suffix) {
            queue_ansi(d, d->output_suffix);
            queue_write(d, "\r\n", 2);
        }
    } else if (!strncmp(command, PREFIX_COMMAND, sizeof(PREFIX_COMMAND) - 1)) {
        set_userstring(&d->output_prefix, command + sizeof(PREFIX_COMMAND) - 1);
    } else if (!strncmp(command, SUFFIX_COMMAND, sizeof(SUFFIX_COMMAND) - 1)) {
        set_userstring(&d->output_suffix, command + sizeof(SUFFIX_COMMAND) - 1);
    } else {
        if (d->connected) {
            if (d->output_prefix) {
                queue_ansi(d, d->output_prefix);
                queue_write(d, "\r\n", 2);
            }
            process_command(d->descriptor, d->player, command, len, mblength);
            if (d->output_suffix) {
                queue_ansi(d, d->output_suffix);
                queue_write(d, "\r\n", 2);
            }
        } else {
            if (d->interactive == 2) {
                handle_read_event(d->descriptor, NOTHING, command, NULL, len, mblength);
            } else {
                check_connect(d, command);
            }
        }
    }
    (d->commands)++;
    return 1;
}

void
interact_warn(dbref player)
{
    if (FLAGS(player) & INTERACTIVE) {
        char buf[BUFFER_LEN];

        sprintf(buf, CINFO "***  %s  ***",
                (FLAGS(player) & READMODE) ?
                "You are currently using a program.  Use \"@Q\" to return to a more reasonable state of control."
                : (DBFETCH(player)->sp.player.
                   insert_mode ?
                   "You are currently inserting MUF program text.  Use \".\" to return to the editor, then \"quit\" if you wish to return to your regularly scheduled Muck universe."
                   : "You are currently using the MUF program editor."));
        anotify(player, buf);
    }
}

void
check_connect(struct descriptor_data *d, const char *msg)
{
    char command[80];
    char user[BUFFER_LEN];
    char password[80];
    const char *why;
    dbref player;
    time_t now;
    int result, xref;
    char msgargs[BUFFER_LEN];
    char buf[BUFFER_LEN];
    char *p = NULL;
    int len = 0;

    parse_connect(msg, command, user, password);
    for (xref = 0; command[xref]; xref++)
        command[xref] = DOWNCASE(command[xref]);
    strcpy(msgargs, user);
    strcat(msgargs, " ");
    strcat(msgargs, password);

    /* Logging needs to happen now for simplicity's sake, but we should not log
       if the string would get parsed by the connect handler. Note that the
       password will still get betrayed if the user typos the first two letters
       of "connect" or "ch", but this is still a vast improvement.
           
       If you want to debug strings passed to the connection handler, define
       DEBUGLOGINS somewhere. This should not be done in "live" environments,
       as it makes hashing your player passwords pointless. -davin */

#ifndef DEBUGLOGINS
    if (tp_log_connects &&
        !(
            ((string_prefix("connect", command) && !string_prefix("c", command))
            || !string_compare(command, "ch"))
        ))
#endif
        log2filetime(CONNECT_LOG, "%2d: %s\r\n", d->descriptor, msg);

    /* do_command will no longer reach this point. -davin */
    if (d->interactive == 2) {
        p = buf;
        while (*msg && (isprint(*msg))) {
            *p++ = *msg++;
            len++;
        }
        *p = '\0';
        handle_read_event(d->descriptor, NOTHING, buf, NULL, len, -2);
        return;
    }

    if ((string_prefix("connect", command) && !string_prefix("c", command))
        || !string_compare(command, "ch")) {
        player = connect_player(user, password);
        if (player == NOTHING) {
            d->fails++;
            queue_ansi(d, connect_fail);
            player = lookup_player(user);
            if (player > 0) {
                now = current_systime;
                result = get_property_value(player, "@/Failed/Count") + 1;
                SETMESG(player, "@/Failed/Host", d->hu->h->name);
                add_property(player, "@/Failed/Time", NULL, (int)now);
                add_property(player, "@/Failed/Count", NULL, result);
            }
            if (d->fails >= 3)
                d->booted = BOOT_DROP;
            if (tp_log_connects)
                log2filetime(CONNECT_LOG,
                             "FAIL: %2d %s pw '%s' %s(%s) %s P#%d\n",
                             d->descriptor, user, "<hidden>", d->hu->h->name,
                             d->hu->u->user, host_as_hex(d->hu->h->a),
                             d->cport);
            show_status("FAIL: %2d %s pw '%s' %s(%s) %s P#%d\n", d->descriptor,
                        user, "<hidden>", d->hu->h->name, d->hu->u->user,
                        host_as_hex(d->hu->h->a), d->cport);

        } else if ((why = reg_user_is_suspended(player))) {
            queue_ansi(d, "\r\n");
            queue_ansi(d, "You are temporarily suspended: ");
            queue_ansi(d, why);
            queue_ansi(d, "\r\n");
            queue_ansi(d, "Please contact ");
            queue_ansi(d, tp_reg_email);
            queue_ansi(d, " for assistance if needed.\r\n");
            if (tp_log_connects)
                log2filetime(CONNECT_LOG, "*LOK: %2d %s %s(%s) %s %s P#%d\n",
                             d->descriptor, unparse_object(player, player),
                             d->hu->h->name, d->hu->u->user,
                             host_as_hex(d->hu->h->a), why, d->cport);
            show_status("*LOK: %2d %s %s(%s) %s %s P#%d\n",
                        d->descriptor, unparse_object(player, player),
                        d->hu->h->name, d->hu->u->user,
                        host_as_hex(d->hu->h->a), why, d->cport);
            d->booted = BOOT_DROP;
        } else if (reg_user_is_barred(d->hu->h->a, player) == TRUE) {
            char buf[1024];

            sprintf(buf, CFG_REG_MSG2, tp_reg_email);
            queue_ansi(d, buf);
            if (tp_log_connects)
                log2filetime(CONNECT_LOG, "*BAN: %2d %s %s(%s) %s P#%d\n",
                             d->descriptor, unparse_object(player, player),
                             d->hu->h->name, d->hu->u->user,
                             host_as_hex(d->hu->h->a), d->cport);
            show_status("*BAN: %2d %s %s(%s) %s P#%d\n",
                        d->descriptor, unparse_object(player, player),
                        d->hu->h->name, d->hu->u->user,
                        host_as_hex(d->hu->h->a), d->cport);
            d->booted = BOOT_DROP;
        } else if ((wizonly_mode ||
                    (tp_playermax && con_players_curr >= tp_playermax_limit)) &&
                   !TMage(player)
            ) {
            if (delayed_shutdown) {
                queue_ansi(d,
                           "Sorry, but the game currently shutting down. Try again later.");
            } else if (wizonly_mode) {
                queue_ansi(d,
                           "Sorry, but the game is in maintenance mode currently, and only wizards are allowed to connect.  Try again later.");
            } else {
                queue_ansi(d, BOOT_MESG);
            }
            queue_ansi(d, "\r\n");
            d->booted = BOOT_DROP;
        } else {
            if (!string_compare(command, "ch") &&
                !(Arch(player) || POWERS(player) & POW_HIDE)) {
                queue_ansi(d, "Only wizards can connect hidden.\r\n");
                d->fails++;
                if (tp_log_connects)
                    log2filetime(CONNECT_LOG,
                                 "FAIL[CH]: %2d %s pw '%s' %s(%s) %s P#%d\n",
                                 d->descriptor, user, "<hidden>",
                                 d->hu->h->name, d->hu->u->user,
                                 host_as_hex(d->hu->h->a), d->cport);
                show_status("FAIL[CH]: %2d %s pw '%s' %s(%s) %s P#%d\n",
                            d->descriptor, user, "<hidden>", d->hu->h->name,
                            d->hu->u->user, host_as_hex(d->hu->h->a), d->cport);
            } else {
                if (tp_log_connects)
                    log2filetime(CONNECT_LOG, "CONN: %2d %s %s(%s) %s P#%d\n",
                                 d->descriptor, unparse_object(player, player),
                                 d->hu->h->name, d->hu->u->user,
                                 host_as_hex(d->hu->h->a), d->cport);
                show_status("CONN: %2d %s %s(%s) %s P#%d\n",
                            d->descriptor, unparse_object(player, player),
                            d->hu->h->name, d->hu->u->user,
                            host_as_hex(d->hu->h->a), d->cport);
                d->connected = 1;
                d->did_connect = 1;
                d->connected_at = current_systime;
                d->player = player;

                update_desc_count_table();
                remember_player_descr(player, d->descriptor);
                if (!string_compare(command, "ch")) {
                    FLAG2(player) |= F2HIDDEN;
                } else {
                    FLAG2(player) &= ~F2HIDDEN;
                }
                if (d->type == CT_PUEBLO) {
                    FLAG2(player) |= F2PUEBLO;
                } else {
                    FLAG2(player) &= ~F2PUEBLO;
                }

                /* If player has enabled 256 color, set the descr flag */
                if (FLAGS(player) & F256COLOR) {
                    DR_RAW_ADD_FLAGS(d, DF_256COLOR);
                }

                /* cks: someone has to initialize this somewhere. */
                DBFETCH(d->player)->sp.player.block = 0;
                if (d->player == NOTHING)
                    d->block = 0;
                spit_file(player, MOTD_FILE);
                interact_warn(player);
                if (sanity_violated && TMage(player)) {
                    notify(player,
                           MARK "WARNING!  The DB appears to be corrupt!");
                }
                if ((d->encoding != ENC_ASCII) &&
                        (d->encoding != ENC_LATIN1 || tp_ascii_descrs)) {
                    anotify_fmt(d->player, CINFO "Negotiated Encoding: " CNOTE "%s",
                            (d->encoding == ENC_LATIN1 ? "ISO-8859-1 (Latin1)" :
                             (d->encoding == ENC_IBM437 ? "IBM437" :
                              (d->encoding == ENC_UTF8 ? "UTF-8" : "UNKNOWN" ))));
                }
                announce_connect(d->descriptor, player);
            }
        }
    } else if (prop_command(d->descriptor, (dbref) NOTHING, command, msgargs, "@logincommand", 1)) { /* Check for replaced login commands */
    } else if (string_prefix("help", command)) { /* Connection Help */
        if (tp_log_connects)
            log2filetime(CONNECT_LOG, "HELP: %2d %s(%s) %s %d cmds P#%d\n",
                         d->descriptor, d->hu->h->name, d->hu->u->user,
                         host_as_hex(d->hu->h->a), d->commands, d->cport);
        show_status("HELP: %2d %s(%s) %s %d cmds P#%d\n",
                    d->descriptor, d->hu->h->name, d->hu->u->user,
                    host_as_hex(d->hu->h->a), d->commands, d->cport);
        help_user(d);
    } else {
        if (tp_log_connects)
            log2filetime(CONNECT_LOG,
                         "TYPO: %2d %s(%s) %s '%s' %d cmds P#%d\n",
                         d->descriptor, d->hu->h->name, d->hu->u->user,
                         host_as_hex(d->hu->h->a), command, d->commands,
                         d->cport);
        show_status("TYPO: %2d %s(%s) %s '%s' %d cmds P#%d\n",
                    d->descriptor, d->hu->h->name, d->hu->u->user,
                    host_as_hex(d->hu->h->a), command, d->commands, d->cport);
        welcome_user(d);
    }
}
void
parse_connect(const char *msg, char *command, char *user, char *pass)
{
    int cnt = 0;
    char *p = NULL;
    const char *start = NULL;
    int bInQuotes = 0;
    int lastSpace = -1;

    while (*msg && isascii(*msg) && isspace(*msg)) /* space */
        msg++;
    p = command;
    cnt = 0;
    while (*msg && isascii(*msg) && !isspace(*msg) && (++cnt < 80))
        *p++ = *msg++;
    *p = '\0';
    cnt = 0;
    while (*msg && isascii(*msg) && isspace(*msg)) /* space */
        msg++;
    if ( tp_spaces_in_playernames ) {
        p = user;
        cnt = 0;
        /* Determine if this is a quoted name */
        if ( *msg && ++cnt < 80 && ( *msg == '\'' || *msg == '\"' ) ){
            bInQuotes = 1;
            msg++; /* Move past the quote */
        }  
        if ( bInQuotes ){
            while (*msg && isascii(*msg) && ++cnt < BUFFER_LEN && 
                   !(*msg == '\'' || *msg == '\"' )) 
                *p++ = *msg++;
            msg++; /* Move past the ending quote */
            *p = '\0';  
        }
        else {
            /* Unquoted name, just treat as normal */
            /* Unquoted name, find the last space in the input
               and assume everything after that is password. */
            cnt = 0;
            start = msg;
            while (*msg && isascii(*msg) && cnt < BUFFER_LEN){
                if (isspace(*msg)){
                    lastSpace = cnt;
                }
                cnt++;
                msg++;
            }
            if (lastSpace == -1){
                /* No space found, assume no password */
                lastSpace = cnt;
            }

            /* At this point, last_space should point to the last
               space in the input. Everything in msg before it
               is name, everything after it is password */
            msg = start;
            cnt = 0; 
            /* copying over name */
            while (*msg && isascii(*msg) && cnt < lastSpace) {
                *p = *msg;    
                ++msg;
                ++p;
                ++cnt;
            }
            *p = '\0';
        }
    }
    else {
        p = user;
        cnt = 0;
        while (*msg && isascii(*msg) && !isspace(*msg) && (++cnt < BUFFER_LEN))
            *p++ = *msg++;
        *p = '\0';
    }
    while (*msg && isascii(*msg) && isspace(*msg)) /* space */
        msg++;
    p = pass;
    cnt = 0;
    while (*msg && isascii(*msg) /* && !isspace(*msg) */ &&(++cnt < 80))
        *p++ = *msg++;
    *p = '\0';
}

int
boot_off(dbref player)
{
    struct descriptor_data *d;
    struct descriptor_data *last = NULL;

    for (d = descriptor_list; d; d = d->next)
        if (d->connected && d->player == player && !d->booted)
            last = d;

    if (last) {
        process_output(last);
        last->booted = BOOT_DROP;
        /* shutdownsock(last); */
        return 1;
    }
    return 0;
}

void
boot_player_off(dbref player)
{
    int di;
    int *darr;
    int dcount;
    struct descriptor_data *d;

    darr = get_player_descrs(player, &dcount);


    /* We need to be a tad more brutal as this player may be getting @toaded */
    for (di = 0; di < dcount; di++) {
        d = descrdata_by_index(darr[di]);
        if (d) {
            announce_disconnect(d);
/*	    forget_player_descr(d->player, d->descriptor); */
            d->connected = 0;
            d->player = NOTHING;
            if (!d->booted)
                d->booted = BOOT_DROP;
        }
    }
    update_desc_count_table();
}


void
close_sockets(const char *msg)
{
    struct descriptor_data *d, *dnext;
    int i;

    (void) pdescrflush(-1);
    for (d = descriptor_list; d; d = dnext) {
        dnext = d->next;

        sockwrite(d, msg, strlen(msg));
        announce_disconnect(d);
        clearstrings(d);                 /** added to clean up **/
        if (shutdown(d->descriptor, 2) < 0)
            perror("shutdown");
        closesocket(d->descriptor);
        freeqs(d);                       /****/
        *d->prev = d->next;              /****/
        if (d->next)                                                                                                                                                                     /****/
            d->next->prev = d->prev;     /****/
                                   /****/
#ifdef MCCP_ENABLED
	    mccp_end(d);
#endif
#ifdef NEWHTTPD
        if (d->http)
            http_deinitstruct(d);
#endif /* NEWHTTPD */
#if defined(DESCRFILE_SUPPORT) || defined(NEWHTTPD)
        if (d->dfile)
            descr_fsenddisc(d);
#endif /* DESCRFILE_SUPPORT */
        host_delete(d->hu);

        telopt_clean(&d->telopt);
        FREE(d);                         /****/
        ndescriptors--;                  /****/
    }
    for (i = 0; i < numsocks; i++) {
        closesocket(sock[i]);
    }
#ifdef USE_SSL
    closesocket(ssl_sock);
#endif

}

struct descriptor_data *
get_descr(int descr, int player)
{
    struct descriptor_data *next = descriptor_list;

    while (next) {
        if (((player > 0) && (next->player == player)) ||
            ((descr > 0) && (next->descriptor == descr)))
            return next;
        next = next->next;
    }
    return NULL;
}

static const char *
descr_flag_description(int descr)
{
    static char dbuf[BUFFER_LEN];
    struct descriptor_data *d = descrdata_by_descr(descr);

    strcpy(dbuf, SYSGREEN "Descr Flags:" SYSYELLOW);
#ifdef NEWHTTPD
    if (DR_RAW_FLAGS(d, DF_HTML))
        strcat(dbuf, " DF_HTML");
#endif /* NEWHTTPD */
    if (DR_RAW_FLAGS(d, DF_PUEBLO))
        strcat(dbuf, " DF_PUEBLO");
    if (DR_RAW_FLAGS(d, DF_MUF))
        strcat(dbuf, " DF_MUF");
    if (DR_RAW_FLAGS(d, DF_IDLE))
        strcat(dbuf, " DF_IDLE");
    if (DR_RAW_FLAGS(d, DF_TRUEIDLE))
        strcat(dbuf, " DF_TRUEIDLE");
    if (DR_RAW_FLAGS(d, DF_INTERACTIVE))
        strcat(dbuf, " DF_INTERACTIVE");
    if (DR_RAW_FLAGS(d, DF_COLOR))
        strcat(dbuf, " DF_COLOR");
    if (DR_RAW_FLAGS(d, DF_HALFCLOSE))
        strcat(dbuf, " DF_HALFCLOSE");
#ifdef USE_SSL
    if (DR_RAW_FLAGS(d, DF_SSL))
        strcat(dbuf, " DF_SSL");
#endif /* USE_SSL */
    if (DR_RAW_FLAGS(d, DF_SUID))
        strcat(dbuf, " DF_SUID");
#ifdef MCCP_ENABLED
    if (DR_RAW_FLAGS(d, DF_COMPRESS))
        strcat(dbuf, " DF_COMPRESS");
#endif /* MCCP_ENABLED */
    if (DR_RAW_FLAGS(d, DF_MXP))
        strcat(dbuf, " DF_MXP");
    if (DR_RAW_FLAGS(d, DF_MSP))
        strcat(dbuf, " DF_MSP");
#ifdef IPV6
    if (DR_RAW_FLAGS(d, DF_IPV6))
        strcat(dbuf, " DF_IPV6");
#endif /* IPV6 */
    if (DR_RAW_FLAGS(d, DF_256COLOR))
        strcat(dbuf, " DF_256COLOR");
    if (DR_RAW_FLAGS(d, DF_TELNET))
        strcat(dbuf, " DF_TELNET");
    if (DR_RAW_FLAGS(d, DF_KEEPALIVE))
        strcat(dbuf, " DF_KEEPALIVE");
    if (DR_RAW_FLAGS(d, DF_WELCOMING))
        strcat(dbuf, " DF_WELCOMING");
    if (DR_RAW_FLAGS(d, DF_USER1))
        strcat(dbuf, " DF_USER1");
    if (DR_RAW_FLAGS(d, DF_USER2))
        strcat(dbuf, " DF_USER2");
    if (DR_RAW_FLAGS(d, DF_USER3))
        strcat(dbuf, " DF_USER3");
    return dbuf;
}

void
do_dinfo(dbref player, const char *arg)
{
    struct descriptor_data *d;
    int who, descr;
    char *ctype = NULL;
    time_t now;

    if (!Wiz(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (!*arg) {
        anotify(player, CINFO "Usage: dinfo descriptor  or  dinfo name");
        return;
    }
    now = current_systime;
    if (strcmp(arg, "me"))
        who = lookup_player(arg);
    else
        who = player;
    descr = atoi(arg);
    if (!(d = get_descr(descr, who))) {
        anotify(player, CINFO "Invalid descriptor or user not online.");
        return;
    }

    switch (d->type) {
        case CT_MUCK:
            ctype = "muck";
            break;
        case CT_MUF:
            ctype = "muf";
            break;
#ifdef NEWHTTPD
        case CT_HTTP:
            ctype = "http";
            break;
#endif /* NEWHTTPD */
        case CT_PUEBLO:
            ctype = "pueblo";
            break;
#ifdef USE_SSL
        case CT_SSL:
            ctype = "ssl";
            break;
#endif /* USE_SSL */
        default:
            ctype = "unknown";
    }

    anotify_fmt(player, "%s"SYSAQUA " descr " SYSYELLOW "%d" SYSBLUE " (%s)",
                d->connected ? ansi_unparse_object(player, d->player) : SYSGREEN
                "[Connecting]", d->descriptor, ctype);

    if (d->flags) {
        /* need to print out the flags */
        anotify_nolisten(player, descr_flag_description(d->descriptor), 1);

	anotify_fmt(player, SYSAQUA "Termtype: "
                        SYSCYAN "%-20s"
                        SYSAQUA "Window: "
                        SYSCYAN "%dx%d",
	    d->telopt.termtypes->data.packed[0].data.string->data,
        d->telopt.width, d->telopt.height);

    anotify_fmt(player,
                        SYSAQUA "Encoding: "
                        SYSCYAN "%-20s"
                        SYSAQUA "  MTTS: " 
                        SYSCYAN "%d",
	    (d->encoding == ENC_RAW ? "RAW" :
            (d->encoding == ENC_ASCII ? "US-ASCII" :
                 (d->encoding == ENC_UTF8 ? "UTF-8" : SYSRED "UNKNOWN"))),
        d->telopt.mtts);
	    //(d->encoding ? (d->encoding == ENC_ASCII ? "US-ASCII" : "UTF-8") : "RAW" ));
    }

    if (Arch(player))
        anotify_fmt(player, SYSAQUA "Host: " SYSCYAN "%s" SYSBLUE "@"
                    SYSCYAN "%s", d->hu->u->user, d->hu->h->name);
    else
        anotify_fmt(player, SYSAQUA "Host: " SYSCYAN "%s", d->hu->h->name);

        anotify_fmt(player, SYSAQUA "IP: " SYSCYAN "%s" SYSYELLOW "(%d) " SYSNAVY
	            "%X", hostToIPex(d->hu->h), d->hu->u->uport, d->hu->h->a);

    anotify_fmt(player, SYSVIOLET "Online: " SYSPURPLE "%s  " SYSBROWN "Idle: "
                SYSYELLOW "%s  " SYSCRIMSON "Commands: " SYSRED "%d",
                time_format_1(now - d->connected_at),
                time_format_2(now - d->last_time), d->commands);

    if (d->connected)
        anotify_fmt(player, SYSAQUA "Location: %s",
                    ansi_unparse_object(player, DBFETCH(d->player)->location));
}

void
do_dwall(dbref player, const char *name, const char *msg)
{
    struct descriptor_data *d;
    int who, descr;
    char buf[BUFFER_LEN];

    if (!Wiz(player) && !(POWERS(player) & POW_ANNOUNCE)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (!*name || !*msg) {
        anotify(player, CINFO "Help: dwall me=#");
        return;
    }
    if (strcmp(name, "me"))
        who = lookup_player(name);
    else
        who = player;
    descr = atoi(name);
    if (!(d = get_descr(descr, who))) {
        anotify(player, CINFO "Invalid descriptor or user not online.");
        return;
    }

    switch (msg[0]) {
        case ':':
        case ';':
            sprintf(buf, MARK "%s %s\r\n", NAME(player), msg + 1);
            break;
        case '@':
            sprintf(buf, MARK "%s\r\n", msg + 1);
            break;
        case '#':
            notify(player, "DWall Help");
            notify(player, "~~~~~~~");
            notify(player, "dwall player=msg -- tell player 'msg'");
            notify(player, "dwall 14=message -- tell ds 14 'message'");
            notify(player, "dwall 14=:poses  -- pose 'poses' to ds 14");
            notify(player, "dwall 14=@boo    -- spoof 'boo' to ds 14");
            notify(player, "dwall 14=!boo    -- same as @ with no 'mark'");
            notify(player, "dwall 14=#       -- this help list");
            notify(player,
                   "Use WHO or WHO! to find ds numbers for players online.");
            return;
        case '!':
            sprintf(buf, "%s\r\n", msg + 1);
            break;
        default:
            sprintf(buf, MARK "%s tells you, \"%s\"\r\n", NAME(player), msg);
            break;
    }

    queue_ansi(d, buf);
    if (!process_output(d))
        d->booted = BOOT_DROP;
    anotify_fmt(player, CSUCC "Message sent to descriptor %d.", d->descriptor);
}

void
do_dboot(dbref player, const char *name)
{
    struct descriptor_data *d;
    int who, descr;

    if (!Arch(player) && !(POWERS(player) & POW_BOOT)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (!*name) {
        anotify(player, CINFO "Usage: dboot descriptor  or  dboot name");
        return;
    }
    if (strcmp(name, "me"))
        who = lookup_player(name);
    else
        who = player;
    descr = atoi(name);
    if (!(d = get_descr(descr, who))) {
        anotify(player, CINFO "Invalid descriptor or user not online.");
        return;
    }
    who = d->player;
    if (OkObj(who) && OkObj(player)) {
        if (Man(who)) {
            anotify(player, CFAIL "You cannot dboot the man!");
            return;
        }
        if (Boy(who) && !Man(player)) {
            anotify(player, CFAIL "Only the man can dboot boys.");
            return;
        }
        if (Arch(who) && !Boy(player)) {
            anotify(player, CFAIL "You cannot dboot ArchWizards.");
            return;
        }
    }

    d->booted = BOOT_DROP;
    anotify(player, CSUCC "Booted.");
}

void
do_armageddon(dbref player, const char *msg)
{
    char buf[BUFFER_LEN];

    if (!Arch(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        log_status("SHAM: Armageddon by %s\n", unparse_object(player, player));
        return;
    }
    if (*msg == '\0' || strcmp(msg, tp_muckname)) {
        anotify(player, CINFO "Usage: @armageddon muckname");
        notify(player, "***WARNING*** All data since last save will be lost!");
        return;
    }
    sprintf(buf, "\r\nImmediate shutdown by %s.\r\n", NAME(player));
    log_status("DDAY: %s(%d): %s\n", NAME(player), player, msg);
    fprintf(stderr, "DDAY: %s(%d)\n", NAME(player), player);
    close_sockets(buf);

#ifdef SPAWN_HOST_RESOLVER
    kill_resolver();
#endif
#ifdef USE_RESLVD
    reslvd_close();
#endif

    exit(1);
}


void
emergency_shutdown(void)
{
    close_sockets("\r\nEep!  Emergency shutdown.  Be back soon!");

#ifdef SPAWN_HOST_RESOLVER
    kill_resolver();
#endif
#ifdef USE_RESLVD
    reslvd_close();
#endif

}


void
dump_users(struct descriptor_data *d, char *user)
{
    struct descriptor_data *dlist;
    char wizwho = (!tp_mortalwho);
    short players = 0;
    short idleplyrs = 0;
    static short player_max = 0;
    const char *p;
    char buf[BUFFER_LEN], dobuf[BUFFER_LEN];
    char plyrbuf[BUFFER_LEN], typbuf[BUFFER_LEN];
    char tbuf[BUFFER_LEN], tbuf2[BUFFER_LEN];

    if (!d)
        return;

    if (!(OkObj(d->player) && d->connected) && tp_secure_who) {
        queue_ansi(d, SYSRED "Login and find out!");
        return;
    }

    while (*user && isspace(*user))
        user++;

    for (; *user && *user == '!'; user++) {
        wizwho++;
        if (wizwho > 2)
            wizwho = 0;
    }

    while (*user && isspace(*user))
        user++;

    if (!d->connected || !OkObj(d->player)
        || (!Mage(d->player) && !(POWERS(d->player) & POW_EXPANDED_WHO)))
        wizwho = 0;

    if (!index(user, '*')) {
        strcpy(tbuf, user);
        user = strcat(tbuf, "*");
    }

    if (!wizwho && tp_who_doing) {
        if ((p = get_property_class((dbref) 0, "_poll"))) {
            SPRINTF(dobuf, "%-43s", "%-43U", p);
        } else {
            // sprintf(dobuf, "%-43s", "Doing...");
            strcpy(dobuf, "Doing...");
        }
    }
    sprintf(plyrbuf, "%-*s", (int)PLAYER_NAME_LIMIT + 6, "Player Name");
    switch (wizwho) {
        case 0:{
            if (tp_who_doing) {
                sprintf(buf,
                        "%s%s%sOn For %sIdle  %s%-.43s\r\n",
                        SYSGREEN, plyrbuf, SYSPURPLE, SYSYELLOW, SYSCYAN,
                        dobuf);
            } else {
                sprintf(buf, "%s%s%sOn For %sIdle\r\n",
                        SYSGREEN, plyrbuf, SYSPURPLE, SYSYELLOW);
            }
            break;
        }
        case 1:{
            sprintf(buf,
                    "%sDS  %s%s%sPort    %sOn For %sIdle %sHost\r\n",
                    SYSRED, SYSGREEN, plyrbuf, SYSCYAN, SYSPURPLE,
                    SYSYELLOW, SYSBLUE);
            break;
        }
        case 2:{
            sprintf(buf,
                    "%sDS  %s%s%sOutput[k]  %sInput[k]  %sCommands %sType\r\n",
                    SYSRED, SYSGREEN, plyrbuf, SYSWHITE,
                    SYSYELLOW, SYSCYAN, SYSBLUE);
            break;
        }
    }
    queue_unhtml(d, buf);
    for (dlist = descriptor_list; dlist; dlist = dlist->next) {
        strcpy(plyrbuf, "");
        strcpy(typbuf, "");
        switch (dlist->type) {
            case CT_MUCK:{
                if (dlist->connected && OkObj(dlist->player)) {
                    strcpy(plyrbuf, NAME(dlist->player));
                } else {
                    strcpy(plyrbuf, "[Connecting]");
                }
                strcpy(typbuf, "Text Port");
                break;
            }
#ifdef USE_SSL
            case CT_SSL:{
                if (dlist->connected && OkObj(dlist->player)) {
                    strcpy(plyrbuf, NAME(dlist->player));
                } else {
                    strcpy(plyrbuf, "[Connecting]");
                }
                strcpy(typbuf, "SSL Port");
                break;
            }
#endif
            case CT_PUEBLO:{
                if (dlist->connected && OkObj(dlist->player)) {
                    strcpy(plyrbuf, NAME(dlist->player));
                } else {
                    strcpy(plyrbuf, "[Connecting]");
                }
                strcpy(typbuf, "Pueblo Port");
                break;
            }
            case CT_MUF:{
                strcpy(plyrbuf, "[MUF]");
                strcpy(typbuf, "MUF Port");
                break;
            }
            case CT_INBOUND:{
                strcpy(plyrbuf, "[Inbound]");
                strcpy(typbuf, "MUF Inbound");
                break;
            }
            case CT_OUTBOUND:{
                strcpy(plyrbuf, "[Outbound]");
                strcpy(typbuf, "MUF Outbound");
                break;
            }
            default:{
                strcpy(plyrbuf, "[Unknown]");
                strcpy(typbuf, "Unknown Port Type");
            }
        }
        if ((!wizwho) && !((dlist->connected && OkObj(dlist->player)) ?
                           !((FLAGS(dlist->player) & DARK) ||
                             (FLAG2(dlist->player) & F2HIDDEN)) ||
                           (FLAG2(dlist->player) & F2LIGHT) : 0)) {
            sprintf(buf, "[%s]", plyrbuf);
            strcpy(plyrbuf, buf);
        }
        if ((wizwho ? 1
             : ((dlist->connected
                 && OkObj(dlist->
                          player)) ? (tp_who_hides_dark ?
                                      !((FLAGS(dlist->player) & DARK)
                                        || (FLAG2(dlist->player) & F2HIDDEN))
                                      || (OkObj(d->player) && Wiz(d->player))
                                      || (d->player == dlist->player)
                                      || (FLAG2(dlist->player) & F2LIGHT) : 1) :
                0)) && equalstr(user, plyrbuf)) {
            if ((OkObj(dlist->player)
                 && dlist->connected) ? (FLAG2(dlist->player) & F2IDLE) : 0) {
                idleplyrs++;
            }
            if (wizwho) {
                if (dlist->connected && OkObj(dlist->player) && wizwho) {
                    strcpy(buf, "");
                    strcpy(tbuf2, unparse_flags(dlist->player, buf));
                    strcpy(buf, tbuf2);
                    strcpy(tbuf2, "");
                    sprintf(tbuf2, "(%s)", buf);
                    strcat(plyrbuf, tbuf2);
                }
            }
            strcpy(buf, "");
            SPRINTF(plyrbuf, "%-*s", "%-*U",
                    (int)PLAYER_NAME_LIMIT + (wizwho ? 5 : 1), plyrbuf);
            switch (wizwho) {
                case 0:{
                    if (tp_who_doing) {
                        SPRINTF(buf, "%s%s %s%10s%s%s%4s%s %s%-.45s\r\n",
                                "%U%U %U%10U%U%U%4U%U %U%-.45U\r\n",
                                SYSGREEN, plyrbuf, SYSPURPLE,
                                time_format_1(current_systime -
                                              dlist->connected_at),
                                (1
                                 ? ((DR_RAW_FLAGS(dlist, DF_IDLE)) ? " " : " ")
                                 : " "), SYSYELLOW,
                                time_format_2(current_systime -
                                              dlist->last_time),
                                ((dlist->connected
                                  && OkObj(dlist->
                                           player)) ? ((FLAGS(dlist->
                                                              player) &
                                                        INTERACTIVE)
                                                       ? "*" : " ") : " "),
                                SYSCYAN,
                                GETDOING(dlist->player) ? GETDOING(dlist->
                                                                   player) :
                                "");
                    } else {
                        SPRINTF(buf, "%s%s %s%10s%s%s%4s%s\r\n",
                                "%U%U %U%10U%U%U%4U%U\r\n",
                                SYSGREEN, plyrbuf, SYSPURPLE,
                                time_format_1(current_systime -
                                              dlist->connected_at),
                                (1
                                 ? ((DR_RAW_FLAGS(dlist, DF_IDLE)) ? "I" : " ")
                                 : " "), SYSYELLOW,
                                time_format_2(current_systime -
                                              dlist->last_time),
                                ((dlist->connected
                                  && OkObj(dlist->
                                           player)) ? ((FLAGS(dlist->
                                                              player) &
                                                        INTERACTIVE)
                                                       ? "*" : " ") : " "));
                    }
                    break;
                }
                case 1:{
                    SPRINTF(buf,
                            "%s%-3d %s%s%s%5d %s%9s%s%s%4s%s%s%s%s%s\r\n",
                            "%U%-3d %U%U%U%5d %U%9U%U%U%4U%U%U%U%U%U\r\n",
                            SYSRED, dlist->descriptor, SYSGREEN, plyrbuf,
                            SYSCYAN, dlist->cport, SYSPURPLE,
                            time_format_1(current_systime -
                                          dlist->connected_at),
                            (1
                             ? ((DR_RAW_FLAGS(dlist, DF_TRUEIDLE)) ? "I" : " ")
                             : " "), SYSYELLOW,
                            time_format_2(current_systime - dlist->last_time),
                            ((dlist->connected
                              && OkObj(dlist->
                                       player)) ? ((FLAGS(dlist->
                                                          player) & INTERACTIVE)
                                                   ? "*" : " ") : " "), SYSBLUE,
                            (d->connected && OkObj(d->player)
                             && Arch(d->player)) ? dlist->hu->u->user : "",
                            (d->connected && OkObj(d->player)
                             && Arch(d->player)) ? "@" : "",
                            dlist->hu->h->name);
                    break;
                }
                case 2:{
                    sprintf(buf, "%s%-3d %s%s %s[%7d] %s[%7d] %s[%7d] %s%s\r\n",
                            SYSRED, dlist->descriptor, SYSGREEN, plyrbuf,
                            SYSWHITE, dlist->output_len / 1024, SYSYELLOW,
                            dlist->input_len / 1024, SYSCYAN, dlist->commands,
                            SYSBLUE, typbuf);
                    break;
                }
            }
            players++;
            queue_unhtml(d, buf);
        }
    }
    if (players > player_max)
        player_max = players;

    sprintf(buf,
            "%s%d player%s connected.  %s(%d Active, %d Idle, Max was %d)\r\n",
            SYSBLUE, players, ((players == 1) ? " is" : "s are"), SYSYELLOW,
            (players - idleplyrs), idleplyrs, player_max);
    queue_unhtml(d, buf);
    return;
}

void
pdump_who_users(int c, char *user)
{
    struct descriptor_data *e;

    if (c < 1)
        return;
    e = descriptor_list;
    while (e && e->descriptor != c) {
        e = e->next;
    }
    if (e->descriptor == c)
        dump_users(e, user);
    return;
}

char *
time_format_1(time_t dt)
{
    struct tm *delta;
    static char buf[64];

    delta = gmtime(&dt);

    if (delta->tm_yday > 0)
        sprintf(buf, "%dd %02d:%02d",
                delta->tm_yday, delta->tm_hour, delta->tm_min);
    else
        sprintf(buf, "%02d:%02d", delta->tm_hour, delta->tm_min);
    return buf;
}

char *
time_format_2(time_t dt)
{
    struct tm *delta;
    static char buf[64];

    delta = gmtime(&dt);

    if (delta->tm_yday > 0)
        sprintf(buf, "%dd", delta->tm_yday);
    else if (delta->tm_hour > 0)
        sprintf(buf, "%dh", delta->tm_hour);
    else if (delta->tm_min > 0)
        sprintf(buf, "%dm", delta->tm_min);
    else
        sprintf(buf, "%ds", delta->tm_sec);
    return buf;
}


void
announce_puppets(dbref player, const char *msg, const char *prop)
{
    dbref what, where;
    const char *ptr, *msg2;
    char buf[BUFFER_LEN];

    for (what = 0; what < db_top; what++) {
        if (Typeof(what) == TYPE_THING && (FLAGS(what) & ZOMBIE)) {
            if (OWNER(what) == player) {
                where = getloc(what);
                if ((!Dark(where)) && (!Dark(player)) && (!Dark(what))) {
                    msg2 = msg;
                    if ((ptr = (char *) get_property_class(what, prop)) && *ptr)
                        msg2 = ptr;
                    SPRINTF(buf, CMOVE "%.512s %.3000s", "%.512U %.3000U",
                            PNAME(what), msg2);
                    anotify_except(DBFETCH(where)->contents, what, buf, what);
                }
            }
        }
    }
}

void
announce_connect(int descr, dbref player)
{
    dbref loc;
    char buf[BUFFER_LEN];
    struct match_data md;
    dbref exit;

    if ((loc = getloc(player)) == NOTHING)
        return;

    total_loggedin_connects++;

    if (Guest(player)) {
        FLAGS(player) &= ~CHOWN_OK;
    }

    DBFETCH(player)->sp.player.last_descr = descr;

    /*
     * See if there's a connect action.  If so, and the player is the first to
     * connect, send the player through it.  If the connect action is set
     * sticky, then suppress the normal look-around.
     */

    /* queue up all _connect programs referred to by properties */
    envpropqueue(descr, player, getloc(player), NOTHING, player, NOTHING,
                 "@connect", "Connect", 1, 1);
    envpropqueue(descr, player, getloc(player), NOTHING, player, NOTHING,
                 "@oconnect", "Oconnect", 1, 0);
    envpropqueue(descr, player, getloc(player), NOTHING, player, NOTHING,
                 "~connect", "Connect", 1, 1);
    envpropqueue(descr, player, getloc(player), NOTHING, player, NOTHING,
                 "~oconnect", "Oconnect", 1, 0);
    if (tp_allow_old_trigs) {
        envpropqueue(descr, player, getloc(player), NOTHING, player, NOTHING,
                     "_connect", "Connect", 1, 1);
        envpropqueue(descr, player, getloc(player), NOTHING, player, NOTHING,
                     "_oconnect", "Oconnect", 1, 0);
    }

    exit = NOTHING;
    if (online(player) == 1) {
        init_match(descr, player, "connect", TYPE_EXIT, &md); /* match for connect */
        md.match_level = 1;
        match_all_exits(&md);
        exit = match_result(&md);
        if (exit == AMBIGUOUS)
            exit = NOTHING;
    }

    if (exit != NOTHING)
        do_move(descr, player, "connect", 1);

    if (exit == NOTHING
        || !((Typeof(exit) == TYPE_EXIT) && (FLAGS(exit) & STICKY))) {
        if (can_move(descr, player, "look", 1)) {
            do_move(descr, player, "look", 1);
        } else {
            do_look_around(descr, player);
        }
    }

    if ((loc = getloc(player)) == NOTHING)
        return;

    if (!tp_quiet_connects) {
        if ((!Dark(player)) && (!Dark(loc)) && (!Hidden(player))) {
            if (online(player) == 1)
                sprintf(buf, CMOVE "%s has connected.", PNAME(player));
            else
                sprintf(buf, CMOVE "%s has reconnected.", PNAME(player));
            anotify_except(DBFETCH(loc)->contents, player, buf, player);
        }

        if (online(player) == 1 && (!Hidden(player))) {
            announce_puppets(player, "wakes up.", "_/pcon");
        }
    }

    ts_useobject(player, player);
    return;
}

void
announce_disconnect(struct descriptor_data *d)
{
    dbref player = d->player;
    dbref loc;
    char buf[BUFFER_LEN];

    if (!d->connected || !OkObj(player) || (Typeof(player) != TYPE_PLAYER) ||
        ((loc = getloc(player)) == NOTHING)
        )
        return;

    total_loggedin_connects--;

    if (dequeue_prog(player, 2))
        anotify(player, CINFO "Foreground program aborted.");
    if (!tp_quiet_connects) {
        if ((!Dark(player)) && (!Dark(loc)) && (!Hidden(player))) {
            if (online(player) == 1)
                sprintf(buf, CMOVE "%s has disconnected.", PNAME(player));
            else
                sprintf(buf, CMOVE "%s has dropped a connection.",
                        PNAME(player));
            anotify_except(DBFETCH(loc)->contents, player, buf, player);
        }
    }

    forget_player_descr(d->player, d->descriptor);
    d->connected = 0;
    d->player = NOTHING;
    update_desc_count_table();

    if (!online(player) && (FLAG2(player) & F2IDLE)) {
        FLAG2(player) &= ~F2IDLE;
    }

    if (!online(player) && (FLAG2(player) & F2TRUEIDLE)) {
        FLAG2(player) &= ~F2TRUEIDLE;
    }

    /* trigger local disconnect action */
    /* queue up all _connect programs referred to by properties */
    envpropqueue(d->descriptor, player, getloc(player), NOTHING,
                 player, NOTHING, "@disconnect", "Disconnect", 1, 1);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING,
                 player, NOTHING, "@odisconnect", "Odisconnect", 1, 0);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING,
                 player, NOTHING, "~disconnect", "Disconnect", 1, 1);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING,
                 player, NOTHING, "~odisconnect", "Odisconnect", 1, 0);
    if (tp_allow_old_trigs) {
        envpropqueue(d->descriptor, player, getloc(player),
                     NOTHING, player, NOTHING, "_disconnect",
                     "Disconnect", 1, 1);
        envpropqueue(d->descriptor, player, getloc(player),
                     NOTHING, player, NOTHING,
                     "_odisconnect", "Odisconnect", 1, 0);
    }
    if (!online(player)) {
        if (can_move(d->descriptor, player, "disconnect", 1)) {
            do_move(d->descriptor, player, "disconnect", 1);
        }
#ifdef MCP_SUPPORT
        gui_dlog_closeall_descr(d->descriptor);
#endif
        if (!Hidden(player)) {
            announce_puppets(player, "falls asleep.", "_/pdcon");
        }
    }
    ts_lastuseobject(player, player);
    DBDIRTY(player);
}


void
announce_idle(struct descriptor_data *d)
{
    dbref player = d->player;
    dbref loc;
    char buf[BUFFER_LEN];

    if (!(DR_RAW_FLAGS(d, DF_TRUEIDLE)))
        DR_RAW_ADD_FLAGS(d, DF_TRUEIDLE);

    if (!d->connected || (loc = getloc(player)) == NOTHING)
        return;

    if ((FLAG2(player) & F2TRUEIDLE))
        return;

    FLAG2(player) |= F2TRUEIDLE;

    if ((!Dark(player)) && (!Dark(loc)) && (tp_enable_idle_msgs)) {
        sprintf(buf, CMOVE "%s has become terminally idle.", PNAME(player));
        anotify_except(DBFETCH(loc)->contents, player, buf, player);
    }

    /* queue up all _idle programs referred to by properties */
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "@idle", "Idle", 1, 1);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "@oidle", "Oidle", 1, 0);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "~idle", "Idle", 1, 1);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "~oidle", "Oidle", 1, 0);
    if (tp_user_idle_propqueue) {
        envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                     NOTHING, "_idle", "Idle", 1, 1);
        envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                     NOTHING, "_oidle", "Oidle", 1, 0);
    }
}

void
announce_unidle(struct descriptor_data *d)
{
    dbref player = d->player;
    dbref loc;
    char buf[BUFFER_LEN];

    if ((DR_RAW_FLAGS(d, DF_TRUEIDLE)))
        DR_RAW_REM_FLAGS(d, DF_TRUEIDLE);

    if (!d->connected || (loc = getloc(player)) == NOTHING)
        return;

    if (!(FLAG2(player) & F2TRUEIDLE))
        return;

    FLAG2(player) &= ~F2TRUEIDLE;

    if ((!Dark(player)) && (!Dark(loc)) && (tp_enable_idle_msgs)) {
        sprintf(buf, CMOVE "%s has unidled.", PNAME(player));
        anotify_except(DBFETCH(loc)->contents, player, buf, player);
    }

    if (OkObj(d->player) ? (Typeof(d->player) & TYPE_PLAYER) : 0) {
        DBFETCH(d->player)->sp.player.last_descr = d->descriptor;
    }

    /* queue up all _unidle programs referred to by properties */
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "@unidle", "Unidle", 1, 1);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "@ounidle", "Ounidle", 1, 0);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "~unidle", "Unidle", 1, 1);
    envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                 NOTHING, "~ounidle", "Ounidle", 1, 0);
    if (tp_user_idle_propqueue) {
        envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                     NOTHING, "_unidle", "Unidle", 1, 1);
        envpropqueue(d->descriptor, player, getloc(player), NOTHING, player,
                     NOTHING, "_ounidle", "Ounidle", 1, 0);
    }
}


void
announce_login(struct descriptor_data *d)
{
    dbref player = 0;

    if (d->connected)
        return;

    /* queue up all _login programs referred to by properties */
    propqueue(d->descriptor, player, 0, 0, 0, 0, "@login", "Login", 1, 1);
    propqueue(d->descriptor, player, 0, 0, 0, 0, "@ologin", "Ologin", 1, 0);
    propqueue(d->descriptor, player, 0, 0, 0, 0, "~login", "Login", 1, 1);
    propqueue(d->descriptor, player, 0, 0, 0, 0, "~ologin", "Ologin", 1, 0);
    propqueue(d->descriptor, player, 0, 0, 0, 0, "_login", "Login", 1, 1);
    propqueue(d->descriptor, player, 0, 0, 0, 0, "_ologin", "Ologin", 1, 0);
}


void
announce_disclogin(struct descriptor_data *d)
{
    dbref player = 0;

    if (!d)
        return;
    if (d->connected)
        return;
    if (d->player != NOTHING)
        return;
    if (d->did_connect)
        return;
    dequeue_prog_descr(d->descriptor, 2);
    /* queue up all _login programs referred to by properties */
    propqueue(d->descriptor, player, 0, 0, 0, 0,
              "@disclogin", "Disclogin", 1, 1);
    propqueue(d->descriptor, player, 0, 0, 0, 0,
              "@odisclogin", "Odisclogin", 1, 0);
    propqueue(d->descriptor, player, 0, 0, 0, 0,
              "~disclogin", "Disclogin", 1, 1);
    propqueue(d->descriptor, player, 0, 0, 0, 0,
              "~odisclogin", "Odisclogin", 1, 0);
    propqueue(d->descriptor, player, 0, 0, 0, 0,
              "_disclogin", "Disclogin", 1, 1);
    propqueue(d->descriptor, player, 0, 0, 0, 0,
              "_odisclogin", "Odisclogin", 1, 0);
}



#ifdef MUD_ID
void
do_setuid(char *name)
{
#include <pwd.h>
    struct passwd *pw;

    if ((pw = getpwnam(name)) == NULL) {
        log_status("can't get pwent for %s\n", name);
        exit(1);
    }
    if (setuid(pw->pw_uid) == -1) {
        log_status("can't setuid(%d): ", pw->pw_uid);
        perror("setuid");
        exit(1);
    }
}

#endif /* MUD_ID */

struct descriptor_data *descr_count_table[MAX_SOCKETS];
int current_descr_count = 0;

void
init_descr_count_lookup(void)
{
    int i;

    for (i = 0; i < MAX_SOCKETS; i++)
        descr_count_table[i] = NULL;
}

void
update_desc_count_table(void)
{
    int c;
    struct descriptor_data *d;

    current_descr_count = 0;
    for (c = 0, d = descriptor_list; d; d = d->next) {
        if (d->connected) {
            d->con_number = c + 1;
            descr_count_table[c++] = d;
            current_descr_count++;
        }
    }
}

struct descriptor_data *
descrdata_by_count(int count)
{
    count--;
    if (count >= current_descr_count || count < 0)
        return NULL;

    return descr_count_table[count];
}

struct descriptor_data *descr_lookup_table[MAX_SOCKETS];

void
init_descriptor_lookup(void)
{
    int i;

    for (i = 0; i < MAX_SOCKETS; i++)
        descr_lookup_table[i] = NULL;
}

int
index_descr(int index)
{
    if ((index < 0) || (index >= MAX_SOCKETS))
        return -1;
    if (descr_lookup_table[index] == NULL)
        return -1;

    return descr_lookup_table[index]->descriptor;
}

int
descr_index(int descr)
{
    int i;

    for (i = 0; i < MAX_SOCKETS; i++)
        if (descr_lookup_table[i]
            && (descr_lookup_table[i]->descriptor == descr))
            return i;

    return -1;
}

int *
get_player_descrs(dbref player, int *count)
{
    int *darr;

    if (Typeof(player) == TYPE_PLAYER) {
        *count = DBFETCH(player)->sp.player.descr_count;
        darr = DBFETCH(player)->sp.player.descrs;
        if (!darr) {
            *count = 0;
        }
        return darr;
    } else {
        *count = 0;
        return NULL;
    }
}



void
remember_player_descr(dbref player, int descr)
{
    int count = DBFETCH(player)->sp.player.descr_count;
    int *arr = DBFETCH(player)->sp.player.descrs;
    int index;

    index = descr_index(descr);

    if ((index < 0) || (index >= MAX_SOCKETS))
        return;

    if (!arr) {
        arr = (int *) malloc(sizeof(int));
        arr[0] = index;
        count = 1;
    } else {
        arr = (int *) realloc(arr, sizeof(int) * (count + 1));
        arr[count] = index;
        count++;
    }
    DBFETCH(player)->sp.player.descr_count = count;
    DBFETCH(player)->sp.player.descrs = arr;
}

void
forget_player_descr(dbref player, int descr)
{
    int count = DBFETCH(player)->sp.player.descr_count;
    int *arr = DBFETCH(player)->sp.player.descrs;
    int index;

    index = descr_index(descr);

    if ((index < 0) || (index >= MAX_SOCKETS))
        return;

    if (!arr) {
        count = 0;
    } else if (count > 1) {
        int src, dest;

        for (src = dest = 0; src < count; src++) {
            if (arr[src] != index) {
                if (src != dest) {
                    arr[dest] = arr[src];
                }
                dest++;
            }
        }
        if (dest != count) {
            count = dest;
            arr = (int *) realloc(arr, sizeof(int) * count);
        }
    } else {
        free((void *) arr);
        arr = NULL;
        count = 0;
    }
    DBFETCH(player)->sp.player.descr_count = count;
    DBFETCH(player)->sp.player.descrs = arr;
}

int
remember_descriptor(struct descriptor_data *d)
{
    int i;

    if (!d)
        return -1;

    for (i = 0; i < MAX_SOCKETS; i++) {
        if (descr_lookup_table[i] == NULL) {
            descr_lookup_table[i] = d;
            return i;
        }
    }

    return -1;
}

void
forget_descriptor(struct descriptor_data *d)
{
    int i;

    if (!d)
        return;

    for (i = 0; i < MAX_SOCKETS; i++)
        if (descr_lookup_table[i] == d)
            descr_lookup_table[i] = NULL;
}

struct descriptor_data *
lookup_descriptor(int index)
{
    if (index >= MAX_SOCKETS || index < 0)
        return NULL;

    return descr_lookup_table[index];
}


struct descriptor_data *
descrdata_by_index(int index)
{
    return lookup_descriptor(index);
}

struct descriptor_data *
descrdata_by_descr(int descr)
{
    return lookup_descriptor(descr_index(descr));
}


/*** JME ***/
int
online(dbref player)
{
    return DBFETCH(player)->sp.player.descr_count;
}

int
pcount(void)
{
    return current_descr_count;
}

time_t
pidle(int count)
{
    struct descriptor_data *d;
    time_t now;

    d = descrdata_by_count(count);

    if (d) {
        now = current_systime;
        return (now - d->last_time);
    }

    return -1;
}

dbref
pdbref(int count)
{
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d)
        return (d->player);

    return NOTHING;
}

time_t
pontime(int count)
{
    struct descriptor_data *d;
    time_t now;

    d = descrdata_by_count(count);

    now = current_systime;
    if (d)
        return (now - d->connected_at);

    return -1;
}

char *
phost(int count)
{
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d)
        return ((char *) d->hu->h->name);

    return (char *) NULL;
}

char *
puser(int count)
{
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d)
        return ((char *) d->hu->u->user);

    return (char *) NULL;
}

/*** Foxen ***/
int
least_idle_player_descr(dbref who)
{
    struct descriptor_data *d;
    struct descriptor_data *best_d = NULL;
    int dcount, di;
    int *darr;
    time_t best_time = 0;

    darr = get_player_descrs(who, &dcount);
    for (di = 0; di < dcount; di++) {
        d = descrdata_by_index(darr[di]);
        if (d && (!best_time || d->last_time > best_time)) {
            best_d = d;
            best_time = d->last_time;
        }
    }
    if (best_d) {
        return best_d->con_number;
    }
    return 0;

}


int
most_idle_player_descr(dbref who)
{
    struct descriptor_data *d;
    struct descriptor_data *best_d = NULL;
    int dcount, di;
    int *darr;
    time_t best_time = 0;

    darr = get_player_descrs(who, &dcount);
    for (di = 0; di < dcount; di++) {
        d = descrdata_by_index(darr[di]);
        if (d && (!best_time || d->last_time < best_time)) {
            best_d = d;
            best_time = d->last_time;
        }
    }
    if (best_d) {
        return best_d->con_number;
    }
    return 0;

}


char *
pipnum(int count)
{
    static char ipnum[40];
    const char *p;
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d) {
        p = host_as_hex(d->hu->h->a);
        strcpy(ipnum, p);
        return ((char *) ipnum);
    }

    return (char *) NULL;
}

char *
pport(int count)
{
    static char port[40];
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d) {
        sprintf(port, "%d", d->hu->u->uport);
        return ((char *) port);
    }

    return (char *) NULL;
}

/*** Foxen ***/
int
pfirstconn(dbref who)
{
    int i;

    i = dbref_first_descr(who);
    if (i > 0)
        return i;
    else
        return 0;
}


void
pboot(int count)
{
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d) {
        process_output(d);
        d->booted = BOOT_DROP;
        /* shutdownsock(d); */
    }
}

void
pdboot(int c)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(c);

    if (d) {
        process_output(d);
        d->booted = BOOT_DROP;
        /* shutdownsock(d); */
    }
}


void
pnotify(int count, char *outstr)
{
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d) {
        queue_ansi(d, outstr);
        queue_write(d, "\r\n", 2);
    }
}


int
pdescr(int count)
{
    struct descriptor_data *d;

    d = descrdata_by_count(count);

    if (d)
        return d->descriptor;

    return -1;
}


int
pdescrcount(void)
{
    struct descriptor_data *d;
    int icount = 0;

    d = descriptor_list;
    while (d) {
        d = d->next;
        icount++;
    }
    return icount;
}


int
pfirstdescr(void)
{
    struct descriptor_data *d;

    d = descriptor_list;

    if (d)
        return d->descriptor;

    return -1;
}


int
plastdescr(void)
{
    struct descriptor_data *d;
    int curdescr = 0;

    d = descriptor_list;

    while (d) {
        curdescr = d->descriptor;
        d = d->next;
    }

    return curdescr;
}


int
pnextdescr(int descr)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(descr);
    if (d)
        d = d->next;
/*    while (d && (!d->connected))
	d = d->next; */
    if (d)
        return d->descriptor;

    return 0;
}

int
pset_idletime(dbref player, int idle_time)
{
    int dcount;
    int *darr;
    struct descriptor_data *d;

    darr = get_player_descrs(player, &dcount);
    if (dcount > 0) {
        d = descrdata_by_index(darr[dcount - 1]);
        d->idletime_set = idle_time;
    } else {
        dcount = 0;
    }
    return dcount;
}

int
pdescrsecure(int c)
{
#ifdef USE_SSL
    struct descriptor_data *d;

    d = descrdata_by_descr(c);

    if (d && d->ssl_session)
        return 1;
    else
        return 0;
#else
    return 0;
#endif
}

int
pdescrcon(int c)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(c);
    if (d) {
        return d->con_number;
    } else {
        return 0;
    }
}


int
pset_user(struct descriptor_data *d, dbref who)
{
    if (d && d->connected) {
        announce_disconnect(d);
        if (who != NOTHING) {
            d->player = who;
            d->connected = 1;
/*          d->connected_at = current_systime; */
            update_desc_count_table();
            remember_player_descr(who, d->descriptor);
            if ((d->encoding != ENC_ASCII) &&
                    (d->encoding != ENC_LATIN1 || tp_ascii_descrs)) {
                anotify_fmt(d->player, CINFO "Negotiated Encoding: " CNOTE "%s",
                        (d->encoding == ENC_LATIN1 ? "ISO-8859-1 (Latin1)" :
                         (d->encoding == ENC_IBM437 ? "IBM437" :
                          (d->encoding == ENC_UTF8 ? "UTF-8" : "UNKNOWN" ))));
            }
            announce_connect(d->descriptor, who);
            if (d->type == CT_PUEBLO) {
                FLAG2(d->player) |= F2PUEBLO;
            } else {
                FLAG2(d->player) &= ~F2PUEBLO;
            }
        }
        return 1;
    }
    return 0;
}

int
plogin_user(struct descriptor_data *d, dbref who)
{
    if (!d || d->connected)
        return 0;

    if (who == NOTHING)
        return 0;

    if (who != NOTHING) {
        d->player = who;
        d->connected = 1;
/*          d->connected_at = current_systime; */
        spit_file(d->player, MOTD_FILE);
        interact_warn(d->player);
        if (sanity_violated && TMage(d->player)) {
            notify(d->player, MARK "WARNING!  The DB appears to be corrupt!");
        }
        update_desc_count_table();
        remember_player_descr(who, d->descriptor);
        if ((d->encoding != ENC_ASCII) &&
                (d->encoding != ENC_LATIN1 || tp_ascii_descrs)) {
            anotify_fmt(d->player, CINFO "Negotiated Encoding: " CNOTE "%s",
                    (d->encoding == ENC_LATIN1 ? "ISO-8859-1 (Latin1)" :
                     (d->encoding == ENC_IBM437 ? "IBM437" :
                      (d->encoding == ENC_UTF8 ? "UTF-8" : "UNKNOWN" ))));
        }
        announce_connect(d->descriptor, who);
        if (d->type == CT_PUEBLO) {
            FLAG2(d->player) |= F2PUEBLO;
        } else {
            FLAG2(d->player) &= ~F2PUEBLO;
        }
    }
    return 1;
}

int
pset_user2(int c, dbref who)
{

    struct descriptor_data *d;
    int result;

    d = descrdata_by_descr(c);
    if (!d)
        return 0;

    if (d->connected)
        result = pset_user(d, who);
    else
        result = plogin_user(d, who);
    d->booted = 0;
    if (d->type == CT_MUF) {
        // Check to see if DF_TELNET was applied to the connection already. 
        // If not, initialize the telnet stuff now.
        if (!DR_RAW_FLAGS(d, DF_TELNET)) {
            DR_RAW_ADD_FLAGS(d, DF_TELNET);
            if (tp_ascii_descrs)
                d->encoding = ENC_ASCII;
            telopt_advertise(d);
        }
        d->type = CT_MUCK;
    }
    return result;
}

void
silent_connect(int descr, dbref player)
{
    dbref loc;
    char buf[BUFFER_LEN];

    if ((loc = getloc(player)) == NOTHING)
        return;


    if (Guest(player)) {
        FLAGS(player) &= ~CHOWN_OK;
    }
    DBFETCH(player)->sp.player.last_descr = descr;
    if (!tp_quiet_connects) {
        if ((!Dark(player)) && (!Dark(loc)) && (!Hidden(player))) {
            if (online(player) == 1)
                sprintf(buf, CMOVE "%s has connected.", PNAME(player));
            else
                sprintf(buf, CMOVE "%s has reconnected.", PNAME(player));
            anotify_except(DBFETCH(loc)->contents, player, buf, player);
        }
    }
    return;
}

void
silent_disconnect(struct descriptor_data *d)
{
    dbref player = d->player;
    dbref loc;
    char buf[BUFFER_LEN];

    if (!d->connected || !OkObj(player) || (Typeof(player) != TYPE_PLAYER) ||
        ((loc = getloc(player)) == NOTHING)
        )
        return;

    if (!tp_quiet_connects) {
        if ((!Dark(player)) && (!Dark(loc)) && (!Hidden(player))) {
            if (online(player) == 1)
                sprintf(buf, CMOVE "%s has disconnected.", PNAME(player));
            else
                sprintf(buf, CMOVE "%s has dropped a connection.",
                        PNAME(player));
            anotify_except(DBFETCH(loc)->contents, player, buf, player);
        }
    }

    if (!online(player) && (FLAG2(player) & F2IDLE)) {
        FLAG2(player) &= ~F2IDLE;
    }
    if (!online(player) && (FLAG2(player) & F2TRUEIDLE)) {
        FLAG2(player) &= ~F2TRUEIDLE;
    }
    DBDIRTY(player);
}

void
transfer_descr_to_player(int descr, dbref source, dbref target)
{
    int count = DBFETCH(target)->sp.player.descr_count;
    int *arr = DBFETCH(target)->sp.player.descrs;
    int index;

    index = descr_index(descr);

    if ((index < 0) || (index >= MAX_SOCKETS))
        return;

    if (!arr) {
        arr = (int *) malloc(sizeof(int));
        arr[0] = index;
        count = 1;
    } else {
        arr = (int *) realloc(arr, sizeof(int) * (count + 1));
        arr[count] = index;
        count++;
    }

    DBFETCH(target)->sp.player.descr_count = count;
    DBFETCH(target)->sp.player.descrs = arr;
    count = DBFETCH(source)->sp.player.descr_count;
    arr = DBFETCH(source)->sp.player.descrs;

    if (!arr) {
        count = 0;
    } else if (count > 1) {
        int src, dest;

        for (src = dest = 0; src < count; src++) {
            if (arr[src] != index) {
                if (src != dest) {
                    arr[dest] = arr[src];
                }
                dest++;
            }
        }
        if (dest != count) {
            count = dest;
            arr = (int *) realloc(arr, sizeof(int) * count);
        }
    } else {
        free((void *) arr);
        arr = NULL;
        count = 0;
    }
    DBFETCH(source)->sp.player.descr_count = count;
    DBFETCH(source)->sp.player.descrs = arr;
}

int
pset_user_suid(int c, dbref who)
{

    struct descriptor_data *d;

    if (who == NOTHING)
	return 0;

    d = descrdata_by_descr(c);
    if (!d)
        return 0;

    if (!d->connected)
        return 0;
    d->booted = 0;

    /* If this descriptor is assigned to a player, silently disconnect this descriptor from the player. */
    if (d->player != NOTHING) {
        total_loggedin_connects--;
	silent_disconnect(d);
	transfer_descr_to_player(d->descriptor, d->player, who);
    }
    
    d->player = who;
    silent_connect(d->descriptor, who);
    total_loggedin_connects++;
    update_desc_count_table();
    DR_ADD_FLAGS(d->descriptor, DF_SUID);

    if (d->type == CT_PUEBLO) {
        FLAG2(d->player) |= F2PUEBLO;
    } else {
        FLAG2(d->player) &= ~F2PUEBLO;
    }
    return 1;
}
int
dbref_first_descr(dbref c)
{
    int dcount;
    int *darr;
    struct descriptor_data *d;

    darr = get_player_descrs(c, &dcount);
    if (dcount > 0) {
        d = descrdata_by_index(darr[dcount - 1]);
        return d->descriptor;
    } else {
        return NOTHING;
    }
}

#ifdef MCP_SUPPORT

McpFrame *
descr_mcpframe(int c)
{
    struct descriptor_data *d;

    d = descriptor_list;
    while (d && d->connected) {
        if (d->descriptor == c) {
            return &d->mcpframe;
        }
        d = d->next;
    }
    return NULL;
}

#endif

int
pdescrflush(int c)
{
    struct descriptor_data *d;
    int i = 0;

    if (c != -1) {
        d = descrdata_by_descr(c);
        if (d) {
            if (!process_output(d)) {
                d->booted = BOOT_DROP;
            }
            i++;
        }
    } else {
        for (d = descriptor_list; d; d = d->next) {
            if (!process_output(d)) {
                d->booted = BOOT_DROP;
            }
            i++;
        }
    }
    return i;
}

int
pdescrtype(int c)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(c);
    if (d && d->descriptor == c)
        return d->type;
    else
        return -1;
}

int
pdescrp(int c)
{
    struct descriptor_data *d;

    for (d = descriptor_list; d && (d->descriptor != c); d = d->next) ;
/*    d = descrdata_by_descr(c); */
    if (d && d->descriptor == c)
        return 1;
    else
        return 0;
}

void
pdescr_welcome_user(int c)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(c);
    if (d && d->descriptor == c)
        welcome_user(d);
    return;
}

void
pdescr_logout(int c)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(c);
    if (d && d->descriptor == c && d->player != NOTHING) {
        log_status("LOGOUT: %2d %s %s(%s) %s P#%d\n",
                   d->descriptor, unparse_object(1, d->player),
                   d->hu->h->name, d->hu->u->user,
                   host_as_hex(d->hu->h->a), d->cport);
        announce_disconnect(d);
        d->connected = 0;
        d->player = NOTHING;
    }
    return;
}

int
pdescrbufsize(int c)
{
    struct descriptor_data *d;

    d = descrdata_by_descr(c);

    if (d) {
        return (tp_max_output - d->output_size);
    }
    return -1;
}

dbref
partial_pmatch(const char *name)
{
    struct descriptor_data *d;
    dbref last = NOTHING;

    d = descriptor_list;
    while (d) {
        if (d->connected && (last != d->player) &&
            (string_prefix(NAME(d->player), name) ||
             lookup_alias(name,1) != NOTHING) ) {
            if (last != NOTHING) {
                last = AMBIGUOUS;
                break;
            }
            last = d->player;
        }
        d = d->next;
    }
    return (last);
}

void
welcome_user(struct descriptor_data *d)
{
    FILE *f;
    char *ptr;
    char buf[BUFFER_LEN];
    const char *fname;
    int buflen = 0;
    char *outbuf = NULL;
    // Used for .ans welcome screens
    bool EOF_seen = 0;
    int welcome_encoding = ENC_ASCII;
    char ansbuf[BUFFER_LEN];
    char *buf_p = buf;
    char *ansbuf_p = ansbuf;
    size_t bytesin;
    size_t bytesout;

    ansbuf[0] = '\0';

    if (d->type == CT_PUEBLO) {
        queue_ansi(d, "\r\nThis world is Pueblo 1.0 Enhanced.\r\n\r\n");
        queue_ansi(d, "</xch_mudtext><img xch_mode=html>");

        fname = reg_site_welcome(d->hu->h->a);
        if (fname && (*fname == '.')) {
            strcpy(buf, WELC_FILE);
        } else if (fname && (*fname == '#')) {
            if (tp_rand_screens > 0)
                sprintf(buf, "data/welcome%d.txt",
                        (rand() % tp_rand_screens) + 1);
            else
                strcpy(buf, WELC_HTML);
        } else if (fname) {
            strcpy(buf, "data/welcome/");
            ptr = buf + strlen(buf);
            while (*fname && (*fname != ' '))
                (*(ptr++)) = (*(fname++));
            *ptr = '\0';
            strcat(buf, ".txt");
        } else if (reg_site_is_barred(d->hu->h->a) == TRUE) {
            strcpy(buf, BARD_FILE);
        } else if (tp_rand_screens > 0) {
            sprintf(buf, "data/welcome%d.txt", (rand() % tp_rand_screens) + 1);
        } else {
            strcpy(buf, WELC_HTML);
        }
        strcpy(buf, WELC_HTML);
#ifdef MCP_SUPPORT
        mcp_negotiation_start(&d->mcpframe, d);
#endif
        if ((f = fopen(buf, "r")) == NULL) {
            queue_unhtml(d, DEFAULT_WELCOME_MESSAGE);
            perror("spit_file: welcome.html");
        } else {
            while (fgets(buf, sizeof buf, f)) {
                ptr = index(buf, '\n');
                if (ptr && ptr > buf && *(ptr - 1) != '\r') {
                    *ptr++ = '\r';
                    *ptr++ = '\n';
                    *ptr++ = '\0';
                }
                queue_html(d, buf);
            }
            fclose(f);
        }
        if (delayed_shutdown) {
            queue_unhtml(d, MARK
                         "<b>The game is currently shutting down, and only wizards can connect.</b>\r\n");
        } else if (wizonly_mode) {
            queue_unhtml(d, MARK
                         "<b>Due to maintenance, only wizards can connect now.</b>\r\n");

        } else if (tp_playermax && con_players_curr >= tp_playermax_limit) {
            queue_unhtml(d, WARN_MESG);
            queue_unhtml(d, "\r\n");
        }
    }
    if (d->type == CT_MUCK
#ifdef USE_SSL
        || d->type == CT_SSL
#endif /* USE_SSL */
        ) {
        fname = reg_site_welcome(d->hu->h->a);
        if (fname && (*fname == '.')) {
            strcpy(buf, WELC_FILE);
            buflen = sizeof WELC_FILE - 1;
        } else if (fname && (*fname == '#')) {
            if (tp_rand_screens > 0) {
                buflen = sprintf(buf, "data/welcome%d.txt",
                        (rand() % tp_rand_screens) + 1);
            } else {
                strcpy(buf, WELC_FILE);
                buflen = sizeof WELC_FILE - 1;
            }
        } else if (fname) {
            strcpy(buf, "data/welcome/");
            ptr = buf + sizeof "data/welcome/" - 1;
            while (*fname && (*fname != ' '))
                (*(ptr++)) = (*(fname++));
            *ptr = '\0';
            strcpy(ptr, ".txt");
            buflen = ptr - buf + 3;
        } else if (reg_site_is_barred(d->hu->h->a) == TRUE) {
            strcpy(buf, BARD_FILE);
            buflen = sizeof BARD_FILE - 1;
        } else if (tp_rand_screens > 0) {
            buflen = sprintf(buf, "data/welcome%d.txt", (rand() % tp_rand_screens) + 1);
        } else {
            strcpy(buf, WELC_FILE);
            buflen = sizeof WELC_FILE - 1;
        }
#ifdef MCP_SUPPORT
        mcp_negotiation_start(&d->mcpframe, d);
#endif
        assert(buflen); // testing
        if (buflen >= 5 && (buflen + 1) < BUFFER_LEN ) {
            if (d->encoding == ENC_UTF8 &&
                    strcpy(buf + buflen - 3, "utf8") && (f = fopen(buf, "r"))) {
                welcome_encoding = ENC_UTF8;
            } else if ((d->encoding == ENC_IBM437 || d->encoding == ENC_UTF8) &&
                    strcpy(buf + buflen - 3, "ans") && (f = fopen(buf, "r"))) {
                welcome_encoding = ENC_IBM437;
            } else {
                strcpy(buf + buflen - 3, "txt");
                f = fopen(buf, "r");
            }
        } else {
            f = fopen(buf, "r");
        }
        if (f == NULL) {
            queue_unhtml(d, DEFAULT_WELCOME_MESSAGE);
            fprintf(stderr, "spit_file: %s\n", buf);
            //perror("spit_file: welcome.txt");
        } else {
            /* Rewritten to allow IBM437 and UTF-8 welcome sceen alternatives.
             * It also has better null and bounds checking. */
            while (fgets(buf, sizeof buf, f)) {
                if (welcome_encoding == ENC_IBM437 && d->encoding != ENC_IBM437) {
                    /* .ans files need a conversion pass if the encoding of the
                     * target descriptor is not ENC_IBM437. */
                    buf_p = buf;
                    ansbuf_p = ansbuf;
                    // fgets: most useless return ever.
                    bytesin = strlen(buf);
                    bytesout = sizeof ansbuf;

                    buflen = strlen(buf);

                    iconv_global = iconv_open("UTF-8", "IBM437");
                    iconv(iconv_global, &buf_p, &bytesin, &ansbuf_p, &bytesout);
                    iconv_close(iconv_global);
                    ptr = strpbrk(ansbuf, "\n\0\x1A");
                    outbuf = ansbuf;
                } else {
                    ptr = strpbrk(buf, "\n\0\x1A");
                    outbuf = buf;
                }
                if (*ptr == '\x1A') {
                    /* Truncate at EOF. Mostly used to prevent rendering the
                     * "SAUCE" of .ans files which have been loaded into the
                     * welcome screen for testing purposes. */
                    EOF_seen = 1;
                    *ptr = '\0';
                }
                if (ptr >= (outbuf + BUFFER_LEN - 3)) {
                    // truncate it
                    ptr = outbuf + BUFFER_LEN - 3;
                    *ptr++ = '\r';
                    *ptr = '\n';
                } else if (ptr != outbuf && *(ptr - 1) != '\r') {
                    *ptr++ = '\r';
                    *ptr = '\n';
                }
                *(ptr + 1) = '\0';
                queue_unhtml(d, outbuf);
                if (EOF_seen) {
                    break;
                }
            }
            fclose(f);
        }
        if (delayed_shutdown) {
            queue_unhtml(d, MARK
                         "The game is currently shutting down, and only wizards can connect.\r\n");
        } else if (wizonly_mode) {
            queue_unhtml(d, MARK
                         "Due to maintenance, only wizards can connect now.\r\n");
        } else if (tp_playermax && con_players_curr >= tp_playermax_limit) {
            queue_unhtml(d, WARN_MESG);
            queue_unhtml(d, "\r\n");
        }
    }
}

void
help_user(struct descriptor_data *d)
{

    FILE *f;
    char buf[BUFFER_LEN];

    if ((f = fopen("data/connect.txt", "r")) == NULL) {
        queue_ansi(d,
                   "The help file is missing, the management has been notified.\r\n");
        perror("spit_file: connect.txt");
    } else {
        while (fgets(buf, sizeof buf, f)) {
            queue_ansi(d, buf);
        }
        fclose(f);
    }
}

/* Any time we need to propagate a descr flag on every connection
 * a player has, this function will take care of it */
void propagate_descr_flag(dbref player, object_flag_type flag, int set)
{
    struct descriptor_data *d = NULL;
    int *darr = NULL;
    int dcount = 0;
    int i = 0;  
    
    darr = get_player_descrs(player, &dcount);
    for (i = 0; i < dcount; ++i ) {
        d = descrdata_by_index(darr[i]);
        if ( set ) {
            DR_RAW_ADD_FLAGS(d, flag);
        } else {
            DR_RAW_REM_FLAGS(d, flag);
        }
    }
}

#ifdef IGNORE_SUPPORT
/* Alynna: in-server ignore support */
void
init_ignore(dbref tgt)
{
    struct object *pobj = DBFETCH(tgt);
    const char *rawstr;
    int i = 0;

    if ((rawstr = get_property_class(tgt, "/@/ignore"))) {
        for (; *rawstr && isspace(*rawstr); rawstr++) ;

        /* extract dbrefs from the prop */
        for (; *rawstr && i < MAX_IGNORES; rawstr++) {
            if (*rawstr == '#')
                continue;

            if (!isdigit(*rawstr))
                break;

            pobj->sp.player.ignore[i] = atoi(rawstr);
            i++;

            for (; *rawstr && !isspace(*rawstr); rawstr++) ;
            for (; *rawstr && isspace(*rawstr); rawstr++) ;

            if (!*rawstr)
                break;
        }
    }

    /* terminate the array */
    if (i <= (MAX_IGNORES - 1))
        pobj->sp.player.ignore[i] = NOTHING;

    /* and set the timestamp */
    pobj->sp.player.ignoretime = current_systime;
    return;
}

char                            /* char, for when you only need a byte's worth. */
ignorance(dbref src, dbref tgt)
{
    struct object *tobj = DBFETCH(tgt);
    int i;

    /* We leave ignorance negatively if the dbref is invalid or the source is a wizard or theirselves. 
       This catches 99.999% of the cases where the system calls something through a standard player
       notify routine (with the src and target equal or the src being a random number), and wastes
       little time returning in that case.  We catch this case first so we can back out fast.
     */

    /* I doubt that these would ever be invalid. If they are, you have bigger problems. -Hinoserm */

    if (src == tgt || !OkObj(src) || Wizard(src))
        return 0;

    /* Ignorance cache and initialization 
     * Save time, only do the target, and only if it's been 10 secs from the last check or its not initialized.
     */
    if (tobj->sp.player.ignoretime + 10 <= current_systime)
        init_ignore(tgt);

    /* If ignore[0] is -1, its not even worth doing an ignore scan. */
    if (tobj->sp.player.ignore[0] == NOTHING)
        return 0;

    /* if we got this far, lets check it out, get out ASAP, BTW. */
    for (i = 0; i < MAX_IGNORES; i++) {
        if (!OkObj(tobj->sp.player.ignore[i]))
            return 0;
        if (OWNER(tobj->sp.player.ignore[i]) == OWNER(src))
            return 1;
    }

    return 0;
}
#endif


#ifdef MODULAR_SUPPORT

char module_error[BUFFER_LEN];

void
module_remember(struct module *m)
{
	if (modules)
		modules->prev = m;
	m->next = modules;
	modules = m;
}

char *
module_load(const char *filename, dbref who)
{
	struct module *m = (struct module *)malloc(sizeof(struct module));
	struct module *mo;
	void *(*func)();
	char *error;

	dlerror();
	m->handle = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);

	if (!m->handle && (error = dlerror()) != NULL) {
		free((void *)m);
		log_status("MODULE_LOAD(%s): ERROR %s\r\n", filename, error); 
		return error;
	}

	func = dlsym(m->handle, "__get_module_info");

	if ((error = dlerror()) != NULL)  {
		dlclose(m->handle);
		free((void *)m);
		log_status("MODULE_LOAD(%s): ERROR %s\r\n", filename, error);
		return error;
	}

	if (!(m->info = (*func)(m))) {
		dlclose(m->handle);
		free((void *)m);
		log_status("MODULE_LOAD(%s): ERROR %s\r\n", filename, module_error);
		return module_error;
	}

	mo = modules;
	while (mo) {
        if (mo->handle == m->handle) {
			strcpy(module_error, "module is already loaded");
			break;
		} else if (!string_compare(mo->info->name, m->info->name)) {
			strcpy(module_error, "module with same name is already loaded");
			dlclose(m->handle);
			break;
		}
		mo = mo->next;
	}

	if (mo) {
		log_status("MODULE_LOAD(%s): ERROR %s\r\n", m->info->name, module_error);
		free((void *)m);
		return module_error;
	}

	/* for (i = 0; mr->info->requires[i].name; i++) {
		
	} */


	m->prims = NULL;
	func = dlsym(m->handle, "__get_prim_list");

	if ((error = dlerror()) == NULL)  {	
		m->prims = (*func)();

		if (m->prims) {
			int i;
			for (i = 0; m->prims[i].name; i++) {
				m->prims[i].mod = m;
				m->prims[i].func = dlsym(m->handle, m->prims[i].sym);

				if ((error = dlerror()) != NULL)  {
					sprintf(module_error, "Bad Primitive (%s): %s", m->prims[i].name,  error);
					log_status("MODULE_LOAD(%s): ERROR %s\r\n", m->info->name, module_error);

					dlclose(m->handle);
					free((void *)m);
					return module_error;
				}

				log_status("MODULE(%s) Has PRIM_%s\r\n", filename, m->prims[i].name);
			}
		}
	}

	strcpy(module_error, "");

	m->progs = NULL;
	m->prev = NULL;
	m->next = NULL;

	module_remember(m);

	func = dlsym(m->handle, "__onload");

	if ((error = dlerror()) == NULL)  {	
        if (!(*func)()) {
            log_status("MODULE(%s) __onload function failed, automatically unloading.", filename);
            module_free(mo);
        }
    }


	return NULL;
}

void
module_free(struct module *m)
{
	void *(*func)();
	char *error;

	if (m->progs) {
		struct mod_proglist *mpr = m->progs;

		while (mpr) {
			struct mod_proglist *ompr = mpr->next;
			/* TODO: this needs to safely deal with loaded muf programs --hinoserm */
			uncompile_program(mpr->prog);

			//free((void *)mpr);
			mpr = ompr;
		}
	}

	func = dlsym(m->handle, "__onunload");

	if ((error = dlerror()) == NULL)  {	
        if (!(int)(*func)()) {
            log_status("MODULE_UNLOAD(%s) __onunload function failed.", m->info->name);
        }
    }

	if (m->next)
	   m->next->prev = m->prev;
	if (m->prev)
		m->prev->next = m->next;
	if (m == modules)
		modules = m->next;
	
	log_status("MODULE: Free'd %s.\r\n", m->info->name);
	dlclose(m->handle);
	free((void *)m);
}


#endif

int printfdbg(int rarg) {
    assert(rarg > -1 && "glibc bug tripped: operation would truncate a multi-byte sequence!");
    return rarg;
}
