dnl
dnl Uname -a, just becuse.
dnl
echo "checking value of uname -a"
AC_DEFINE_UNQUOTED(UNAME_VALUE, "$(uname -a)", [The value of the uname -a command.])


if test ! -r inc/config.h
then
    echo "Generating inc/config.h..."
    cp inc/config.h.in inc/config.h
else
    echo -e "\nPreserving your existing inc/config.h."
    echo "You may be missing new features."
fi

echo " "
echo "You should review the options in inc/config.h, and"
echo "then type the following to build your system:"
echo " "
echo " make"
echo " "

if test "$unigbrk_nag" = "yes"
then
    echo " "
    echo "WARNING: libunistring is present but less than 0.9.4. It is not likely"
    echo "         to be supplied by your OS packaging. Please download, compile,"
    echo "         and install the following source code. (and rerun configure)"
    echo " "
    echo "         http://ftp.gnu.org/gnu/libunistring/libunistring-0.9.4.tar.gz"
    echo " "
    echo "         Don't forget to run \"ldconfig\" as root after installing the"
    echo "         new library!"
else
    if test "$HAVE_LIBUNISTRING" = "yes"
    then
        echo "If the build fails due to a missing u8_grapheme_breaks symbol, try"
        echo "re-running configure like so:"
        echo " "
        echo "LDFLAGS=\"-L/usr/local/lib\" ./configure"
        echo " "
    fi
fi

