AUTOMAKE_OPTIONS = foreign

lib_LTLIBRARIES = \
    libglobalQA.la

AM_LDFLAGS = \
  -L$(libdir) \
  -L$(OFFLINE_MAIN)/lib \
  -L$(OFFLINE_MAIN)/lib64

AM_CPPFLAGS = \
  -I$(includedir) \
  -isystem$(OFFLINE_MAIN)/include \
  -isystem$(ROOTSYS)/include

pkginclude_HEADERS = \
  GlobalQA.h

libglobalQA_la_SOURCES = \
  GlobalQA.cc

libglobalQA_la_LIBADD = \
  -lcalo_io \
  -lcdbobjects \
  -lffamodules \
  -lffarawobjects \
  -lfun4all \
  -lglobalvertex_io \
  -lmbd_io \
  -lphool \
  -lqautils \
  -lSubsysReco \
  -lzdcinfo_io

################################################
# linking tests

noinst_PROGRAMS = \
  testexternals

testexternals_SOURCES = testexternals.C
testexternals_LDADD = libglobalQA.la

testexternals.C:
	echo "//*** this is a generated file. Do not commit, do not edit" > $@
	echo "int main()" >> $@
	echo "{" >> $@
	echo "  return 0;" >> $@
	echo "}" >> $@

clean-local:
	rm -f $(BUILT_SOURCES)
