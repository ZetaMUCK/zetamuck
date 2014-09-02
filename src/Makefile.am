#!/bin/make -f

# #######################################################################
#			User tunable options
# #######################################################################

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
INCLUDE= ./inc
# Destination for binaries
DEST=${ROOT}/game

# #######################################################################
#  	   Usualy don't need to change anything past here.
# #######################################################################

#
# Include path, Cflags...
#
AM_CFLAGS=${OPTIM} @DEBUG_FLAGS@ ${DEFS} -I${INCLUDE}

#
# for saving old copies.
#
##MV= mv -f
##RM= -rm -f

#
# Misc stuff for odd targets.
#
##SRC_PREFIX=
##MAKE= make
##MKTAGS= ctags

##########################################################################
#            You should not have to change anything below here           #
##########################################################################

MISCSRC= Makefile ChangeLog COPYING

CSRC= array.c boolexp.c cgi.c compile.c compress.c create.c db.c  \
	debugger.c disassem.c diskprop.c edit.c events.c game.c hashtab.c  \
	help.c inst.c interp.c log.c look.c match.c mcp.c mcppkgs.c        \
	mcpgui.c mfuns.c mfuns2.c mfuns3.c msgparse.c move.c mufevent.c    \
	crt_malloc.c oldcompress.c player.c predicates.c p_array.c         \
	p_connects.c p_db.c p_error.c p_float.c p_file.c p_html.c p_http.c \
        p_math.c p_mcp.c p_misc.c p_muf.c p_mysql.c p_props.c p_socket.c   \
        p_stack.c p_strings.c p_system.c p_regex.c p_json.c \
      property.c propdirs.c props.c random.c rob.c set.c          \
      signal.c smatch.c speech.c strftime.c stringutil.c timequeue.c     \
      timestamp.c tune.c unparse.c utils.c wiz.c reg.c netresolve.c      \
      sanity.c newhttp.c version.c sha1.c json.c

MSRC= olddecomp.c reconst.c interface.c
TSRC= telnet/telnet.c telnet/charset.c telnet/mccp.c telnet/mssp.c telnet/termtype.c

SRC= ${MISCSRC} ${CSRC} ${TSRC} ${MSRC}

AM_CFLAGS += ${UINC}

SUBDIRS =

noinst_PROGRAMS = protomuck
EXTRA_PROGRAMS = resolver reslvd mancheck topwords olddecompress
protomuck_SOURCES = ${CSRC} ${TSRC} interface.c

topwords_SOURCES = topwords.c hashtab.c
olddecompress_SOURCES = olddecomp.c oldcompress.c
protomuck_LDADD = ${LIBUNISTRING}

cert game/server.pem:
	@if grep "^#define USE_SSL" inc/autoconf.h > /dev/null; then \
		echo; \
		if [ -r ../game/data/server.pem ]; then \
			echo "Will not overwrite game/data/server.pem"; \
			echo "Remove that file and do 'make cert' again";\
			echo "to create a new certificate."; \
			echo; \
		else \
			if [ ! -r ${HOME}/.rnd ]; then \
			    openssl rand -rand /etc/hosts:/etc/passwd 0; \
			fi; \
			echo "Creating secure certificate. Please answer all the questions."; \
			echo "The key and certificate will be stored in the game/data/server.pem file."; \
			echo; \
			openssl req -x509 -nodes -out game/data/server.pem -keyout ../game/data/server.pem -config protomuck.cnf -new -days 730; \
		fi; \
	fi


#############################################################
# Funky stuff for debugging and coding work.
#

svnver:
	sed -i 's/#define SVNVER .*/#define SVNVER "'`svnversion .`'"/' inc/version.h

##tags: ${CSRC} ${MSRC}
##	${MKTAGS} ${CSRC} ${TSRC} interface.c inc/*.h

##sources: ${SRC}

##names:
##	@for i in ${SRC}; do echo ${SRC_PREFIX}$$i; done


########################################################
# Cleanup stuff.
#


##cleaner: clean
##	${RM} Makefile config.status config.cache inc/autoconf.h
##	${RM} ${TARGETS} config.log
##	cp Makefile.cfg Makefile
##	@if test -d "autom4te.cache"; then \
##		rm -Rf ./autom4te.cache; \
## 	fi

##clobber: cleaner
##	${RM} tags *~ *.bak *.orig

##distclean: cleaner

install: protomuck
	${MV} ${TARGETS} ${DEST}

##setup:
##	./setup

