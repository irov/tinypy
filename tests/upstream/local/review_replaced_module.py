"""A normal module may publish another object through sys.modules."""
import sys
sys.modules[__name__] = 42
