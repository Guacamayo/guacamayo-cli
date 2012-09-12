SUBDIRS=src

DISTCLEANFILES = *~ Makefile.in install-sh missing depcomp *.m4 config.log config.status Makefile

local-distclean:
	$(RM) -rf autom4te.cache

distclean: distclean-recursive local-distclean
