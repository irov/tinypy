"""Project-authored checks for truth, attributes, inheritance and descriptors."""

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
