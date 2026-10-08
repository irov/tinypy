"""Project-authored functional checks for the October runtime review."""

import sys
import unittest


class ReviewRegressions(unittest.TestCase):
    def test_filter_immutable_subtype_item_protocol(self):
        for base in (tuple, str, unicode):
            class Items(base):
                def __iter__(self):
                    raise AssertionError('filter must use item access')
                def __len__(self):
                    raise AssertionError('filter must use storage length')
                def __getitem__(self, index):
                    self.calls.append(index)
                    return self.values[index]
            source = Items('abc')
            source.values = (0, 7, 9) if base is tuple else (base(''), base('XY'), base('z'))
            expected = (7, 9) if base is tuple else base('XYz')
            for predicate in (None, bool):
                source.calls = []
                result = filter(predicate, source)
                self.assertIs(type(result), base)
                self.assertEqual(result, expected)
                self.assertEqual(source.calls, [0, 1, 2])

    def test_filter_indexed_errors_propagate(self):
        for base in (tuple, str, unicode):
            class Items(base):
                def __getitem__(self, index):
                    raise self.failure('item failed')
            source = Items('a')
            for failure in (IndexError, StopIteration, ValueError):
                source.failure = failure
                self.assertRaises(failure, filter, None, source)

    def test_filter_text_item_validation_order(self):
        for base in (str, unicode):
            class Items(base):
                def __getitem__(self, index):
                    self.calls.append(index)
                    return self.values[index]
            source = Items('abc')
            source.values = (0, base('a'), base('b'))
            source.calls = []
            self.assertRaises(TypeError, filter, None, source)
            self.assertEqual(source.calls, [0])
            source.calls = []
            self.assertEqual(filter(bool, source), base('ab'))
            self.assertEqual(source.calls, [0, 1, 2])
            class CustomTruth(base):
                def __nonzero__(self):
                    raise ValueError('truth was requested')
            source.values = (CustomTruth('x'), base(''), base('z'))
            self.assertEqual(filter(None, source), base('xz'))
            self.assertRaises(ValueError, filter, bool, source)

    def test_filter_bool_truth_protocol(self):
        class Truth(object):
            def __init__(self, value):
                self.value = value
                self.calls = 0
            def __nonzero__(self):
                self.calls += 1
                if self.value == 2:
                    raise ValueError('truth failed')
                return self.value
        for predicate in (None, bool):
            no, yes, failing, trailing = [Truth(value) for value in (0, 1, 2, 1)]
            self.assertEqual(filter(predicate, [no, yes]), [yes])
            self.assertEqual((no.calls, yes.calls), (1, 1))
            self.assertRaises(ValueError, filter, predicate, [failing, trailing])
            self.assertEqual((failing.calls, trailing.calls), (1, 0))

    def test_map_none_copy_and_subclass_iteration(self):
        for base in (list, tuple):
            for values in ([], [[1], [2]]):
                source = base(values)
                result = map(None, source)
                self.assertIs(type(result), list)
                self.assertIsNot(result, source)
                self.assertEqual(result, values)
                for index in range(len(values)):
                    self.assertIs(result[index], values[index])
                result.append('extra')
                self.assertEqual(list(source), values)
            class Items(base):
                def __iter__(self):
                    return iter(['alternate'])
            self.assertEqual(map(None, Items([1, 2])), ['alternate'])

    def test_sequence_deletion_protocol(self):
        class Sequence:
            def __init__(self):
                self.deleted = []
            def __delitem__(self, key):
                self.deleted.append(key)
        sequence = Sequence()
        del sequence[2]
        del sequence[-1]
        self.assertEqual(sequence.deleted, [2, -1])
        class RejectDeletion(object):
            def __delitem__(self, key):
                raise KeyError(key)
        with self.assertRaises(KeyError) as error:
            del RejectDeletion()[3]
        self.assertEqual(error.exception.args, (3,))

    def test_reversed_length_hint(self):
        for sequence in ('hello', tuple('hello'), list('hello'), xrange(5)):
            iterator = reversed(sequence)
            self.assertEqual(iterator.__length_hint__(), len(sequence))
            next(iterator)
            self.assertEqual(iterator.__length_hint__(), len(sequence) - 1)
            self.assertEqual(list(iterator), list(sequence)[-2::-1])
            self.assertEqual(iterator.__length_hint__(), 0)
        class ChangingLength:
            def __init__(self):
                self.calls = 0
            def __len__(self):
                self.calls += 1
                if self.calls > 1:
                    raise ZeroDivisionError('length failed')
                return 10
            def __getitem__(self, index):
                return index
        iterator = reversed(ChangingLength())
        self.assertRaises(ZeroDivisionError, iterator.__length_hint__)

    def test_reversed_subclass_ignores_keywords(self):
        class ReverseSubclass(reversed):
            pass
        self.assertEqual(list(ReverseSubclass([1, 2, 3], ignored=True)), [3, 2, 1])
        self.assertEqual(list(ReverseSubclass([1, 2, 3], sequence='unused')), [3, 2, 1])

    def test_sequence_consumers_call_length_hints(self):
        class Source(object):
            def __init__(self, events, missing_length=False):
                self.events = events
                self.missing_length = missing_length
                self.index = 0
            def __iter__(self):
                self.events.append('iter')
                return self
            def next(self):
                self.events.append(('next', self.index))
                if self.index == 3:
                    raise StopIteration
                value = self.index
                self.index += 1
                return value
            def __len__(self):
                self.events.append('len')
                if self.missing_length:
                    raise AttributeError('no length')
                return 7
            def __length_hint__(self):
                self.events.append('hint')
                return 5

        consumers = (
            (list, [0, 1, 2]),
            (tuple, (0, 1, 2)),
            (sorted, [0, 1, 2]),
            (lambda value: filter(None, value), [1, 2]),
            (lambda value: map(None, value), [0, 1, 2]),
        )
        for consumer, expected in consumers:
            events = []
            self.assertEqual(consumer(Source(events)), expected)
            self.assertEqual(events[:2], ['iter', 'len'])
            events = []
            self.assertEqual(consumer(Source(events, True)), expected)
            self.assertEqual(events[:3], ['iter', 'len', 'hint'])

        events = []
        self.assertEqual(zip(Source(events), [10, 11, 12]), [(0, 10), (1, 11), (2, 12)])
        self.assertEqual(events[:2], ['len', 'iter'])

        class BrokenLength(Source):
            def __len__(self):
                self.events.append('len')
                raise ValueError('length failed')
        for consumer, unused in consumers:
            events = []
            self.assertRaises(ValueError, consumer, BrokenLength(events))
            self.assertEqual(events, ['iter', 'len'])
        events = []
        self.assertRaises(ValueError, zip, BrokenLength(events), [1])
        self.assertEqual(events, ['len'])
        events = []
        self.assertEqual(map(None, Source(events), BrokenLength(events)), [(0, 0), (1, 1), (2, 2)])
        self.assertEqual(events[:4], ['iter', 'len', 'iter', 'len'])
        events = []
        self.assertRaises(ValueError, zip, Source(events), BrokenLength(events))
        self.assertEqual(events, ['len', 'len'])

        class BrokenHintDescriptor(object):
            def __get__(self, instance, owner):
                raise AttributeError('descriptor failed')
        class DescriptorSource(object):
            __length_hint__ = BrokenHintDescriptor()
            def __iter__(self):
                return self
            def next(self):
                raise StopIteration
        self.assertRaises(AttributeError, list, DescriptorSource())

    def test_sequence_consumers_reject_minus_one_length_hint(self):
        class Source(object):
            def __init__(self):
                self.done = False
            def __iter__(self):
                return self
            def next(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return 1
            def __length_hint__(self):
                return -1
        consumers = (
            list,
            tuple,
            sorted,
            lambda value: filter(None, value),
            lambda value: map(None, value),
            lambda value: zip(value, [1]),
        )
        for consumer in consumers:
            self.assertRaises(SystemError, consumer, Source())
        self.assertEqual(map(lambda value: value, Source()), [1])
        self.assertEqual(map(None, Source(), [2]), [(1, 2)])

    def test_reversed_noncallable_releases_reference(self):
        import _weakref as weakref
        class Receiver(object):
            pass
        def source():
            pass
        receiver = Receiver()
        observed = weakref.ref(receiver)
        source.__reversed__ = receiver
        for unused in range(10):
            self.assertRaises(TypeError, reversed, source)
            self.assertIs(observed(), receiver)
        del source.__reversed__
        receiver = None
        self.assertIs(observed(), None)

    def test_reversed_xrange_values_and_exhaustion(self):
        for bounds in ((0,), (1,), (5,), (3, 12, 2), (9, -4, -3), (-9, 4, 3)):
            sequence = xrange(*bounds)
            expected = list(sequence)[::-1]
            iterator = reversed(sequence)
            self.assertIs(iter(iterator), iterator)
            if expected:
                self.assertEqual(next(iterator), expected[0])
                self.assertEqual(list(iterator), expected[1:])
            else:
                self.assertEqual(list(iterator), [])
            self.assertRaises(StopIteration, next, iterator)
            self.assertEqual(list(reversed(sequence)), expected)

    def test_builtins_module_namespace_and_cache(self):
        module = type(sys)('private_builtins')
        module.parcel = 19
        module.len = lambda unused: 71
        scope = {'__builtins__': module, 'sys': sys}
        try:
            exec 'def sample():\n    return parcel\n' in scope
            sample = scope['sample']
            self.assertEqual(sample(), 19)
            module.parcel = 29
            self.assertEqual(sample(), 29)
            self.assertEqual(eval('len([])', scope), 71)
            self.assertIs(eval('sys._getframe().f_builtins', scope), module.__dict__)
            self.assertRaises(NameError, eval, 'True', scope)
            del module.parcel
            self.assertRaises(NameError, sample)
        finally:
            sample = None
            scope.clear()

    def test_builtins_invalid_namespace_is_minimal(self):
        for supplied in (None, 17, [], object()):
            scope = {'__builtins__': supplied, 'sys': sys}
            try:
                self.assertIs(eval('None', scope), None)
                self.assertRaises(NameError, eval, 'len([])', scope)
                self.assertRaises(NameError, eval, 'True', scope)
                actual = eval('sys._getframe().f_builtins', scope)
                self.assertEqual(actual, {'None': None})
                self.assertIs(scope['__builtins__'], supplied)
            finally:
                scope.clear()

    def test_builtins_removed_before_function_call(self):
        scope = {'__builtins__': {'parcel': 19}, 'sys': sys}
        try:
            exec 'def sample():\n    return parcel\ndef builtins():\n    return sys._getframe().f_builtins\n' in scope
            self.assertEqual(scope['sample'](), 19)
            del scope['__builtins__']
            self.assertRaises(NameError, scope['sample'])
            self.assertEqual(scope['builtins'](), {'None': None})
        finally:
            scope.clear()

    def test_eval_inherits_current_builtins(self):
        builtins = {'eval': eval, 'parcel': 37}
        inner = {}
        scope = {'__builtins__': builtins, 'inner': inner}
        try:
            exec 'def sample():\n    return eval("parcel", inner)\n' in scope
            self.assertEqual(scope['sample'](), 37)
            self.assertIs(inner['__builtins__'], builtins)
            builtins['parcel'] = 43
            self.assertEqual(scope['sample'](), 43)
        finally:
            scope.clear()
            inner.clear()

    def test_tuple_concatenation_subtypes_and_identity(self):
        class Parcel(tuple):
            pass
        for left in ((), (1,), (1, 2), Parcel(), Parcel([1, 2])):
            for right in ((), (3,), (3, 4), Parcel(), Parcel([3, 4])):
                result = left + right
                self.assertIs(type(result), tuple)
                self.assertEqual(result, tuple(list(left) + list(right)))
                if result:
                    self.assertIsNot(result, left)
                    self.assertIsNot(result, right)

    def test_long_float_powers(self):
        for exponent in (64, 65, 69, 70, 75, 80, 95, 127, 200, 500):
            value = 1L << exponent
            expected = 2.0 ** exponent
            self.assertEqual(float(value), expected)
            self.assertEqual(float(-value), -expected)
            self.assertEqual(value * 1.0, expected)
            self.assertEqual(value / 1.0, expected)

    def test_long_float_rounding(self):
        for exponent in (65, 75, 90, 100, 200):
            step = 1L << (exponent - 52)
            base = 1L << exponent
            expected = 2.0 ** exponent
            self.assertEqual(float(base + step // 2), expected)
            self.assertEqual(float(base + step // 2 + 1), expected + float(step))
            self.assertEqual(float(base + step + step // 2), expected + float(2 * step))

    def test_long_float_overflow(self):
        self.assertRaises(OverflowError, float, 1L << 1100)
        self.assertRaises(OverflowError, float, -(1L << 1100))

    def test_numeric_base_representation(self):
        class Count(int):
            def __format__(self, spec):
                return "custom"
        self.assertEqual(format(Count(19)), "custom")
        self.assertEqual(hex(Count(19)), "0x13")
        self.assertEqual(oct(Count(19)), "023")
        self.assertEqual(bin(Count(19)), "0b10011")

    def test_metaclass_base_call(self):
        class Meta(type):
            def __call__(cls, *args, **kwargs):
                result = type.__call__(cls, *args, **kwargs)
                result.from_meta = True
                return result
        class Record(object):
            __metaclass__ = Meta
            def __init__(self, value):
                self.value = value
        instance = Record(37)
        self.assertEqual(instance.value, 37)
        self.assertTrue(instance.from_meta)

    def test_classic_base_attribute_refresh(self):
        class Classic:
            value = 11
        class Record(Classic, object):
            pass
        record = Record()
        for unused in range(3):
            self.assertEqual(record.value, 11)
        Classic.value = 29
        for unused in range(3):
            self.assertEqual(record.value, 29)

    def test_new_style_finalizer_assignment(self):
        calls = []
        def release(instance):
            calls.append(instance.value)
        class Record(object):
            pass
        Record.__del__ = release
        record = Record()
        record.value = 17
        del record
        self.assertEqual(calls, [17])
        del Record.__del__

    def test_hasattr_interrupt(self):
        class Record(object):
            @property
            def value(self):
                raise KeyboardInterrupt("stop")
        self.assertRaises(KeyboardInterrupt, hasattr, Record(), "value")

    def test_hasattr_system_exit(self):
        class Record(object):
            @property
            def value(self):
                raise SystemExit(3)
        self.assertRaises(SystemExit, hasattr, Record(), "value")

    def test_hasattr_exception(self):
        class Record(object):
            @property
            def value(self):
                raise ValueError("missing")
        self.assertFalse(hasattr(Record(), "value"))

    def test_defaults_use_tail(self):
        def record(first, second):
            return first, second
        record.func_defaults = (3, 5, 7, 11)
        self.assertEqual(record(), (7, 11))
        self.assertEqual(record(13), (13, 11))

    def test_import_hook(self):
        calls = []
        module = object()
        def importer(name, globals, locals, fromlist, level=-1):
            calls.append((name, fromlist, level))
            return module
        scope = {"__builtins__": {"__import__": importer}}
        exec "import parcels" in scope
        self.assertIs(scope["parcels"], module)
        self.assertEqual(calls, [("parcels", None, -1)])

    def test_ord_bytearray(self):
        self.assertEqual(ord(bytearray("A")), 65)
        self.assertEqual(ord(bytearray("\xff")), 255)
        self.assertRaises(TypeError, ord, bytearray())
        self.assertRaises(TypeError, ord, bytearray("AB"))

    def test_dict_copy_preserves_cached_hashes(self):
        calls = []
        class Key(object):
            def __hash__(self):
                calls.append(1)
                return 31
        key = Key()
        original = {key: "parcel"}
        copied = original.copy()
        self.assertEqual(calls, [1])
        self.assertEqual(len(copied), 1)
        self.assertIs(copied.keys()[0], key)
        self.assertEqual(copied.values(), ["parcel"])

    def test_dict_copy_collision_keys(self):
        calls = []
        class Key(object):
            def __hash__(self):
                return 31
            def __eq__(self, other):
                calls.append(1)
                return self is other
        first, second = Key(), Key()
        original = {first: "one", second: "two"}
        del calls[:]
        copied = original.copy()
        self.assertEqual(calls, [1])
        self.assertEqual(len(copied), 2)
        self.assertEqual(sorted(copied.values()), ["one", "two"])

    def test_set_symmetric_update_preserves_cached_hashes(self):
        calls = []
        class Key(object):
            def __hash__(self):
                calls.append(1)
                return 31
        key = Key()
        other = set([key])
        target = set()
        del calls[:]
        target.symmetric_difference_update(other)
        self.assertEqual(calls, [])
        self.assertIs(list(target)[0], key)

    def test_descriptor_protocol_uses_type(self):
        class Descriptor(object):
            def __get__(self, instance, owner):
                return 23
        descriptor = Descriptor()
        descriptor.__get__ = lambda instance, owner: 99
        class Record(object):
            field = descriptor
        self.assertEqual(Record().field, 23)
        self.assertEqual(Record.field, 23)

    def test_classic_descriptor_failure(self):
        class Descriptor(object):
            def __get__(self, instance, owner):
                raise ValueError("descriptor")
        class Record:
            field = Descriptor()
            def __getattr__(self, name):
                return "fallback"
        self.assertRaises(ValueError, getattr, Record(), "field")

    def test_large_sequence_index(self):
        for sequence in ("abc", u"abc", (1, 2, 3), [1, 2, 3]):
            self.assertRaises(IndexError, lambda: sequence[1L << 90])
            self.assertRaises(IndexError, lambda: sequence[-(1L << 90)])

    def test_slice_limits(self):
        values = range(10)
        self.assertEqual(values[::-2], [9, 7, 5, 3, 1])
        self.assertEqual(values[-100:100:3], [0, 3, 6, 9])
        self.assertEqual(values[::-(1L << 90)], [9])
        self.assertEqual(values[1L << 90:], [])

    def test_partial_state(self):
        from _functools import partial
        def record(*args, **kwargs):
            return args, kwargs
        value = partial(record, 3, name="old")
        value.__setstate__((record, (7,), {"name": "new"}, {"label": "parcel"}))
        self.assertEqual(value(11), ((7, 11), {"name": "new"}))
        self.assertEqual(value.label, "parcel")

    def test_print_redirect_none(self):
        class Output(object):
            softspace = 0
            def __init__(self):
                self.pieces = []
            def write(self, piece):
                self.pieces.append(piece)
        previous = sys.stdout
        target = Output()
        try:
            sys.stdout = target
            print >>None, "parcel"
        finally:
            sys.stdout = previous
        self.assertEqual("".join(target.pieces), "parcel\n")

    def test_new_returns_subclass_initializer(self):
        calls = []
        class Parent(object):
            def __new__(cls):
                return object.__new__(cls.selected)
            def __init__(self):
                calls.append("parent")
        class Child(Parent):
            def __init__(self):
                calls.append("child")
        Parent.selected = Child
        try:
            self.assertIs(type(Parent()), Child)
            self.assertEqual(calls, ["child"])
        finally:
            del Parent.selected

    def test_container_representation(self):
        self.assertEqual(repr([1, (2, {"parcel": 3})]), "[1, (2, {'parcel': 3})]")
        self.assertEqual(repr(set([3])), "set([3])")

    def test_shared_code_global_cache(self):
        def read():
            return parcel
        first_scope = {"parcel": 11}
        second_scope = {"parcel": 23}
        first = type(read)(read.func_code, first_scope)
        second = type(read)(read.func_code, second_scope)
        for unused in range(4):
            self.assertEqual(first(), 11)
            self.assertEqual(second(), 23)
        first_scope["parcel"] = 37
        self.assertEqual(first(), 37)
        self.assertEqual(second(), 23)
        first_scope.clear()
        self.assertRaises(NameError, first)

    def test_shared_code_builtin_cache(self):
        def read():
            return parcel
        builtins = {"parcel": 11}
        scope = {"__builtins__": builtins}
        read = type(read)(read.func_code, scope)
        self.assertEqual(read(), 11)
        builtins["parcel"] = 23
        self.assertEqual(read(), 23)
        scope["parcel"] = 37
        self.assertEqual(read(), 37)
        del scope["parcel"]
        self.assertEqual(read(), 23)

    def test_exception_clear_in_called_function(self):
        def clear():
            sys.exc_clear()
        try:
            raise ValueError("parcel")
        except ValueError:
            clear()
            self.assertEqual(sys.exc_info(), (None, None, None))

    def test_float_sum(self):
        self.assertEqual(sum([0.25, 1, True, 0.5]), 2.75)
        self.assertEqual(sum([0.25, 0.5], 2.0), 2.75)
        self.assertEqual(sum([], -0.0), -0.0)
        class Number(object):
            def __radd__(self, value):
                return value + 3.0
        self.assertEqual(sum([0.25, Number(), 0.5]), 3.75)

    def test_struct_offsets(self):
        import _struct
        data = bytearray("--" + _struct.pack(">d", 3.5) + "--")
        self.assertEqual(_struct.unpack_from(">d", data, 2), (3.5,))
        _struct.pack_into(">d", data, 2, 7.25)
        self.assertEqual(_struct.unpack_from(">d", data, 2), (7.25,))

    def test_classic_instance_special_methods(self):
        class Record:
            pass
        record = Record()
        record.__call__ = lambda value: value + 1
        record.__iter__ = lambda: iter([3, 5])
        self.assertEqual(record(11), 12)
        self.assertEqual(list(record), [3, 5])

    def test_slot_delete(self):
        class Record(object):
            __slots__ = ("parcel",)
        record = Record()
        record.parcel = 11
        del record.parcel
        self.assertRaises(AttributeError, delattr, record, "parcel")

    def test_exception_custom_new(self):
        calls = []
        class Problem(Exception):
            def __new__(cls, *args):
                calls.append(args)
                return Exception.__new__(cls)
        problem = Problem("parcel")
        self.assertEqual(problem.args, ("parcel",))
        try:
            raise Problem, "delivery"
        except Problem as caught:
            self.assertEqual(caught.args, ("delivery",))
        self.assertEqual(calls, [("parcel",), ("delivery",)])

    def test_eval_generator_code(self):
        def parcels():
            yield 3
            yield 5
        result = eval(parcels.func_code)
        self.assertEqual(list(result), [3, 5])

    def test_numeric_constructor_overrides(self):
        class Count(int):
            def __int__(self):
                return 23
            def __long__(self):
                return 37L
            def __float__(self):
                return 11.5
            def __complex__(self):
                return 2j
        value = Count(5)
        self.assertEqual(int(value), 23)
        self.assertEqual(long(value), 37L)
        self.assertEqual(float(value), 11.5)
        self.assertEqual(complex(value), 2j)
        self.assertEqual(int.__int__(value), 5)

    def test_numeric_component_types(self):
        class Count(int):
            pass
        class Real(float):
            pass
        value = Count(5)
        self.assertIs(type(value.real), int)
        self.assertIs(type(value.numerator), int)
        self.assertIs(type(value.conjugate()), int)
        self.assertIs(type(Real(3.5).real), float)
        self.assertIs(type(int.__index__(True)), int)

    def test_reflected_numeric_order(self):
        class Count(int):
            def __radd__(self, other):
                return "reflected"
        self.assertEqual(1.0 + Count(2), 3.0)
        self.assertEqual(1 + Count(2), "reflected")

    def test_power_uses_numeric_storage(self):
        class Count(int):
            def __mul__(self, other):
                return 999
            def __mod__(self, other):
                return 999
        value = Count(3)
        self.assertEqual(value ** 2, 9)
        self.assertEqual(pow(value, 2, 5), 4)

    def test_codec_error_handlers(self):
        import _codecs
        error = UnicodeEncodeError("ascii", u"\xe9x", 0, 2, "parcel")
        self.assertEqual(_codecs.lookup_error("replace")(error), (u"??", 2))
        self.assertEqual(_codecs.lookup_error("xmlcharrefreplace")(error), (u"&#233;&#120;", 2))
        self.assertEqual(_codecs.lookup_error("backslashreplace")(error), (u"\\xe9\\x78", 2))
        self.assertEqual(_codecs.lookup_error("ignore")(error), (u"", 2))

    def test_expandtabs_unicode_length(self):
        self.assertEqual(u"\tA\t".expandtabs(0), u"A")
        self.assertEqual(len(u"\tA\t".expandtabs(0)), 1)
        self.assertEqual(len(u"\t".expandtabs(4)), 4)

    def test_float_fromhex(self):
        self.assertEqual(float.fromhex("10"), 16.0)
        self.assertEqual(float.fromhex("1.8"), 1.5)
        self.assertEqual(float.fromhex("a.p2"), 40.0)
        self.assertEqual(float.fromhex(" -0X1.8p+2 "), -6.0)
        self.assertEqual(float.fromhex("inf"), float("inf"))
        self.assertRaises(ValueError, float.fromhex, "1.8p")

    def test_type_winning_metaclass(self):
        calls = []
        class Meta(type):
            def __new__(cls, name, bases, namespace):
                calls.append(name)
                return type.__new__(cls, name, bases, namespace)
        class Base(object):
            __metaclass__ = Meta
        calls[:] = []
        derived = type('Derived', (Base,), {})
        self.assertEqual(calls, ['Derived'])
        self.assertIs(type(derived), Meta)
        self.assertEqual(derived.__module__, __name__)
        self.assertIs(derived.__doc__, None)
        namespace = {'__module__': 'custom', '__doc__': 'description'}
        record = type('Record', (), namespace)
        self.assertEqual(record.__module__, 'custom')
        self.assertEqual(record.__doc__, 'description')
        self.assertEqual(namespace, {'__module__': 'custom', '__doc__': 'description'})

    def test_classic_class_metadata(self):
        class Left:
            value = 11
        class Right:
            value = 29
        class Record(Left):
            pass
        record = Record()
        Record.__bases__ = (Right,)
        self.assertEqual(record.value, 29)
        Record.__name__ = 'Renamed'
        self.assertEqual(Record.__name__, 'Renamed')
        self.assertTrue(str(Record).endswith('.Renamed'))
        namespace = {'value': 37, '__module__': 'custom'}
        Record.__dict__ = namespace
        self.assertIs(Record.__dict__, namespace)
        self.assertEqual(record.value, 37)
        self.assertEqual(str(Record), 'custom.Renamed')
        self.assertRaises(TypeError, setattr, Record, '__bases__', (object,))
        self.assertRaises(TypeError, setattr, Record, '__name__', 5)
        self.assertRaises(TypeError, setattr, Record, '__dict__', [])

    def test_slot_namespace_conflict(self):
        for namespace in ({'__slots__': ('value',), 'value': 1}, {'__slots__': ('__value',), '_Record__value': 1}):
            record_type = type('Record', (), namespace)
            instance = record_type()
            attribute = 'value' if 'value' in namespace else '_Record__value'
            self.assertEqual(getattr(instance, attribute), 1)
            self.assertRaises(AttributeError, setattr, instance, attribute, 29)

    def test_generator_throw_tuple(self):
        def new_generator():
            try:
                yield None
            except ValueError as error:
                yield error.args
        stream = new_generator()
        next(stream)
        self.assertEqual(stream.throw(ValueError, (11, 29)), (11, 29))
        stream.close()
        class Classic:
            def __init__(self, *args):
                self.args = args
        def old_generator():
            try:
                yield None
            except Classic as error:
                yield error.args
        stream = old_generator()
        next(stream)
        self.assertEqual(stream.throw(Classic, (11, 29)), (11, 29))
        stream.close()

    def test_print_unicode_softspace(self):
        class Output:
            def __init__(self):
                self.parts = []
            def write(self, value):
                self.parts.append(value)
        output = Output()
        print >>output, u'a\u2003',
        self.assertIs(type(output.softspace), int)
        self.assertEqual(output.softspace, 0)
        print >>output, 'b',
        self.assertEqual(output.softspace, 1)
        print >>output
        self.assertEqual(u''.join(output.parts), u'a\u2003b\n')
        self.assertEqual(output.softspace, 0)

    def test_text_conversion_edges(self):
        class Text(object):
            def __str__(self):
                return u'caf\xe9'
        self.assertEqual('%s' % Text(), u'caf\xe9')
        self.assertIs(type('%s' % Text()), unicode)
        self.assertEqual('%5%' % (), '    %')
        self.assertEqual('%-5%' % (), '%    ')
        self.assertEqual('abc'.find('a', 10**30), -1)
        self.assertEqual(format(0.1, '.0'), '0.1')
        self.assertRaises(ValueError, float.fromhex, 'nan(payload)')
        self.assertRaises(ValueError, float.fromhex, 'infinite')
        self.assertRaises(ValueError, pow, 1j, 2, 3)

    def test_translate_buffer_table(self):
        table = ''.join(chr(value) for value in range(256))
        self.assertEqual('abc'.translate(bytearray(table)), 'abc')
        self.assertEqual('abc'.translate(buffer(table)), 'abc')
        self.assertEqual('abc'.translate(None, bytearray('b')), 'ac')

    def test_codec_error_range(self):
        try:
            u'\xe9\xe9x'.encode('ascii')
        except UnicodeEncodeError as error:
            self.assertEqual((error.start, error.end), (0, 2))
        else:
            self.fail('ASCII encoding must reject non-ASCII text')
        self.assertEqual(u'\xe9\xe9x'.encode('ascii', 'replace'), '??x')
        self.assertEqual(str(EnvironmentError(None, 'x')), '[Errno None] x')

    def test_enumerate_long_start(self):
        for start in (sys.maxint - 1, sys.maxint, sys.maxint + 1, 1L << 80):
            self.assertEqual(list(enumerate(['a', 'b'], start)), [(start, 'a'), (start + 1, 'b')])

    def test_range_long_bounds(self):
        start = 1L << 80
        self.assertEqual(range(start, start + 3), [start, start + 1, start + 2])
        self.assertEqual(range(start, start - 6, -2), [start, start - 2, start - 4])
        self.assertEqual(range(start, start - 1), [])
        self.assertEqual(range(0, 2 * start, start), [0, start])
        self.assertEqual([type(value) for value in range(1L, 3L)], [int, int])
        self.assertRaises(ValueError, range, start, start + 1, 0)
        class Index(object):
            def __index__(self):
                return 3
        self.assertRaises(TypeError, range, Index())
        class Integer(object):
            def __int__(self):
                return 3
        self.assertEqual(range(Integer()), [0, 1, 2])

    def test_exception_metaclass_match(self):
        calls = []
        class Meta(type):
            def __subclasscheck__(cls, other):
                calls.append(other)
                return True
        class Match(Exception):
            __metaclass__ = Meta
        try:
            raise ValueError('parcel')
        except Match as error:
            self.assertIs(type(error), ValueError)
            self.assertEqual(str(error), 'parcel')
        else:
            self.fail('metaclass exception match was ignored')
        self.assertEqual(calls, [ValueError])

    def test_property_subclass_doc(self):
        class Field(property):
            pass
        def getter(instance):
            'getter doc'
            return 29
        field = Field(getter)
        self.assertEqual(field.__doc__, 'getter doc')
        field = field.setter(lambda instance, value: None)
        self.assertEqual(field.__doc__, 'getter doc')
        self.assertIs(type(field), Field)
        self.assertIs(Field(getter, doc='explicit').__doc__, None)

    def test_module_replaces_registration(self):
        from local import review_replaced_module
        self.assertEqual(review_replaced_module, 42)
        self.assertEqual(sys.modules['local.review_replaced_module'], 42)

    def test_module_removes_registration(self):
        self.assertRaises(ImportError, __import__, 'local.review_deleted_module')
        self.assertFalse('local.review_deleted_module' in sys.modules)


    def test_intern_dynamic_strings(self):
        first = ''.join(['parcel', '_', 'dynamic'])
        second = ''.join(['parcel', '_', 'dynamic'])
        self.assertIsNot(first, second)
        self.assertIs(intern(first), intern(second))
        del first, second
        for index in range(300):
            text = 'parcel_%d' % index
            self.assertIs(intern(text), intern('parcel_%d' % index))

    def test_intern_compiler_names(self):
        code = compile('def parcel_receiver(parcel_argument): return parcel_argument', '<intern>', 'exec')
        self.assertIs(code.co_names[0], intern(''.join(['parcel', '_receiver'])))
        function = code.co_consts[0]
        self.assertIs(function.co_varnames[0], intern(''.join(['parcel', '_argument'])))
        constant = compile("'parcel_constant'", '<intern>', 'eval').co_consts[0]
        self.assertIs(constant, intern(''.join(['parcel', '_constant'])))

    def test_custom_mro_reordering(self):
        class Meta(type):
            def mro(cls):
                result = type.mro(cls)
                if cls.__name__ == 'Parcel':
                    result[1], result[2] = result[2], result[1]
                return result
        class Left(object):
            value = 'left'
        class Right(object):
            value = 'right'
        class Parcel(Left, Right):
            __metaclass__ = Meta
        self.assertEqual(Parcel.__mro__, (Parcel, Right, Left, object))
        self.assertEqual(type.mro(Parcel), [Parcel, Left, Right, object])
        self.assertEqual(Parcel().value, 'right')
        Right.value = 'updated'
        self.assertEqual(Parcel().value, 'updated')

    def test_custom_mro_extra_class(self):
        class Extra(object):
            value = 41
        class Meta(type):
            def mro(cls):
                return [cls, Extra, object]
        class Parcel(object):
            __metaclass__ = Meta
        self.assertEqual(Parcel().value, 41)
        self.assertTrue(isinstance(Parcel(), Extra))
        self.assertEqual(Parcel.__bases__, (object,))

    def test_custom_mro_invalid_entries(self):
        class Meta(type):
            def mro(cls):
                return [cls, 7, object]
        self.assertRaises(TypeError, Meta, 'Parcel', (object,), {})
        class Other(type):
            def mro(cls):
                return [cls, list, object]
        self.assertRaises(TypeError, Other, 'Parcel', (object,), {})

    def test_custom_mro_rebase(self):
        calls = []
        class Meta(type):
            def mro(cls):
                calls.append(cls.__name__)
                return type.mro(cls)
        class Left(object):
            value = 11
        class Right(object):
            value = 19
        class Parcel(Left):
            __metaclass__ = Meta
        class Child(Parcel):
            pass
        del calls[:]
        Parcel.__bases__ = (Right,)
        self.assertEqual(calls, ['Parcel', 'Child'])
        self.assertEqual(Child().value, 19)
        self.assertEqual(Parcel.__mro__, (Parcel, Right, object))

    def test_custom_mro_rebase_rollback(self):
        class Left(object):
            value = 11
        class Right(object):
            value = 19
        class Meta(type):
            def mro(cls):
                if cls.__name__ == 'Child' and cls.__bases__[0].__bases__ == (Right,):
                    raise ValueError('blocked')
                return type.mro(cls)
        class Parcel(Left):
            __metaclass__ = Meta
        class Child(Parcel):
            pass
        self.assertRaises(ValueError, setattr, Parcel, '__bases__', (Right,))
        self.assertEqual(Parcel.__bases__, (Left,))
        self.assertEqual(Child().value, 11)


    def test_float_fromhex_rounding(self):
        cases = [
            ('0x1.00000000000008p0', 1.0),
            ('0x1.000000000000080001p0', float.fromhex('0x1.0000000000001p0')),
            ('0x1.00000000000018p0', float.fromhex('0x1.0000000000002p0')),
            ('0x1p-1075', 0.0),
            ('0x1.00000000000001p-1075', float.fromhex('0x1p-1074')),
            ('0x3p-1075', float.fromhex('0x1p-1073')),
            ('0x1.fffffffffffffp1023', float.fromhex('0x1.fffffffffffffp1023')),
            ('-0x0p999999999999999999', -0.0),
            ('0x1p-999999999999999999', 0.0),
        ]
        for text, expected in cases:
            self.assertEqual(float.fromhex(text).hex(), expected.hex())
        self.assertRaises(OverflowError, float.fromhex, '0x1.fffffffffffff8p1023')
        for exponent in [-1074, -1073, -1022, -1021, -53, -1, 0, 1, 52, 100, 1022]:
            for coefficient in [1.0, 1.25, 1.5, 1.75, 1.9999999999999998]:
                text = coefficient.hex().split('p')[0] + 'p%d' % exponent
                value = float.fromhex(text)
                self.assertEqual(float.fromhex(value.hex()), value)

    def test_cmp_nan_fallback(self):
        first, second = float('nan'), float('nan')
        self.assertEqual(cmp(first, first), 0)
        self.assertEqual(abs(cmp(first, second)), 1)
        self.assertEqual(cmp(first, second), -cmp(second, first))
        self.assertEqual(cmp(first, second), cmp(id(first), id(second)))
        for other in [1, 1L, 1.0, None, 'parcel']:
            self.assertEqual(abs(cmp(first, other)), 1)
            self.assertEqual(cmp(first, other), -cmp(other, first))
            if type(first) is type(other):
                expected = cmp(id(first), id(other))
            elif isinstance(other, (int, long)):
                # Numbers of different types order like CPython's static
                # type objects, where float precedes int and long.
                expected = -1
            else:
                continue
            self.assertEqual(cmp(first, other), expected)
        self.assertFalse(first < second)
        self.assertFalse(first > second)
        self.assertFalse(first == second)

    def test_list_index_error_uses_needle_repr(self):
        class Needle(object):
            def __repr__(self):
                return '<needle>'
        try:
            [].index(Needle())
        except ValueError as error:
            self.assertEqual(str(error), '<needle> is not in list')
        else:
            self.fail('list.index accepted a missing value')

        class BrokenNeedle(object):
            def __repr__(self):
                raise RuntimeError('broken repr')
        try:
            [].index(BrokenNeedle())
        except RuntimeError as error:
            self.assertEqual(str(error), 'broken repr')
        else:
            self.fail('list.index suppressed a repr failure')

    def test_slice_and_iterator_diagnostics(self):
        for arguments, message in [
                ((), 'slice expected at least 1 arguments, got 0'),
                ((1, 2, 3, 4), 'slice expected at most 3 arguments, got 4')]:
            try:
                slice(*arguments)
            except TypeError as error:
                self.assertEqual(str(error), message)
            else:
                self.fail('slice accepted an invalid argument count')

        class MissingNext(object):
            def __iter__(self):
                return self
        try:
            iter(MissingNext())
        except TypeError as error:
            self.assertEqual(str(error), "iter() returned non-iterator of type 'MissingNext'")
        else:
            self.fail('iter accepted an object without next')

        try:
            slice(None).indices(1L << 100)
        except OverflowError as error:
            self.assertEqual(str(error), "cannot fit 'long' into an index-sized integer")
        else:
            self.fail('slice.indices accepted an oversized length')

    def test_numeric_default_classification(self):
        class Zeta(object):
            def __int__(self):
                return 1
        class Omega(object):
            def __float__(self):
                return 1.0
        class Alpha(object):
            pass
        for numeric in [Zeta(), Omega()]:
            self.assertEqual(cmp(numeric, Alpha()), -1)
            self.assertEqual(cmp(Alpha(), numeric), 1)
            self.assertTrue(numeric < Alpha())
            self.assertTrue(Alpha() > numeric)

    def test_slice_negative_length(self):
        cases = [
            (slice(None), (-5, -5, 1)),
            (slice(1, 4), (-5, -5, 1)),
            (slice(-3, None), (0, -5, 1)),
            (slice(None, None, -1), (-6, sys.maxsize - 4, -1)),
            (slice(None, 1, -2), (-6, -6, -2)),
        ]
        for selection, expected in cases:
            self.assertEqual(selection.indices(-5), expected)

    def test_unicode_repr_contexts(self):
        class Parcel(object):
            def __repr__(self):
                return u'parcel'
        self.assertEqual(repr(Parcel()), 'parcel')
        self.assertIs(type(repr(Parcel())), str)
        self.assertEqual('%r' % Parcel(), 'parcel')
        self.assertEqual(u'%r' % Parcel(), u'parcel')
        class Foreign(object):
            def __repr__(self):
                return u'\u20ac'
        self.assertRaises(UnicodeEncodeError, repr, Foreign())
        self.assertRaises(UnicodeEncodeError, lambda value: '%r' % value, Foreign())
        self.assertRaises(UnicodeEncodeError, lambda value: u'%r' % value, Foreign())

    def test_finalizer_unraisable(self):
        class Sink(object):
            def __init__(self):
                self.parts = []
            def write(self, text):
                self.parts.append(text)
        class Parcel(object):
            def __del__(self):
                raise ValueError('release parcel')
        sink, previous = Sink(), sys.stderr
        sys.stderr = sink
        try:
            try:
                raise KeyError('pending parcel')
            except KeyError:
                parcel = Parcel()
                del parcel
                self.assertIs(sys.exc_info()[0], KeyError)
        finally:
            sys.stderr = previous
        diagnostic = ''.join(sink.parts)
        self.assertTrue(diagnostic.startswith('Exception ValueError:'))
        self.assertTrue('release parcel' in diagnostic)
        self.assertTrue(diagnostic.endswith(' ignored\n'))

    def test_weakref_unraisable(self):
        import _weakref as weakref
        class Sink(object):
            def __init__(self):
                self.parts = []
            def write(self, text):
                self.parts.append(text)
        class Parcel(object):
            pass
        def callback(reference):
            raise ValueError('weak parcel')
        sink, previous = Sink(), sys.stderr
        sys.stderr = sink
        try:
            parcel = Parcel()
            reference = weakref.ref(parcel, callback)
            del parcel
            self.assertIs(reference(), None)
        finally:
            sys.stderr = previous
        diagnostic = ''.join(sink.parts)
        self.assertTrue(diagnostic.startswith('Exception ValueError:'))
        self.assertTrue('weak parcel' in diagnostic)
        self.assertTrue(diagnostic.endswith(' ignored\n'))

    def test_tuple_subtype_factory(self):
        class Parcel(tuple):
            pass
        for source in [(), (1, 2), [1, 2], iter([1, 2])]:
            result = Parcel(source)
            self.assertIs(type(result), Parcel)
            self.assertEqual(result, tuple(result))
        self.assertEqual(filter(None, Parcel([0, 1, 2])), (1, 2))


    def test_item_view_equality_operand_order(self):
        calls = []
        class Stored(object):
            def __eq__(self, other):
                calls.append("stored")
                return True
        class Candidate(object):
            def __eq__(self, other):
                calls.append("candidate")
                return False
        self.assertFalse(("key", Candidate()) in {"key": Stored()}.viewitems())
        self.assertEqual(calls, ["candidate"])

    def test_sort_partial_result_on_comparison_failure(self):
        expectations = ((3, [2, 5, 4, 1, 3, 0]),
                        (5, [2, 4, 5, 1, 3, 0]),
                        (8, [1, 2, 4, 5, 3, 0]))
        for stop, expected in expectations:
            values = [5, 2, 4, 1, 3, 0]
            calls = [0]
            def compare(first, second):
                calls[0] += 1
                if calls[0] == stop:
                    raise ValueError("stop")
                return cmp(first, second)
            self.assertRaises(ValueError, values.sort, cmp=compare)
            self.assertEqual(values, expected)

    def test_sort_structured_runs_and_stability(self):
        for size in (31, 32, 63, 64, 65, 127, 257, 511):
            values = [(index * 37 % 23, index) for index in range(size)]
            expected = [(key, index) for key in range(23)
                        for index in range(size) if index * 37 % 23 == key]
            values.sort(key=lambda value: value[0])
            self.assertEqual(values, expected)
            values.sort(key=lambda value: value[0], reverse=True)
            reverse_expected = [(key, index) for key in range(22, -1, -1)
                                for index in range(size) if index * 37 % 23 == key]
            self.assertEqual(values, reverse_expected)
        values = range(512, 768) + range(512) + range(768, 1024)
        values.sort()
        self.assertEqual(values, range(1024))

    def test_sort_failure_preserves_elements(self):
        original = [(index * 71) % 257 for index in range(257)]
        for stop in (100, 300, 650, 900):
            values = original[:]
            calls = [0]
            def compare(first, second):
                calls[0] += 1
                if calls[0] == stop:
                    raise ValueError("stop")
                return cmp(first, second)
            try:
                values.sort(cmp=compare)
            except ValueError:
                pass
            self.assertEqual(sorted(values), range(257))

    def test_integer_keyword_arguments(self):
        for constructor in (int, long):
            self.assertEqual(constructor(x="101", base=2), 5)
            self.assertEqual(constructor(x=7), 7)
            self.assertEqual(constructor("11", base=2), 3)
            self.assertRaises(TypeError, constructor, "11", x="10")
            self.assertRaises(TypeError, constructor, "11", 2, base=3)
            self.assertRaises(TypeError, constructor, base=2)
            self.assertRaises(TypeError, constructor, unknown=1)

    def test_percent_mapping_and_positional_arguments(self):
        self.assertRaises(TypeError, lambda: "%(a)s %s" % {"a": 1})
        self.assertEqual("%s %(a)s" % {"a": 1}, "{'a': 1} 1")
        self.assertEqual("%(a)s %(a)s" % {"a": 1}, "1 1")

    def test_eval_null_source_error(self):
        for source in ("1\0+2", u"1\0+2"):
            with self.assertRaises(TypeError) as caught:
                eval(source)
            self.assertEqual(str(caught.exception), "expected string without null bytes")

    def test_import_ignored_non_dict_globals(self):
        self.assertIs(__import__("sys", 5), sys)
        self.assertIs(__import__("sys", 5, {}, [], 0), sys)

    def test_import_empty_name_components(self):
        self.assertIs(__import__("sys."), sys)
        self.assertIs(__import__("sys.", {}, {}, ["path"], 0), sys)
        self.assertRaises(ValueError, __import__, "sys..path")
        self.assertRaises(ValueError, __import__, ".sys")

    def test_import_inferred_package_metadata(self):
        scope = {"__name__": "sys.child"}
        self.assertIs(__import__("sys", scope), sys)
        self.assertEqual(scope["__package__"], "sys")
        scope = {"__name__": "standalone"}
        self.assertIs(__import__("sys", scope), sys)
        self.assertIs(scope["__package__"], None)

    def test_generator_eval_uses_globals(self):
        def sample():
            yield marker
        generator = eval(sample.func_code, {"marker": 19}, {"marker": 29})
        self.assertEqual(next(generator), 19)
        self.assertRaises(StopIteration, next, generator)

    def test_escaping_frame_not_reused(self):
        def sample(value):
            return sys._getframe()
        first = sample(19)
        second = sample(29)
        self.assertIsNot(first, second)
        self.assertIs(first.f_code, sample.func_code)
        self.assertIs(second.f_code, sample.func_code)
        self.assertEqual(first.f_lasti, second.f_lasti)
        del first, second

    def test_nested_handled_exception_state(self):
        def plain():
            return sys.exc_info()[0]
        def catches():
            try:
                raise ValueError("inner")
            except ValueError:
                self.assertIs(plain(), ValueError)
            return sys.exc_info()[0]
        try:
            raise KeyError("outer")
        except KeyError:
            self.assertIs(plain(), KeyError)
            self.assertIs(catches(), ValueError)
            self.assertIs(sys.exc_info()[0], KeyError)

    def test_nested_finally_with_control_flow(self):
        events = []
        class Context(object):
            def __enter__(self):
                events.append("enter")
            def __exit__(self, kind, value, traceback):
                events.append("exit")
        def sample():
            for index in range(3):
                try:
                    with Context():
                        if index == 0:
                            continue
                        if index == 1:
                            break
                finally:
                    events.append(index)
            try:
                return 37
            finally:
                with Context():
                    events.append("return")
        self.assertEqual(sample(), 37)
        self.assertEqual(events, ["enter", "exit", 0, "enter", "exit", 1,
                                  "enter", "return", "exit"])

    def test_unicode_repr_nested_contexts(self):
        class Text(object):
            def __repr__(self):
                return u"ascii"
        value = Text()
        self.assertEqual(repr([value]), "[ascii]")
        self.assertEqual(repr((value,)), "(ascii,)")
        self.assertEqual(repr({"key": value}), "{'key': ascii}")
        self.assertEqual("%r" % value, "ascii")
        self.assertEqual(u"%r" % value, u"ascii")

    def test_long_multiplication_shapes(self):
        for bits in (16, 63, 129, 1100, 2200):
            value = (1L << bits) + 37
            for multiplier in (-32767, -3, 0, 1, 7, 32767):
                product = value * multiplier
                self.assertEqual(product, multiplier * value)
                if multiplier:
                    self.assertEqual(product // multiplier, value)
            other = (1L << (bits // 2)) + 19
            self.assertEqual(value * other // other, value)

    def test_source_bom_cookie_alias(self):
        self.assertRaises(SyntaxError, compile, "\xef\xbb\xbf# coding: utf8\nvalue = 1\n", "alias.py", "exec")
        for encoding in ("utf-8", "utf_8", "UTF-8", "utf-8-sig"):
            code = compile("\xef\xbb\xbf# coding: " + encoding + "\nvalue = 1\n", "alias.py", "exec")
            scope = {}
            exec code in scope
            self.assertEqual(scope["value"], 1)

    def test_custom_mro_introspection(self):
        events = []
        class Meta(type):
            def mro(cls):
                events.append(cls.__mro__)
                return iter(type.mro(cls))
        class Parcel(object):
            __metaclass__ = Meta
        self.assertEqual(events, [None])
        self.assertEqual(Parcel.__mro__, (Parcel, object))

    def test_custom_mro_retains_extra_class(self):
        import _weakref
        class Extra(object):
            value = 43
        holder = [Extra]
        reference = _weakref.ref(Extra)
        class Meta(type):
            def mro(cls):
                return [cls, holder[0], object]
        class Parcel(object):
            __metaclass__ = Meta
        Extra = None
        holder[:] = []
        self.assertIsNot(reference(), None)
        self.assertEqual(Parcel().value, 43)
        Parcel = None

    def test_failed_exception_subclass_check_is_unraisable(self):
        class Sink(object):
            def __init__(self):
                self.parts = []
            def write(self, text):
                self.parts.append(text)
        class Meta(type):
            def __subclasscheck__(cls, other):
                raise ValueError("matching failed")
        class Match(Exception):
            __metaclass__ = Meta
        sink = Sink()
        previous = sys.stderr
        sys.stderr = sink
        try:
            try:
                raise KeyError("original")
            except Match:
                self.fail("failed subclass hook matched")
            except KeyError as error:
                self.assertEqual(error.args, ("original",))
        finally:
            sys.stderr = previous
        diagnostic = "".join(sink.parts)
        self.assertIn("ValueError", diagnostic)
        self.assertIn("matching failed", diagnostic)
        self.assertIn("ignored", diagnostic)

    def test_star_import_string_all(self):
        name = "local_review_all"
        module = type(sys)(name)
        module.__all__ = "ab"
        module.a = 19
        module.b = 29
        sys.modules[name] = module
        try:
            scope = {}
            exec "from local_review_all import *" in scope
            self.assertEqual((scope["a"], scope["b"]), (19, 29))
        finally:
            del sys.modules[name]


if __name__ == "__main__":
    unittest.main()
