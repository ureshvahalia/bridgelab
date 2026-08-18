# Compiler and flags
CXX = g++
CC  = gcc
LEX = flex
YACC = bison

# Default build type
TARGET = Debug

# ── DDS version selection ──────────────────────────────────────────────────────
# DDS=2 (default) links the bundled DDS 2.9.0 library in ../lib — output paths
# are unchanged from before this option existed.
# DDS=3 links DDS 3.0 (built from https://github.com/dds-bridge/dds, develop
# branch), staged in ../lib/dds3. Both versions are linked against the same
# shared/dll.h: DDS 3.0 renamed its own struct/field names internally, but the
# memory layout is unchanged, so the old header remains valid against either
# library with no source changes. Can be overridden: make DDS=3
DDS ?= 2
ifeq ($(DDS),2)
    DDS_SUFFIX =
else
    DDS_SUFFIX = -dds$(DDS)
endif

# ── Platform detection ────────────────────────────────────────────────────────
# Detects MSYS2, Cygwin, or Linux (including WSL).
# Can be overridden on the command line: make PLAT=linux
UNAME_O := $(shell uname -o 2>/dev/null)
UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(UNAME_O),Msys)
    PLAT = msys2
else ifeq ($(UNAME_S),$(filter $(UNAME_S),CYGWIN_NT CYGWIN))
    PLAT = cygwin
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    PLAT = cygwin
else
    PLAT = linux
endif

# ── Platform-specific settings ────────────────────────────────────────────────
ifeq ($(PLAT),msys2)
    # MSYS2 UCRT64: shared lib/ at repo root; GSL linked statically, only dds.dll needed at runtime
    PLAT_INCLUDES  = -I/ucrt64/include
    PLAT_LDFLAGS   = -L../lib -static-libgcc -static-libstdc++
    PLAT_LIBS      = ../lib/ucrt64/dds.dll -Wl,-Bstatic -lgsl -lgslcblas -Wl,-Bdynamic
    PLAT_COPY_DLLS = ../lib/ucrt64/dds.dll
    EXE_SUFFIX     = .exe

else ifeq ($(PLAT),cygwin)
    # Cygwin: system GSL; DDS must be a Cygwin-built shared library
    PLAT_INCLUDES =
    PLAT_LDFLAGS  = -L../lib
    PLAT_LIBS     = -ldds -lgsl -lgslcblas
    EXE_SUFFIX    = .exe

else
    # Linux / WSL — libdds linked by path; GSL/boost come from system (../lib has MinGW builds)
    PLAT_INCLUDES =
    PLAT_LDFLAGS  =
    PLAT_LIBS     = ../lib/libdds.a -lboost_thread -lboost_system -lm -lrt -lgsl -lgslcblas
    EXE_SUFFIX    =
endif

# DDS=3 override: different library layout/link requirements per platform.
# DDS 3.0 has no boost dependency; GSL is still needed (our own pack.cpp RNG).
ifeq ($(DDS),3)
    ifeq ($(PLAT),linux)
        PLAT_LIBS = -Wl,--start-group ../lib/dds3/linux/libdds3.a -Wl,--end-group -lpthread -lm -lgsl -lgslcblas
    else
        $(error DDS=3 is not yet built for PLAT=$(PLAT) — build it via WSL/Linux first; see lib/dds3/)
    endif
endif

# ── Directory layout ──────────────────────────────────────────────────────────
SRC_DIR = src
BIN_DIR = bin/$(PLAT)/$(TARGET)$(DDS_SUFFIX)
OBJ_DIR = obj/$(PLAT)/$(TARGET)$(DDS_SUFFIX)

# ── Shared source directory ───────────────────────────────────────────────────
SHARED_DIR = ../shared

# ── Flex/Bison sources and generated outputs ──────────────────────────────────
LEX_SRC     = $(SHARED_DIR)/bridge.l
YACC_SRC    = $(SHARED_DIR)/bridge.y
LEX_OUT     = $(SHARED_DIR)/bridge.scanner.cc
YACC_OUT    = $(SHARED_DIR)/bridge.parser.cc
YACC_HEADER = $(SHARED_DIR)/bridge.parser.hh

# ── Object files ──────────────────────────────────────────────────────────────
OBJS = \
    $(OBJ_DIR)/bridge.parser.o \
    $(OBJ_DIR)/calcScores.o \
    $(OBJ_DIR)/csvparser.o \
    $(OBJ_DIR)/dealerIO.o \
    $(OBJ_DIR)/fnscpp.o \
    $(OBJ_DIR)/fns_common.o \
    $(OBJ_DIR)/hands.o \
    $(OBJ_DIR)/dealerDeal.o \
    $(OBJ_DIR)/deallab.o \
    $(OBJ_DIR)/log.o \
    $(OBJ_DIR)/majMinExpand.o \
    $(OBJ_DIR)/master.o \
    $(OBJ_DIR)/oneDeal.o \
    $(OBJ_DIR)/pack.o \
    $(OBJ_DIR)/rawScore.o \
    $(OBJ_DIR)/parse_rules.o \
    $(OBJ_DIR)/simplify.o \
    $(OBJ_DIR)/tnode.o \
    $(OBJ_DIR)/translations.o \
    $(OBJ_DIR)/trumpAskExpand.o

DEPS = $(OBJS:.o=.d)

# ── Compiler flags ────────────────────────────────────────────────────────────
COMMON_FLAGS  = -fopenmp -Wno-free-nonheap-object
BASE_INCLUDES = -I$(SRC_DIR) -I. -I$(SHARED_DIR) -I../lib $(PLAT_INCLUDES)

ifeq ($(TARGET),Debug)
    CFLAGS  = -Wall -Wextra -g -DDEALER -DDEBUG2 $(BASE_INCLUDES) -fexceptions $(COMMON_FLAGS)
    LDFLAGS = -g $(PLAT_LDFLAGS) $(COMMON_FLAGS)
else ifeq ($(TARGET),Release)
    CFLAGS  = -Wall -Wextra -O2 -flto=auto -DDEALER $(BASE_INCLUDES) -fexceptions $(COMMON_FLAGS)
    LDFLAGS = -O2 -flto=auto $(PLAT_LDFLAGS) $(COMMON_FLAGS)
endif

# ── Build rules ───────────────────────────────────────────────────────────────
$(BIN_DIR)/deallab$(EXE_SUFFIX): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(PLAT_LIBS)
	$(if $(PLAT_COPY_DLLS),$(foreach dll,$(PLAT_COPY_DLLS),cp $(dll) $(BIN_DIR)/;))

# Lex and Yacc outputs — written to shared/ so both projects share one generated copy
$(LEX_OUT): $(LEX_SRC)
	$(LEX) -o $@ $<

$(YACC_OUT) $(YACC_HEADER): $(YACC_SRC) $(LEX_OUT)
	$(YACC) -o $(YACC_OUT) -d $(YACC_SRC)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@

# Compile shared .c files to .o files
$(OBJ_DIR)/%.o: $(SHARED_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@

# Compile shared .cpp files to .o files
$(OBJ_DIR)/%.o: $(SHARED_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@

# bridge.scanner.cc is #included at the bottom of bridge.parser.cc; depend on both.
# Suppress free-nonheap-object: known false positive in bison-generated code.
$(OBJ_DIR)/bridge.parser.o: $(YACC_OUT) $(LEX_OUT)
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CFLAGS) -Wno-free-nonheap-object -MMD -MF $(@:.o=.d) -c $< -o $@

-include $(DEPS)

# ── Utility targets ───────────────────────────────────────────────────────────
# Generated files in shared/ are not removed — use clean-generated
clean:
	rm -rf bin/$(PLAT) obj/$(PLAT)

clean-all:
	rm -rf bin obj

# Remove flex/bison generated files from shared/ (forces full regeneration next build)
clean-generated:
	rm -f $(LEX_OUT) $(YACC_OUT) $(YACC_HEADER)

.PHONY: all clean clean-all clean-generated Debug Release

all: Debug Release

Debug:
	$(MAKE) TARGET=Debug

Release:
	$(MAKE) TARGET=Release
