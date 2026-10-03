# SPDX-License-Identifier: GPL-2.0-only (derived from NetSurf's monkey frontend Makefiles)
#
# Makefile for the NetSurf "gnustep" target (Florence). Copied into
# netsurf/frontends/gnustep/ by scripts/build_netsurf.sh.
#
CWARNFLAGS += -Wno-error

CFLAGS += -std=gnu99 -Dgnustep -Dnsgnustep \
	  -DGNUSTEP_RESPATH=\"$(NETSURF_GNUSTEP_RESOURCES)\" \
	  -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE \
	  $(FLORENCE_CFLAGS)

LDFLAGS += -lm

MESSAGES_FILTER=gtk
MESSAGES_TARGET=$(FRONTEND_RESOURCES_DIR)

# Plain C glue. FloUI.m and FloPage.m (the Objective-C UI) are compiled and linked by
# scripts/build_netsurf.sh, against the iokit port's GNUstep (tools/gnustep_env.sh).
S_FRONTEND := gs_core.c gs_schedule.c gs_filetype.c gs_fetch.c gs_bitmap.c \
	gs_layout.c gs_plot.c gs_window.c gs_download.c gs_startpage.c gs_search.c gs_blocker.c gs_lists.c

SOURCES = $(S_COMMON) $(S_IMAGE) $(S_BROWSER) $(S_PDF) $(S_FRONTEND)
EXETARGET := florence

install-gnustep:

package-gnustep:
