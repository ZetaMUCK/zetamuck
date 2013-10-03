AC_ARG_ENABLE(gprof,
[  --enable-gprof          compile with gprof profiling enabled],
[
if test "$enableval" = "yes"; then
    DEBUG_FLAGS+="-pg"
fi
])

AC_ARG_ENABLE(debug,
[  --enable-debug[=number]   Compile with extended debugging enabled. This is the
                          same as setting -g(number) in CFLAGS. The keyword "full"
                          is the same as "--enable-debug=3" and disables compiler
                          optimizations. (CFLAGS=-g3 -O0) ],
[
    if test "$enableval" = "no"; then
        DEBUG_FLAGS+=""
    elif test "$enableval" = "full"; then
        DEBUG_FLAGS="-g3 -O0"
    elif test "$enableval" != "yes"; then
        # TODO: Make sure it's a number.
        DEBUG_FLAGS+="-g${enableval}"
    else
        DEBUG_FLAGS+="-g"
    fi

],
[
    # Assume they want debugging symbols.
    DEBUG_FLAGS+=" -g"
])

AC_SUBST([DEBUG_FLAGS])
