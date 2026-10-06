"""Test support for normal Python 2 execution, with Py3k warnings disabled."""

import sys

have_unicode = True
verbose = False
TestFailed = AssertionError


class Py3kWarningsDisabled(object):
    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        return False


def check_py3k_warnings(*filters, **kwargs):
    # Upstream only checks these warnings when Python is invoked with -3.
    # Neither the CLI nor the reference runner enables that mode.
    if getattr(sys, "py3kwarning", False):
        raise NotImplementedError("the upstream adapter does not support -3")
    return Py3kWarningsDisabled()


def cpython_only(function):
    function.__unittest_skip__ = True
    function.__unittest_skip_why__ = "CPython implementation detail"
    return function


def requires_unicode(function):
    return function


def run_unittest(*classes):
    raise RuntimeError("use run_case.py to execute each upstream test separately")
