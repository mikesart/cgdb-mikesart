AM_CXXFLAGS = -I$(top_srcdir)/lib/adt

# create the input library
noinst_LIBRARIES = libwm.a
libwm_a_SOURCES = \
    wm.cpp \
    wm.h \
    wm_splitter.cpp \
    wm_splitter.h \
    wm_window.cpp \
    wm_window.h \
    types.h

# Installs the driver programs into progs directory
noinst_PROGRAMS = wm_driver

# This is the wm driver
wm_driver_LDFLAGS =
wm_driver_LDADD = libwm.a
wm_driver_SOURCES = wm_driver.cpp
wm_driver_CXXFLAGS = $(AM_CXXFLAGS)
