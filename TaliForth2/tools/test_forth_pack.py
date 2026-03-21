# Test Routine for forth_pack.py
# Scot W. Stevenson <scot.stevenson@gmail.com>
# First version: 28. Feb 2018
# This version: 28. Feb 2018

import forth_pack
from unittest import TestCase, main


class TestForthPack(TestCase):
    """Tests functions that support"""

    def test_remove_bracket_comment(self):
        """Test routine to remove inline comments"""

        table = ((' no comment', 'no comment'),
                 ('a ( comment ) here', 'a here'),
                 ('( beginning ) comment', 'comment'),
                 ('.( print command)', '.( print command)'),
                 ('a final ( comment)', 'a final'),
                 ('a ( comment ) with .( print )', 'a with .( print )'))

        for (given, expected) in table:
            words = forth_pack.parse_line(given)
            self.assertEqual(' '.join(words), expected)


if __name__ == '__main__':
    main()
