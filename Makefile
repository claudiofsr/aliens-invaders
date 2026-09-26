# ==============================================================================
# Modular, Verbose, Portable & Parallel-Safe Makefile for Aliens Invaders
# Standard: C++20 | Backend: Native SDL3 | Platforms: Linux / BSD / macOS
# ==============================================================================

VERSION       := 0.10.0
EXE           := aliens-invaders
CXX           ?= g++

srcdir        := src
assets        := assets
builddir      := build
toolsdir      := tools
cmakedir      := build-cmake

PREFIX        ?= /usr
DESTDIR       ?=
bindir        := $(PREFIX)/bin
mandir        := $(PREFIX)/share/man/man6
datadir       := $(PREFIX)/share
pixmapdir     := $(PREFIX)/share/pixmaps
appdir        := $(PREFIX)/share/applications
hicolordir    := $(PREFIX)/share/icons/hicolor

COLOR_RESET   := \033[0m
COLOR_BOLD    := \033[1m
COLOR_INFO    := \033[1;36m
COLOR_OK      := \033[1;32m
COLOR_WARN    := \033[1;33m
COLOR_ERR     := \033[1;31m
COLOR_STEP    := \033[1;34m
COLOR_MUTED   := \033[0;90m

NPROC         := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
PKG_CONFIG    := $(shell which pkg-config 2>/dev/null)
UNAME_S       := $(shell uname -s 2>/dev/null || echo Linux)

ifeq ($(PKG_CONFIG),)
  $(error [ERROR] 'pkg-config' not found! Please install: sudo pacman -S pkgconf / apt install pkg-config)
endif

SDL3_CHECK := $(shell $(PKG_CONFIG) --exists sdl3 && echo ok)
ifneq ($(SDL3_CHECK),ok)
  $(error [ERROR] SDL3 development libraries not found! Please install libsdl3-dev / sdl3)
endif

SDL3_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3)
SDL3_LIBS   := $(shell $(PKG_CONFIG) --libs sdl3)

INC_DIRS := -I$(srcdir)/application \
            -I$(srcdir)/audio \
            -I$(srcdir)/cli \
            -I$(srcdir)/combat \
            -I$(srcdir)/graphics \
            -I$(srcdir)/input \
            -I$(srcdir)/menu \
            -I$(srcdir)/core \
            -I$(srcdir)/entities \
            -I$(srcdir)/platform \
            -I$(srcdir)/simulation \
            -I$(srcdir)/starfield \
            -I$(srcdir)/highscore \
            -I$(srcdir)/telemetry \
            -I$(srcdir)/ui

COMPILE_OPTS := -DVERSION_STRING=\"$(VERSION)\"
BASE_LIBS    := -lm $(SDL3_LIBS)

BASE_WARN := -Wall -Wextra -pedantic -Wshadow -Wcast-align \
             -Woverloaded-virtual -Wnon-virtual-dtor -Wformat=2 \
             -Wformat-security -Wconversion -Wsign-conversion -Wnull-dereference -std=c++20

ifeq "$(MAKECMDGOALS)" "sanitize"
    BUILD_MODE    := SANITIZE (ASan + UBSan)
    export LSAN_OPTIONS := suppressions=tools/lsan.supp:fast_unwind_on_malloc=0
    GOAL_OPTS     := -g -fsanitize=address,undefined -fno-omit-frame-pointer
    OPTIMISE_OPTS := -O1
else ifeq "$(MAKECMDGOALS)" "debug"
    BUILD_MODE    := DEBUG (Symbols, No Optimization)
    GOAL_OPTS     := -g -fno-omit-frame-pointer
    OPTIMISE_OPTS := -O0
else
    BUILD_MODE    := RELEASE (Max Performance)
    GOAL_OPTS     := -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DNDEBUG
    OPTIMISE_OPTS := -O2 -fomit-frame-pointer
endif

CXXFLAGS := $(BASE_WARN) $(GOAL_OPTS) $(OPTIMISE_OPTS) $(COMPILE_OPTS) $(INC_DIRS) $(SDL3_CFLAGS)
LDFLAGS  := $(GOAL_OPTS) $(BASE_LIBS)

BAKE_TOOL     := $(toolsdir)/bake_assets
EMBEDDED_SRC  := $(srcdir)/platform/embedded_assets.cc
PNG_ASSETS    := $(wildcard $(assets)/png/*.png)

SRCS := $(shell find $(srcdir) -name "*.cc" 2>/dev/null | sort)
ifeq ($(filter $(EMBEDDED_SRC),$(SRCS)),)
  SRCS += $(EMBEDDED_SRC)
endif

OBJS := $(patsubst $(srcdir)/%.cc, $(builddir)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

ICON_SIZES := 48x48 96x96 128x128 256x256

# ==============================================================================
# Helper Macros & Functions (DRY & Cross-Platform)
# ==============================================================================

define FORMAT_FILE_STATS
bsz=$$(wc -c < "$(1)" 2>/dev/null || echo 0); \
hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
pmod=$$(stat -c '%04a' "$(1)" 2>/dev/null || stat -f '%04Lp' "$(1)" 2>/dev/null || echo "0644")
endef

define INSTALL_FILE
install -c -m $(1) "$(2)" "$(3)"; \
$(call FORMAT_FILE_STATS,$(3)); \
printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(3)" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"
endef

define UNINSTALL_FILE
if [ -f "$(1)" ]; then \
	$(call FORMAT_FILE_STATS,$(1)); \
	printf "%b==> [UNIN]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)" "$(1)" "$(COLOR_ERR)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
	rm -f "$(1)"; \
fi
endef

define PRINT_BUILD_BANNER
sz=$$(wc -c < "$(builddir)/$(EXE)" 2>/dev/null || echo 0); \
h_sz=$$(awk -v s="$$sz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
printf "\n%b==============================================================================%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"; \
printf " %b>>> Build completed successfully! Binary: ./%s <<<%b\n" "$(COLOR_OK)" "$(builddir)/$(EXE)" "$(COLOR_RESET)"; \
printf " • Target Mode:     %b%s%b\n" "$(COLOR_INFO)" "$(BUILD_MODE)" "$(COLOR_RESET)"; \
printf " • Executable Size: %b%s (%s bytes)%b\n" "$(COLOR_BOLD)" "$$h_sz" "$$sz" "$(COLOR_RESET)"; \
printf "%b==============================================================================%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
endef

define PRINT_HELP_BANNER
printf "\n%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf " %bALIENS INVADERS - MAKEFILE COMMAND REFERENCE%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"; \
printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "  %bNative Make Commands:%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"; \
printf "    • %bmake%b                 - Compile release build (-O2, parallel-safe)\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bmake -j%s%b            - Compile using all available CPU threads\n" "$(COLOR_BOLD)" "$(NPROC)" "$(COLOR_RESET)"; \
printf "    • %bmake test%b            - Compile and run headless C++20 test suite\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bmake debug%b           - Compile with debug symbols and no optimization (-O0 -g)\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bmake sanitize%b        - Compile with AddressSanitizer and UBSan (-fsanitize=...)\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bsudo make install%b    - Install binary, icons, desktop entries and man pages\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bsudo make uninstall%b  - Remove installed binary and all desktop assets\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bmake clean%b           - Remove all object files, binaries and baked assets\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "\n  %bCMake Alternative Commands:%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"; \
printf "    • %bmake cmake-build%b     - Configure and compile via CMake\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bsudo make cmake-install%b - Install system files via CMake\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bsudo make cmake-uninstall%b - Remove installed files via CMake manifest\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "    • %bmake cmake-clean%b     - Clean CMake build cache\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"; \
printf "%b==============================================================================%b\n\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
endef

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

.PHONY: all debug sanitize clean install uninstall check-deps help test \
        cmake-build cmake-install cmake-uninstall cmake-clean

# ==============================================================================
# Build Targets
# ==============================================================================

all: check-deps $(builddir)/$(EXE)
ifeq ($(filter install,$(MAKECMDGOALS)),)
	@$(call PRINT_BUILD_BANNER)
	@$(call PRINT_HELP_BANNER)
endif

debug: all
sanitize: all

check-deps:
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b==> Checking system build environment and dependencies...%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"
	@printf "    • Target Build Mode: %b%s%b\n" "$(COLOR_BOLD)" "$(BUILD_MODE)" "$(COLOR_RESET)"
	@printf "    • Platform OS:       %s (%s)\n" "$(UNAME_S)" "$$(uname -m)"
	@printf "    • Compiler:          %s (%s)\n" "$(CXX)" "$$( $(CXX) --version | head -n1 )"
	@printf "    • C++20 standard:    %bAvailable%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@printf "    • SDL3 library:      %bAvailable%b (%s)\n" "$(COLOR_OK)" "$(COLOR_RESET)" "$$( $(PKG_CONFIG) --modversion sdl3 )"
	@printf "    • Display pipeline:  %bNative Wayland / Vulkan & OpenGL%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@printf "    • Install Prefix:    %s (DESTDIR='%s')\n" "$(PREFIX)" "$(DESTDIR)"
	@printf "    • Parallel Threads:  %s cores (-j%s)\n" "$(NPROC)" "$(NPROC)"
	@if [ -d "$(builddir)" ] && [ ! -w "$(builddir)" ]; then \
		printf "%b[ERROR] Build directory '$(builddir)' is not writable by current user ($$USER)!\n" "$(COLOR_ERR)"; \
		printf "        Run: sudo chown -R \$$USER:\$$USER %s%b\n" "$(builddir)" "$(COLOR_RESET)"; \
		exit 1; \
	fi
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"

$(BAKE_TOOL): $(toolsdir)/bake_assets.cc
	@mkdir -p $(dir $@)
	@$(CXX) -std=c++20 -O2 $< -o $@
	@$(call FORMAT_FILE_STATS,$@); \
	printf "%b==> [TOOL]%b %-33s %b->%b %-33s %b[%10s]%b\n" \
		"$(COLOR_STEP)" "$(COLOR_RESET)" "$<" "$(COLOR_MUTED)" "$(COLOR_RESET)" "$@" "$(COLOR_OK)" "$$hsz" "$(COLOR_RESET)"

$(EMBEDDED_SRC): $(BAKE_TOOL) $(PNG_ASSETS)
	@$(BAKE_TOOL)
	@$(call FORMAT_FILE_STATS,$@); \
	printf "%b==> [BAKE]%b %-33s %b->%b %-33s %b[%10s]%b\n" \
		"$(COLOR_STEP)" "$(COLOR_RESET)" "assets/png/ ($(words $(PNG_ASSETS)) images)" "$(COLOR_MUTED)" "$(COLOR_RESET)" "$@" "$(COLOR_OK)" "$$hsz" "$(COLOR_RESET)"

$(OBJS): $(EMBEDDED_SRC)

$(builddir)/$(EXE): $(OBJS)
	@mkdir -p $(dir $@)
	@printf "\n%b==> [LINK] %bLinking %d object files into executable '%s'...%b\n" "$(COLOR_OK)" "$(COLOR_BOLD)" "$(words $(OBJS))" "$@" "$(COLOR_RESET)"
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(builddir)/%.o: $(srcdir)/%.cc
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@$(call FORMAT_FILE_STATS,$@); \
	printf "%b==> [CXX]%b  %-33s %b->%b %-33s %b[%10s]%b\n" \
		"$(COLOR_STEP)" "$(COLOR_RESET)" "$<" "$(COLOR_MUTED)" "$(COLOR_RESET)" "$@" "$(COLOR_OK)" "$$hsz" "$(COLOR_RESET)"

-include $(DEPS)

# ==============================================================================
# Automated Testing
# ==============================================================================

test: check-deps $(builddir)/unit_tests
	@printf "\n%b==> Executing C++20 automated headless test suite...%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"
	@./$(builddir)/unit_tests
	@$(call PRINT_HELP_BANNER)

$(builddir)/unit_tests: tests/unit_tests.cc $(srcdir)/highscore/highscore_table.cc $(srcdir)/entities/formation_grid.cc
	@mkdir -p $(dir $@)
	@$(CXX) -std=c++20 $(GOAL_OPTS) $(OPTIMISE_OPTS) $(BASE_WARN) $(INC_DIRS) $^ -o $@ $(SDL3_LIBS) -lm

# ==============================================================================
# Installation & Packaging
# ==============================================================================

install: all
	@printf "\n%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b INSTALLING ALIENS INVADERS v%s ON SYSTEM %b\n" "$(COLOR_INFO)" "$(VERSION)" "$(COLOR_RESET)"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@install -d -m 0755 "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)" "$(DESTDIR)$(pixmapdir)" "$(DESTDIR)$(appdir)"
	@for d in $(ICON_SIZES); do install -d -m 0755 "$(DESTDIR)$(hicolordir)/$$d/apps"; done
	@$(call INSTALL_FILE,0755,$(builddir)/$(EXE),$(DESTDIR)$(bindir)/$(EXE))
	@$(call INSTALL_FILE,0644,$(assets)/desktop/aliens-invaders.desktop,$(DESTDIR)$(appdir)/aliens-invaders.desktop)
	@for s in $(ICON_SIZES); do \
		$(call INSTALL_FILE,0644,$(assets)/icons/hicolor/$$s/apps/aliens-invaders.png,$(DESTDIR)$(hicolordir)/$$s/apps/aliens-invaders.png); \
	done
	@$(call INSTALL_FILE,0644,$(assets)/icons/hicolor/96x96/apps/aliens-invaders.png,$(DESTDIR)$(pixmapdir)/aliens-invaders.png)
	@sed 's/@VERSION@/$(VERSION)/g' "$(assets)/desktop/aliens-invaders.6x" > "$(DESTDIR)$(mandir)/aliens-invaders.6"
	@chmod 0644 "$(DESTDIR)$(mandir)/aliens-invaders.6"
	@$(call FORMAT_FILE_STATS,$(DESTDIR)$(mandir)/aliens-invaders.6); \
	printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(DESTDIR)$(mandir)/aliens-invaders.6" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -qtf "$(DESTDIR)$(hicolordir)" 2>/dev/null && \
		printf "%b==> [CACHE] Icon cache updated successfully.%b\n" "$(COLOR_OK)" "$(COLOR_RESET)" || true
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(DESTDIR)$(appdir)" 2>/dev/null && \
		printf "%b==> [CACHE] Desktop database updated successfully.%b\n" "$(COLOR_OK)" "$(COLOR_RESET)" || true
	@if [ -n "$$SUDO_USER" ]; then chown -R "$$SUDO_USER" "$(builddir)" "$(BAKE_TOOL)" "$(EMBEDDED_SRC)" 2>/dev/null || true; fi
	@$(call PRINT_BUILD_BANNER)
	@$(call PRINT_HELP_BANNER)

uninstall:
	@printf "\n%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b UNINSTALLING ALIENS INVADERS v%s FROM SYSTEM %b\n" "$(COLOR_WARN)" "$(VERSION)" "$(COLOR_RESET)"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@$(call UNINSTALL_FILE,$(DESTDIR)$(bindir)/$(EXE))
	@$(call UNINSTALL_FILE,$(DESTDIR)$(appdir)/aliens-invaders.desktop)
	@for s in $(ICON_SIZES); do $(call UNINSTALL_FILE,$(DESTDIR)$(hicolordir)/$$s/apps/aliens-invaders.png); done
	@$(call UNINSTALL_FILE,$(DESTDIR)$(pixmapdir)/aliens-invaders.png)
	@$(call UNINSTALL_FILE,$(DESTDIR)$(mandir)/aliens-invaders.6)
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -qtf "$(DESTDIR)$(hicolordir)" 2>/dev/null || true
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(DESTDIR)$(appdir)" 2>/dev/null || true
	@printf "\n%b[OK] Aliens Invaders has been uninstalled from system.%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@$(call PRINT_HELP_BANNER)

clean:
	@printf "%b==> Cleaning local build artifacts and generated assets...%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)"
	@if [ -d "$(builddir)" ] && [ ! -w "$(builddir)" ]; then \
		printf "%b[ERROR] Build directory '$(builddir)' is not writable by current user ($$USER)!\n" "$(COLOR_ERR)"; \
		printf "        Run: sudo chown -R \$$USER:\$$USER %s%b\n" "$(builddir)" "$(COLOR_RESET)"; \
		exit 1; \
	fi
	@rm -rf $(builddir) $(builddir)-sanitize $(builddir)-cmake $(EXE) $(BAKE_TOOL) $(EMBEDDED_SRC)
	@printf "%b[OK]%b Build directory cleaned.\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@$(call PRINT_HELP_BANNER)

# ==============================================================================
# CMake Integration Targets
# ==============================================================================

cmake-build:
	@printf "%b==> Building via CMake (Release)...%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"
	@cmake -B $(cmakedir) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
	@cmake --build $(cmakedir) --parallel $(NPROC)
ifeq ($(filter cmake-install,$(MAKECMDGOALS)),)
	@$(call PRINT_HELP_BANNER)
endif

cmake-install: cmake-build
	@printf "%b==> Installing via CMake...%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"
	@DESTDIR=$(DESTDIR) cmake --install $(cmakedir)
	@$(call PRINT_HELP_BANNER)

cmake-uninstall:
	@printf "%b==> Uninstalling CMake installation...%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)"
	@if [ -f "$(cmakedir)/install_manifest.txt" ]; then \
		xargs rm -vf < $(cmakedir)/install_manifest.txt; \
		printf "%b[OK] CMake uninstallation completed.%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"; \
	else \
		printf "%b[ERROR] Manifest '$(cmakedir)/install_manifest.txt' not found!%b\n" "$(COLOR_ERR)" "$(COLOR_RESET)"; \
	fi
	@$(call PRINT_HELP_BANNER)

cmake-clean:
	@rm -rf $(cmakedir)
	@printf "%b[OK] CMake build directory cleaned.%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@$(call PRINT_HELP_BANNER)

# ==============================================================================
# Help Menu
# ==============================================================================

help:
	@$(call PRINT_HELP_BANNER)
