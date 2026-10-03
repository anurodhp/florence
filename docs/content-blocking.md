# Content blocking

Florence blocks network requests with the Safari content-blocker format (WebKit rule-list
JSON), so existing lists work unchanged. It is on by default (**View > Block Ads and Trackers**).

## Where lists come from

* `Florence.app/Resources/blocklist-default.json`: a small built-in list.
* `~/.netsurf/blocklists/*.json`: every file here is loaded, in name order, after the built-in one.
  Restart (or Preferences > Reload lists) to pick up changes.

## EasyList

On first run and then once a week (when the file is older than seven days) a short-lived child process (this executable run with `--update-lists`, so the browser itself stays single-threaded) downloads
`https://easylist.to/easylist/easylist.txt`, converts the Adblock Plus syntax to the format above and
installs it as `~/.netsurf/blocklists/easylist.json`, then the blocker reloads. A failed or partial download
leaves the previous list. Turn it off, or update at once, in Preferences > Content Blocking.

The converter keeps network rules (`||host^`, `|prefix`, `*` wildcards, `$third-party`, `$script`, `$image`,
`$stylesheet`, `$domain=`, ...) and `@@` exceptions (emitted last so they override), plus up to 1000 plain
site-wide `##selector` hiding rules. It drops what a Safari list cannot say: `/regex/` filters, `$popup`, `$csp`,
`$redirect`, `$removeparam`, negated types, scriptlets and site-specific or procedural hiding rules.

## Supported

* `trigger`: `url-filter`, `url-filter-is-case-sensitive`, `resource-type`, `load-type`
  (`first-party`/`third-party`), `if-domain`, `unless-domain`, `if-top-url`, `unless-top-url`.
* `action`: `block`, `ignore-previous-rules`, `css-display-none`.
* `url-filter` regex subset: `. [] ^ $ \ ( ) | * + ?`, `\d \w \s`. Matching is bounded (step
  budget per rule) so a pathological pattern cannot hang the UI.

## Not supported (rules are skipped)

`block-cookies`, `make-https`, `if-frame-url`/`unless-frame-url`-style frame triggers.

## Compiled cache

Parsing a big list and compiling its patterns takes seconds and tens of megabytes on a Pi, so the compiled
rules are saved to `~/.netsurf/blocklists.cache` and the next start maps that file read-only instead of
parsing anything: setup takes about a millisecond and the rules cost almost no private memory, since the
kernel can drop the pages when it needs them and read them back from the file. The cache is rebuilt
whenever a list file is added, removed or changed (it records every file's path, size and modification
time), and a missing, truncated or damaged cache is ignored and rebuilt. Rules are always read through
bounds-checked offsets, so a damaged cache can lose a rule but not crash the browser. Delete the file to
force a rebuild. Lists load in file-name order.

## Notes

* NetSurf's fetcher does not tell us the resource type, so it is guessed from the URL extension;
  an unknown extension matches any type.
* `css-display-none` rules that apply everywhere are written to `~/.netsurf/adblock.css` and take
  effect after a restart. Rules scoped to domains are not applied.
* Requests are refused in `fetch_start()`; this is the second deliberate edit to NetSurf, made to
  the build copy only by `scripts/build_netsurf.sh` (it aborts if upstream's anchors change).
