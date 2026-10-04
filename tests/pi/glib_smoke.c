/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* Runs on the Pi: exercises what WebKit's UI process leans on in GLib/GIO. Prints PASS/FAIL per check. */
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <gio/gunixmounts.h>
static int fails;
#define CHECK(c) do { int ok_ = !!(c); printf("%s %s\n", ok_ ? "PASS" : "FAIL", #c); fflush(stdout); if (!ok_) fails++; } while (0)
static GMainLoop *loop; static int ticks;
static gboolean tick2(gpointer p) { if (++ticks == 3) g_main_loop_quit(loop); return G_SOURCE_CONTINUE; }
int main(void) {
    GString *s = g_string_new("glib"); g_string_append_printf(s, " %d", 66); CHECK(strcmp(s->str, "glib 66") == 0); g_string_free(s, TRUE);
    GRegex *re = g_regex_new("^a+b$", 0, 0, NULL); CHECK(re && g_regex_match(re, "aaab", 0, NULL) && !g_regex_match(re, "aac", 0, NULL)); g_regex_unref(re);
    CHECK(g_get_monotonic_time() > 0);
    CHECK(g_get_host_name() != NULL);
    CHECK(g_get_home_dir() != NULL);
    GObject *o = g_object_new(G_TYPE_OBJECT, NULL); CHECK(o != NULL); g_object_unref(o);
    gchar *contents = NULL; CHECK(g_file_get_contents("/etc/passwd", &contents, NULL, NULL) && contents && *contents); g_free(contents);
    GFile *f = g_file_new_for_path("/etc"); GFileInfo *fi = g_file_query_info(f, "standard::type", 0, NULL, NULL);
    CHECK(fi && g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY); if (fi) g_object_unref(fi); g_object_unref(f);
    loop = g_main_loop_new(NULL, FALSE); g_timeout_add(20, tick2, NULL); g_main_loop_run(loop); CHECK(ticks == 3);
    GError *err = NULL; GSubprocess *sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, &err, "/bin/echo", "hi", NULL);
    CHECK(sp != NULL); if (sp) { gchar *out = NULL; CHECK(g_subprocess_communicate_utf8(sp, NULL, NULL, &out, NULL, NULL) && out && strncmp(out, "hi", 2) == 0); g_free(out); g_object_unref(sp); }
    GList *mounts = g_unix_mounts_get(NULL); CHECK(mounts != NULL); g_list_free_full(mounts, (GDestroyNotify)g_unix_mount_free);
    int sv[2]; GSocket *gs = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_DATAGRAM, 0, NULL); CHECK(gs != NULL); (void)sv; if (gs) g_object_unref(gs);
    printf(fails ? "RESULT FAIL %d\n" : "RESULT OK\n", fails); return fails != 0;
}
