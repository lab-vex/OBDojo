#!/usr/bin/env python3
"""Checks that the app's demo unit (DemoLink.kt) has the same settings as the firmware:
same keys in the same order, with the same labels, groups, types, defaults, reboot flags,
options and limits. Run from anywhere: python3 tools/check_demo_defs.py"""
import os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
fw = open(os.path.join(root, "firmware/obdojo/obdojo_fw.cpp"), encoding="utf-8").read()
kt = open(os.path.join(root, "android/app/src/main/java/au/obdojo/app/DemoLink.kt"), encoding="utf-8").read()

macros = dict(re.findall(r'#define (\w+) ("(?:[^"\\]|\\.)*")', fw))
TYPES = {"S_STR": "str", "S_PASS": "pass", "S_INT": "int", "S_BOOL": "bool", "S_FLOAT": "float", "S_SEL": "sel"}


def cstr(expr):
    """Joins adjacent C string literals and macros: "Signal 1 (" SIG_HELP ")" -> one string."""
    out = ""
    for tok in re.findall(r'"(?:[^"\\]|\\.)*"|\w+', expr):
        if tok.startswith('"'):
            out += tok[1:-1]
        elif tok in macros:
            out += macros[tok][1:-1]
        else:
            raise ValueError("unknown macro " + tok)
    return out


table = fw[fw.index("static const SDef SDEFS[K_COUNT] = {"):]
table = table[:table.index("\n};")]
firm = []
for line in table.splitlines():
    line = line.strip()
    if not line.startswith("{\""):
        continue
    body = line[1:line.rindex("}")]
    # split on commas outside quotes
    parts, cur, q = [], "", False
    for ch in body:
        if ch == '"':
            q = not q
        if ch == "," and not q:
            parts.append(cur.strip()); cur = ""
        else:
            cur += ch
    parts.append(cur.strip())
    key, label, group, typ, dflt, reboot, opts, mn, mx = parts
    firm.append((cstr(key), cstr(label), cstr(group), TYPES[typ], cstr(dflt), reboot == "true", cstr(opts), float(mn), float(mx)))

demo = []
for m in re.finditer(r'Def\("([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", (true|false), "([^"]*)", ([\d.]+), ([\d.]+)\)', kt):
    k, l, g, t, d, r, o, mn, mx = m.groups()
    demo.append((k, l, g, t, d, r == "true", o, float(mn), float(mx)))

names = ("key", "label", "group", "type", "default", "reboot", "options", "min", "max")
bad = 0
if [f[0] for f in firm] != [d[0] for d in demo]:
    print("Setting keys differ:\n  firmware:", [f[0] for f in firm], "\n  demo:    ", [d[0] for d in demo])
    bad += 1
for f in firm:
    d = next((x for x in demo if x[0] == f[0]), None)
    if d is None:
        continue
    for i, n in enumerate(names):
        if f[i] != d[i]:
            print(f"{f[0]}: {n} is {f[i]!r} in the firmware but {d[i]!r} in DemoLink.kt")
            bad += 1
if bad:
    sys.exit(1)
print(f"Demo settings match the firmware ({len(firm)} settings)")
