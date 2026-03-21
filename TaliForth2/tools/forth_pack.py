#!/usr/bin/env python3
# Compact Forth code to a single line without comments
# Scot W. Stevenson <scot.stevenson@gmail.com>
# First version: 27. Feb 2018
# First version: 01. Mar 2018

# This version : 18. Nov 2018
# Modfied by SamCo to use regular expressions to locate/remove comments.

"""Convert Forth code to compact ASCII for inclusion in the TaliForth binary.

Takes normal Forth code and strips out the comments before being
compacted to an ASCII code that contains only a single whitespace
between words. This can then be included into a 64tass assembler file
with the .binary command. Outputs result on standard output
"""

import argparse
import sys
import re


# Regular expressions used to find/remove Forth comments:

# ((Beginning of line followed by "(" ) or (whitespace followed by
# "(" )) followed by whitespace followed by zero or more non-")"
# characters followed by ")"
paren_comment = re.compile(r"(^\(|\s\()\s[^)]*\)")

# ((Beginning of line followed by ":" ) or (whitespace followed by
# ":" )) followed by whitespace followed by "(" followed by
# whitespace
paren_definition = re.compile(r"(^:|\s:)\s\(\s")

# ((Beginning of line followed by "\" ) or (whitespace followed by
# "\" )) followed by whitespace followed by anything all the way
# to the end of the line.
backslash_comment = re.compile(r"(^\\|\s\\)(\s.*)?$")


def parse_line(line: str) -> list[str]:
        line = line.strip()

        # Remove all ( ... ) comments.
        # Take special care not to remove a definition of "("
        if not paren_definition.search(line):
            line = paren_comment.sub(" ", line)

        # Remove all \ ... comments
        line = backslash_comment.sub("", line)

        # Paren comments were replaced with a space.
        # Remove any space from the beginning/end of the line.
        return line.strip().split()


def main():
    all_words = []

    with open(args.source, "r") as f:
        for line in f.readlines():
            # Add only the non-comment words to the results.
            all_words += parse_line(line)

    # Merge everything into one big line for compact printing
    one_line = ' '.join(all_words)

    # Add trailing space because we might be adding another batch of
    # Forth words after this one (say, forth_words and user_words
    # Use sys.stdout.write() instead of print() because we don't want
    # the line feed at the end
    sys.stdout.write(f"{one_line} ")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--input', dest='source', required=True,
                        help='Forth source code file (required)')
    args = parser.parse_args()

    main()
