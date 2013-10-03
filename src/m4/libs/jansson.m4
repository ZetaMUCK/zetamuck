
AC_ARG_WITH(jansson,
[  --with-jansson[=janssonpath]  use Jansson for JSON support],
[
if test "$withval" != no; then
    if test "$withval" = yes; then
        JANSSONTOP=
    else
        JANSSONTOP=$withval
    fi

    found_jansson="0"
    if test "$JANSSONTOP" = ""; then
        AC_CHECK_HEADERS(jansson.h,
        found_jansson="1")

        AC_CHECK_HEADERS(jansson.h,
            AC_DEFINE(HAVE_JANSSON_H, [], [Defined if jansson.h exists.])
            found_jansson="1"
        )
    else
        AC_CHECK_HEADERS($JANSSONTOP/include/jansson.h,
            AC_DEFINE(HAVE_JANSSON_H, [], [Defined if jansson.h exists.])
            INCL="-I$JANSSONTOP/include -L$JANSSONTOP/lib $INCL"
            LIBS="-L$JANSSONTOP/lib $LIBS"
            found_jansson="1"
        )
    fi

    if test "$found_janssonh" = "0"; then
        AC_MSG_ERROR([cannot locate jansson.h header file.])
    fi

    AC_DEFINE(JSON_SUPPORT, [], [Defined to enable JSON support])
    AC_SUBST(INCL)
    LIBS="-ljansson $LIBS"
fi
])

