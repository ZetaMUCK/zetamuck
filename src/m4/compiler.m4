AC_CHECK_FUNCS(mallinfo getrlimit getrusage random snprintf vsnprintf)
AC_CHECK_MEMBER([struct mallinfo.hblks])
AC_CHECK_MEMBER([struct mallinfo.keepcost])
AC_CHECK_MEMBER([struct mallinfo.treeoverhead])
AC_CHECK_MEMBER([struct mallinfo.grain])
AC_CHECK_MEMBER([struct mallinfo.allocated])
AC_CHECK_MEMBER([struct tm.tm_gmtoff])
AC_CHECK_DECL([_timezone])

dnl
dnl Check if socklen_t is defined in sys/socket.h.
dnl
AC_DEFUN([TYPE_SOCKLEN_T],
[AC_CACHE_CHECK([for socklen_t], ac_cv_type_socklen_t,
[
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
     #include <sys/socket.h>]], [[socklen_t len = 42; return 0;]])],[ac_cv_type_socklen_t=yes],[ac_cv_type_socklen_t=no])
  ])
  if test $ac_cv_type_socklen_t != yes; then
    AC_DEFINE(socklen_t, int, [The type that holds socket length values on this system.])
  fi
])
TYPE_SOCKLEN_T


dnl
dnl Types and structures
dnl
AC_TYPE_UID_T
AC_STRUCT_TM
AC_CHECK_TYPES(long long)

dnl
dnl Compiler characteristics
dnl
AC_C_CONST
