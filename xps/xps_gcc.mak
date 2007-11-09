# The "?=" style of this makefile is designed to facilitate "deriving"
# your own make file from it by setting your own custom options, then include'ing
# this file. In its current form, this file will compile using default options
# and locations. It is recommended that you make any modifications to settings
# in this file by creating your own makefile which includes this one.
#
# This file only defines the portions of the makefile that are different
# between the present language switcher vs. the standard pcl6 makefile which
# is included near the bottom. All other settings default to the base makefile.

# Define the name of this makefile.
MAKEFILE+= ../xps/xps_gcc.mak

# Include XPS support
XPS_INCLUDED?=TRUE

# Font scaler
PL_SCALER?=afs

# The build process will put all of its output in this directory:
# GENDIR is defined in the 'base' makefile, but we need its value immediately
GENDIR?=./obj

# The sources are taken from these directories:
MAINSRCDIR?=../main
PSSRCDIR?=../gs/src
XPSSRCDIR?=../xps
PSLIBDIR?=../gs/lib
ICCSRCDIR?=../gs/icclib
PNGSRCDIR?=../gs/libpng

# PLPLATFORM indicates should be set to 'ps' for language switch
# builds and null otherwise.
PLPLATFORM?=

# If you want to build the individual packages in their own directories,
# you can define this here, although normally you won't need to do this:
GLGENDIR?=$(GENDIR)
GLOBJDIR?=$(GENDIR)
PSGENDIR?=$(GENDIR)
PSOBJDIR?=$(GENDIR)
JGENDIR?=$(GENDIR)
JOBJDIR?=$(GENDIR)
ZGENDIR?=$(GENDIR)
ZOBJDIR?=$(GENDIR)

# Executable path\name w/o the .EXE extension
TARGET_XE?=$(GENDIR)/gxps

# Main file's name
# this is already in pcl6_gcc.mak
#XPS_TOP_OBJ?=$(XPSOBJDIR)/xpstop.$(OBJ)
#TOP_OBJ+= $(XPS_TOP_OBJ)
#XCFLAGS?=-DXPS_INCLUDED

# Choose COMPILE_INITS=1 for init files and fonts in ROM (otherwise =0)
COMPILE_INITS?=0

# configuration assumptions
GX_COLOR_INDEX_DEFINE?=-DGX_COLOR_INDEX_TYPE="unsigned long long"
HAVE_STDINT_H_DEFINE?=-DHAVE_STDINT_H
HAVE_MKSTEMP_DEFINE?=-DHAVE_MKSTEMP

GCFLAGS?=-Wall -Wpointer-arith -Wstrict-prototypes -Wwrite-strings -DNDEBUG $(HAVE_STDINT_H_DEFINE) $(HAVE_MKSTEMP_DEFINE) $(GX_COLOR_INDEX_DEFINE)
CFLAGS?=-g -O0 $(GCFLAGS) $(XCFLAGS)

#FEATURE_DEVS?=$(DD)gsnogc.dev\
#	$(DD)ttflib.dev $(DD)psf1lib.dev $(DD)psf2lib.dev

# "Subclassed" makefile
include $(MAINSRCDIR)/pcl6_gcc.mak

# Subsystems
# this is already in pcl6_gcc.mak
#include $(XPSSRCDIR)/xps.mak
