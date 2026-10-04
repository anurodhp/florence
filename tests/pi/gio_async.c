/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* GIO asynchronous file operations on the Pi: WebKit's glib port loads file:// URLs through libsoup/GIO (GTask worker threads).
 * Each operation must complete within a few seconds with the main loop running. */
#include <gio/gio.h>
#include <stdio.h>
static GMainLoop *loop; static int done, fails; static const char *what;
static gboolean timeout(gpointer p) { (void)p; printf("FAIL %s: no completion in 10 s\n", what); fails++; g_main_loop_quit(loop); return G_SOURCE_REMOVE; }
static void finish(const char *name, gboolean ok) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) fails++; done = 1; g_main_loop_quit(loop); }
static void on_load(GObject *o, GAsyncResult *r, gpointer d) { (void)d; gchar *c = NULL; gsize n = 0; GError *e = NULL; gboolean ok = g_file_load_contents_finish(G_FILE(o), r, &c, &n, NULL, &e); finish("g_file_load_contents_async", ok && n > 0); g_free(c); if (e) g_error_free(e); }
static void on_info(GObject *o, GAsyncResult *r, gpointer d) { (void)d; GError *e = NULL; GFileInfo *i = g_file_query_info_finish(G_FILE(o), r, &e); finish("g_file_query_info_async", i != NULL); if (i) g_object_unref(i); if (e) g_error_free(e); }
static void on_read(GObject *o, GAsyncResult *r, gpointer d) { (void)d; GError *e = NULL; GFileInputStream *s = g_file_read_finish(G_FILE(o), r, &e); finish("g_file_read_async", s != NULL); if (s) g_object_unref(s); if (e) g_error_free(e); }
static void run(const char *name, void (*start)(GFile *)) {
    GFile *f = g_file_new_for_path("/etc/passwd"); what = name; done = 0;
    loop = g_main_loop_new(NULL, FALSE); guint t = g_timeout_add_seconds(10, timeout, NULL);
    start(f); g_main_loop_run(loop); if (!done) {} g_source_remove(t); g_main_loop_unref(loop); g_object_unref(f);
}
static void s_load(GFile *f) { g_file_load_contents_async(f, NULL, on_load, NULL); }
static void s_info(GFile *f) { g_file_query_info_async(f, "standard::*", 0, G_PRIORITY_DEFAULT, NULL, on_info, NULL); }
static void s_read(GFile *f) { g_file_read_async(f, G_PRIORITY_DEFAULT, NULL, on_read, NULL); }
int main(void) { setvbuf(stdout, NULL, _IONBF, 0); run("load_contents", s_load); run("query_info", s_info); run("read", s_read); printf(fails ? "RESULT FAIL %d\n" : "RESULT OK\n", fails); return fails != 0; }
