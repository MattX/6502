#!/usr/bin/env python3

# 64tass generates vice labels in some random order
# This script sorts them in ascending hex order which is more useful
# for human consumption or other post-processing like analyzing memory usage
#
# Usage:
#   64tass --vice-labels --labels=folder/vice.txt ...
#   python3 sort_vice_labels.py folder/vice.txt

import sys

if len(sys.argv) != 2:
    print(f"Usage: python3 sort_vice_labels.py folder/vice_labels.txt")
    sys.exit(1)

fname = sys.argv[1]

try:
    lines = open(fname).read().splitlines()
    lines = sorted(lines, key=lambda s: int(s.split()[1], 16))
    open(fname, "w").write("\n".join(lines))
    sys.exit(0)
except FileNotFoundError:
    print(f"Failed to read from '{fname}'")
except IndexError:
    print("Expected file containing labels like 'al fc00 somesymbol'")
except PermissionError:
    print(f"Failed to write to '{fname}'")

sys.exit(2)