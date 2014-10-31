#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AC_CONFIG_SRCDIR([smatch.c])
AC_CONFIG_HEADERS([inc/autoconf.h])
AC_USE_SYSTEM_EXTENSIONS

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_MAKE_SET
#AC_PROG_CXX
AC_PROG_SED

# Checks for libraries.
#
m4_include([m4/libs.m4])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h inttypes.h locale.h malloc.h memory.h netdb.h netinet/in.h stdint.h stdlib.h string.h strings.h sys/file.h sys/ioctl.h sys/mount.h sys/param.h sys/socket.h sys/time.h sys/vfs.h unistd.h])
m4_include([m4/headers.m4])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_PID_T
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_STRUCT_TIMEZONE
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T
m4_include([m4/compiler.m4])

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([bzero floor gethostbyaddr gethostbyname getpagesize gettimeofday inet_ntoa isascii mblen memmove memset mkdir modf pow rmdir select setlocale socket sqrt strcasecmp strchr strdup strerror strncasecmp strrchr strstr strtol tzset])
m4_include([m4/libfuncs.m4])

# Custom stuff
m4_include([m4/features.m4])
m4_include([m4/debug.m4])
m4_include([m4/misc.m4])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
