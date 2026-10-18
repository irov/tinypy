"""Imports a sibling relative to the subpackage once its package failed."""
RUNS = []


def sibling_value():
    from . import sibling
    return sibling.VALUE
