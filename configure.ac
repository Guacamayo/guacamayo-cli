m4_define([guacacli_major], [0])
m4_define([guacacli_minor], [1])
m4_define([guacacli_micro], [0])

m4_define([guacacli_version], [guacacli_major.guacacli_minor.guacacli_micro])

AC_PREREQ(2.53)
AC_INIT([guacamayo-guacacli], [guacacli_version], [http://guacamayo-project.org])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.9 foreign no-dist-gzip dist-xz tar-ustar])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

MAJOR_VERSION=guacacli_major
MINOR_VERSION=guacacli_minor
MICRO_VERSION=guacacli_micro
VERSION=guacacli_version
AC_SUBST(MAJOR_VERSION)
AC_SUBST(MINOR_VERSION)
AC_SUBST(MICRO_VERSION)
AC_SUBST(VERSION)

# check for programs
AC_PROG_CC
AM_PROG_CC_C_O

PKG_PROG_PKG_CONFIG

# check for headers
AC_HEADER_STDC

AC_CHECK_HEADERS_ONCE([guacamayo-version.h])

modules="glib-2.0"

PKG_CHECK_MODULES(CLI, "$modules")

WORKING_READLINE=no
AC_CHECK_LIB(readline, readline, [WORKING_READLINE=yes], [WORKING_READLINE=no])
AC_CHECK_LIB(history,add_history,[WORKING_HISTORY=yes],[WORKING_HISTORY=no])

if test x$cross_compiling != xyes ; then
   if test x$WORKING_READLINE != xyes ; then
       AC_MSG_ERROR([Cound not find working readline library.])
   fi
   if test x$WORKING_HISTORY != xyes ; then
       AC_MSG_ERROR([Cound not find working history library.])
   fi
fi

CLI_LIBS="$CLI_LIBS -lreadline -lhistory"

AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug],
                                   [Enable debuggin information]),
              [D_CFLAGS="-g -O0 -DDEBUG"], [D_CFLAGS="-g -O2"])

CLI_CFLAGS="$CLI_CFLAGS $D_CFLAGS"

AC_SUBST(CLI_LIBS)
AC_SUBST(CLI_CFLAGS)


AC_CONFIG_FILES([
    Makefile
    src/Makefile
])

AC_OUTPUT

AC_MSG_RESULT([Type make to build guaca-cli])
