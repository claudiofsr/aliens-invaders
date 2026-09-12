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
             -Woverloaded-virtual -Wnon-virtual-dtor -Wformat=2 -std=c++20

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
    GOAL_OPTS     := -fstack-protector-strong -D_FORTIFY_SOURCE=2
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

.PHONY: all debug sanitize clean install uninstall check-deps help

# Sequential phase enforcement: check-deps runs cleanly first, then parallel compilation begins
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

# Atomic line output: compilation finishes first, then single-line summary is printed without interleaving
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
	@rm -rf $(builddir) $(EXE) $(BAKE_TOOL) $(EMBEDDED_SRC) 2>/dev/null || true
	@printf "%b[OK]%b Build directory cleaned.\n" "$(COLOR_OK)" "$(COLOR_RESET)"

install: all
	@printf "\n"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b               INSTALLING ALIENS INVADERS v%s ON SYSTEM              %b\n" "$(COLOR_INFO)" "$(VERSION)" "$(COLOR_RESET)"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "\n"
	@printf "%b[1/3] Creating system directories...%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)"
	@install -d -m 0755 "$(DESTDIR)$(bindir)" && printf "  + [dir]  chmod 0755  %s\n" "$(DESTDIR)$(bindir)"
	@install -d -m 0755 "$(DESTDIR)$(mandir)" && printf "  + [dir]  chmod 0755  %s\n" "$(DESTDIR)$(mandir)"
	@install -d -m 0755 "$(DESTDIR)$(datadir)/pixmaps" && printf "  + [dir]  chmod 0755  %s\n" "$(DESTDIR)$(datadir)/pixmaps"
	@install -d -m 0755 "$(DESTDIR)$(datadir)/applications" && printf "  + [dir]  chmod 0755  %s\n" "$(DESTDIR)$(datadir)/applications"
	@for sz in 48x48 96x96 128x128 256x256; do \
		install -d -m 0755 "$(DESTDIR)$(datadir)/icons/hicolor/$$sz/apps" && \
		printf "  + [dir]  chmod 0755  %s\n" "$(DESTDIR)$(datadir)/icons/hicolor/$$sz/apps"; \
	done
	@printf "\n%b[2/3] Installing executable, man page, and desktop assets with size telemetry...%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)"
	@total_bytes=0; \
	install_file() { \
		perm="$$1"; src="$$2"; dst="$$3"; tag="$$4"; \
		sz=$$(wc -c < "$$src" 2>/dev/null || echo 0); \
		total_bytes=$$((total_bytes + sz)); \
		h_sz=$$(awk -v s="$$sz" 'BEGIN { \
			if (s >= 1048576) printf "%.2f MiB", s/1048576; \
			else if (s >= 1024) printf "%.2f KiB", s/1024; \
			else printf "%d B", s; \
		}'); \
		install -c -m "$$perm" "$$src" "$$dst" && \
		printf "  + [%-4s] chmod %s  [%9s]  %s\n" "$$tag" "$$perm" "$$h_sz" "$$dst"; \
	}; \
	install_file 0755 "$(builddir)/$(EXE)" "$(DESTDIR)$(bindir)/$(EXE)" "bin"; \
	install_file 0644 "$(assets)/desktop/aliens-invaders.desktop" "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop" "desk"; \
	for sz in 48x48 96x96 128x128 256x256; do \
		install_file 0644 "$(assets)/icons/hicolor/$$sz/apps/aliens-invaders.png" "$(DESTDIR)$(datadir)/icons/hicolor/$$sz/apps/aliens-invaders.png" "icon"; \
	done; \
	install_file 0644 "$(assets)/icons/hicolor/96x96/apps/aliens-invaders.png" "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png" "icon"; \
	sed 's/@VERSION@/$(VERSION)/g' "$(assets)/desktop/aliens-invaders.6x" > "$(DESTDIR)$(mandir)/aliens-invaders.6"; \
	chmod 0644 "$(DESTDIR)$(mandir)/aliens-invaders.6"; \
	man_sz=$$(wc -c < "$(DESTDIR)$(mandir)/aliens-invaders.6" 2>/dev/null || echo 0); \
	total_bytes=$$((total_bytes + man_sz)); \
	man_h_sz=$$(awk -v s="$$man_sz" 'BEGIN { if (s >= 1024) printf "%.2f KiB", s/1024; else printf "%d B", s; }'); \
	printf "  + [man ] chmod 0644  [%9s]  %s\n" "$$man_h_sz" "$(DESTDIR)$(mandir)/aliens-invaders.6"; \
	tot_h=$$(awk -v s="$$total_bytes" 'BEGIN { \
		if (s >= 1048576) printf "%.2f MiB", s/1048576; \
		else if (s >= 1024) printf "%.2f KiB", s/1024; \
		else printf "%d B", s; \
	}'); \
	printf "\n  %b• Total Installed Footprint:%b %b%s (%d bytes)%b across 8 files\n" "$(COLOR_BOLD)" "$(COLOR_RESET)" "$(COLOR_OK)" "$$tot_h" "$$total_bytes" "$(COLOR_RESET)"
	@printf "\n%b[3/3] Updating system caches and permissions...%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)"
	@which gtk-update-icon-cache >/dev/null 2>&1 && \
		gtk-update-icon-cache -qtf "$(DESTDIR)$(datadir)/icons/hicolor" 2>/dev/null && \
		printf "  + [cache] Updated icon cache: %s\n" "$(DESTDIR)$(datadir)/icons/hicolor" || true
	@which update-desktop-database >/dev/null 2>&1 && \
		update-desktop-database "$(DESTDIR)$(datadir)/applications" 2>/dev/null && \
		printf "  + [cache] Updated desktop database: %s\n" "$(DESTDIR)$(datadir)/applications" || true
	@if [ -n "$$SUDO_USER" ]; then \
		chown -R "$$SUDO_USER:$$SUDO_USER" "$(builddir)" "$(toolsdir)" "$(srcdir)" 2>/dev/null || true; \
	fi
	@printf "\n%b>>> Installation completed successfully! <<<%b\n\n" "$(COLOR_OK)" "$(COLOR_RESET)"

uninstall:
	@printf "\n"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "%b              UNINSTALLING ALIENS INVADERS FROM SYSTEM              %b\n" "$(COLOR_WARN)" "$(COLOR_RESET)"
	@printf "%b==============================================================================%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "\n"
	@printf "%b[1/2] Removing installed files and calculating freed disk space...%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)"
	@freed_bytes=0; \
	remove_file() { \
		file="$$1"; tag="$$2"; \
		if [ -f "$$file" ]; then \
			sz=$$(wc -c < "$$file" 2>/dev/null || echo 0); \
			freed_bytes=$$((freed_bytes + sz)); \
			h_sz=$$(awk -v s="$$sz" 'BEGIN { \
				if (s >= 1048576) printf "%.2f MiB", s/1048576; \
				else if (s >= 1024) printf "%.2f KiB", s/1024; \
				else printf "%d B", s; \
			}'); \
			rm -f "$$file" && printf "  - [%-4s] [%9s freed]  Removed: %s\n" "$$tag" "$$h_sz" "$$file"; \
		fi; \
	}; \
	remove_file "$(DESTDIR)$(bindir)/$(EXE)" "bin"; \
	remove_file "$(DESTDIR)$(datadir)/applications/aliens-invaders.desktop" "desk"; \
	for sz in 48x48 96x96 128x128 256x256; do \
		remove_file "$(DESTDIR)$(datadir)/icons/hicolor/$$sz/apps/aliens-invaders.png" "icon"; \
	done; \
	remove_file "$(DESTDIR)$(datadir)/pixmaps/aliens-invaders.png" "icon"; \
	remove_file "$(DESTDIR)$(mandir)/aliens-invaders.6" "man"; \
	tot_freed=$$(awk -v s="$$freed_bytes" 'BEGIN { \
		if (s >= 1048576) printf "%.2f MiB", s/1048576; \
		else if (s >= 1024) printf "%.2f KiB", s/1024; \
		else printf "%d B", s; \
	}'); \
	printf "\n  %b• Total Disk Space Freed:%b %b%s (%d bytes)%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)" "$(COLOR_OK)" "$$tot_freed" "$$freed_bytes" "$(COLOR_RESET)"
	@printf "\n%b[2/2] Updating system caches...%b\n" "$(COLOR_STEP)" "$(COLOR_RESET)"
	@which gtk-update-icon-cache >/dev/null 2>&1 && \
		gtk-update-icon-cache -qtf "$(DESTDIR)$(datadir)/icons/hicolor" 2>/dev/null && \
		printf "  - [cache] Refreshed icon cache: %s\n" "$(DESTDIR)$(datadir)/icons/hicolor" || true
	@which update-desktop-database >/dev/null 2>&1 && \
		update-desktop-database "$(DESTDIR)$(datadir)/applications" 2>/dev/null && \
		printf "  - [cache] Refreshed desktop database: %s\n" "$(DESTDIR)$(datadir)/applications" || true
	@printf "\n%b[OK] Aliens Invaders has been completely uninstalled from system.%b\n\n" "$(COLOR_OK)" "$(COLOR_RESET)"

help:
	@printf "%bUsage for Aliens Invaders Makefile:%b\n" "$(COLOR_BOLD)" "$(COLOR_RESET)"
	@printf "  make              - Compile release build (-O2)\n"
	@printf "  make -j\$(nproc)   - Compile using all CPU cores in parallel\n"
	@printf "  make sanitize     - Compile with ASan and UBSan\n"
	@printf "  make debug        - Compile with debug symbols (-g -O0)\n"
	@printf "  make install      - Install binary and desktop entries with size telemetry\n"
	@printf "  make uninstall    - Remove all installed system files with freed space report\n"
	@printf "  make clean        - Clean build artifacts\n"
