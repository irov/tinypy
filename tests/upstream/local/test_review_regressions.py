"""Project-authored functional checks for the October runtime review."""

import sys
import unittest


class ReviewRegressions(unittest.TestCase):
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
        start = 1L << 80
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


if __name__ == "__main__":
    unittest.main()
