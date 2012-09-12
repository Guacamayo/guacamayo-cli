
AM_CFLAGS = $(CLI_CFLAGS)

bin_PROGRAMS=guaca-cli

guaca_cli_SOURCES =	main.c						\
			hostname.c hostname.h				\
			timezone.c timezone.h				\
			connman.c  connman.h				\
			mtn-connman.c mtn-connman.h			\
			mtn-connman-service.c mtn-connman-service.h


guaca_cli_LDADD   = $(CLI_LIBS)

DISTCLEANFILES = *~ Makefile.in
