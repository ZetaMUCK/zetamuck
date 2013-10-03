
AC_ARG_WITH(ssl,
[  --with-ssl[=ssltop_path]  enable ssl encryption and authorization],
[
if test "$withval" != no; then
    if test "$withval" = yes; then
            SSLTOP=
    else
            SSLTOP=$withval
    fi
    
    found_sslh="0"
    if test "$SSLTOP" = ""; then
            AC_CHECK_HEADERS(ssl.h ssl/ssl.h openssl/ssl.h,found_sslh="1")
    else
            oldinc=$INC
            INC="-I$SSLTOP $oldinc"
            AC_CHECK_HEADERS($SSLTOP/ssl.h,
                    AC_DEFINE(HAVE_SSL_H, [], [Enabled if ssl.h exists.])
                    found_sslh="1"
            )
            if test "$found_sslh" = "0"; then
                    INC="-I$SSLTOP/include $oldinc"
                    AC_CHECK_HEADERS($SSLTOP/include/ssl.h,
                            AC_DEFINE(HAVE_SSL_H, [], [Enabled if include/ssl.h exists.])
                            found_sslh="1"
                    )
            fi
            if test "$found_sslh" = "0"; then
                    INC="-I$SSLTOP/include $oldinc"
                    AC_CHECK_HEADERS($SSLTOP/include/ssl/ssl.h,
                            AC_DEFINE(HAVE_SSL_SSL_H, [], [Enabled if include/ssl/ssl.h exists.])
                            found_sslh="1"
                    )
            fi
            if test "$found_sslh" = "0"; then
                    INC="-I$SSLTOP/include $oldinc"
                    AC_CHECK_HEADERS($SSLTOP/include/openssl/ssl.h,
                            AC_DEFINE(HAVE_OPENSSL_SSL_H, [], [Enabled if include/openssl/ssh.h exists.])
                            found_sslh="1"
                    )
            fi
    fi
    if test "$found_sslh" = "0"; then
            AC_MSG_ERROR([cannot locate ssl.h header file.])
    fi
    
    found_ssl_lib="0"
    if test "$SSLTOP" = ""; then
            AC_TRY_LINK_FUNC(SSL_accept, found_ssl_lib="1")
            AC_CHECK_LIB(ssl, SSL_accept,
                    LIBS="-lssl -lcrypto $LIBS"
                    found_ssl_lib="1"
            )
    else
            LIBS="-L$SSLTOP/lib -lssl -lcrypto $LIBS"
            AC_CHECK_LIB(ssl, SSL_accept, found_ssl_lib="1")
    fi
    if test "$found_ssl_lib" = "0"; then
            AC_MSG_ERROR([cannot locate libssl library.])
    fi
    
    AC_DEFINE(USE_SSL, [], [Defined to enable SSL support.])
    AC_SUBST(DEFS)
    AC_SUBST(INC)
    AC_SUBST(SSLTOP)
fi
])
