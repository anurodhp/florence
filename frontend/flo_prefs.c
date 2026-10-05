/*
 * Florence: settings, remembered in one small key=value file. Plain C, no WebKit: the UI reads and writes
 * them through flo.h. A handful of keys, so a linear table and a rewrite of the whole file on each change
 * (changes are rare and the file is tiny).
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flo.h"

#define MAX_PREFS 32
#define MAX_LEN 512

static struct { char key[48]; char val[MAX_LEN]; } table[MAX_PREFS];
static int count;
static char file[1024];

static void save(void)
{
	FILE *f;
	int i;

	if (file[0] == '\0' || (f = fopen(file, "w")) == NULL)
		return;
	for (i = 0; i < count; i++)
		fprintf(f, "%s=%s\n", table[i].key, table[i].val);
	fclose(f);
}

static int find(const char *key)
{
	int i;

	for (i = 0; i < count; i++)
		if (strcmp(table[i].key, key) == 0)
			return i;
	return -1;
}

void flo_prefs_init(const char *path)
{
	char line[MAX_LEN + 64];
	FILE *f;

	snprintf(file, sizeof file, "%s", path);
	count = 0;
	if ((f = fopen(path, "r")) == NULL)
		return;
	while (fgets(line, sizeof line, f) != NULL && count < MAX_PREFS) {
		char *eq = strchr(line, '='), *nl = strchr(line, '\n');
		if (eq == NULL || eq == line || eq - line >= (long)sizeof table[0].key)
			continue;
		if (nl != NULL)
			*nl = '\0';
		*eq = '\0';
		snprintf(table[count].key, sizeof table[count].key, "%s", line);
		snprintf(table[count].val, sizeof table[count].val, "%s", eq + 1);
		count++;
	}
	fclose(f);
}

const char *flo_pref_get(const char *key, const char *def)
{
	int i = find(key);
	return i >= 0 ? table[i].val : def;
}

void flo_pref_set(const char *key, const char *val)
{
	int i = find(key);

	if (i < 0) {
		if (count >= MAX_PREFS || strlen(key) >= sizeof table[0].key)
			return;
		i = count++;
		snprintf(table[i].key, sizeof table[i].key, "%s", key);
	} else if (strcmp(table[i].val, val) == 0) {
		return;
	}
	snprintf(table[i].val, sizeof table[i].val, "%s", val);
	save();
}

bool flo_opt_hide_ads(void) { return strcmp(flo_pref_get("hide_ads", "1"), "1") == 0; }
int  flo_opt_font_min(void) { return atoi(flo_pref_get("font_min", "85")); }
const char *flo_opt_homepage(void) { return flo_pref_get("homepage", ""); }
void flo_opt_set_homepage(const char *url) { flo_pref_set("homepage", url); }
