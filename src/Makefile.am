AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libbitcoin-watcher.la
AM_CPPFLAGS = -I$(srcdir)/../include $(libbitcoin_CFLAGS)
libbitcoin_watcher_la_SOURCES = \
    watcher.cpp \
    picker.cpp

libbitcoin_watcher_la_LIBADD = $(libbitcoin_LIBS)

