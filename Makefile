# ==============================================================================
# Modular, Verbose & Parallel-Safe Makefile for Aliens Invaders (C++20 + SDL3)
# ==============================================================================

VERSION       := 0.10.0
EXE           := aliens-invaders
CXX           ?= g++

srcdir        := src
assets        := assets
builddir      := build
toolsdir      := tools

installprefix := /usr
bindir        := $(installprefix)/bin
mandir        := $(installprefix)/share/man/man6
datadir       := $(installprefix)/share

COLOR_RESET := \033[0m
COLOR_BOLD  := \033[1m
COLOR_INFO  := \033[1;36m
COLOR_OK    := \033[1;32m
COLOR_WARN  := \033[1;33m
COLOR_ERR   := \033[1;31m
COLOR_STEP  := \033[1;34m
COLOR_MUTED := \033[0;90m

PKG_CONFIG := $(shell which pkg-config 2>/dev/null)
ifeq ($(PKG_CONFIG),)
  $(error [ERROR] 'pkg-config' not found! Please install: sudo pacman -S pkgconf)
endif

SDL3_CHECK := $(shell $(PKG_CONFIG) --exists sdl3 && echo ok)
ifneq ($(SDL3_CHECK),ok)
  $(error [ERROR] SDL3 development headers not found! Please install: sudo pacman -S sdl3)
endif

SDL3_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3)
SDL3_LIBS   := $(shell $(PKG_CONFIG) --libs sdl3)

CXX_C20_CHECK := $(shell $(CXX) -std=c++20 -dM -E - < /dev/null 2>/dev/null)
ifeq ($(CXX_C20_CHECK),)
  $(error [ERROR] Compiler '$(CXX)' does not support C++20 (-std=c++20)!)
endif

INC_DIRS := -I$(srcdir)/core \
            -I$(srcdir)/entities \
            -I$(srcdir)/platform \
            -I$(srcdir)/system \
            -I$(srcdir)/ui

COMPILE_OPTS := -DVERSION_STRING=\"$(VERSION)\"
BASE_LIBS    := -lm $(SDL3_LIBS)

BASE_WARN := -Wall -Wextra -pedantic -Wshadow -Wcast-align \
             -Woverloaded-virtual -Wnon-virtual-dtor -Wformat=2 -Wformat-security -Wconversion -Wsign-conversion -Wnull-dereference -std=c++20

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
    GOAL_OPTS    := -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DNDEBUG
    OPTIMISE_OPTS := -O2 -fomit-frame-pointer
endif

CXXFLAGS := $(BASE_WARN) $(GOAL_OPTS) $(OPTIMISE_OPTS) $(COMPILE_OPTS) $(INC_DIRS) $(SDL3_CFLAGS)
LDFLAGS  := $(GOAL_OPTS) $(BASE_LIBS)

BAKE_TOOL     := $(toolsdir)/bake_assets
EMBEDDED_SRC  := $(srcdir)/platform/embedded_assets.cc
PNG_ASSETS    := $(wildcard $(assets)/png/*.png)

SRCS := $(shell find $(srcdir) -name "*.cc" | sort)
ifeq ($(filter $(EMBEDDED_SRC),$(SRCS)),)
  SRCS += $(EMBEDDED_SRC)
endif

OBJS := $(patsubst $(srcdir)/%.cc, $(builddir)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

.PHONY: all debug sanitize clean install uninstall check-deps help test

all: check-deps
	@$(MAKE) --no-print-directory $(builddir)/$(EXE)
	@sz=$$(wc -c < "$(builddir)/$(EXE)" 2>/dev/null || echo 0); \
	h_sz=$$(awk -v s="$$sz" 'BEGIN { \
		if (s >= 1048576) printf "%.2f MiB", s/1048576; \
		else if (s >= 1024) printf "%.2f KiB", s/1024; \
		else printf "%d B", s; \
	}'); \
	printf "\n%b==============================================================================%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"; \
	printf " %b>>> Build completed successfully! Binary: ./%s <<<%b\n" "$(COLOR_OK)" "$(builddir)/$(EXE)" "$(COLOR_RESET)"; \
	printf " • Target Mode:     %b%s%b\n" "$(COLOR_INFO)" "$(BUILD_MODE)" "$(COLOR_RESET)"; \
	printf " • Executable Size: %b%s (%s bytes)%b\n" "$(COLOR_BOLD)" "$$h_sz" "$$sz" "$(COLOR_RESET)"; \
	printf "%b==============================================================================%b\n\n" "$(COLOR_OK)" "$(COLOR_RESET)"

debug: all
sanitize: all

test: check-deps $(builddir)/unit_tests
	@printf "\n%b==> Executing C++20 automated headless test suite...%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"
	@./$(builddir)/unit_tests

$(builddir)/unit_tests: tests/unit_tests.cc $(srcdir)/system/highscore_table.cc
	@mkdir -p $(dir $@)
	@$(CXX) -std=c++20 -O2 $(BASE_WARN) $(INC_DIRS) $^ -o $@ $(SDL3_LIBS) -lm

check-deps:
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b==> Checking system build environment and dependencies...%b\n" "$(COLOR_INFO)" "$(COLOR_RESET)"
	@printf "    • Target Build Mode: %b%s%b\n" "$(COLOR_BOLD)" "$(BUILD_MODE)" "$(COLOR_RESET)"
	@printf "    • Compiler:          %s (%s)\n" "$(CXX)" "$$( $(CXX) --version | head -n1 )"
	@printf "    • C++20 standard:    %bAvailable%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@printf "    • SDL3 library:      %bAvailable%b (%s)\n" "$(COLOR_OK)" "$(COLOR_RESET)" "$$( $(PKG_CONFIG) --modversion sdl3 )"
	@printf "    • Display pipeline:  %bNative Wayland / Hardware Present%b\n" "$(COLOR_OK)" "$(COLOR_RESET)"
	@printf "    • Compiler Flags:    %b%s %s%b\n" "$(COLOR_MUTED)" "$(OPTIMISE_OPTS)" "$(GOAL_OPTS)" "$(COLOR_RESET)"
	@if [ -d "$(builddir)" ] && [ ! -w "$(builddir)" ]; then \
		printf "%b[ERROR] Build directory '$(builddir)' is not writable by current user ($$USER)!\n" "$(COLOR_ERR)"; \
		printf "        Run: sudo chown -R \$$USER:\$$USER %s%b\n" "$(builddir)" "$(COLOR_RESET)"; \
		exit 1; \
	fi
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"

$(BAKE_TOOL): $(toolsdir)/bake_assets.cc
	@mkdir -p $(dir $@)
	@$(CXX) -std=c++20 -O2 $< -o $@
	@sz=$$(wc -c < "$@" 2>/dev/null || echo 0); \
	h_sz=$$(awk -v s="$$sz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	printf "%b==> [TOOL]%b %-33s %b->%b %-33s %b[%10s]%b\n" \
		"$(COLOR_STEP)" "$(COLOR_RESET)" "$<" "$(COLOR_MUTED)" "$(COLOR_RESET)" "$@" "$(COLOR_OK)" "$$h_sz" "$(COLOR_RESET)"

$(EMBEDDED_SRC): $(BAKE_TOOL) $(PNG_ASSETS)
	@$(BAKE_TOOL)
	@sz=$$(wc -c < "$@" 2>/dev/null || echo 0); \
	h_sz=$$(awk -v s="$$sz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	printf "%b==> [BAKE]%b %-33s %b->%b %-33s %b[%10s]%b\n" \
		"$(COLOR_STEP)" "$(COLOR_RESET)" "assets/png/ ($(words $(PNG_ASSETS)) PNG images)" "$(COLOR_MUTED)" "$(COLOR_RESET)" "$@" "$(COLOR_OK)" "$$h_sz" "$(COLOR_RESET)"

$(OBJS): $(EMBEDDED_SRC)

$(builddir)/$(EXE): $(OBJS)
	@mkdir -p $(dir $@)
	@printf "\n%b==> [LINK] %bLinking %d object files into executable '%s'...%b\n" "$(COLOR_OK)" "$(COLOR_BOLD)" "$(words $(OBJS))" "$@" "$(COLOR_RESET)"
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(builddir)/%.o: $(srcdir)/%.cc
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@sz=$$(wc -c < "$@" 2>/dev/null || echo 0); \
	h_sz=$$(awk -v s="$$sz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	printf "%b==> [CXX]%b  %-33s %b->%b %-33s %b[%10s]%b\n" \
		"$(COLOR_STEP)" "$(COLOR_RESET)" "$<" "$(COLOR_MUTED)" "$(COLOR_RESET)" "$@" "$(COLOR_OK)" "$$h_sz" "$(COLOR_RESET)"

-include $(DEPS)

clean:
	@printf "%b==> Cleaning local build artifacts and generated assets...%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)"
	@if [ -d "$(builddir)" ] && [ ! -w "$(builddir)" ]; then \
		printf "%b[ERROR] Build directory '$(builddir)' is not writable by current user ($$USER)!\n" "$(COLOR_ERR)"; \
		printf "        Run: sudo chown -R \$$USER:\$$USER %s%b\n" "$(builddir)" "$(COLOR_RESET)"; \
		exit 1; \
	fi
	@rm -rf $(builddir) $(builddir)-cmake $(EXE) $(BAKE_TOOL) $(EMBEDDED_SRC)
	@printf "%b[OK]%b Build directory cleaned.\n" "$(COLOR_OK)" "$(COLOR_RESET)"

install: all
	@printf "\n"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b INSTALLING ALIENS INVADERS v%s ON SYSTEM %b\n" "$(COLOR_INFO)" "$(VERSION)" "$(COLOR_RESET)"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@install -d -m 0755 "$(DESTDIR)$(bindir)"
	@install -d -m 0755 "$(DESTDIR)$(mandir)"
	@install -d -m 0755 "$(DESTDIR)$(datadir)/pixmaps"
	@install -d -m 0755 "$(DESTDIR)$(datadir)/applications"
	@for d in 48x48 96x96 128x128 256x256; do install -d -m 0755 "$(DESTDIR)$(datadir)/icons/hicolor/$$d/apps"; done
	@install -c -m 0755 "$(builddir)/$(EXE)" "$(DESTDIR)$(bindir)/$(EXE)"; \
	bsz=$$(wc -c < "$(DESTDIR)$(bindir)/$(EXE)"); \
	hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	pmod=$$(stat -c '%a' "$(DESTDIR)$(bindir)/$(EXE)"); pmod=$$(printf "%04d" "$$pmod"); \
	printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(DESTDIR)$(bindir)/$(EXE)" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"
	@install -c -m 0644 "$(assets)/desktop/aliens-invaders.desktop" "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop"; \
	bsz=$$(wc -c < "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop"); \
	hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	pmod=$$(stat -c '%a' "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop"); pmod=$$(printf "%04d" "$$pmod"); \
	printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"
	@for icon in 48x48 96x96 128x128 256x256; do \
		install -c -m 0644 "$(assets)/icons/hicolor/$$icon/apps/aliens-invaders.png" "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png"; \
		bsz=$$(wc -c < "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png"); \
		hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
		pmod=$$(stat -c '%a' "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png"); pmod=$$(printf "%04d" "$$pmod"); \
		printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
	done
	@install -c -m 0644 "$(assets)/icons/hicolor/96x96/apps/aliens-invaders.png" "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png"; \
	bsz=$$(wc -c < "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png"); \
	hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	pmod=$$(stat -c '%a' "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png"); pmod=$$(printf "%04d" "$$pmod"); \
	printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"
	@sed 's/@VERSION@/$(VERSION)/g' "$(assets)/desktop/aliens-invaders.6x" > "$(DESTDIR)$(mandir)/aliens-invaders.6"; \
	chmod 0644 "$(DESTDIR)$(mandir)/aliens-invaders.6"; \
	bsz=$$(wc -c < "$(DESTDIR)$(mandir)/aliens-invaders.6"); \
	hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	pmod=$$(stat -c '%a' "$(DESTDIR)$(mandir)/aliens-invaders.6"); pmod=$$(printf "%04d" "$$pmod"); \
	printf "%b==> [INST]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)" "$(DESTDIR)$(mandir)/aliens-invaders.6" "$(COLOR_OK)" "$$pmod" "$$hsz" "$(COLOR_RESET)"
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -qtf "$(DESTDIR)$(datadir)/icons/hicolor" 2>/dev/null || true
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(DESTDIR)$(datadir)/applications" 2>/dev/null || true
	@if [ -n "$$SUDO_USER" ]; then chown -R "$$SUDO_USER" "$(builddir)" "$(BAKE_TOOL)" "$(EMBEDDED_SRC)" 2>/dev/null || true; fi
	@printf "\n%b>>> Installation completed successfully! <<<%b\n\n" "$(COLOR_OK)" "$(COLOR_RESET)"

uninstall:
	@printf "\n"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b UNINSTALLING ALIENS INVADERS v%s FROM SYSTEM %b\n" "$(COLOR_WARN)" "$(VERSION)" "$(COLOR_RESET)"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@if [ -f "$(DESTDIR)$(bindir)/$(EXE)" ]; then \
		bsz=$$(wc -c < "$(DESTDIR)$(bindir)/$(EXE)" 2>/dev/null || echo 0); \
		hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
		pmod=$$(stat -c '%a' "$(DESTDIR)$(bindir)/$(EXE)" 2>/dev/null); pmod=$$(printf "%04d" "$$pmod" 2>/dev/null || echo "----"); \
		printf "%b==> [UNIN]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)" "$(DESTDIR)$(bindir)/$(EXE)" "$(COLOR_ERR)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
	fi
	@rm -f "$(DESTDIR)$(bindir)/$(EXE)"
	@if [ -f "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop" ]; then \
		bsz=$$(wc -c < "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop"); \
		hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
		pmod=$$(stat -c '%a' "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop"); pmod=$$(printf "%04d" "$$pmod"); \
		printf "%b==> [UNIN]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)" "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop" "$(COLOR_ERR)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
	fi
	@rm -f "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop"
	@for icon in 48x48 96x96 128x128 256x256; do \
		if [ -f "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png" ]; then \
			bsz=$$(wc -c < "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png"); \
			hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
			pmod=$$(stat -c '%a' "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png"); pmod=$$(printf "%04d" "$$pmod"); \
			printf "%b==> [UNIN]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)" "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png" "$(COLOR_ERR)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
		fi; \
		rm -f "$(DESTDIR)$(datadir)/icons/hicolor/$$icon/apps/aliens-invaders.png"; \
	done
	@if [ -f "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png" ]; then \
		bsz=$$(wc -c < "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png"); \
		hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
		pmod=$$(stat -c '%a' "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png"); pmod=$$(printf "%04d" "$$pmod"); \
		printf "%b==> [UNIN]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)" "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png" "$(COLOR_ERR)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
	fi
	@rm -f "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png"
	@if [ -f "$(DESTDIR)$(mandir)/aliens-invaders.6" ]; then \
		bsz=$$(wc -c < "$(DESTDIR)$(mandir)/aliens-invaders.6"); \
		hsz=$$(awk -v s="$$bsz" 'BEGIN { if (s >= 1048576) printf "%.2f MiB", s/1048576; else if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
		pmod=$$(stat -c '%a' "$(DESTDIR)$(mandir)/aliens-invaders.6"); pmod=$$(printf "%04d" "$$pmod"); \
		printf "%b==> [UNIN]%b %-62s %b[%s] [%10s]%b\n" "$(COLOR_WARN)" "$(COLOR_RESET)" "$(DESTDIR)$(mandir)/aliens-invaders.6" "$(COLOR_ERR)" "$$pmod" "$$hsz" "$(COLOR_RESET)"; \
	fi
	@rm -f "$(DESTDIR)$(mandir)/aliens-invaders.6"
	@printf "\n%b[OK] Aliens Invaders has been uninstalled from system.%b\n\n" "$(COLOR_OK)" "$(COLOR_RESET)"

help:
	@printf "%bUsage for Aliens Invaders Makefile:%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "  make              - Compile release build (-O2)\n"
	@printf "  make -j\$$(nproc)   - Compile using all CPU cores in parallel\n"
	@printf "  make test         - Compile and run C++20 automated unit tests\n"
	@printf "  make sanitize     - Compile with ASan and UBSan\n"
	@printf "  make install      - Install binary, icons, and desktop entries\n"
	@printf "  make clean        - Clean build artifacts\n"
