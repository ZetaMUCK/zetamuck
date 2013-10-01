/* strings */
extern const char *tp_dumpwarn_mesg;
extern const char *tp_deltawarn_mesg;
extern const char *tp_dumpdeltas_mesg;
extern const char *tp_dumping_mesg;
extern const char *tp_dumpdone_mesg;

extern const char *tp_mailserver;
extern const char *tp_servername;
extern const char *tp_leave_message;
extern const char *tp_huh_mesg;
extern const char *tp_noperm_mesg;
extern const char *tp_noguest_mesg;

extern const char *tp_idleboot_msg;
extern const char *tp_unidle_command;
extern const char *tp_unidle_command_msg;
extern const char *tp_penny;
extern const char *tp_pennies;
extern const char *tp_cpenny;
extern const char *tp_cpennies;

extern const char *tp_muckname;
extern const char *tp_userflag_name;

extern const char *tp_reg_email;

extern const char *tp_proplist_counter_fmt;
extern const char *tp_proplist_entry_fmt;
extern const char *tp_mysql_hostname;
extern const char *tp_mysql_database;
extern const char *tp_mysql_username;
extern const char *tp_mysql_password;
extern const char *tp_sex_prop;

#ifdef USE_SSL
extern const char *tp_ssl_keyfile_passwd;
#endif /* USE_SSL */

#ifdef USE_RESLVD
extern const char *tp_reslvd_address;
#endif /* USE_RESLVD */

/* times */

extern time_t tp_dump_interval;
extern time_t tp_dump_warntime;
extern time_t tp_monolithic_interval;
extern time_t tp_clean_interval;
extern time_t tp_aging_time;
extern time_t tp_idletime;
extern time_t tp_connidle;
extern time_t tp_maxidle;
extern time_t tp_cron_interval;
extern time_t tp_archive_interval;
extern time_t tp_shutdown_delay;

/* integers */

extern int tp_textport;
extern int tp_puebloport;
#ifdef USE_SSL
extern int tp_sslport;
#endif

extern int tp_max_object_endowment;
extern int tp_object_cost;
extern int tp_exit_cost;
extern int tp_link_cost;
extern int tp_room_cost;
extern int tp_lookup_cost;
extern int tp_max_pennies;
extern int tp_penny_rate;
extern int tp_start_pennies;

extern int tp_command_burst_size;
extern int tp_commands_per_time;
extern int tp_command_time_msec;

extern int tp_max_delta_objs;
extern int tp_max_loaded_objs;
extern int tp_max_process_limit;
extern int tp_max_plyr_processes;
extern int tp_max_instr_count;
extern int tp_instr_slice;
extern int tp_msec_slice;
extern int tp_mpi_max_commands;
extern int tp_pause_min;
extern int tp_free_frames_pool;
extern int tp_max_output;
extern int tp_rand_screens;

extern int tp_listen_mlev;
extern int tp_wizhidden_access_bit;
extern int tp_userflag_mlev;
extern int tp_playermax_limit;
extern int tp_max_player_name_length;

extern int tp_dump_copies;
extern int tp_min_progbreak_lev;
#ifdef MCP_SUPPORT
extern int tp_mcp_muf_mlev;
#endif
extern int tp_max_wiz_preempt_count;
extern int tp_mysql_result_limit;

#ifdef NEWHTTPD                 /* hinoserm */
extern int tp_wwwport;          /* hinoserm */
extern int tp_web_port;         /* hinoserm */
extern int tp_web_logfile_lvl;  /* hinoserm */
extern int tp_web_logwall_lvl;  /* hinoserm */
extern int tp_web_htmuf_mlvl;   /* hinoserm */
extern int tp_web_max_files;    /* hinoserm */
extern int tp_web_max_filesize; /* hinoserm */
extern int tp_web_max_users;    /* hinoserm */
#endif                          /* hinoserm */

/* dbrefs */
extern dbref tp_quit_prog;
extern dbref tp_login_who_prog;
extern dbref tp_player_start;
extern dbref tp_reg_wiz;
extern dbref tp_player_prototype;
extern dbref tp_cron_prog;
extern dbref tp_default_parent;
#ifdef NEWHTTPD                         /* hinoserm */
extern dbref tp_www_root;               /* hinoserm */
#endif                                  /* hinoserm */

/* booleans */
extern int tp_hostnames;
extern int tp_log_commands;
extern int tp_log_interactive;
extern int tp_log_connects;
extern int tp_log_mud_commands;
extern int tp_log_failed_commands;
extern int tp_log_programs;
extern int tp_log_guests;
extern int tp_log_suspects;
extern int tp_log_wizards;
extern int tp_log_files;
extern int tp_log_sockets;
extern int tp_log_failedhelp;
extern int tp_db_events;                /* brevantes */
extern int tp_dbdump_warning;
extern int tp_deltadump_warning;
extern int tp_periodic_program_purge;

extern int tp_secure_who;
extern int tp_who_doing;
extern int tp_realms_control;
extern int tp_listeners;
extern int tp_listeners_obj;
extern int tp_listeners_env;
extern int tp_zombies;
extern int tp_wiz_vehicles;
extern int tp_wiz_puppets;
extern int tp_wiz_name;
extern int tp_recycle_frobs;
extern int tp_m1_name_notify;
extern int tp_teleport_to_player;
extern int tp_secure_teleport;
extern int tp_exit_darking;
extern int tp_thing_darking;
extern int tp_dark_sleepers;
extern int tp_who_hides_dark;
extern int tp_compatible_priorities;
extern int tp_do_mpi_parsing;
extern int tp_look_propqueues;
extern int tp_dump_propqueues;
extern int tp_lock_envcheck;
extern int tp_diskbase_propvals;
extern int tp_idleboot;
extern int tp_playermax;
extern int tp_db_readonly;
extern int tp_process_timer_limit;
extern int tp_pcreate_copy_props;
extern int tp_enable_home;
extern int tp_enable_idle_msgs;
extern int tp_user_idle_propqueue;
extern int tp_use_self_on_command;
extern int tp_quiet_moves;
extern int tp_quiet_connects;
extern int tp_proplist_int_counter;
#ifdef MCP_SUPPORT
extern int tp_enable_mcp;
#endif
#ifdef CONTROLS_SUPPORT
extern int tp_wiz_realms;
#endif
extern int tp_enable_commandprops;
extern int tp_old_parseprop;
extern int tp_mpi_needflag;
extern int tp_mortalwho;
extern int tp_guest_needflag;
extern int tp_fb_controls;
extern int tp_allow_old_trigs;
extern int tp_multi_wizlevels;
extern int tp_auto_archive;
extern int tp_optimize_muf;
extern int tp_compatible_muf_perms;
extern int tp_allow_unidle; 
extern int tp_alt_infinity_handler;
extern int tp_autolinking;
extern int tp_spaces_in_playernames;
extern int tp_mush_format_escapes;
extern int tp_strict_mush_escapes;
extern int tp_ascii_descrs;
extern int tp_muf_profiling;
extern int tp_player_aliasing;

/* extern int tp_require_has_mpi_arg; */
#ifdef NEWHTTPD                         /* hinoserm */
extern int tp_web_allow_players;        /* hinoserm */
extern int tp_web_allow_playerhtmuf;    /* hinoserm */
extern int tp_web_allow_htmuf;          /* hinoserm */
extern int tp_web_allow_vhosts;         /* hinoserm */
extern int tp_web_allow_files;          /* hinoserm */
extern int tp_web_allow_dirlist;        /* hinoserm */
extern int tp_web_allow_mpi;            /* alynna */
#endif

extern int tune_count_parms(void);
extern void tune_load_parms_from_file(FILE *f, dbref player, int cnt);
extern void tune_save_parms_to_file(FILE *f);
extern stk_array *tune_parms_array(const char *pattern, int mlev);

extern int tp_building;
extern int tp_all_can_build_rooms;
extern int tp_restricted_building;

