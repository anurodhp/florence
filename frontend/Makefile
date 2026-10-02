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

# Plain C glue, then the Objective-C user interface.
S_FRONTEND := gs_core.c gs_schedule.c gs_filetype.c gs_fetch.c gs_bitmap.c \
	gs_layout.c gs_plot.c gs_window.c \
	FloUI.m FloPage.m

SOURCES = $(S_COMMON) $(S_IMAGE) $(S_BROWSER) $(S_PDF) $(S_FRONTEND)
EXETARGET := florence

install-gnustep:

package-gnustep:
