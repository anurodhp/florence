/*
 * Florence: start-up, shutdown and the miscellaneous table of the NetSurf core.
 * Follows frontends/monkey/main.c of NetSurf 3.11 (options, resource search path,
 * messages, urldb) with the GNUstep UI on top. GPL-2.0-only (see gs.h).
 *
 * Tuned for small machines (a Raspberry Pi 3: four slow cores, 1 GB): no
 * JavaScript, a small memory cache, no animated images, few parallel fetches.
 * Set FLORENCE_FULL=1 to keep NetSurf's own defaults for those.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/filepath.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "netsurf/netsurf.h"
#include "netsurf/browser_window.h"
#include "netsurf/misc.h"
#include "netsurf/window.h"
#include "netsurf/fetch.h"
#include "netsurf/bitmap.h"
#include "netsurf/layout.h"
#include "netsurf/clipboard.h"
#include "content/urldb.h"

#include "gnustep/gs.h"
#include "gnustep/gs_schedule.h"
#include "gnustep/gs_filetype.h"
#include "gnustep/gs_fetch.h"

extern struct gui_window_table *flo_window_table;
extern struct gui_clipboard_table *flo_clipboard_table;
extern struct gui_bitmap_table *flo_bitmap_table;
extern struct gui_layout_table *flo_layout_table;

char **respaths;                /* resource search path (gs_fetch.c uses it) */

static char home_dir[PATH_MAX];
static char urls_path[PATH_MAX];       /* the URL database; 3.11 has no option for it */

static const char *home_path(char *buf, size_t len, const char *name)
{
	snprintf(buf, len, "%s/.netsurf/%s", home_dir, name);
	return buf;
}

static char **init_resources(void)
{
	char path[PATH_MAX * 2];
	const char *langv[] = { "en", NULL };
	char **pathv, **res;

	snprintf(path, sizeof(path), "%s/.netsurf/:%s", home_dir, GNUSTEP_RESPATH);
	pathv = filepath_path_to_strvec(path);
	res = filepath_generate(pathv, langv);
	filepath_free_strvec(pathv);
	return res;
}

/* ---- the miscellaneous table -------------------------------------------- */

static nserror flo_schedule(int ms, void (*cb)(void *p), void *p)
{
	nserror err = gs_schedule(ms, cb, p);
	flo_ui_wake();          /* the UI's timer may be sleeping past this new deadline */
	return err;
}

static struct gui_misc_table misc_table = {
	.schedule = flo_schedule,
};

/* ---- options ------------------------------------------------------------ */

static nserror set_defaults(struct nsoption_s *defaults)
{
	char buf[PATH_MAX], res[PATH_MAX];

	nsoption_setnull_charp(cookie_file, strdup(home_path(buf, sizeof(buf), "Cookies")));
	nsoption_setnull_charp(cookie_jar, strdup(home_path(buf, sizeof(buf), "Cookies")));
	if (filepath_sfind(respaths, res, "ca-bundle") != NULL)
		nsoption_setnull_charp(ca_bundle, strdup(res));
	return NSERROR_OK;
}

/* applied after the user's Choices file, so they cannot turn the unaffordable on */
static void apply_overrides(void)
{
	nsoption_set_bool(enable_javascript, false);
	nsoption_set_bool(core_select_menu, true);      /* we have no native <select> popup */
	if (getenv("FLORENCE_FULL") == NULL) {
		nsoption_set_bool(animate_images, false);
		nsoption_set_bool(background_images, true);
		nsoption_set_int(memory_cache_size, 8 * 1024 * 1024);
		nsoption_set_int(max_fetchers, 8);
		nsoption_set_int(max_fetchers_per_host, 4);
		nsoption_set_int(max_cached_fetch_handles, 4);
	}
}

/* ---- life cycle --------------------------------------------------------- */

void flo_trace(const char *stage)
{
	if (getenv("FLORENCE_TRACE") != NULL)
		fprintf(stderr, "florence: %s\n", stage);
}

int flo_core_init(int argc, char **argv)
{
	char buf[PATH_MAX];
	const char *h = getenv("HOME");
	nserror err;
	struct netsurf_table table = {
		.misc = &misc_table,
		.window = flo_window_table,
		.clipboard = flo_clipboard_table,
		.fetch = gs_fetch_table,
		.bitmap = flo_bitmap_table,
		.layout = flo_layout_table,
	};

	snprintf(home_dir, sizeof(home_dir), "%s", h != NULL ? h : "/tmp");
	home_path(buf, sizeof(buf), "");
	mkdir(buf, 0700);

	flo_trace("core: register tables");
	if (netsurf_register(&table) != NSERROR_OK) {
		fprintf(stderr, "florence: operation table registration failed\n");
		return -1;
	}
	flo_bitmap_init();

	flo_trace("core: resources + options");
	respaths = init_resources();
	if (nsoption_init(set_defaults, &nsoptions, &nsoptions_default) != NSERROR_OK) {
		fprintf(stderr, "florence: options init failed\n");
		return -1;
	}
	nsoption_read(home_path(buf, sizeof(buf), "Choices"), nsoptions);
	apply_overrides();

	if (filepath_sfind(respaths, buf, "Messages") == NULL ||
	    messages_add_from_file(buf) != NSERROR_OK)
		fprintf(stderr, "florence: no Messages file on the resource path (%s)\n", GNUSTEP_RESPATH);

	flo_trace("core: netsurf_init");
	err = netsurf_init(NULL);       /* NULL: no disk backing store path override */
	if (err != NSERROR_OK) {
		fprintf(stderr, "florence: netsurf_init failed (%d)\n", (int)err);
		return -1;
	}
	flo_trace("core: urldb");
	urldb_load(home_path(urls_path, sizeof(urls_path), "URLs"));
	urldb_load_cookies(nsoption_charp(cookie_file));

	gs_fetch_filetype_init(filepath_sfind(respaths, buf, "mimetypes") != NULL ? buf : "/etc/mime.types");
	flo_trace("core: ready");
	return 0;
}

void flo_core_fini(void)
{
	urldb_save(urls_path);
	urldb_save_cookies(nsoption_charp(cookie_jar));
	netsurf_exit();
	gs_fetch_filetype_fin();
	if (respaths != NULL) {
		filepath_free_strvec(respaths);
		respaths = NULL;
	}
}

int flo_schedule_run(void)
{
	return gs_schedule_run();
}

void flo_open_url(const char *url)
{
	nsurl *u = NULL;
	struct browser_window *bw = NULL;

	flo_trace("open_url");
	if (url == NULL || *url == '\0')
		url = nsoption_charp(homepage_url) != NULL ? nsoption_charp(homepage_url) : "about:welcome";
	if (nsurl_create(url, &u) != NSERROR_OK)
		return;
	browser_window_create(BW_CREATE_HISTORY, u, NULL, NULL, &bw);
	nsurl_unref(u);
	flo_trace("open_url: created");
}

const char *flo_resource_dir(void)
{
	return GNUSTEP_RESPATH;
}
