gl_LIBUNISTRING

unigbrk_nag="no"

if test "$HAVE_LIBUNISTRING" = "yes"
then
    AC_CHECK_HEADERS(unigbrk.h, HAVE_UNIGBRK_H="yes")

    if test "$HAVE_UNIGBRK_H" = "yes"
    then
        AC_DEFINE(UTF8_SUPPORT, [], [Defined if UTF8 support is available.])
    else
        AC_MSG_WARN("libunistring must be 0.9.4 or greater. (missing unigbrk.h)")
        unigbrk_nag="yes"
    fi
fi
