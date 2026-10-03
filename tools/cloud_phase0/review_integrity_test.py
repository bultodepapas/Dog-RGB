"""Compatibility command for the independent-review regression cases.

These cases now run in the normal test_*.py discovery matrix.
"""

import unittest
from test_integrity import OutboxIntegrityReviewRegression

if __name__ == "__main__":
    unittest.main(verbosity=2)
