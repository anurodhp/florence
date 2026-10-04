# SPDX-License-Identifier: GPL-2.0-only
import re
class LooseVersion:
    def __init__(self, vstring):
        self.vstring = vstring
        self.version = [int(p) if p.isdigit() else p for p in re.split(r'(\d+|\.)', vstring) if p and p != '.']
    def _cmp(self, other):
        o = other.version if isinstance(other, LooseVersion) else LooseVersion(other).version
        return (self.version > o) - (self.version < o)
    def __eq__(self, o): return self._cmp(o) == 0
    def __lt__(self, o): return self._cmp(o) < 0
    def __le__(self, o): return self._cmp(o) <= 0
    def __gt__(self, o): return self._cmp(o) > 0
    def __ge__(self, o): return self._cmp(o) >= 0
    def __str__(self): return self.vstring
