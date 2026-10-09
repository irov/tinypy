"""Project-authored checks for truth, attributes, inheritance and descriptors."""

import _weakref
import sys
import unittest


class ObjectProtocols(unittest.TestCase):
    def test_boolean_numeric_relationship(self):
        self.assertIsInstance(True, int)
        self.assertIsInstance(False, int)
        self.assertEqual(True + True, 2)
        self.assertIs(type(True + True), int)
        for left in (False, True):
            for right in (False, True):
                self.assertIs(type(left & right), bool)
                self.assertIs(type(left | right), bool)
                self.assertIs(type(left ^ right), bool)
                self.assertEqual(left & right, bool(int(left) & int(right)))
                self.assertEqual(left | right, bool(int(left) | int(right)))
                self.assertEqual(left ^ right, bool(int(left) ^ int(right)))

    def test_short_circuit_retains_operands(self):
        payload = ["parcel"]
        self.assertIs([] or payload, payload)
        self.assertIs(payload and payload, payload)
        self.assertEqual(False and (1 / 0), False)
        self.assertEqual(True or (1 / 0), True)

    def test_nonzero_precedes_length(self):
        class Decision(object):
            def __nonzero__(self):
                return False

            def __len__(self):
                raise LookupError("length must not be consulted")

        self.assertIs(bool(Decision()), False)

    def test_truth_exception_propagates(self):
        class Decision(object):
            def __nonzero__(self):
                raise LookupError("decision unavailable")

        self.assertRaises(LookupError, bool, Decision())

    def test_attribute_lifecycle(self):
        class Record(object):
            category = "delivery"

        record = Record()
        self.assertEqual(getattr(record, "absent", 13), 13)
        setattr(record, "count", 29)
        self.assertEqual(record.__dict__, {"count": 29})
        record.category = "return"
        self.assertEqual(Record.category, "delivery")
        del record.category
        self.assertEqual(record.category, "delivery")
        delattr(record, "count")
        self.assertFalse(hasattr(record, "count"))
        self.assertRaises(AttributeError, getattr, record, "count")

    def test_property_data_descriptor(self):
        class Record(object):
            def __init__(self):
                self.storage = 8

            def read(self):
                return self.storage * 3

            def write(self, value):
                self.storage = value

            amount = property(read, write)

        record = Record()
        record.__dict__["amount"] = 99
        self.assertEqual(record.amount, 24)
        record.amount = 7
        self.assertEqual(record.amount, 21)

    def test_read_only_property(self):
        class Record(object):
            @property
            def amount(self):
                return 23

        record = Record()
        self.assertEqual(record.amount, 23)
        self.assertRaises(AttributeError, setattr, record, "amount", 9)

    def test_descriptor_receives_owner(self):
        class Descriptor(object):
            def __get__(self, instance, owner):
                return instance, owner

        class Record(object):
            slot = Descriptor()

        record = Record()
        self.assertEqual(Record.slot, (None, Record))
        self.assertEqual(record.slot, (record, Record))

    def test_diamond_resolution(self):
        class Root(object):
            marker = "root"

        class Left(Root):
            pass

        class Right(Root):
            marker = "right"

        class Leaf(Left, Right):
            pass

        self.assertEqual(Leaf().marker, "right")
        self.assertEqual(Leaf.__mro__, (Leaf, Left, Right, Root, object))

    def test_notimplemented_uses_reflected_operation(self):
        class Parcel(object):
            def __add__(self, other):
                return NotImplemented

        class Destination(object):
            def __radd__(self, other):
                return "reflected delivery"

        self.assertEqual(Parcel() + Destination(), "reflected delivery")

    def test_weakref_subclass_holding_its_referent(self):
        calls = []

        class Referent(object):
            pass

        class Ref(_weakref.ref):
            pass

        referent = Referent()
        first = Ref(referent, calls.append)
        first.referent = referent
        del referent, first
        referent = Referent()
        first = Ref(referent, calls.append)
        second = Ref(referent, calls.append)
        first.second = second
        second.referent = referent
        del referent, second, first
        self.assertEqual(calls, [])

    def test_weak_proxy_compares_only_proxies(self):
        calls = []

        class Ordered(object):
            def __eq__(self, other):
                calls.append(("eq", type(other).__name__))
                return "equal"

            def __cmp__(self, other):
                calls.append(("cmp", type(other).__name__))
                return -1

        first = Ordered()
        second = Ordered()
        proxy = _weakref.proxy(first)
        self.assertIs(proxy == 1, False)
        self.assertIs(proxy != 1, True)
        self.assertEqual(calls, [])
        self.assertEqual(cmp(proxy, _weakref.proxy(second)), -1)
        self.assertEqual(calls, [("cmp", "Ordered")])
        self.assertEqual(proxy == first, "equal")
        try:
            hash(proxy)
        except TypeError as error:
            self.assertEqual(str(error), "unhashable type: 'weakproxy'")
        else:
            self.fail("weak proxies are unhashable")

    def test_classic_lookup_recursion_is_raised(self):
        class Base:
            pass

        class Loop(Base):
            def __getattr__(self, name):
                return self

        self.assertRaises(RuntimeError, str, Loop())
        self.assertTrue(isinstance(Loop(), Base))

    def test_builtin_exception_type_names(self):
        self.assertEqual(repr(ValueError), "<type 'exceptions.ValueError'>")
        self.assertEqual((ValueError.__name__, ValueError.__module__), ("ValueError", "exceptions"))
        self.assertEqual(repr(KeyError("k")), "KeyError('k',)")
        try:
            ValueError("v") ^ None
        except TypeError as error:
            self.assertEqual(str(error), "unsupported operand type(s) for ^: 'exceptions.ValueError' and 'NoneType'")
        self.assertRaises(AttributeError, getattr, Exception(), "__module__")
        self.assertNotIn("__module__", dir(IOError(1, 2)))
        self.assertNotIn("__module__", vars(ValueError))

    def test_class_assignment_checks_layout(self):
        class Wide(long):
            pass

        class OtherWide(long):
            pass

        class Plain(object):
            pass

        class Classic:
            pass

        try:
            Wide(1).__class__ = OtherWide
        except TypeError as error:
            self.assertEqual(str(error), "__class__ assignment: 'Wide' object layout differs from 'OtherWide'")
        else:
            self.fail("long subclasses must not share a layout")
        try:
            Plain().__class__ = Classic
        except TypeError as error:
            self.assertEqual(str(error), "__class__ must be set to new-style class, not 'classobj' object")
        else:
            self.fail("a classic class must be rejected")

    def test_dict_assignment_uses_builtin_base(self):
        class Error(Exception):
            pass

        class Base(object):
            pass

        class Module(Base, type(sys)):
            pass

        error = Error()
        try:
            error.__dict__ = 1.5
        except TypeError as failure:
            self.assertEqual(str(failure), "__dict__ must be a dictionary")
        else:
            self.fail("exception __dict__ must be a dictionary")
        module = Module("spam")
        self.assertRaises(TypeError, setattr, module, "__dict__", {})
        self.assertRaises(TypeError, delattr, module, "__dict__")
        self.assertRaises(TypeError, Base.__dict__["__dict__"].__set__, module, {})

    def test_exception_getitem_takes_an_index(self):
        error = Exception(1, 2, 3)
        self.assertEqual(error.__getitem__(-1), 3)
        self.assertEqual(error.__getitem__(True), 2)
        try:
            error.__getitem__("a")
        except TypeError as failure:
            self.assertEqual(str(failure), "'str' object cannot be interpreted as an index")
        else:
            self.fail("exception items take an index")
        self.assertRaises(IndexError, error.__getitem__, 3)

    def test_exception_unicode_follows_str(self):
        class Error(Exception):
            def __str__(self):
                return u"f\xf6\xf6"

        self.assertEqual(unicode(Error()), u"f\xf6\xf6")
        for error in (KeyError("k"), IOError(2, "missing"), SyntaxError("bad", ("f", 1, 2, "x")),
                      UnicodeDecodeError("ascii", "\xc3", 0, 1, "bad"), UnicodeEncodeError("ascii", u"\u1234", 0, 1, "bad"),
                      UnicodeTranslateError(u"\u1234", 0, 1, "bad")):
            self.assertEqual(unicode(error), str(error))

    def test_unicode_error_argument_types(self):
        for error_type, args, message in (
            (UnicodeDecodeError, ("ascii", u"x", 0, 1, "r"), "argument 2 must be str, not unicode"),
            (UnicodeEncodeError, ("ascii", "x", 0, 1, "r"), "argument 2 must be unicode, not str"),
            (UnicodeEncodeError, (1, u"x", 0, 1, "r"), "argument 1 must be str, not int"),
            (UnicodeTranslateError, (u"x", 0, 1, None), "argument 4 must be str, not None"),
            (UnicodeDecodeError, ("ascii",), "function takes exactly 5 arguments (1 given)"),
        ):
            try:
                error_type(*args)
            except TypeError as error:
                self.assertEqual(str(error), message)
            else:
                self.fail(message)

    def test_class_doc_descriptor_is_bound(self):
        class Doc(object):
            def __get__(self, instance, owner):
                return (instance, owner)

        class Documented(object):
            __doc__ = Doc()

        self.assertEqual(Documented.__doc__, (None, Documented))
        documented = Documented()
        self.assertEqual(documented.__doc__, (documented, Documented))

    def test_classmethod_new_receives_class_twice(self):
        class Factory(object):
            @classmethod
            def __new__(*args):
                return args

        class Derived(Factory):
            pass

        self.assertEqual(Factory(1, 2), (Factory, Factory, 1, 2))
        self.assertEqual(Derived(1, 2), (Derived, Derived, 1, 2))

    def test_secondary_base_provides_dict(self):
        class WithDict(object):
            __slots__ = ["__dict__"]

        class WithWeakref(object):
            __slots__ = ["__weakref__"]

        class Combined(WithWeakref, WithDict):
            __slots__ = []

        combined = Combined()
        combined.parcel = 42
        self.assertEqual(combined.__dict__, {"parcel": 42})
        self.assertTrue(hasattr(combined, "__weakref__"))

    def test_type_has_no_abstractmethods(self):
        class Meta(type):
            pass

        for owner in (type, Meta):
            try:
                getattr(owner, "__abstractmethods__")
            except AttributeError as error:
                self.assertEqual(str(error), "__abstractmethods__")
            else:
                self.fail("__abstractmethods__ must be missing")

    def test_code_is_weakly_referenceable(self):
        calls = []
        code = compile("1", "parcel", "eval")
        reference = _weakref.ref(code, calls.append)
        self.assertIs(reference(), code)
        del code
        self.assertIs(reference(), None)
        self.assertEqual(calls, [reference])

    def test_builtin_new_checks_receiver(self):
        for new, args, message in (
            (object.__new__, (), "object.__new__(): not enough arguments"),
            (object.__new__, ("",), "object.__new__(X): X is not a type object (str)"),
            (list.__new__, (object,), "list.__new__(object): object is not a subtype of list"),
            (int.__new__, (str,), "int.__new__(str): str is not a subtype of int"),
        ):
            try:
                new(*args)
            except TypeError as error:
                self.assertEqual(str(error), message)
            else:
                self.fail(message)
        empty = super.__new__(super)
        self.assertIs(empty.__thisclass__, None)
        self.assertEqual(repr(empty), "<super: <class 'NULL'>, NULL>")

    def test_classic_instance_constructor(self):
        class Probe:
            pass

        class Classic:
            def __init__(self):
                raise AssertionError("instance() must not call __init__")

        instance_type = type(Probe())
        namespace = {"parcel": 3}
        instance = instance_type(Classic, namespace)
        self.assertIs(instance.__class__, Classic)
        self.assertIs(instance.__dict__, namespace)
        self.assertEqual(instance_type(Classic, None).__dict__, {})
        self.assertRaises(TypeError, instance_type, 1)
        self.assertRaises(TypeError, instance_type, Classic, 1)

def truth_check(value, expected):
    def check(self):
        self.assertIs(bool(value), expected)
        self.assertIs(not value, not expected)
    return check


def length_check(length):
    def check(self):
        class Sized(object):
            def __len__(self):
                return length

        self.assertIs(bool(Sized()), length != 0)
    return check


for label, value, expected in (
    ("none", None, False), ("zero", 0, False), ("wide_zero", 0L, False),
    ("real_zero", 0.0, False), ("empty_text", "", False),
    ("empty_unicode", u"", False), ("empty_list", [], False),
    ("empty_tuple", (), False), ("empty_mapping", {}, False),
    ("negative", -7, True), ("wide", 7L, True), ("real", 0.25, True),
    ("text", "parcel", True), ("list", [0], True),
    ("mapping", {"parcel": 0}, True), ("instance", object(), True),
):
    setattr(ObjectProtocols, "test_truth_" + label, truth_check(value, expected))

for length in (0, 1, 23):
    setattr(ObjectProtocols, "test_length_%d" % length, length_check(length))
