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
	nserror err;

	flo_trace("open_url");
	if (url == NULL || *url == '\0')
		url = nsoption_charp(homepage_url) != NULL ? nsoption_charp(homepage_url) : "about:welcome";
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

const char *flo_resource_dir(void)
{
	return GNUSTEP_RESPATH;
}
