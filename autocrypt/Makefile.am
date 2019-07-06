## Process this file with automake to produce Makefile.in
include $(top_srcdir)/flymake.am

AUTOMAKE_OPTIONS = 1.6 foreign

AM_CPPFLAGS = -I$(top_srcdir) -I../intl

noinst_LIBRARIES = libautocrypt.a

libautocrypt_a_SOURCES = autocrypt.c autocrypt.h autocrypt_db.c autocrypt_private.h \
	autocrypt_schema.c
