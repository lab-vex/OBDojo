#!/bin/bash
# Compiles the real firmware on a PC against a simulated Ninja 300 K-line (engine ECU on 11,
# ABS on 28, MCP2003A echo and its 25 ms cut-off) and checks fast init, KWP2000 framing,
# live data, codes, the sweep / monitor tools and the ELM327 / app links.
set -e
cd "$(dirname "$0")"
for t in test_core; do
  g++ -std=gnu++17 -g -fsanitize=address,undefined -Imock -o /tmp/$t $t.cpp
  echo "== $t"
  /tmp/$t > /tmp/$t.log 2>&1 || true
  grep -E "FAIL|failure|runtime error|ERROR: AddressSanitizer" /tmp/$t.log
  grep -q "^0 failure(s)" /tmp/$t.log || { cat /tmp/$t.log; exit 1; }
done
echo "All tests passed"
