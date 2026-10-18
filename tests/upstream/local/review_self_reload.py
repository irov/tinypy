"""A module whose reload reloads it again."""
import sys
RUNS = globals().get('RUNS', 0) + 1
if RUNS > 1:
    reload(sys.modules[__name__])
