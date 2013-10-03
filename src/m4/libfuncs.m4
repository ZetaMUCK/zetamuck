AC_CHECK_FUNCS(mallinfo getrlimit getrusage random snprintf vsnprintf setproctitle)
AC_CHECK_MEMBER([struct mallinfo.hblks])
AC_CHECK_MEMBER([struct mallinfo.keepcost])
AC_CHECK_MEMBER([struct mallinfo.treeoverhead])
AC_CHECK_MEMBER([struct mallinfo.grain])
AC_CHECK_MEMBER([struct mallinfo.allocated])
AC_CHECK_MEMBER([struct tm.tm_gmtoff])
AC_CHECK_DECL([_timezone])

