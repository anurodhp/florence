# Toolbar icons

The seven SVGs in `src/` are from [Lucide](https://lucide.dev) (lucide-static 1.50.0), ISC licensed; the
chevrons, `x`, `plus` and `lock` derive from Feather (MIT). Both notices are in `LICENSE-lucide.txt`, which
must stay with them. ISC and MIT are permissive and compatible with this repo's MIT licence; the icons keep their own notice.

`tools/make_toolbar_icons.sh` renders them to `*.tiff` (the colour states the buttons use). The TIFFs are
committed so the Mac build needs no SVG tools.
