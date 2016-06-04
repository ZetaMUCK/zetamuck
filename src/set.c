#include "copyright.h"
#include "config.h"

/* commands which set parameters */
#include "strings.h"

#include "db.h"
#include "params.h"
#include "tune.h"
#include "props.h"
#include "match.h"
#include "interface.h"
#include "externs.h"

char lflag_name[32][32];
int lflag_mlev[32];

static dbref
match_controlled(int descr, dbref player, const char *name)
{
    dbref match;
    struct match_data md;

    init_match(descr, player, name, NOTYPE, &md);
    match_absolute(&md);
    match_everything(&md);

    match = noisy_match_result(&md);
    if (match != NOTHING && !controls(player, match)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return NOTHING;
    } else {
        return match;
    }
}

void
do_name(int descr, dbref player, const char *name, char *newname)
{
    dbref thing;
    char *password;
    char *placeholder = NULL; 
    char oldName[BUFFER_LEN];
    char nName[BUFFER_LEN];
    struct match_data md;
    int failsafe = 0; 

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    init_match(descr, player, name, NOTYPE, &md);
    match_absolute(&md);
    match_everything(&md);

    thing = noisy_match_result(&md);

    if (thing != NOTHING && !(controls(player, thing)
                              || (Typeof(thing) == TYPE_PLAYER
                                  && (POWERS(player) & POW_PLAYER_CREATE)))) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }

    if (thing != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }

        /* check for bad name */
        if (*newname == '\0') {
            anotify_nolisten2(player, CINFO "Give it what new name?");
            return;
        }

        /* check for renaming a player */
        if (Typeof(thing) == TYPE_PLAYER) {
            if (tp_wiz_name && (!Mage(player)
                                && !(POWERS(player) & POW_PLAYER_CREATE))) {
                anotify_nolisten2(player,
                                  CINFO
                                  "Only wizards can change player names.");
                return;
            }
            
            /* split off password */
            /* Akari - 09/27/09 - @name for players now requires a
             * = sign to seperate off the password. */
            password = newname;
            while (1)
            {
                /* Infinite loop failsafe check */
                if ( failsafe++ > 100 )
                    break; /* More than 10 spaces would just be egregious, right? */

                /* Pass over the name. Find potential end */
                for (;*password && !isspace(*password) && *password != '='; password++) ;
                /* If we're out of string, break out of the loop */
                if (!(*password))
                    break;
 
                /* Mark this as a possible end of the entered name */
                if (*password)
                    placeholder = password;  
                
                /* See if we encountered a =. If so, terminate the string, advance
                   1 character, and break the loop. */
                if ( *password == '=' ) {
                    *placeholder = '\0';
                    password++;
                    break;
                }
 
                /* Scan ahead to find if a = comes next */
                while (*password && isspace(*password))
                    password++;

                /* Now see if we stumbled across a = or more characters */
                if ( *password && *password == '=' ) {
                    /* Mark the end of the name */
                    *placeholder = '\0';
                    password++; 
                    break; /* Get out of the loop */
                }
            }
                  
            /* eat whitespace */
            while (*password && isspace(*password))
                password++;
            
            /* check for null password */
            if (!*password) {
                anotify_nolisten2(player,
                                  CINFO
                                  "You must specify a password to change a player name.");
                anotify_nolisten2(player,
                                  CNOTE "E.g.: name player=newname=password");
                if (Wiz(OWNER(player)) || POWERS(player) & POW_PLAYER_CREATE)
                    anotify_nolisten2(player,
                                      SYSYELLOW
                                      "Wizards may use 'yes' for non-wizard players.");
                return;
            }
            if (!(Wiz(player) || POWERS(player) & POW_PLAYER_CREATE)
                || TMage(thing) || strcmp(password, "yes")) {
                if (!check_password(thing, password)) {
                    anotify_nolisten2(player, CFAIL "Incorrect password.");
                    return;
                }
            }
            if (string_compare(newname, NAME(thing))
                && !ok_player_name(newname)) {
                anotify_nolisten2(player,
                                  CFAIL
                                  "That name is either taken or invalid.");
                return;
            }
            /* everything ok, notify */
            log_status("NAME: %s(%d) to %s by %s\n",
                       NAME(thing), thing, newname, NAME(player));

            /* remove alias sharing the new name, if present. */
            clear_alias(0, newname);

            strcpy(oldName, NAME(thing));
            strcpy(nName, newname);
            delete_player(thing);
            if (NAME(thing))
                free((void *) NAME(thing));
            ts_modifyobject(player, thing);
            NAME(thing) = alloc_string(newname);
            add_player(thing);
            anotify_fmt(player,
                        CSUCC "Name changed from %s to %s.", oldName, nName);
            return;
        } else {
            if (!ok_name(newname)) {
                anotify_nolisten2(player,
                                  CFAIL "That is not a reasonable name.");
                return;
            }
        }

        /* everything ok, change the name */
        strcpy(oldName, NAME(thing));
        strcpy(nName, newname);
        if (NAME(thing)) {
            free((void *) NAME(thing));
        }
        ts_modifyobject(player, thing);
        NAME(thing) = alloc_string(newname);
        anotify_fmt(player,
                    CSUCC "Name changed from %s to %s.", oldName, nName);
        DBDIRTY(thing);
        if (Typeof(thing) == TYPE_EXIT && MLevel(thing)) {
            SetMLevel(thing, 0);
            anotify_nolisten2(player,
                              CINFO "Action priority Level reset to zero.");
        }
    }
}

void
do_describe(int descr, dbref player, const char *name, const char *description)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETDESC(thing, description);
        if (*description)
            anotify_nolisten2(player, CSUCC "Description set.");
        else
            anotify_nolisten2(player, CSUCC "Description cleared.");
    }
}

void
do_idescribe(int descr, dbref player, const char *name, const char *description)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETIDESC(thing, description);
        if (*description)
            anotify_nolisten2(player, CSUCC "IDescription set.");
        else
            anotify_nolisten2(player, CSUCC "IDescription cleared.");
    }
}


void
do_ansidescribe(int descr, dbref player, const char *name,
                const char *description)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETANSIDESC(thing, description);
        if (*description)
            anotify_nolisten2(player, CSUCC "ANSIDescription set.");
        else
            anotify_nolisten2(player, CSUCC "ANSIDescription cleared.");
    }
}

void
do_iansidescribe(int descr, dbref player, const char *name,
                 const char *description)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETIANSIDESC(thing, description);
        if (*description)
            anotify_nolisten2(player, CSUCC "IANSIDescription set.");
        else
            anotify_nolisten2(player, CSUCC "IANSIDescription cleared.");
    }
}


void
do_htmldescribe(int descr, dbref player, const char *name,
                const char *description)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETHTMLDESC(thing, description);
        if (*description)
            anotify_nolisten2(player, CSUCC "HTMLDescription set.");
        else
            anotify_nolisten2(player, CSUCC "HTMLDescription cleared.");
    }
}

void
do_ihtmldescribe(int descr, dbref player, const char *name,
                 const char *description)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETIHTMLDESC(thing, description);
        if (*description)
            anotify_nolisten2(player, CSUCC "IHTMLDescription set.");
        else
            anotify_nolisten2(player, CSUCC "IHTMLDescription cleared.");
    }
}

void
do_doing(int descr, dbref player, const char *name, const char *mesg)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if (*mesg) {
        thing = match_controlled(descr, player, name);
    } else {
        thing = player;
        mesg = name;
    }
    if (thing != NOTHING) {
        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        ts_modifyobject(player, thing);
        SETDOING(thing, mesg);
        if (*mesg)
            anotify_nolisten2(player, CSUCC "Doing set.");
        else
            anotify_nolisten2(player, CSUCC "Doing cleared.");
    }
}

void
do_fail(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETFAIL(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

void
do_success(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETSUCC(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

/* sets the drop message for player */
void
do_drop_message(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETDROP(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

void
do_osuccess(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETOSUCC(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

void
do_ofail(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETOFAIL(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

void
do_odrop(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETODROP(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

void
do_oecho(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETOECHO(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

void
do_pecho(int descr, dbref player, const char *name, const char *message)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        SETPECHO(thing, message);
        anotify_nolisten2(player, CSUCC "Message set.");
    }
}

/* sets a lock on an object to the lockstring passed to it.
   If the lockstring is null, then it unlocks the object.
   this returns a 1 or a 0 to represent success. */
int
setlockstr(int descr, dbref player, dbref thing, const char *keyname)
{
    struct boolexp *key;

    if (*keyname != '\0') {
        key = parse_boolexp(descr, player, keyname, 0);
        if (key == TRUE_BOOLEXP) {
            return 0;
        } else {
            /* everything ok, do it */
            ts_modifyobject(player, thing);
            SETLOCK(thing, key);
            return 1;
        }
    } else {
        ts_modifyobject(player, thing);
        CLEARLOCK(thing);
        return 1;
    }
}

void
do_conlock(int descr, dbref player, const char *name, const char *keyname)
{
    dbref thing;
    struct match_data md;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    init_match(descr, player, name, NOTYPE, &md);
    match_absolute(&md);
    match_everything(&md);

    switch (thing = match_result(&md)) {
        case NOTHING:
            anotify_nolisten2(player,
                              CINFO
                              "I don't see what you want to set the container-lock on!");
            return;
        case AMBIGUOUS:
            anotify_nolisten2(player,
                              CINFO
                              "I don't know which one you want to set the container-lock on!");
            return;
        default:
            if (!controls(player, thing)) {
                anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
                return;
            }
            break;
    }

    if (!*keyname) {
        PData pdat;

        pdat.flags = PROP_LOKTYP;
        pdat.data.lok = TRUE_BOOLEXP;
        set_property(thing, "_/clk", &pdat);
        ts_modifyobject(player, thing);
        anotify_nolisten2(player, CSUCC "Container lock cleared.");
    } else {
        PData pdat;

        pdat.data.lok = parse_boolexp(descr, player, keyname, 0);
        if (pdat.data.lok == TRUE_BOOLEXP) {
            anotify_nolisten2(player, CINFO "I don't understand that key.");
        } else {
            /* everything ok, do it */
            pdat.flags = PROP_LOKTYP;
            set_property(thing, "_/clk", &pdat);
            ts_modifyobject(player, thing);
            anotify_nolisten2(player, CSUCC "Container lock set.");
        }
    }
}

void
do_flock(int descr, dbref player, const char *name, const char *keyname)
{
    dbref thing;
    struct match_data md;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    init_match(descr, player, name, NOTYPE, &md);
    match_absolute(&md);
    match_everything(&md);

    switch (thing = match_result(&md)) {
        case NOTHING:
            anotify_nolisten2(player, CINFO "I don't see that here.");
            return;
        case AMBIGUOUS:
            anotify_nolisten2(player, CINFO "I don't know which one you mean!");
            return;
        default:
            if (!controls(player, thing)) {
                anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
                return;
            }
            break;
    }

    if (force_level) {
        anotify_nolisten2(player,
                          CFAIL
                          "You can't use @flock from an @force or {force}.");
        return;
    }

    if (!*keyname) {
        PData pdat;

        pdat.flags = PROP_LOKTYP;
        pdat.data.lok = TRUE_BOOLEXP;
        set_property(thing, "@/flk", &pdat);
        ts_modifyobject(player, thing);
        anotify_nolisten2(player, CSUCC "Force lock cleared.");
    } else {
        PData pdat;

        pdat.data.lok = parse_boolexp(descr, player, keyname, 0);
        if (pdat.data.lok == TRUE_BOOLEXP) {
            anotify_nolisten2(player, CINFO "I don't understand that key.");
        } else {
            /* everything ok, do it */
            pdat.flags = PROP_LOKTYP;
            set_property(thing, "@/flk", &pdat);
            ts_modifyobject(player, thing);
            anotify_nolisten2(player, CSUCC "Force lock set.");
        }
    }
}

void
do_chlock(int descr, dbref player, const char *name, const char *keyname)
{
    dbref thing;
    struct match_data md;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    init_match(descr, player, name, NOTYPE, &md);
    match_absolute(&md);
    match_everything(&md);

    switch (thing = match_result(&md)) {
        case NOTHING:
            anotify_nolisten2(player, CINFO "I don't see that here.");
            return;
        case AMBIGUOUS:
            anotify_nolisten2(player, CINFO "I don't know which one you mean!");
            return;
        default:
            if (!truecontrols(player, thing) &&
                !((FLAGS(thing) & CHOWN_OK) && controls(player, thing))) {
                anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
                return;
            }
            break;
    }

    if (!*keyname) {
        PData pdat;

        pdat.flags = PROP_LOKTYP;
        pdat.data.lok = TRUE_BOOLEXP;
        set_property(thing, CHLK_PROP, &pdat);
        ts_modifyobject(player, thing);
        anotify_nolisten2(player, CSUCC "Chown lock cleared.");
    } else {
        PData pdat;

        pdat.data.lok = parse_boolexp(descr, player, keyname, 0);
        if (pdat.data.lok == TRUE_BOOLEXP) {
            anotify_nolisten2(player, CINFO "I don't understand that key.");
        } else {
            /* everything ok, do it */
            pdat.flags = PROP_LOKTYP;
            set_property(thing, CHLK_PROP, &pdat);
            ts_modifyobject(player, thing);
            anotify_nolisten2(player, CSUCC "Chown lock set.");
        }
    }
}

void
do_lock(int descr, dbref player, const char *name, const char *keyname)
{
    dbref thing;
    struct boolexp *key;
    struct match_data md;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    init_match(descr, player, name, NOTYPE, &md);
    match_absolute(&md);
    match_everything(&md);

    switch (thing = match_result(&md)) {
        case NOTHING:
            anotify_nolisten2(player, CINFO "I don't see that here.");
            return;
        case AMBIGUOUS:
            anotify_nolisten2(player, CINFO "I don't know which one you mean!");
            return;
        default:
            if (!controls(player, thing)) {
                anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
                return;
            }
            break;
    }
    if (keyname && *keyname) {
        key = parse_boolexp(descr, player, keyname, 0);
        if (key == TRUE_BOOLEXP) {
            anotify_nolisten2(player, CINFO "I don't understand that key.");
        } else {
            /* everything ok, do it */
            SETLOCK(thing, key);
            ts_modifyobject(player, thing);
            anotify_nolisten2(player, CSUCC "Locked.");
        }
    } else
        do_unlock(descr, player, name);
}

void
do_unlock(int descr, dbref player, const char *name)
{
    dbref thing;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if ((thing = match_controlled(descr, player, name)) != NOTHING) {
        ts_modifyobject(player, thing);
        CLEARLOCK(thing);
        anotify_nolisten2(player, CSUCC "Unlocked.");
    }
}

int
controls_link(dbref who, dbref what)
{
    switch (Typeof(what)) {
        case TYPE_EXIT:
        {
            int i = DBFETCH(what)->sp.exit.ndest;

            while (i > 0) {
                if (controls(who, DBFETCH(what)->sp.exit.dest[--i]))
                    return 1;
            }
            if (who == OWNER(DBFETCH(what)->location))
                return 1;
            return 0;
        }

        case TYPE_ROOM:
        {
            if (controls(who, DBFETCH(what)->sp.room.dropto))
                return 1;
            return 0;
        }

        case TYPE_PLAYER:
        {
            if (controls(who, DBFETCH(what)->sp.player.home))
                return 1;
            return 0;
        }

        case TYPE_THING:
        {
            if (controls(who, DBFETCH(what)->sp.thing.home))
                return 1;
            return 0;
        }

        case TYPE_PROGRAM:
        default:
            return 0;
    }
}

/* If quiet is true, error messages won't display. */
void
_do_unlink(int descr, dbref player, const char *name, int quiet)
{
    dbref exit;
    char destin[BUFFER_LEN];
    struct match_data md;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }
    strcpy(destin, "*NOTHING*");
    init_match(descr, player, name, TYPE_EXIT, &md);
    match_all_exits(&md);
    match_registered(&md);
    match_here(&md);
    match_neighbor(&md);
    match_absolute(&md);
    match_player(&md);
    switch (exit = match_result(&md)) {
        case NOTHING:
            anotify_nolisten2(player, CINFO "I don't see that here.");
            break;
        case AMBIGUOUS:
            anotify_nolisten2(player, CINFO "I don't know which one you mean!");
            break;
        default:
            if (!controls(player, exit) && !controls_link(player, exit)) {
                anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            } else {
                switch (Typeof(exit)) {
                    case TYPE_EXIT:
                        if (DBFETCH(exit)->sp.exit.ndest != 0) {
                            DBFETCH(OWNER(exit))->sp.player.pennies +=
                                tp_link_cost;
                            DBDIRTY(OWNER(exit));
                        }
                        ts_modifyobject(player, exit);
                        DBSTORE(exit, sp.exit.ndest, 0);
                        if (DBFETCH(exit)->sp.exit.dest) {
                            strcpy(destin,
                                   unparse_object(player,
                                                  DBFETCH(exit)->sp.exit.
                                                  dest[0]));
                            free((void *) DBFETCH(exit)->sp.exit.dest);
                            DBSTORE(exit, sp.exit.dest, NULL);
                        }
                        if (!quiet)
                            anotify_fmt(player, CSUCC "%s unlinked from %s.",
                                        unparse_object(player, exit), destin);
                        if (MLevel(exit)) {
                            SetMLevel(exit, 0);
                            if (!quiet)
                                anotify_nolisten2(player,
                                                  CINFO
                                                  "Action priority Level reset to 0.");
                        }
                        break;
                    case TYPE_ROOM:
                        ts_modifyobject(player, exit);
                        DBSTORE(exit, sp.room.dropto, NOTHING);
                        if (!quiet)
                            anotify_fmt(player, CSUCC "Dropto removed from %s.",
                                        unparse_object(player, exit));
                        break;
                    case TYPE_THING:
                        ts_modifyobject(player, exit);
                        DBSTORE(exit, sp.thing.home, OWNER(exit));
                        if (!quiet)
                            anotify_fmt(player,
                                        CSUCC "%s's home reset to owner.",
                                        NAME(exit));
                        break;
                    case TYPE_PLAYER:
                        ts_modifyobject(player, exit);
                        DBSTORE(exit, sp.player.home, tp_player_start);
                        if (!quiet)
                            anotify_fmt(player,
                                        CSUCC
                                        "%s's home reset to default player start room.",
                                        NAME(exit));
                        break;
                    default:
                        anotify_nolisten2(player,
                                          CFAIL "You can't unlink that!");
                        break;
                }
            }
    }
}

void
do_unlink(int descr, dbref player, const char *name)
{
    /* do a regular @unlink */
    _do_unlink(descr, player, name, 0);
}

void
do_unlink_quiet(int descr, dbref player, const char *name)
{
    _do_unlink(descr, player, name, 1);
}

/* do_relink()
 * re-link an exit object without having to use @unlink inbetween. 
 */
void
do_relink(int descr, dbref player, const char *thing_name,
          const char *dest_name)
{
    dbref thing;
    dbref dest;
    dbref good_dest[MAX_LINKS];
    struct match_data md;
    int ndest;

    init_match(descr, player, thing_name, TYPE_EXIT, &md);
    match_all_exits(&md);
    match_neighbor(&md);
    match_possession(&md);
    match_me(&md);
    match_here(&md);
    match_absolute(&md);
    match_registered(&md);
    if (Mage(OWNER(player)) || POWERS(OWNER(player)) & POW_LONG_FINGERS)
        match_player(&md);

    if ((thing = noisy_match_result(&md)) == NOTHING)
        return;

    /* check if new target would be valid. */
    switch (Typeof(thing)) {
        case TYPE_EXIT:
            if (DBFETCH(thing)->sp.exit.ndest != 0)
                if (!controls(player, thing)) {
                    anotify(player, CFAIL "Permission denied.");
                    return;
                }
            if (!Wizard(OWNER(player)) && (DBFETCH(player)->sp.player.pennies <
                                           (tp_link_cost + tp_exit_cost))) {
                anotify_fmt(player, CFAIL "It costs %d %s to link this exit.",
                            (tp_link_cost + tp_exit_cost),
                            (tp_link_cost + tp_exit_cost == 1) ? tp_penny :
                            tp_pennies);
                return;
            }
            if (!Builder(player)) {
                anotify(player,
                        CFAIL "Only authoried builders may seize exits.");
                return;
            }
            ndest = link_exit_dry(descr, player, thing, (char *) dest_name,
                                  good_dest);
            if (ndest == 0) {
                anotify(player, CINFO "Invalid target.");
                return;
            }
            break;
        case TYPE_THING:
        case TYPE_PLAYER:
            init_match(descr, player, dest_name, TYPE_ROOM, &md);
            match_neighbor(&md);
            match_absolute(&md);
            match_registered(&md);
            match_me(&md);
            match_here(&md);
            if (Typeof(thing) == TYPE_THING)
                match_possession(&md);
            if ((dest = noisy_match_result(&md)) == NOTHING)
                return;
            if (!controls(player, thing)
                || !can_link_to(player, Typeof(thing), dest)) {
                anotify(player, CFAIL "Permission denied.");
                return;
            }
            if (parent_loop_check(thing, dest)) {
                anotify(player, CFAIL "That would cause a parent paradox.");
                return;
            }
            break;
        case TYPE_ROOM:
            init_match(descr, player, dest_name, TYPE_ROOM, &md);
            match_neighbor(&md);
            match_possession(&md);
            match_registered(&md);
            match_absolute(&md);
            match_home(&md);

            if ((dest = noisy_match_result(&md)) == NOTHING)
                return;
            if (!controls(player, thing)
                || !can_link_to(player, Typeof(thing), dest)
                || (thing == dest)) {
                anotify(player, CFAIL "Permission denied.");
                return;
            }
            break;
        case TYPE_PROGRAM:
            anotify(player, CFAIL "You can't link programs to things.");
            return;
            break;
        default:
            init_match(descr, player, dest_name, NOTYPE, &md);
            match_null(&md);

            if ((dest = noisy_match_result(&md)) == NOTHING) {
                anotify(player, CFAIL "Unknown object type.");
                log_status("PANIC: weird object: Typeof(%d) = %d\n", thing,
                           Typeof(thing));
                return;
            }
    }

    do_unlink_quiet(descr, player, thing_name);
    anotify(player, CSUCC "Attempting to relink...");
    do_link(descr, player, thing_name, dest_name);
}



void
do_chown(int descr, dbref player, const char *name, const char *newowner)
{
    dbref thing;
    dbref owner;
    dbref oldOwner;
    struct match_data md;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    init_match(descr, player, name, NOTYPE, &md);
    match_everything(&md);
    if ((thing = noisy_match_result(&md)) == NOTHING)
        return;

    if (*newowner && string_compare(newowner, "me")) {
        if ((owner = lookup_player(newowner)) == NOTHING) {
            anotify_nolisten2(player, CINFO "Who?");
            return;
        }
    } else {
        owner = OWNER(player);
    }

    if (!truecontrols(OWNER(player), owner)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (!truecontrols(OWNER(player), thing) && (!(FLAGS(thing) & CHOWN_OK) ||
                                                Typeof(thing) == TYPE_PROGRAM ||
                                                !test_lock(descr, player, thing,
                                                           CHLK_PROP))) {
        if (!(POWERS(player) & POW_CHOWN_ANYTHING)) {
            anotify_nolisten2(player,
                              CFAIL "You can't take possession of that.");
            return;
        }
    }
    if ((Protect(owner) && !(player == owner))
        || (Protect(OWNER(thing)) && !(player == OWNER(thing)))) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    oldOwner = OWNER(thing);
    switch (Typeof(thing)) {
        case TYPE_ROOM:
            if (!Mage(OWNER(player)) && DBFETCH(player)->location != thing) {
                anotify_nolisten2(player, CINFO "You can only chown \"here\".");
                return;
            }
            ts_modifyobject(player, thing);
            OWNER(thing) = OWNER(owner);
            break;
        case TYPE_THING:
            if (!Mage(OWNER(player)) && DBFETCH(thing)->location != player) {
                anotify_nolisten2(player, CINFO "You aren't carrying that.");
                return;
            }
            ts_modifyobject(player, thing);
            OWNER(thing) = OWNER(owner);
            break;
        case TYPE_PLAYER:
            anotify_nolisten2(player, CFAIL "Players always own themselves.");
            return;
        case TYPE_EXIT:
        case TYPE_PROGRAM:
            ts_modifyobject(player, thing);
            OWNER(thing) = OWNER(owner);
            break;
        case TYPE_GARBAGE:
            anotify_nolisten2(player, CFAIL "Nobody wants garbage.");
            return;
    }
    if (owner == player) {
        char buf[BUFFER_LEN], buf1[BUFFER_LEN];

        strcpy(buf1, unparse_object(player, thing));
        sprintf(buf, CSUCC "Owner of %s changed to you from %s.",
                buf1, unparse_object(player, oldOwner));
        anotify_nolisten2(player, buf);
    } else {
        char buf[BUFFER_LEN], buf1[BUFFER_LEN], buf2[BUFFER_LEN];

        strcpy(buf1, unparse_object(player, thing));
        strcpy(buf2, unparse_object(player, owner));
        sprintf(buf, CSUCC "Owner of %s changed to %s from %s.", buf1, buf2,
                unparse_object(player, oldOwner));
        anotify_nolisten2(player, buf);
    }
    DBDIRTY(thing);
}


/* Note: Gender code taken out.  All gender references are now to be handled
   by property lists...
   Setting of flags and property code done here.  Note that the PROP_DELIMITER
   identifies when you're setting a property.
   A @set <thing>= :clear
   will clear all properties.
   A @set <thing>= type:
   will remove that property.
   A @set <thing>= propname:string
   will add that string property or replace it.
   A @set <thing>= propname:^value
   will add that integer property or replace it.
 */

void
do_sm(dbref player, dbref thing, int mlev)
{
    player = OWNER(player);

    if ((mlev > MLevel(player)) || (MLevel(thing) > MLevel(player)) ||
        ((mlev > LMPI) && !Mage(player) && (Typeof(thing) != TYPE_PROGRAM)) ||
        ((Typeof(thing) == TYPE_PLAYER) && !Man(player) && !Boy(player) &&
         ((mlev >= LMAGE) || TMage(thing))
        ) ||
        ((Typeof(thing) == TYPE_PLAYER) && (!Wiz(player) ||
                                            (mlev >= MLevel(player))
                                            || (MLevel(thing) >= MLevel(player))
         ) && !Man(player))
        ) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (force_level) {
        anotify_nolisten2(player,
                          CFAIL
                          "Can't set this flag from an @force or {force}.");
        return;
    }

    if (mlev >= LMAGE)
        anotify_nolisten2(player, CSUCC "Wizard level set.");
    else if (mlev)
        anotify_nolisten2(player, CSUCC "Mucker level set.");
    else if (TMage(thing))
        anotify_nolisten2(player, CSUCC "Wizard bit removed.");
    else
        anotify_nolisten2(player, CSUCC "Mucker bit removed.");

    SetMLevel(thing, mlev);
}

void
do_mush_set(int descr, dbref player, char *name, char *setting, char *command)
{
    char *prop;
    dbref thing;


    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if (tp_db_readonly) {
        anotify_nolisten2(player, CFAIL DBRO_MESG);
        return;
    }

    (void) *command++;
    if (!(*command)) {
        anotify_nolisten2(player, CFAIL "That is a bad name for a property!");
        return;
    }
    prop = command;

    if (index(command, PROP_DELIMITER)) {
        anotify_nolisten2(player, CFAIL "That is a bad name for a property!");
        return;
    }

    if (!WizHidden(OWNER(player)) && Prop_Hidden(prop)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }

    if (!Wiz(OWNER(player)) && Prop_SeeOnly(prop)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }

    /* find thing */
    if ((thing = match_controlled(descr, player, name)) == NOTHING)
        return;

    if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }

    if (Prop_SysPerms(thing, prop)) {
        anotify_nolisten2(player,
                          CFAIL
                          "That property is already used as a system property.");
    }

    if (!(*setting)) {
        ts_modifyobject(player, thing);
        remove_property(thing, prop);
        anotify_nolisten2(player, CSUCC "Property removed.");
    } else {
        ts_modifyobject(player, thing);
        add_property(thing, prop, setting, 0);
        anotify_nolisten2(player, CSUCC "Property set.");
    }

    return;
}

void
do_set(int descr, dbref player, const char *name, const char *flag)
{
    dbref thing;
    const char *p;
    object_flag_type f = 0, f2 = 0, f4 = 0;
    int i=0;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    if (tp_db_readonly) {
        anotify_nolisten2(player, CFAIL DBRO_MESG);
        return;
    }

    /* find thing */
    if ((thing = match_controlled(descr, player, name)) == NOTHING)
        return;

    /* move p past NOT_TOKEN if present */
    for (p = flag; *p && (*p == NOT_TOKEN || isspace(*p)); p++) ;

    /* Now we check to see if it's a property reference */
    /* if this gets changed, please also modify boolexp.c */
    if (index(flag, PROP_DELIMITER)) {
        /* copy the string so we can muck with it */
        char *type = alloc_string(flag); /* type */
        char *pclass = (char *) index(type, PROP_DELIMITER); /* class */
        char *x;                /* to preserve string location so we can free it */
        char *temp;
        int ival = 0;

        if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }

        x = type;
        while (isspace(*type) && (*type != PROP_DELIMITER))
            type++;
        if (*type == PROP_DELIMITER) {
            /* clear all properties */
            for (type++; isspace(*type); type++) ;

            if (string_compare(type, "clear")) {
                anotify_nolisten2(player,
                                  CINFO
                                  "Use '@set <obj>=:clear' to clear all props on an object.");
                free((void *) x);
                return;
            }
            remove_property_list(thing, Arch(OWNER(player)));
            ts_modifyobject(player, thing);
            anotify_nolisten2(player,
                              CSUCC "All user-owned properties removed.");
            free((void *) x);
            return;
        }
        /* get rid of trailing spaces and slashes */
        for (temp = pclass - 1; temp >= type && isspace(*temp); temp--) ;
        while (temp >= type && *temp == '/')
            temp--;
        *(++temp) = '\0';

        pclass++;               /* move to next character */
        /* while (isspace(*pclass) && *pclass) pclass++; */
        if (*pclass == '^' && number(pclass + 1))
            ival = atoi(++pclass);

        if (Prop_SysPerms(thing, type)) {
            anotify_nolisten2(player,
                              CFAIL
                              "That property is already used as a system property.");
        }

        if (!WizHidden(OWNER(player)) && Prop_Hidden(type)) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            free((void *) x);
            return;
        }
        if (!Wiz(OWNER(player)) && Prop_SeeOnly(type)) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            free((void *) x);
            return;
        }
        if (!(*pclass)) {
            ts_modifyobject(player, thing);
            remove_property(thing, type);
            anotify_nolisten2(player, CSUCC "Property removed.");
        } else {
            ts_modifyobject(player, thing);
            if (ival) {
                add_property(thing, type, NULL, ival);
            } else {
                add_property(thing, type, pclass, 0);
            }
            anotify_nolisten2(player, CSUCC "Property set.");
        }
        free((void *) x);
        return;
    }
    /* identify flag */
    if (*p == '\0') {
        anotify_nolisten2(player, CINFO "You must specify a flag to set.");
        return;
    } else if (string_prefix("ABODE", p) ||
               string_prefix("AUTOSTART", p) || string_prefix("ABATE", p)) {
        f = ABODE;
    } else if (!string_compare("0", p) || !string_compare("M0", p) ||
               !string_compare("W0", p)
               || ((string_prefix("MEEPER", p) || string_prefix("MPI", p)
                    || string_prefix("MUCKER", p) || string_prefix("MAGE", p)
                    || string_prefix("WIZARD", p)
                    || string_prefix("ARCHWIZARD", p)
                    || !string_compare("M1", p) || !string_compare("M2", p)
                    || !string_compare("M3", p) || !string_compare("W1", p)
                    || !string_compare("W2", p) || !string_compare("W3", p)
                    || !string_compare("W4", p) || !string_compare("BOY", p)
                   ) && (*flag == NOT_TOKEN))) {
        do_sm(player, thing, 0);
        return;
    } else if (string_prefix("MPI", p) || string_prefix("MEEPER", p)) {
        do_sm(player, thing, LMPI);
        return;
    } else if (!string_compare("1", p) || !string_compare("M1", p) ||
               string_prefix("MUCKER", p)) {
        do_sm(player, thing, LM1);
        return;
    } else if (!string_compare("2", p) || !string_compare("M2", p)) {
        do_sm(player, thing, LM2);
        return;
    } else if (!string_compare("3", p) || !string_compare("M3", p)) {
        do_sm(player, thing, LM3);
        return;
    } else if (!string_compare("W1", p) || string_prefix("MAGE", p)) {
        do_sm(player, thing, LMAGE);
        return;
    } else if (!string_compare("W2", p) || string_prefix("WIZARD", p)) {
        do_sm(player, thing, LWIZ);
        return;
    } else if (!string_compare("W3", p) || string_prefix("ARCHWIZARD", p)) {
        do_sm(player, thing, LARCH);
        return;
    } else if (!string_compare("W4", p) || !string_compare("BOY", p)) {
        do_sm(player, thing, LBOY);
        return;
    } else if (string_prefix("PARENT", p)
               || (string_prefix("PROG_DEBUG", p) && !string_prefix("PRO", p))
               || !string_compare("%", p)) {
        f2 = F2PARENT;
    } else if (string_prefix("ZOMBIE", p) || string_prefix("PUPPET", p)) {
        f = ZOMBIE;
    } else if (string_prefix("VEHICLE", p) || string_prefix("VIEWABLE", p)) {
        if (*flag == NOT_TOKEN && Typeof(thing) == TYPE_THING) {
            dbref obj = DBFETCH(thing)->contents;

            for (; obj != NOTHING; obj = DBFETCH(obj)->next) {
                if (Typeof(obj) == TYPE_PLAYER) {
                    anotify_nolisten2(player,
                                      CINFO
                                      "That vehicle still has players in it!");
                    return;
                }
            }
        }
        f = VEHICLE;
    } else if (string_prefix("LINK_OK", p)) {
        f = LINK_OK;

    } else if (string_prefix("XFORCIBLE", p) ||
               string_prefix("EXPANDED_DEBUG", p)) {
        if (force_level) {
            anotify_nolisten2(player,
                              CFAIL
                              "Can't set this flag from an @force or {force}.");
            return;
        }
        f = XFORCIBLE;

    } else if ((string_prefix("DARK", p)) || (string_prefix("DEBUG", p))) {
        f = DARK;
    } else if ((string_prefix("STICKY", p)) || (string_prefix("SETUID", p)) ||
               (string_prefix("SILENT", p))) {
        f = STICKY;
    } else if (string_prefix("QUELL", p)) {
        f = QUELL;
    } else if (string_prefix("BUILDER", p) || string_prefix("BOUND", p)) {
        f = BUILDER;
    } else if (string_prefix("CHOWN_OK", p) || string_prefix("COLOR_ANSI", p)
               || string_prefix("COLOR_ON", p)) {
        f = CHOWN_OK;
    } else if (string_prefix("256COLOR", p) || string_prefix("&", p)){
        f = F256COLOR;
#ifdef CONTROLS_SUPPORT
    } else if (string_prefix("CONTROLS", p) || string_prefix("~", p)) {
        f2 = F2CONTROLS;
#endif
    } else if (string_prefix("IMMOBILE", p) || string_prefix("|", p)) {
        f2 = F2IMMOBILE;
    } else if (string_prefix("JUMP_OK", p)) {
        f = JUMP_OK;
    } else if (string_prefix("HAVEN", p) || string_prefix("HARDUID", p)
               || string_prefix("HIDE", p)) {
        f = HAVEN;

    } else if (string_prefix("GUEST", p)) {
        f2 = F2GUEST;
    } else if (string_prefix("LOGWALL", p) || !string_compare("!", p)) {
        f2 = F2LOGWALL;
    } else if (string_prefix("LIGHT", p) || !string_compare("O", p) ||
               string_prefix("OLDCOMMENT", p)) {
        f2 = F2LIGHT;
    } else if (string_prefix("MUFCOUNT", p) || !string_compare("+", p)) {
        f2 = F2MUFCOUNT;
    } else if (string_prefix("PROTECT", p) || !string_compare("*", p)) {
        f2 = F2PROTECT;
    } else if (string_prefix("ANTIPROTECT", p) || !string_compare("K", p)) {
        f2 = F2ANTIPROTECT;
    } else if (string_prefix("EXAMINE_OK", p) || !string_compare("Y", p)) {
        f2 = F2EXAMINE_OK;
    } else if (string_prefix("NO_COMMAND", p) ||
               !string_compare("NO_OPTIMIZE", p)) {
        f2 = F2NO_COMMAND;
    } else if (string_prefix("HIDDEN", p) || !string_compare("#", p)) {
        f2 = F2HIDDEN;
    } else if (string_prefix("IDLE", p) || !string_compare("I", p)) {
        f2 = F2IDLE;
    } else if (string_prefix("SUSPECT", p) || !string_compare("_", p)) {
        f2 = F2SUSPECT;
    } else if (string_prefix("MOBILE", p) || string_prefix("OFFER", p) ||
               string_prefix("?", p) || (*tp_userflag_name
                                         && string_prefix(tp_userflag_name,
                                                          p))) {
        f2 = F2MOBILE;
    } else {
	i=0; while (i<32 && string_compare(lflag_name[i],p)) i++;
	if (i == 32) {
            anotify_nolisten2(player, CINFO "I don't recognize that flag.");
	    return;
	} else f4 = LFLAGx(i);
    }
    if ((Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing))))
        && !(f2 == F2PROTECT)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (f4) { /* LOCAL flags!  They are SO COOL! */
	if (MLevel(player) < lflag_mlev[i]) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
	    return;
	}
        /* else everything is ok, do the set */
        if (*flag == NOT_TOKEN) {
            /* reset the flag */
            ts_modifyobject(player, thing);
            LFLAG(thing) &= ~f4;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag reset.");
        } else {
            /* set the flag */
            ts_modifyobject(player, thing);
            LFLAG(thing) |= f4;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag set.");
        }
	return;
    }
    if (f) { /* old ass flags */
        /* check for restricted flag */
        if (restricted(player, thing, f)) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        /* else everything is ok, do the set */
        if (*flag == NOT_TOKEN) {
            /* reset the flag */
            ts_modifyobject(player, thing);
            FLAGS(thing) &= ~f;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag reset.");
            if ( f == F256COLOR ) {
                /* Clear desrc flag from all player connections */
                propagate_descr_flag(player, DF_256COLOR, 0);
            }
        } else {
            /* set the flag */
            ts_modifyobject(player, thing);
            FLAGS(thing) |= f;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag set.");
            if ( f == F256COLOR ) {
                /* We have to set the related descr flag on
                   all connections this player has */
                propagate_descr_flag(player, DF_256COLOR, 1);
            }
        }
        return;
    }
    if (f2) { /* New f(l)ags */
        /* check for restricted flag */
        if (restricted2(player, thing, f2)) {
            anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
            return;
        }
        /* else everything is ok, do the set */
        if (*flag == NOT_TOKEN) {
            /* reset the flag */
            ts_modifyobject(player, thing);
            FLAG2(thing) &= ~f2;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag reset.");
        } else {
            /* set the flag */
            ts_modifyobject(player, thing);
            FLAG2(thing) |= f2;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag set.");
        }
    return;
    } 
    if (f4) { /* LOCAL flags!  They are SO COOL! */
	/* -1 = Free for all
	    0 = Anything I control
	   >0 = Mlevel required */

	if (lflag_mlev[i]!=-1)
	    if ((lflag_mlev[i]==0 && !controls(player, thing)) &&
	        (lflag_mlev[i]>0 && MLevel(player) < lflag_mlev[i])) {
        	anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
		return;
	    }
        /* else everything is ok, do the set */
        if (*flag == NOT_TOKEN) {
            /* reset the flag */
            ts_modifyobject(player, thing);
            LFLAG(thing) &= ~f4;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag reset.");
        } else {
            /* set the flag */
            ts_modifyobject(player, thing);
            LFLAG(thing) |= f4;
            DBDIRTY(thing);
            anotify_nolisten2(player, CSUCC "Flag set.");
        }
    return;
    }
}

void
do_propset(int descr, dbref player, const char *name, const char *prop)
{
    dbref thing;
    char *p, *q;
    char buf[BUFFER_LEN];
    char *type, *pname, *value;
    struct match_data md;
    PData pdat;

    if (tp_db_readonly)
        return;

    if (Guest(player)) {
        anotify_fmt(player, CFAIL "%s", tp_noguest_mesg);
        return;
    }

    /* find thing */
    if ((thing = match_controlled(descr, player, name)) == NOTHING)
        return;

    if (Protect(thing) && !(MLevel(player) > MLevel(OWNER(thing)))) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }

    while (isspace(*prop))
        prop++;
    strcpy(buf, prop);

    type = p = buf;
    while (*p && *p != PROP_DELIMITER)
        p++;
    if (*p)
        *p++ = '\0';

    if (*type) {
        q = type + strlen(type) - 1;
        while (q >= type && isspace(*q))
            *(q--) = '\0';
    }

    pname = p;
    while (*p && *p != PROP_DELIMITER)
        p++;
    if (*p)
        *p++ = '\0';
    value = p;

    while (*pname == PROPDIR_DELIMITER || isspace(*pname))
        pname++;
    if (*pname) {
        q = pname + strlen(pname) - 1;
        while (q >= pname && isspace(*q))
            *(q--) = '\0';
    }

    if (!*pname) {
        anotify_nolisten2(player, CINFO "What property?");
        return;
    }

    if (Prop_SysPerms(thing, pname)) {
        anotify_nolisten2(player,
                          CFAIL
                          "That property is already used as a system property.");
    }

    if (!WizHidden(OWNER(player)) && Prop_Hidden(pname)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }
    if (!Wiz(OWNER(player)) && Prop_SeeOnly(pname)) {
        anotify_fmt(player, CFAIL "%s", tp_noperm_mesg);
        return;
    }

    if (!*type || string_prefix("string", type)) {
        add_property(thing, pname, value, 0);
    } else if (string_prefix("integer", type)) {
        if (!number(value)) {
            anotify_nolisten2(player, CINFO "That's not an integer.");
            return;
        }
        add_property(thing, pname, NULL, atoi(value));
    } else if (string_prefix("float", type)) {
        if (!ifloat(value)) {
            notify(player, "That's not a floating point number!");
            return;
        }
        pdat.flags = PROP_FLTTYP;
        pdat.data.fval = strtod(value, NULL);
        set_property(thing, pname, &pdat);
    } else if (string_prefix("dbref", type)) {
        init_match(descr, player, value, NOTYPE, &md);
        match_absoluteEx(&md);
        match_everything(&md);
        match_ambiguous(&md);
        match_home(&md);
        match_null(&md);
        if ((pdat.data.ref = noisy_match_result(&md)) == NOTHING)
            return;
        pdat.flags = PROP_REFTYP;
        set_property(thing, pname, &pdat);
    } else if (string_prefix("lock", type)) {
        pdat.data.lok = parse_boolexp(descr, player, value, 0);
        if (pdat.data.lok == TRUE_BOOLEXP) {
            anotify_nolisten2(player, CINFO "I don't understand that lock.");
            return;
        }
        pdat.flags = PROP_LOKTYP;
        set_property(thing, pname, &pdat);
    } else if (string_prefix("erase", type)) {
        if (*value) {
            anotify_nolisten2(player,
                              CINFO
                              "Don't give a value when erasing a property.");
            return;
        }
        remove_property(thing, pname);
        anotify_nolisten2(player, CSUCC "Property erased.");
        return;
    } else {
        anotify_nolisten2(player, CINFO "What type of property?");
        anotify_nolisten2(player,
                          CNOTE
                          "Valid types are string, int, float, dbref, lock, and erase.");
        return;
    }
    anotify_nolisten2(player, CSUCC "Property set.");
}

void 
lflags_update()
{
    int i;
    PropPtr p;
    char buf[BUFFER_LEN];

    for (i=0; i<32; i++) {
	sprintf(buf, "@flags/%d/mlev", i);
	p = get_property((dbref)0, buf);
	if (!p)
	    lflag_mlev[i] = 0;
	else
	    lflag_mlev[i] = PropDataVal(p);
	sprintf(buf, "@flags/%d/name", i);
	p = get_property((dbref)0, buf);
	if (!p) {
	    sprintf(buf,"LFLAG%d",i);
	    strncpy(lflag_name[i], buf, 32);
	} else {
	    strncpy(lflag_name[i], PropDataUNCStr(p), 32);
	}
    }

}

void
do_flags(int descr, dbref player, const char *args)
{
    int i;
    PData pdat;
    int lflag;
    int lmlev;
    char *orig[2];

    int bufsize = 256;
    char *buf=(char *)malloc(bufsize);
    char *lname=(char *)malloc(32);
    
    /* Sanity */
    orig[0]=buf; orig[1]=lname;

    if (tp_db_readonly)
        return;

    if (!Arch(player)) {
        anotify(player, CFAIL "Only Archwizards (W3) or higher can define the Flags.");
        return;
    }

    if (!args || !string_compare(args,"#update")) {
	/* Put flag update routine call right here. */
	lflags_update();
	anotify(player, "The Local DB flags have been read from /@flags/ on #0, and updated in memory.");
	anotify(player, "Syntax: @flags <flagnum 0..31> <flagname> <mlevel to write>");
	return;
    }

    if (!string_compare(args,"#list")) {
	anotify_fmt(player,"%8s%40s%8s","Flag","Flag Name","Level");
	for (i=0; i<32; i++)
    	    anotify_fmt(player,"%8d%40s%8d",i,lflag_name[i],lflag_mlev[i]);
	return;
    }

    i = sscanf(args, "%d %s %d", &lflag, lname, &lmlev);
    if (i != 3) {
	anotify_fmt(player, "Syntax: @flags <flagnum 0..31> <flagname> <mlevel to write> (%d arguments detected, 3 were needed)",i);
	return;
    }

    /* Arg 1: The flag number. */
    if (lflag < 0 || lflag > 31) {
	anotify(player, "Invalid local flag number.  Must be between 0 and 31.  See help @flags for more info.");
	return;
    }

    /* Arg 2: The flag name.  Defaults to LFLAG* */
    if (!lname || !*lname) sprintf(lname, "LFLAG%d", lflag);

    /* Arg 3: The write mlevel.  Local flags are always readable. */
    if (lmlev < -1 || lmlev > 9) {
	anotify(player, "Invalid MLevel.  Must be between -1 and 9.  See help @flags for more info.");
	return;
    }

    /* Now we do the actual work. */
    buf = orig[0]; memset(buf, 0, bufsize);
    pdat.flags = PROP_STRTYP;
    pdat.data.str = lname;
    sprintf(buf, "@flags/%d/name", lflag);
    set_property(0, buf, &pdat);
    buf = orig[0]; memset(buf, 0, bufsize);
    pdat.flags = PROP_INTTYP;
    pdat.data.val = lmlev;
    sprintf(buf, "@flags/%d/mlev", lflag);
    set_property(0, buf, &pdat);
    lflags_update();
    anotify_fmt(player, SYSGREEN "Local flag %d was named %s and given an mlevel of %d.", lflag, lname, lmlev);

    free(orig[0]); free(orig[1]);
    return;
}

/* Alias support is managed by the @alias propdir on #0. There are two subdirs:
 *
 * @alias/names/ - The actual aliases. Stores aliasname:dbref pairs. Non-dbref
 *                 props are removed as they're detected.
 * @alias/last/  - The last alias managed for a given dbref via the @alias
 *                 command. This is where the single alias managed by the @alias
 *                 command is tracked. Stores refnum:aliasname pairs. Non-string
 *                 props are removed as they're detected.
 *
 * Players interact with aliases via the @alias command. They are allowed to
 * manage a single entry in the @alias/names/ propdir. Whenever they change
 * their alias via the command, the @alias/last/(dbref) prop is looked up to
 * obtain their previous alias. The corresponding key is then removed from the
 * @alias/names/ propdir, and the new entry is added to both.
 *
 * Entries are automatically killed in @alias/names/ based on @pcreates, @frobs,
 * and @names. Entries in @alias/last/ are killed based on @frobs. Any entries
 * manually applied to @alias/names/ via @propset have to be manually removed.
 * They will be unavailable for players to set/clear and only the automated
 * cleanups will remove them.
 *
 * In addition to the usual restrictions on player names, aliases cannot contain
 * the characters '/' or ':' as these would let alias names escape the boundary
 * of the @alias/names/ propdir.
 */
void
do_alias(dbref player, const char *arg1, const char *arg2, int delimited) {
    dbref target = player;
    const char *name, *alias;

    if (*arg2 == '\0' && !delimited) {
        /* indicates "@alias foo" syntax */
        name = arg2;
        alias = arg1;
    } else {
        /* indicates "@alias foo=" or "@alias foo=bar" syntax */
        name = arg1;
        alias = arg2;
    }

    if (!Mage(player)) {
        /* stop players from setting an @alias when they're @tuned off - note
         * that this won't stop them from clearing one they've already set. */
        if (!tp_player_aliasing && alias) {
            anotify_nolisten2(player, CFAIL
                              "The @alias command is disabled on this site, sorry.");
            return;
        }

        if (*name != '\0' &&
            string_compare(name, "me")) {
            anotify_nolisten2(player, CFAIL
                              "Only mages can specify the name of a player.");
            return;
        }
    }

    /* validate target - re-aliasing a player using their old alias is fine. */
    if (*name != '\0' && strcmp(name, "me") &&
            ((target = lookup_player(name)) == NOTHING) ) {
        anotify_nolisten2(player, CINFO "Who?");
        return;
    }

    if (*alias == '\0') {
        clear_alias(target, NULL);
        anotify_nolisten2(player, CSUCC "Alias cleared.");
        return;
    } else {
        switch (set_alias(target, alias, 1)) {
            case (NOTHING):
                anotify_nolisten2(player, CFAIL "Illegal alias name.");
                break;
            case (AMBIGUOUS):
                anotify_nolisten2(player, CFAIL "That alias is already taken.");
                break;
            default:
                anotify_nolisten2(player, CSUCC "Alias set.");
                break;
        }
    }
    

    return;
}

void
do_encoding(int descr, dbref player, const char *arg) {

    struct descriptor_data *d = descrdata_by_descr(descr);

    if (!arg || arg[0] == '\0') {
        anotify(player, CINFO "Current character encoding:");
        if (d->encoding == ENC_ASCII) {
            anotify(player, CINFO "US-ASCII");
#ifdef UTF8_SUPPORT
        } else if (d->encoding == ENC_LATIN1) {
            anotify(player, CINFO "Latin1"
        }
        } else if (d->encoding == ENC_UTF8) {
            anotify(player, CINFO "UTF-8");
#endif
        } else if (d->encoding == ENC_RAW) {
            anotify(player, CFAIL "RAW");
        } else {
            anotify(player, CFAIL "<unknown>");
        }
        return;
    }

    if (!string_compare(arg, "US-ASCII") || !string_compare(arg, "ASCII") || 
        !string_compare(arg, "ANSI") ) { 
        d->encoding = ENC_ASCII;
        anotify(player, CSUCC "Encoding set.");
#ifdef UTF8_SUPPORT
    } else if ( !string_compare(arg, "UTF-8")   || !string_compare(arg, "UTF8") ||
                !string_compare(arg, "UNICODE") ) {
        d->encoding = ENC_UTF8;
        anotify(player, CSUCC "Encoding set.");
#endif
    } else if ( !string_compare(arg, "RAW") ) {
        d->encoding = ENC_RAW;
        anotify(player, CSUCC "Encoding set.");
        anotify(player, CFAIL "Warning: Your terminal may render garbage in this mode.");
    } else { /* unrecognized, show help */
        anotify(player, CFAIL "Unrecognized encoding type.");
        anotify(player, CINFO "Supported encodings:");
#ifdef UTF8_SUPPORT
        if (d->encoding == ENC_UTF8) {
            anotify(player, CINFO "    UTF-8 (current)");
        } else {
            anotify(player, CINFO "    UTF-8");
        }
        if (d->encoding == ENC_LATIN1) {
            anotify(player, CINFO "    Latin1 (current)");
        } else {
            anotify(player, CINFO "    Latin1");
        }
#endif
        if (d->encoding == ENC_ASCII) {
            anotify(player, CINFO "    US-ASCII (current)");
        } else {
            anotify(player, CINFO "    US-ASCII");
        }
        if (d->encoding == ENC_RAW) {
            anotify(player, CINFO "      RAW (current)");
        } else {
            anotify(player, CINFO "      RAW (not recommended)");
        }

    }
}
