checkpcredir() { :
    if test -f "$1/include/pcre/pcre.h"; then
        AC_DEFINE(HAVE_PCREINCDIR, [], [PCRE headers are under pcre dir.])
        pcredir="$1"
        return 0
    fi
    if test -f "$1/include/pcre.h"; then
        pcredir="$1"
        return 0
    fi
    return 1
}
dnl Let the user specify where to find the PCRE libs
AC_ARG_WITH(pcre,
    [  --with-pcre=DIR         location of installed PCRE libraries/include files],
    [
    pcreenable=1
    if test "$withval" = "no"; then
        pcreenable=0
    elif test "$withval" != "yes"; then
        AC_MSG_CHECKING([for user-specified PCRE directory])
        checkpcredir "$withval"
        if test -z "$pcredir"; then
            AC_MSG_RESULT([missing])
        else
            AC_MSG_RESULT([found it])
        fi
    fi

    ],
    [
    pcreenable=2
    ]
)
dnl PCRE isn't disabled and user didn't specify a valid path - search for it.
if test -z "$pcredir" -a "$pcreenable" != 0; then
    AC_MSG_CHECKING([for PCRE directory in the usual places])
    for maindir in /usr /usr/local /usr/pkg /opt /sw
    do
        for dir in $maindir $maindir/pcre
        do
            checkpcredir $dir && break 2
        done
    done
    if test -z "$pcredir"; then
        AC_MSG_RESULT([not found])
        if test "pcreenable" = 2; then
            AC_MSG_ERROR([Could not find your PCRE library installation dir at the specified location. Specify "no" to build without.])
        else
            AC_MSG_WARN([PCRE library not found, will not build regexp prims.])
        fi
    else
        AC_MSG_RESULT([$pcredir])
    fi
fi

if test "$pcreenable" != "0"; then
    AC_SUBST(pcredir)
    AC_DEFINE_UNQUOTED(pcredir, "$pcredir", [The base path to the installation for PCRE.  Usually /usr.])

    if test "$pcredir"; then
        INC="$INC -I$pcredir/include"
        LIBS="$LIBS -L$pcredir/lib -lpcre"
        AC_CHECK_LIB(pcre, pcre_free)
        AC_DEFINE(PCRE_SUPPORT, [], [Defined to enable PCRE support])
    fi
    AC_SUBST(INC)
else
    AC_MSG_NOTICE([PCRE support disabled by user, building without regexp prims.])
fi
