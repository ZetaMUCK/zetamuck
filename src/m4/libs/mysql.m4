
AC_ARG_WITH(mysql,
[  --with-mysql[=mysqlpath]  use MySQL database support],
[
if test "$withval" != no; then
    if test "$withval" = yes; then
        MYSQLTOP=
    else
        MYSQLTOP=$withval
    fi
    
    found_mysql="0"
    if test "$MYSQLTOP" = ""; then
        AC_CHECK_HEADERS(mysql.h,
    	found_mysql="1")
    
        AC_CHECK_HEADERS(mysql/mysql.h,
            AC_DEFINE(HAVE_MYSQL_H, [], [Defined if mysql.h exists.])
            INCL="-Imysql -L/usr/lib/mysql $INCL" 
            found_mysql="1"
        )
    else
        AC_CHECK_HEADERS($MYSQLTOP/include/mysql/mysql.h,  
            AC_DEFINE(HAVE_MYSQL_H, [], [Defined if mysql.h exists.])
            INCL="-I$MYSQLTOP/include -I$MYSQLTOP/include/mysql -L$MYSQLTOP/lib -L$MYSQLTOP/lib/mysql $INCL"
            LIBS="-L$MYSQLTOP/lib -L$MYSQLTOP/lib/mysql $LIBS"
            found_mysql="1"
        )
    fi
    
    if test "$found_mysqlh" = "0"; then
        AC_MSG_ERROR([cannot locate mysql.h header file.])
    fi
    
    AC_DEFINE(SQL_SUPPORT, [], [Defined to enable MySQL support])
    AC_SUBST(INCL)
    LIBS="-lmysqlclient $LIBS"
fi
])
