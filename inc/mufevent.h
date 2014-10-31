#ifndef MUF_EVENT_H
#define MUF_EVENT_H

struct mufevent_process {
    struct mufevent_process *next, *prev;
    dbref player;
    dbref prog;
    int descr;                  /* Added in proto for login screen support */
    int filtercount;
    char **filters;
    struct frame *fr;
};

struct mufevent {
    struct mufevent *next;
    char *event;
    struct inst data;
};

#define MUFEVENT_ALL	-1
#define MUFEVENT_FIRST	-2
#define MUFEVENT_LAST	-3

extern int muf_event_dequeue(dbref prog, int sleeponly);
extern int muf_event_dequeue_frame(struct frame *fr);
extern int muf_event_dequeue_pid(int pid);
extern struct frame *muf_event_pid_frame(int pid);
extern int muf_event_controls(dbref player, int pid);
extern void muf_event_register(dbref player, dbref prog, struct frame *fr);
extern void muf_event_register_specific(dbref player, dbref prog,
                                        struct frame *fr, int eventcount,
                                        char **eventids);
extern int muf_event_count(struct frame *fr);
extern void muf_event_add(struct frame *fr, char *event, struct inst *val,
                          int exclusive);
extern void muf_event_remove(struct frame *fr, char *event, int which);
extern void muf_event_purge(struct frame *fr);
extern void muf_event_process(void);
extern int muf_event_exists(struct frame *fr, const char *eventid);
extern int muf_event_list(dbref player, char *pat);
extern int muf_event_read_notify(int descr, dbref player, const char *cmd);
extern stk_array *get_mufevent_pids(stk_array *nw, dbref ref);
extern stk_array *get_mufevent_pidinfo(stk_array *nw, int pid);
extern struct mufevent *muf_event_peek(struct frame *fr);
extern int muf_event_dequeue_descr(int descr, int sleeponly);

/* for MUF "interrupts" */
extern struct muf_interrupt *muf_interrupt_find(struct frame *fr,
                                                const char *id);
extern int muf_interrupt_check_byevent(struct frame *fr,
                                       const char *event,
                                       struct inst *val);
extern void muf_interrupt_clean(struct frame *fr);
extern int muf_interrupt_exit(struct frame *fr);
#endif /* MUF_EVENT_H */
