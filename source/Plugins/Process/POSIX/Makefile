##===- source/Plugins/Process/POSIX/Makefile ---------------*- Makefile -*-===##
# 
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

LLDB_LEVEL := ../../../..
LIBRARYNAME := lldbPluginProcessPOSIX
BUILD_ARCHIVE = 1

include $(LLDB_LEVEL)/../../Makefile.config

# Extend the include path so we may locate UnwindLLDB.h
CPPFLAGS += -I$(LLDB_LEVEL)/source/Plugins/Utility

ifeq ($(HOST_OS),Linux)
CPPFLAGS += -I$(LLDB_LEVEL)/source/Plugins/Process/Linux
endif

ifeq ($(HOST_OS),FreeBSD)
# Extend the include path so we may locate ProcessMonitor
CPPFLAGS += -I $(LLDB_LEVEL)/source/Plugins/Process/FreeBSD
endif

include $(LLDB_LEVEL)/Makefile
