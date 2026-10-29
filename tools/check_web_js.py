#!/usr/bin/env python3
"""Pulls the script out of the firmware's web page (web_ui.h) and has node check it parses."""
import os, re, subprocess, sys, tempfile

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
h = open(os.path.join(root, "firmware/obdojo/web_ui.h"), encoding="utf-8").read()
scripts = re.findall(r"<script>(.*?)</script>", h, re.S)
if not scripts:
    sys.exit("no <script> in web_ui.h")
with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
    f.write("\n".join(scripts))
r = subprocess.run(["node", "--check", f.name])
os.unlink(f.name)
if r.returncode:
    sys.exit(1)
print("Web page script parses")
