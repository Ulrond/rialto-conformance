#/**
# * Copyright 2026 RDK Management
# *
# * Licensed under the Apache License, Version 2.0 (the "License");
# * you may not use this file except in compliance with the License.
# * You may obtain a copy of the License at
# *
# * http://www.apache.org/licenses/LICENSE-2.0
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# *
# * SPDX-License-Identifier: Apache-2.0
# */
#
# Downstream ut-core Makefile (the §4 packaging model). It sets SRC_DIRS /
# INC_DIRS / the link surface and delegates to ut-core's Makefile. It compiles
# ONLY this repo's src/ tree and links ONLY Rialto's public client library +
# the GStreamer libs needed to drive the rialtomse*sink elements. It does NOT
# build Rialto and never compiles against Rialto internals.

ROOT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
TOP_DIR  := $(ROOT_DIR)
BIN_DIR  := $(ROOT_DIR)/build/bin

UT_CORE_DIR := $(ROOT_DIR)/framework/ut-core

# C++ / GoogleTest backend — the only variant this suite supports.
VARIANT := CPP
TARGET  ?= linux
TARGET_EXEC := rialto_conformance

# --- Source + include layout (the one binary, same sources on every platform)
SRC_DIRS := $(ROOT_DIR)/src
INC_DIRS := $(ROOT_DIR)/include

# We never touch ut-core's own compile flags (CFLAGS/CXXFLAGS/XCFLAGS) — those are
# ut-core's. The sanctioned caller extension points are INC_DIRS (ut-core folds
# these into BOTH the .c and .cpp compiles via INC_FLAGS) and YLDFLAGS (folded
# into the link). So we contribute the Rialto + GStreamer include DIRECTORIES to
# INC_DIRS and their libraries to YLDFLAGS, and otherwise leave ut-core's build
# alone (g++ defaults to gnu++17, which the suite is written against).

# --- Firebolt interface: Rialto's PUBLIC client library + headers ----------------------
# Headers (the API reference the cases are written against) come from the pinned
# rialto checkout that install.sh placed under framework/rialto, with pkg-config
# as a fallback. The runtime library (libRialtoClient) is resolved via pkg-config
# from the Rialto installed on the build host/target — the suite never builds it.
PKG_CONFIG ?= pkg-config

# Opt-in local software platform: if build-rialto.sh has built a native Rialto,
# its install prefix carries RialtoClient.pc + libRialtoClient. Auto-discover it
# (no env needed) and prepend to PKG_CONFIG_PATH BEFORE the pkg-config queries
# below, plus an rpath so the binary finds the built libs at runtime. Absent on a
# real target — there the installed platform Rialto is resolved instead.
RIALTO_NATIVE_PREFIX := $(ROOT_DIR)/framework/.native-install
ifneq ($(wildcard $(RIALTO_NATIVE_PREFIX)/lib/pkgconfig/RialtoClient.pc),)
export PKG_CONFIG_PATH := $(RIALTO_NATIVE_PREFIX)/lib/pkgconfig:$(PKG_CONFIG_PATH)
RIALTO_NATIVE_LIBDIR := $(RIALTO_NATIVE_PREFIX)/lib
endif

RIALTO_SRC_INC := $(ROOT_DIR)/framework/rialto/media/public/include
# Include DIRECTORIES (strip the -I from pkg-config --cflags) for INC_DIRS.
RIALTO_INC := $(patsubst -I%,%,$(filter -I%,$(shell $(PKG_CONFIG) --cflags RialtoClient 2>/dev/null)))
ifneq ($(wildcard $(RIALTO_SRC_INC)),)
RIALTO_INC += $(RIALTO_SRC_INC)
endif
RIALTO_LIBS   := $(shell $(PKG_CONFIG) --libs   RialtoClient 2>/dev/null)
ifeq ($(strip $(RIALTO_LIBS)),)
RIALTO_LIBS := -lRialtoClient
endif

# --- mseSink interface: GStreamer core/base, to introspect rialtomse*sink elements ----
# gstreamer-audio provides the GstStreamVolume interface the audio sink implements
# (RC-CORE-MSE-003), so it joins core/base/app in the introspection link surface.
GST_PKGS := gstreamer-1.0 gstreamer-app-1.0 gstreamer-audio-1.0
GST_INC  := $(patsubst -I%,%,$(filter -I%,$(shell $(PKG_CONFIG) --cflags $(GST_PKGS) 2>/dev/null)))
GST_LIBS := $(shell $(PKG_CONFIG) --libs $(GST_PKGS) 2>/dev/null)
ifeq ($(strip $(GST_LIBS)),)
GST_LIBS := -lgstreamer-1.0 -lgstapp-1.0 -lgstaudio-1.0 -lgobject-2.0 -lglib-2.0
endif

# Sanctioned ut-core hooks: include dirs via INC_DIRS, libraries via YLDFLAGS.
INC_DIRS += $(RIALTO_INC) $(GST_INC)
YLDFLAGS := $(RIALTO_LIBS) $(GST_LIBS)

# rpath to the locally-built Rialto (software platform), when present.
ifneq ($(strip $(RIALTO_NATIVE_LIBDIR)),)
YLDFLAGS += -Wl,-rpath,$(RIALTO_NATIVE_LIBDIR) -L$(RIALTO_NATIVE_LIBDIR)
endif

# Opt-in leak checking: MEMCHECK=1 links the suite against standalone
# LeakSanitizer (-fsanitize=leak), which reports leaks at process exit for the
# L3 memory-usage dimension. It goes through YLDFLAGS — ut-core's only sanctioned
# link hook — so ut-core's own compile flags (CFLAGS/CXXFLAGS/XCFLAGS) are left
# untouched (invariant: never override ut-core's build flags). LSan needs no
# compile-time instrumentation (unlike full AddressSanitizer, which would require
# a ut-core compile hook the caller does not have); it intercepts the allocator
# at runtime and scans for unreachable blocks at exit. Deep server-side ASan is a
# separate opt-in in build-rialto.sh (that script owns Rialto's own build flags).
ifdef MEMCHECK
YLDFLAGS += -fsanitize=leak
endif

# For an arm target, vendor libs (incl. the installed RialtoClient) live under
# libs/ and need an rpath so the deployed binary finds them on the device.
ifeq ($(TARGET),arm)
HAL_LIB_DIR := $(ROOT_DIR)/libs
YLDFLAGS += -Wl,-rpath,$(HAL_LIB_DIR) -L$(HAL_LIB_DIR)
export HAL_LIB_DIR
endif

export TOP_DIR BIN_DIR SRC_DIRS INC_DIRS TARGET TARGET_EXEC VARIANT YLDFLAGS

.PHONY: build clean cleanall framework

build: | framework
	$(MAKE) -C $(UT_CORE_DIR)

# Guard: the framework must be installed at fixed versions first (install.sh).
framework:
	@if [ ! -f $(UT_CORE_DIR)/Makefile ]; then \
	    echo "ERROR: framework/ut-core not found. Run ./install.sh (installs pinned deps)."; \
	    exit 1; \
	fi

clean:
	@if [ -f $(UT_CORE_DIR)/Makefile ]; then $(MAKE) -C $(UT_CORE_DIR) clean; fi
	rm -rf $(ROOT_DIR)/build

cleanall:
	@if [ -f $(UT_CORE_DIR)/Makefile ]; then $(MAKE) -C $(UT_CORE_DIR) cleanall; fi
	rm -rf $(ROOT_DIR)/build $(ROOT_DIR)/framework
