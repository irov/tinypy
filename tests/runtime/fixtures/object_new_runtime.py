# Portable Python 2.7 behavior checks adapted from CPython 2.7.18
# Lib/test/test_descr.py:ClassPropertiesAndMethods.test_object_new.
# Copyright (c) 2001-2020 Python Software Foundation; All Rights Reserved.
# Distributed under PSF License Version 2; see dependencies/python/LICENSE
# in the Mengine source tree.


def assert_type_error(callable, *args):
    try:
        callable(*args)
    except TypeError:
        return

    raise AssertionError("expected TypeError")


class Plain(object):
    pass


object.__new__(Plain)
assert_type_error(object.__new__, Plain, 5)
object.__init__(Plain())
assert_type_error(object.__init__, Plain(), 5)


class InitOnly(object):
    def __init__(self, value):
        self.value = value


object.__new__(InitOnly)
object.__new__(InitOnly, 5)
object.__init__(InitOnly(3))
assert_type_error(object.__init__, InitOnly(3), 5)


class NewOnly(object):
    def __new__(cls, value):
        return object.__new__(cls)


object.__new__(NewOnly)
assert_type_error(object.__new__, NewOnly, 5)
object.__init__(NewOnly(3))
object.__init__(NewOnly(3), 5)


class NewAndInit(object):
    def __new__(cls, value):
        return object.__new__(cls)

    def __init__(self, value):
        self.value = value


object.__new__(NewAndInit)
object.__new__(NewAndInit, 5)
object.__init__(NewAndInit(3))
instance = NewAndInit(3)
object.__init__(instance, 5)
assert instance.value == 3
