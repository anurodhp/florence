/*
 * Florence: NetSurf's page-search table. The core does the searching and highlighting; this only
 * reports to the UI whether the last search matched. The context pointer the core hands back is the
 * UI object passed in flo_win_find.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <stdbool.h>
#include <stddef.h>

#include "utils/errors.h"
#include "netsurf/search.h"

#include "gnustep/gs.h"

static void s_status(bool found, void *p)
{
	if (p != NULL)
		flo_ui_find_status(p, found);
}

static void s_hourglass(bool active, void *p) { }
static void s_add_recent(const char *string, void *p) { }
static void s_forward_state(bool active, void *p) { }
static void s_back_state(bool active, void *p) { }

static struct gui_search_table search_table = {
	.status = s_status,
	.hourglass = s_hourglass,
	.add_recent = s_add_recent,
	.forward_state = s_forward_state,
	.back_state = s_back_state,
};

struct gui_search_table *flo_search_table = &search_table;
