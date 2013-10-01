#ifdef NEWHTTPD

extern void prim_descr_safeboot(PRIM_PROTOTYPE);
extern void prim_body_getchar(PRIM_PROTOTYPE);
extern void prim_body_nextchar(PRIM_PROTOTYPE);
extern void prim_body_prevchar(PRIM_PROTOTYPE);
extern void prim_httpdata(PRIM_PROTOTYPE);
extern void prim_httpsendheader(PRIM_PROTOTYPE);

#define PRIMS_HTTP_FUNCS prim_descr_safeboot, prim_body_getchar,  \
    prim_body_nextchar, prim_body_prevchar, prim_httpdata, prim_httpsendheader

#define PRIMS_HTTP_NAMES "DESCR_SAFEBOOT", "BODY_GETCHAR", "BODY_NEXTCHAR",  \
    "BODY_PREVCHAR", "HTTPDATA", "HTTPSENDHEADER"

#define PRIMS_HTTP_CNT 6

#else /* !NEWHTTPD */
#define PRIMS_HTTP_CNT 0
#endif /* NEWHTTPD */
