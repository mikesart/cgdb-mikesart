AM_CXXFLAGS = \
    -I$(top_srcdir)/lib/adt \
    -I$(top_srcdir)/lib/rline \
    -I$(top_srcdir)/lib/util \
    -I$(top_srcdir)/lib/tgdb \
    -I$(top_srcdir)/lib/libmigdb/src

noinst_LIBRARIES = libtgdb.a

libtgdb_a_SOURCES = \
    tgdb.cpp \
    tgdb.h \
    tgdb_command.cpp \
    tgdb_command.h \
    tgdb_types.cpp \
    tgdb_types.h \
    a2-tgdb.cpp \
    a2-tgdb.h \
    annotate.cpp \
    annotate.h \
    annotate_two.h \
    commands.cpp \
    commands.h \
    data.cpp \
    data.h \
    globals.cpp \
    globals.h \
    state_machine.cpp \
    state_machine.h

noinst_PROGRAMS = tgdb_driver

tgdb_driver_LDFLAGS = \
    -L$(top_builddir)/lib/adt \
    -L$(top_builddir)/lib/rline \
    -L$(top_builddir)/lib/util \
    -L$(top_builddir)/lib/tgdb \
    -L$(top_builddir)/lib/libmigdb/src

tgdb_driver_LDADD = \
    libtgdb.a \
    $(top_builddir)/lib/libmigdb/src/libmigdb.a \
    $(top_builddir)/lib/rline/librline.a \
    $(top_builddir)/lib/adt/libadt.a \
    $(top_builddir)/lib/util/libcgdbutil.a

tgdb_driver_SOURCES = driver.cpp
