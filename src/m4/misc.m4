dnl
dnl Uname -a, just becuse.
dnl
echo "checking value of uname -a"
AC_DEFINE_UNQUOTED(UNAME_VALUE, "`uname -a`", [The value of the uname -a command.])


if test \! -r inc/config.h
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
