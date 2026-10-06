"""Import must fail when a module removes its registration while loading."""
import sys
del sys.modules[__name__]
