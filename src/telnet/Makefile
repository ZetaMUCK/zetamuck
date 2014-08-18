#!/bin/make -f

# #######################################################################
#			User tunable options
# #######################################################################

#
# Prepend line.  used for compiling with things like nice.
#
# PRE= nice -10

#
# Optimizations
# -O			General optimizations, recomended.
# -O2			Heavier optimizations, use at own risk.
# -O0           No optimizations, useful inside of debuggers.
#
# Code comitted to trunk should not generate compiler warnings.
#
# Use ./configure --with-debug=# to set a higher debugging level, or
# --without-debug to compile without -g at all.
#
# --enable-debug=full is a shortcut for -g3 -O0.
# --enable-profiling generates profiling data for gprof. Rarely needed.

OPTIM=-Wall -O2 -U_FORTIFY_SOURCE

#
# Paths
#

# Right below the src/include directories
ROOT= ..

# Where the include files are
INCLUDE= ../inc
# Destination for binaries
DEST=${ROOT}/game

# #######################################################################
# 		Variables set by the configure script.
# #######################################################################
CC=gcc
#CXX=@CXX@
DEFS=-DHAVE_CONFIG_H
LIBR=-lz -lpcre  -L/usr/lib -lpcre -lm -lnsl

# #######################################################################
#  	   Usualy don't need to change anything past here.
# #######################################################################

#
# Include path, Cflags...
#
CFLAGS=${OPTIM}  -g ${DEFS} -I${INCLUDE}  -I/usr/include 
#CXXFLAGS=${OPTIM} ${DEFS} -I${INCLUDE}  -I/usr/include 

#
# for saving old copies.
#
MV= mv -f
RM= -rm -f

#
# Misc stuff for odd targets.
#
SRC_PREFIX=
MAKE= make

##########################################################################
#            You should not have to change anything below here           #
##########################################################################

SRC= telnet.c charset.c mccp.c mssp.c termtype.c
OBJ= telnet.o charset.o mccp.o mssp.o termtype.o

telnet:        ${P} ${SRC}

#############################################################
# Funky stuff for debugging and coding work.
#


########################################################
# Cleanup stuff.
#


clean:
	${RM} ${OBJ}


# #######################################################################
# #######################################################################

# DO NOT DELETE THIS LINE -- make depend depends on it.
