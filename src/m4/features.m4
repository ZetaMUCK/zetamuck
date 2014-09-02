
# 'readlink -f' isn't reliably cross platform. :/
cd $srcdir
srcdir_full=`pwd -P`
branchdir=${srcdir_full##*/branches/}
cd $OLDPWD # yes, they can be different.

if test "$srcdir_full" != "$branchdir"
then
    # before: davin/foo/bar/src
    branchdir=`echo ${branchdir%*/src} | sed 's/\//:/'`
    #  after: davin:foo/bar
    AC_DEFINE_UNQUOTED(PROTO_BRANCH, [$branchdir], [Branch of source code in the repo, used by version.h.])
else
    AC_DEFINE(PROTO_BRANCH, [trunk], [Branch of source code in the repo, used by version.h.])
fi

AC_ARG_ENABLE(modules,
[  --enable-modules          use modular code support],
[

found_dl="0"
AC_CHECK_HEADERS(dlfcn.h,
    AC_DEFINE(HAVE_DLFCN_H, [], [Defined if dlfcn.h exists.])
    found_dl="1"
)

if test "$found_dl" = "0"; then
    AC_MSG_ERROR([cannot locate dlfcn.h header file. (required for modular support)])
fi

AC_CHECK_LIB(dl, dlopen)

AC_DEFINE(MODULAR_SUPPORT, [], [Defined to enable modular support])
LIBS="-rdynamic $LIBS"
])

AC_ARG_ENABLE(reslvd,
[  --enable-reslvd         enable shared resolver support (reslvd)],
[
if test "$enableval" != "no"; then
    AC_DEFINE(SUPPORT_RESLVD, [], [Defined to enable reslvd support.])
fi
])

AC_ARG_ENABLE(compress,
[  --disable-compress      disable database compression],
[
if test "$enableval" = "no"; then
    AC_DEFINE(NO_COMPRESS, [], [Defined to DISABLE compression support.])
fi
])

AC_ARG_ENABLE(ipv6,
[  --enable-ipv6           enable ipv6],
[
if test "$enableval" != "no"; then
    AC_DEFINE(IPV6, [], [Defined to compile for ipv6])
fi
])

dnl AC_ARG_ENABLE(unicode,
dnl [  --enable-unicode           enable unicode],
dnl [
dnl if test "$enableval" != "no"; then
dnl     AC_DEFINE(UTF8_SUPPORT, [], [Defined to build with UTF8 support])
dnl fi
dnl AM_CONDITIONAL([BUILD_LIBUNISTRING], [test $enableval = yes])
dnl ])

dnl
dnl Experimental threading
dnl
AC_ARG_ENABLE(experimental-threading,
[  --enable-experimental-threading  Enable experimental threading support],
[
if test "$enableval" != no; then
    AC_CHECK_HEADERS(pthread.h)
    AC_CHECK_LIB(pthread, pthread_create)
    AC_DEFINE(EXPERIMENTAL_THREADING, [], [Defined to enable experimental threading support])
fi
])
