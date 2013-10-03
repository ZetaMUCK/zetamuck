AC_SEARCH_LIBS([strerror],[cposix])

m4_include([m4/libs/jansson.m4])
m4_include([m4/libs/openssl.m4])
m4_include([m4/libs/mysql.m4])
m4_include([m4/libs/pcre.m4])
m4_include([m4/libs/zlib.m4])

AC_CHECK_LIB([m],[main],[LIBS="$LIBS -lm"],[],[])ac_cv_lib_m=ac_cv_lib_m_main
AC_CHECK_LIB([socket],[main],[LIBS="$LIBS -lsocket" ],[],[])ac_cv_lib_socket=ac_cv_lib_socket_main
AC_CHECK_LIB([nsl],[main],[LIBS="$LIBS -lnsl"],[],[])ac_cv_lib_nsl=ac_cv_lib_nsl_main
AC_CHECK_LIB(util, setproctitle)
