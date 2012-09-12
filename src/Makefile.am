
AM_CFLAGS = $(CLI_CFLAGS)

bin_PROGRAMS=guacamayo-cli

guacamayo_cli_SOURCES =	main.c						\
			hostname.c hostname.h				\
			timezone.c timezone.h				\
			connman.c  connman.h				\
			mtn-connman.c mtn-connman.h			\
			mtn-connman-service.c mtn-connman-service.h


guacamayo_cli_LDADD   = $(CLI_LIBS)

DISTCLEANFILES = *~ Makefile.in
