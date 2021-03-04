## Process this file with automake to produce Makefile.in
include $(top_srcdir)/flymake.am

AUTOMAKE_OPTIONS = 1.6 foreign

subdir = doc

if BUILD_INFO
INFO_DOCFILES = mutt.info mutt.texi
endif
if BUILD_DOC
BUILD_DOC_TARGETS = stamp-doc-chunked manual.html manual.txt $(INFO_DOCFILES)
endif

DEFS = -DSYSCONFDIR=\"$(sysconfdir)\" -DBINDIR=\"$(bindir)\" -DHAVE_CONFIG_H=1
AM_CPPFLAGS = -I. -I.. -I$(includedir) -I$(top_srcdir)

MAKEDOC_CPP = $(CPP) $(AM_CPPFLAGS) $(DEFS) $(CPPFLAGS) -D_MAKEDOC -C

EXTRA_DIST = dotlock.man		\
        smime_keys.man                  \
	mutt.man			\
	pgpewrap.man			\
	pgpring.man			\
	PGP-Notes.txt			\
	applying-patches.txt		\
	devel-notes.txt			\
	gen-map-doc			\
	muttrc.man.head			\
	muttrc.man.tail			\
	mbox.man			\
	mmdf.man			\
	manual.xml.head			\
	manual.xml.tail			\
	instdoc.sh.in			\
	patch-notes.txt			\
	smime-notes.txt			\
	Muttrc.head			\
	makedoc.pl makedoc-defs.h 	\
	mutt.css mutt.xsl html.xsl chunk.xsl $(BUILT_DISTFILES)

CHUNKED_DOCFILES = index.html intro.html gettingstarted.html \
	configuration.html mimesupport.html advancedusage.html \
	optionalfeatures.html security.html tuning.html reference.html miscellany.html

HTML_DOCFILES = manual.html $(CHUNKED_DOCFILES)

# Note: these are bundled with a dist tarball, so they can be
# installed from $(srcdir), after checking that they haven't been
# rebuilt in the build directory.  This could happen in a git checkout
# with a separate build directory, for example.
#
# Muttrc and stamp-doc-rc are not included because of docdir substitution.
# manual.txt is not included because of different generation tool preferences.
BUILT_DISTFILES = stamp-doc-xml		\
		stamp-doc-chunked	\
		muttrc.man		\
		$(HTML_DOCFILES)	\
		$(INFO_DOCFILES)

srcdir_DOCFILES = PGP-Notes.txt applying-patches.txt	\
	devel-notes.txt patch-notes.txt smime-notes.txt

topsrcdir_DOCFILES = COPYRIGHT GPL INSTALL ChangeLog	\
	README NEWS TODO README.SECURITY README.SSL 

all: makedoc-all

makedoc-all: mutt.1 smime_keys.1 muttrc.man stamp-doc-rc pgpewrap.1 mutt_pgpring.1 $(BUILD_DOC_TARGETS)

install-data-local: makedoc-all instdoc
	$(MKDIR_P) $(DESTDIR)$(mandir)/man1
	$(MKDIR_P) $(DESTDIR)$(mandir)/man5
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)
	./instdoc mutt.1 $(DESTDIR)$(mandir)/man1/mutt.1
	./instdoc smime_keys.1 $(DESTDIR)$(mandir)/man1/smime_keys.1
	./instdoc pgpewrap.1 $(DESTDIR)$(mandir)/man1/pgpewrap.1
	./instdoc mutt_pgpring.1 $(DESTDIR)$(mandir)/man1/mutt_pgpring.1
	test x$(DOTLOCK_TARGET) = x || ./instdoc $(srcdir)/dotlock.man \
		$(DESTDIR)$(mandir)/man1/mutt_dotlock.1
	-if [ -f muttrc.man ] ; then \
	  ./instdoc muttrc.man $(DESTDIR)$(mandir)/man5/muttrc.5 ; \
	elif [ -f $(srcdir)/muttrc.man ] ; then \
	  ./instdoc $(srcdir)/muttrc.man $(DESTDIR)$(mandir)/man5/muttrc.5 ; \
	fi
	./instdoc $(srcdir)/mbox.man $(DESTDIR)$(mandir)/man5/mbox.5
	./instdoc $(srcdir)/mmdf.man $(DESTDIR)$(mandir)/man5/mmdf.5
	$(MKDIR_P) $(DESTDIR)$(docdir)
	for f in $(topsrcdir_DOCFILES) ; do \
		$(INSTALL) -m 644 $(top_srcdir)/$$f $(DESTDIR)$(docdir) ; \
	done
	for f in $(srcdir_DOCFILES) ; do \
		$(INSTALL) -m 644 $(srcdir)/$$f $(DESTDIR)$(docdir) ; \
	done
	-if [ -s manual.txt ] ; then \
	  $(INSTALL) -m 644 manual.txt $(DESTDIR)$(docdir) ; \
	fi
	-for f in $(HTML_DOCFILES) ; do \
	  if [ -f $$f ] ; then \
	    $(INSTALL) -m 644 $$f $(DESTDIR)$(docdir) ; \
	  elif [ -f $(srcdir)/$$f ] ; then \
	    $(INSTALL) -m 644 $(srcdir)/$$f $(DESTDIR)$(docdir) ; \
	  fi \
	done
	$(INSTALL) -m 644 Muttrc $(DESTDIR)$(sysconfdir)/Muttrc.dist
	-if [ -f $(DESTDIR)$(pkgdatadir)/Muttrc ] ; then \
		mv $(DESTDIR)$(pkgdatadir)/Muttrc* $(DESTDIR)$(sysconfdir) ; \
	elif [ -f $(DESTDIR)$(pkgdatadir)/../Muttrc ] ; then \
	 	mv $(DESTDIR)$(pkgdatadir)/../Muttrc* $(DESTDIR)$(sysconfdir) ; \
	elif [ ! -f $(DESTDIR)$(sysconfdir)/Muttrc ] ; then \
		$(INSTALL) -m 644 Muttrc $(DESTDIR)$(sysconfdir) ; \
	fi
	-if [ -f mutt.info ] ; then \
	  $(MKDIR_P) $(DESTDIR)$(infodir) ; \
	  $(INSTALL) -m 644 mutt.info $(DESTDIR)$(infodir) ; \
	  if command -v install-info >/dev/null 2>&1 ; then \
	    install-info $(DESTDIR)$(infodir)/mutt.info $(DESTDIR)$(infodir)/dir ; \
	  fi \
	elif [ -f $(srcdir)/mutt.info ] ; then \
	  $(MKDIR_P) $(DESTDIR)$(infodir) ; \
	  $(INSTALL) -m 644 $(srcdir)/mutt.info $(DESTDIR)$(infodir) ; \
	  if command -v install-info >/dev/null 2>&1 ; then \
	    install-info $(DESTDIR)$(infodir)/mutt.info $(DESTDIR)$(infodir)/dir ; \
	  fi \
	fi

uninstall-local:
	for f in mutt.1 mutt_dotlock.1 smime_keys.1 pgpewrap.1 mutt_pgpring.1; do \
		rm -f $(DESTDIR)$(mandir)/man1/$$f ; \
	done
	for f in muttrc.5 mbox.5 mmdf.5 ; do \
		rm -f $(DESTDIR)$(mandir)/man5/$$f ; \
	done
	for f in $(srcdir_DOCFILES) $(topsrcdir_DOCFILES) $(HTML_DOCFILES) ; \
	do \
		rm -f $(DESTDIR)$(docdir)/$$f ; \
	done
	-rm -f $(DESTDIR)$(docdir)/manual.txt
	for i in Muttrc ; do \
	  if cmp -s $(DESTDIR)$(sysconfdir)/$$i.dist $(DESTDIR)$(sysconfdir)/$$i ; then \
		rm -f $(DESTDIR)$(sysconfdir)/$$i ; \
	  fi ; \
	  rm -f $(DESTDIR)$(sysconfdir)/$${i}.dist ; \
	done
	-if [ -f $(DESTDIR)$(infodir)/mutt.info ] ; then \
	  if command -v install-info >/dev/null 2>&1 ; then \
	    install-info --delete $(DESTDIR)$(infodir)/mutt.info $(DESTDIR)$(infodir)/dir ; \
	  fi ; \
	  rm -f $(DESTDIR)$(infodir)/mutt.info ; \
	fi

check:
manual.txt: manual.html
	-LC_ALL=C lynx -localhost -dump -nolist -nonumbers -with_backspaces -display_charset=us-ascii manual.html > $@ || \
	LC_ALL=C w3m -T text/html -I utf-8 -O utf-8 -dump < manual.html > $@ || \
	LC_ALL=C elinks -dump -no-numbering -no-references manual.html | sed -e 's,\\001, ,g' > $@

Muttrc: stamp-doc-rc

stamp-doc-rc: $(top_srcdir)/init.h makedoc.pl $(srcdir)/Muttrc.head
	-rm -f Muttrc stamp-doc-rc
	sed -e 's,[@]docdir[@],$(docdir),' $(srcdir)/Muttrc.head > Muttrc
	$(CPP) $(AM_CPPFLAGS) $(DEFS) $(CPPFLAGS) -D_MAKEDOC -C \
	  $(top_srcdir)/init.h | perl $(srcdir)/makedoc.pl -c >> Muttrc
	touch stamp-doc-rc

manual.html: $(srcdir)/html.xsl $(srcdir)/mutt.xsl stamp-doc-xml $(srcdir)/mutt.css
	-xsltproc --nonet -o $@ $(srcdir)/html.xsl manual.xml

stamp-doc-chunked: $(srcdir)/chunk.xsl $(srcdir)/mutt.xsl stamp-doc-xml $(srcdir)/mutt.css
	-xsltproc --nonet $(srcdir)/chunk.xsl manual.xml
	touch stamp-doc-chunked

$(CHUNKED_DOCFILES): stamp-doc-chunked

manual.pdf: manual.tex
	-if test -f manual.tex; then pdfjadetex manual.tex; pdfjadetex manual.tex; pdfjadetex manual.tex; fi

# Note / Request For Help:
# On Debian, the below invocation of openjade is not working for me.
# I was able to generate manual.tex by instead running:
#     openjade -t tex -D $(DSLROOT) -d print/docbook.dsl \
#       -i xml /usr/share/xml/declaration/xml.dcl manual.xml
# However, being completely unfamiliar with these tools, am unsure how to
# make this portable.
manual.tex: stamp-doc-xml
	-if test -n "$(DSLROOT)"; then \
	  openjade -t tex -D $(DSLROOT) -d print/docbook.dsl dtds/decls/xml.dcl manual.xml; \
	fi

mutt.texi: stamp-doc-xml
	-$(DB2XTEXI) --encoding=utf-8 \
	    --string-param output-file=mutt \
	    --string-param 'directory-category=Email-software' \
	    --string-param 'directory-description=Text based mail reader' \
	    manual.xml
	@-if test ! -f mutt.texi; then \
	  echo "WARNING: mutt.texi generation failed!"; \
	  echo "To generate Gnu Info documentation, install docbook2x."; \
	  echo "On Debian based systems, this is the 'docbook2x' package."; \
	  echo "On Red Hat/Fedora systems, this is the 'docbook2X' package."; \
	  echo "After installing the package, please re-run configure."; \
	fi

mutt.info: mutt.texi
	-$(MAKEINFO) --no-split -o mutt.info mutt.texi

validate: stamp-doc-xml
	xmllint --noout --noblanks --postvalid manual.xml

spellcheck:
	-aspell -d american --mode=sgml  --encoding=utf-8 -p $(srcdir)/mutt.pwl check manual.xml.head
	-aspell -d american --mode=nroff --encoding=utf-8 -p $(srcdir)/mutt.pwl check muttrc.man.head
	-aspell -d american --mode=ccpp  --encoding=utf-8 -p $(srcdir)/mutt.pwl check $(top_srcdir)/init.h

sortcheck: manual.xml
	sed -n -e '1,/^<sect1 id="variables">/d' -e '1,/^<sect1 id="functions">/s/<sect2 id="\([^"]*\)">/\1/p' < manual.xml > vars.tmp.1
	sort < vars.tmp.1 > vars.tmp.2
	cmp -s vars.tmp.1 vars.tmp.2 || diff -u vars.tmp.1 vars.tmp.2 | less
	rm -rf vars.tmp.1 vars.tmp.2

clean-local:
	rm -f *~ *.html *.orig *.rej  stamp-doc-* *.ps mutt.1 smime_keys.1 muttrc.man pgpewrap.1 mutt_pgpring.1
	rm -f *.aux *.log *.tex *.out mutt.texi $(INFO_DOCFILES)

DISTCLEANFILES=manual.txt manual.html manual.xml manual.pdf html \
	       instdoc Muttrc

instdoc: instdoc.sh
	cat instdoc.sh > instdoc
	chmod a+x instdoc

update-doc: stamp-doc-rc $(BUILD_DOC_TARGETS)

muttrc.man: makedoc.pl $(top_srcdir)/init.h muttrc.man.head muttrc.man.tail
	$(MAKEDOC_CPP) $(top_srcdir)/init.h | perl $(srcdir)/makedoc.pl -m | 	    \
		cat $(srcdir)/muttrc.man.head - $(srcdir)/muttrc.man.tail\
		> muttrc.man

EDIT = sed -e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@pkgdatadir\@,$(pkgdatadir),g' \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@docdir\@,$(docdir),g'

mutt.1: $(srcdir)/mutt.man
	$(EDIT) $(srcdir)/mutt.man > $@

pgpewrap.1: $(srcdir)/pgpewrap.man
	$(EDIT) $(srcdir)/pgpewrap.man > $@

mutt_pgpring.1: $(srcdir)/pgpring.man
	$(EDIT) $(srcdir)/pgpring.man > $@

smime_keys.1: $(srcdir)/smime_keys.man
	$(EDIT) $(srcdir)/smime_keys.man > $@

stamp-doc-xml: makedoc.pl $(top_srcdir)/init.h \
               manual.xml.head $(top_srcdir)/functions.h $(top_srcdir)/OPS* manual.xml.tail \
               $(srcdir)/gen-map-doc $(top_srcdir)/VERSION $(top_srcdir)/ChangeLog
	( date=`(cd $(top_srcdir) && ./mkreldate.sh)` && \
	  version=`(cd $(top_srcdir) && env sh ./version.sh)` && \
	  sed -e "s/@VERSION\@/$$version ($$date)/" $(srcdir)/manual.xml.head && \
	  $(MAKEDOC_CPP) $(top_srcdir)/init.h | perl $(srcdir)/makedoc.pl -s && \
	  $(MAKEDOC_CPP) $(top_srcdir)/functions.h | \
	    perl $(srcdir)/gen-map-doc $(srcdir)/manual.xml.tail $(top_srcdir)/OPS* \
	) > manual.xml
	touch stamp-doc-xml
