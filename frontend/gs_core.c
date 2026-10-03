/*
 * Florence: start-up, shutdown and the miscellaneous table of the NetSurf core.
 * Follows frontends/monkey/main.c of NetSurf 3.11 (options, resource search path,
 * messages, urldb) with the GNUstep UI on top. SPDX-License-Identifier: GPL-2.0-only (derived from NetSurf frontends; see LICENSE).
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
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
#include <unistd.h>

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
#include "netsurf/download.h"
#include "netsurf/search.h"
#include "content/urldb.h"

#include "gnustep/gs.h"
#include "gnustep/gs_schedule.h"
#include "gnustep/gs_filetype.h"
#include "gnustep/gs_fetch.h"

extern struct gui_window_table *flo_window_table;
extern struct gui_clipboard_table *flo_clipboard_table;
extern struct gui_bitmap_table *flo_bitmap_table;
extern struct gui_download_table *flo_download_table;
extern struct gui_search_table *flo_search_table;
extern struct gui_layout_table *flo_layout_table;

char **respaths;                /* resource search path (gs_fetch.c uses it) */

static char home_dir[PATH_MAX];
static char urls_path[PATH_MAX];       /* the URL database; 3.11 has no option for it */

static const char *home_path(char *buf, size_t len, const char *name)
{
	snprintf(buf, len, "%s/.netsurf/%s", home_dir, name);
	return buf;
}

/* ---- Florence's own preferences: ~/.netsurf/Florence.conf, "key=value" lines -------------------
 * For what NetSurf's option table has no slot for (search engine, downloads folder, ...). Tiny and
 * fixed-size: no allocation after load. */
#define PREF_MAX 24
static struct { char key[24]; char val[PATH_MAX]; } prefs[PREF_MAX];
static int npref;
static bool drop_cookies, drop_history;        /* "clear" was asked: honoured when the databases are saved at quit */

static void prefs_load(void)
{
	char path[PATH_MAX], line[PATH_MAX + 32], *eq, *nl;
	FILE *f = fopen(home_path(path, sizeof(path), "Florence.conf"), "r");

	if (f == NULL)
		return;
	while (npref < PREF_MAX && fgets(line, sizeof(line), f) != NULL) {
		if ((nl = strchr(line, '\n')) != NULL)
			*nl = '\0';
		if ((eq = strchr(line, '=')) == NULL || eq == line || eq - line >= (int)sizeof(prefs[0].key))
			continue;
		*eq = '\0';
		snprintf(prefs[npref].key, sizeof(prefs[npref].key), "%s", line);
		snprintf(prefs[npref].val, sizeof(prefs[npref].val), "%s", eq + 1);
		npref++;
	}
	fclose(f);
}

static void prefs_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];
	FILE *f;
	int i;

	home_path(path, sizeof(path), "Florence.conf");
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	if ((f = fopen(tmp, "w")) == NULL)
		return;
	for (i = 0; i < npref; i++)
		fprintf(f, "%s=%s\n", prefs[i].key, prefs[i].val);
	if (fclose(f) == 0)
		rename(tmp, path);
}

const char *flo_pref_get(const char *key, const char *def)
{
	int i;

	for (i = 0; i < npref; i++)
		if (strcmp(prefs[i].key, key) == 0)
			return prefs[i].val;
	return def;
}

void flo_pref_set(const char *key, const char *val)
{
	int i;

	if (val == NULL || strchr(val, '\n') != NULL || strlen(key) >= sizeof(prefs[0].key))
		return;
	for (i = 0; i < npref && strcmp(prefs[i].key, key) != 0; i++)
		;
	if (i == npref) {
		if (npref == PREF_MAX)
			return;
		npref++;
		snprintf(prefs[i].key, sizeof(prefs[i].key), "%s", key);
	}
	snprintf(prefs[i].val, sizeof(prefs[i].val), "%s", val);
	prefs_save();
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
	static int seen;
	if (seen < 4 && getenv("FLORENCE_TRACE") != NULL) {
		seen++;
		fprintf(stderr, "florence: schedule(%d ms, %p)\n", ms, (void *)cb);
	}
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
	defaults[NSOPTION_block_advertisements].value.b = true;     /* content blocking is on unless the user turns it off */
	if (filepath_sfind(respaths, res, "ca-bundle") != NULL)
		nsoption_setnull_charp(ca_bundle, strdup(res));
	return NSERROR_OK;
}

/* applied after the user's Choices file, so they cannot turn the unaffordable on */
static void apply_overrides(void)
{
#ifdef FLO_WITH_JS
	/* a JavaScript build: off until the user turns it on (View menu, FLORENCE_JS=1 or Choices) */
	if (getenv("FLORENCE_JS") != NULL)
		nsoption_set_bool(enable_javascript, true);
#else
	nsoption_set_bool(enable_javascript, false);
#endif
	nsoption_set_bool(core_select_menu, true);      /* we have no native <select> popup */
	if (getenv("FLORENCE_FULL") == NULL) {
		int mb = atoi(flo_pref_get("cache_mb", "8"));

		nsoption_set_bool(animate_images, strcmp(flo_pref_get("animate", "0"), "1") == 0);
		nsoption_set_bool(background_images, true);
		nsoption_set_int(memory_cache_size, (mb < 1 || mb > 64 ? 8 : mb) * 1024 * 1024);
		nsoption_set_int(max_fetchers, 8);
		nsoption_set_int(max_fetchers_per_host, 4);
		nsoption_set_int(max_cached_fetch_handles, 4);
	}
}

/* ---- crash report --------------------------------------------------------
 * There is no debugger on the target. On SIGSEGV/SIGBUS print the fault address, pc, lr and a
 * frame-pointer walk, plus the run-time address of flo_core_init: subtract the address `nm` shows
 * for flo_core_init in nsgnustep to get the load slide, then look the addresses up with `atos` or
 * `nm` on the build host. */
int flo_core_init(int argc, char **argv);

static void on_crash(int sig, siginfo_t *si, void *ctx)
{
	fprintf(stderr, "florence: CRASH signal %d, fault address %p\n", sig, si != NULL ? si->si_addr : NULL);
	fprintf(stderr, "florence: flo_core_init is at %p (slide = this - nm address)\n", (void *)flo_core_init);
#if defined(__APPLE__) && defined(__arm64__)
	{
		ucontext_t *uc = ctx;
		uintptr_t pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
		uintptr_t lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
		uintptr_t *f = (uintptr_t *)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
		int i;

		fprintf(stderr, "florence: pc %p lr %p\n", (void *)pc, (void *)lr);
		for (i = 0; i < 24 && f != NULL && ((uintptr_t)f & 7) == 0 && (uintptr_t)f > 0x10000; i++) {
			fprintf(stderr, "florence:   #%d %p\n", i, (void *)f[1]);
			if ((uintptr_t *)f[0] <= f)
				break;
			f = (uintptr_t *)f[0];
		}
	}
#endif
	signal(sig, SIG_DFL);    /* returning re-runs the faulting instruction: the usual death */
}

static void install_crash_report(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_crash;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
}

/* ---- content blocker ---------------------------------------------------------- */

/* Reads the bundled starter list and every *.json in ~/.netsurf/blocklists (Safari content-blocker
 * format), indexes them, and (re)writes ~/.netsurf/adblock.css from their cosmetic rules. */
void flo_blocker_reload(void)
{
	char dir[PATH_MAX], list[PATH_MAX], css[PATH_MAX], cache[PATH_MAX];

	home_path(dir, sizeof(dir), "blocklists");
	mkdir(dir, 0755);
	/* the compiled rules are kept in ~/.netsurf/blocklists.cache and mapped on later starts; they are
	 * rebuilt whenever a list file is added, removed or changed */
	flo_blocker_setup(filepath_sfind(respaths, list, "blocklist-default.json") != NULL ? list : NULL, dir,
			  home_path(css, sizeof(css), "adblock.css"), home_path(cache, sizeof(cache), "blocklists.cache"));
	flo_blocker_enable(nsoption_bool(block_advertisements));
	if (getenv("FLORENCE_TRACE") != NULL)
		fprintf(stderr, "florence: blocker: %d rules from %d files, %d cosmetic selectors, %s\n",
			flo_blocker_rule_count(), flo_blocker_file_count(), flo_blocker_css_count(),
			flo_blocker_enabled() ? "on" : "off");
}

/* ---- life cycle --------------------------------------------------------- */

void flo_trace(const char *stage)
{
	if (getenv("FLORENCE_TRACE") != NULL)
		fprintf(stderr, "florence: %s\n", stage);
}

/* netsurf_register() keeps this pointer for the life of the process (guit): it must not be a local */
static struct netsurf_table table;

int flo_core_init(int argc, char **argv)
{
	char buf[PATH_MAX];
	const char *h = getenv("HOME");
	nserror err;

	install_crash_report();
	snprintf(home_dir, sizeof(home_dir), "%s", h != NULL ? h : "/tmp");
	home_path(buf, sizeof(buf), "");
	mkdir(buf, 0700);

	table.misc = &misc_table;
	table.window = flo_window_table;
	table.clipboard = flo_clipboard_table;
	table.download = flo_download_table;
	table.search = flo_search_table;
	table.fetch = gs_fetch_table;
	table.bitmap = flo_bitmap_table;
	table.layout = flo_layout_table;
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
	prefs_load();
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
	flo_blocker_reload();
	if (strcmp(flo_pref_get("autoupdate", "1"), "1") == 0)
		flo_lists_update(false);        /* first run, then weekly: a background thread */
	flo_trace("core: urldb");
	urldb_load(home_path(urls_path, sizeof(urls_path), "URLs"));
	urldb_load_cookies(nsoption_charp(cookie_file));

	gs_fetch_filetype_init(filepath_sfind(respaths, buf, "mimetypes") != NULL ? buf : "/etc/mime.types");
	flo_trace("core: ready");
	return 0;
}

void flo_core_fini(void)
{
	if (drop_history)
		unlink(urls_path);
	else
		urldb_save(urls_path);
	if (drop_cookies)
		unlink(nsoption_charp(cookie_jar));
	else
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
	nserror err;

	flo_trace("open_url");
	if (url == NULL || *url == '\0') {
		const char *home = nsoption_charp(homepage_url);

		if (strcmp(flo_pref_get("newwin", "start"), "home") == 0 && home != NULL && *home != '\0')
			url = home;
		else
			url = flo_startpage_url();        /* bookmarks and recent sites, like Safari's start page */
		if (url == NULL)
			url = home != NULL ? home : "about:welcome";
	}
	if (nsurl_create(url, &u) != NSERROR_OK) {
		flo_trace("open_url: nsurl_create failed");
		return;
	}
	flo_trace("open_url: url parsed, calling browser_window_create");
	err = browser_window_create(BW_CREATE_HISTORY, u, NULL, NULL, &bw);
	nsurl_unref(u);
	if (getenv("FLORENCE_TRACE") != NULL)
		fprintf(stderr, "florence: open_url: browser_window_create returned %d, bw=%p\n", (int)err, (void *)bw);
}

bool flo_js_available(void)
{
#ifdef FLO_WITH_JS
	return true;
#else
	return false;
#endif
}

bool flo_js_enabled(void)
{
	return flo_js_available() && nsoption_bool(enable_javascript);
}

/* Write ~/.netsurf/Choices with only what the user chose: the low-power values apply_overrides()
 * imposes on every start are put back to NetSurf's defaults for the write, then restored. */
static void save_choices(void)
{
	char path[PATH_MAX];
	int mc = nsoption_int(memory_cache_size), mf = nsoption_int(max_fetchers);
	int mfh = nsoption_int(max_fetchers_per_host), mch = nsoption_int(max_cached_fetch_handles);
	bool ai = nsoption_bool(animate_images), csm = nsoption_bool(core_select_menu);

	nsoption_set_int(memory_cache_size, nsoptions_default[NSOPTION_memory_cache_size].value.i);
	nsoption_set_int(max_fetchers, nsoptions_default[NSOPTION_max_fetchers].value.i);
	nsoption_set_int(max_fetchers_per_host, nsoptions_default[NSOPTION_max_fetchers_per_host].value.i);
	nsoption_set_int(max_cached_fetch_handles, nsoptions_default[NSOPTION_max_cached_fetch_handles].value.i);
	nsoption_set_bool(animate_images, nsoptions_default[NSOPTION_animate_images].value.b);
	nsoption_set_bool(core_select_menu, nsoptions_default[NSOPTION_core_select_menu].value.b);
	nsoption_write(home_path(path, sizeof(path), "Choices"), nsoptions, nsoptions_default);
	nsoption_set_int(memory_cache_size, mc);
	nsoption_set_int(max_fetchers, mf);
	nsoption_set_int(max_fetchers_per_host, mfh);
	nsoption_set_int(max_cached_fetch_handles, mch);
	nsoption_set_bool(animate_images, ai);
	nsoption_set_bool(core_select_menu, csm);
}

void flo_js_set(bool on)
{
	if (!flo_js_available())
		return;
	nsoption_set_bool(enable_javascript, on);
	save_choices();
}

bool flo_opt_hide_ads(void) { return nsoption_bool(block_advertisements); }
void flo_opt_set_hide_ads(bool on)
{
	nsoption_set_bool(block_advertisements, on);
	flo_blocker_enable(on);                         /* requests are blocked at once; page styling next start */
	save_choices();
}
bool flo_opt_dnt(void) { return nsoption_bool(do_not_track); }
void flo_opt_set_dnt(bool on) { nsoption_set_bool(do_not_track, on); save_choices(); }
void flo_lists_update(bool force)
{
	gs_lists_start(home_dir, nsoption_charp(ca_bundle), force);
}

bool flo_opt_referer(void) { return nsoption_bool(send_referer); }
void flo_opt_set_referer(bool on) { nsoption_set_bool(send_referer, on); save_choices(); }
const char *flo_opt_homepage(void) { return nsoption_charp(homepage_url) != NULL ? nsoption_charp(homepage_url) : ""; }
void flo_opt_set_homepage(const char *url)
{
	nsoption_set_charp(homepage_url, url != NULL && *url != '\0' ? strdup(url) : NULL);
	save_choices();
}
void flo_clear_cookies(void) { drop_cookies = true; unlink(nsoption_charp(cookie_jar)); }
void flo_clear_history(void) { drop_history = true; unlink(urls_path); }
int flo_opt_font_min(void) { return nsoption_int(font_min_size); }
void flo_opt_set_font_min(int tenths) { nsoption_set_int(font_min_size, tenths); save_choices(); }

const char *flo_resource_dir(void)
{
	return GNUSTEP_RESPATH;
}
