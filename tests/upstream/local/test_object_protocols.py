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

    def assert_message(self, exception, message, function, *args):
        try:
            function(*args)
        except exception as error:
            self.assertEqual(str(error), message)
        else:
            self.fail(message)

    def test_layout_conflict_with_builtin_base(self):
        class Error(Exception):
            pass

        class Slotted(object):
            __slots__ = ('a',)

        def make_class(bases):
            class Made(bases[0], bases[1]):
                pass
            return Made

        conflict = 'multiple bases have instance lay-out conflict'
        for base in (int, tuple, str, list, dict, float, long, unicode, set):
            self.assert_message(TypeError, 'Error when calling the metaclass bases\n    ' + conflict, make_class, (Error, base))
            self.assert_message(TypeError, conflict, type, 'Made', (base, Error), {})
        self.assert_message(TypeError, conflict, type, 'Made', (Exception, Slotted), {})
        self.assert_message(TypeError, conflict, type, 'Made', (UnicodeEncodeError, UnicodeDecodeError), {})
        self.assert_message(TypeError, conflict, type, 'Made', (SystemExit, EnvironmentError), {})
        for bases in ((Error, object), (ValueError, KeyError), (IOError, OSError), (UnicodeEncodeError, ValueError), (SystemExit, ValueError), (Slotted, Error)):
            if bases[0] is Slotted:
                self.assert_message(TypeError, conflict, type, 'Made', bases, {})
            else:
                self.assertIs(type('Made', bases, {}).__mro__[1], bases[0])

    def test_uncreatable_builtin_types(self):
        def generator():
            yield 1

        def closure():
            value = 1
            def inner():
                return value
            return inner

        try:
            raise ValueError
        except ValueError:
            traceback = sys.exc_info()[2]
        for value in (generator(), None, Ellipsis, NotImplemented, iter([]), closure().func_closure[0], sys._getframe(), traceback):
            kind = type(value)
            unsafe = 'object.__new__(%s) is not safe, use %s.__new__()' % (kind.__name__, kind.__name__)
            self.assert_message(TypeError, "cannot create '%s' instances" % kind.__name__, kind)
            self.assert_message(TypeError, unsafe, kind.__new__, kind)
            self.assert_message(TypeError, unsafe, object.__new__, kind)
        dictproxy = type(type.__dict__)
        self.assert_message(TypeError, "cannot create 'dictproxy' instances", dictproxy)
        self.assert_message(TypeError, 'object.__new__(dictproxy) is not safe, use dictproxy.__new__()', object.__new__, dictproxy)

    def test_recursion_limit_below_depth(self):
        limit = sys.getrecursionlimit()
        self.assertEqual([lowered_recursion_limit(value) for value in (3, 2, 1)], [3, 2, 1])
        self.assertEqual(bounded_recursion(limit, 10), 10)
        self.assertEqual(bounded_recursion(100, 2000), 'maximum recursion depth exceeded')
        self.assertEqual(sys.getrecursionlimit(), limit)

    def test_exception_members(self):
        self.assertEqual(SystemExit(1).__dict__, {})
        self.assertEqual(EnvironmentError(1, 'a', 'f').__dict__, {})
        self.assertEqual(SyntaxError('m', ('f', 1, 2, 'l')).__dict__, {})
        self.assertEqual(EnvironmentError(1, 'a', 'f').__reduce__(), (EnvironmentError, (1, 'a', 'f')))
        self.assertEqual(IOError(1, 'a').__reduce__(), (IOError, (1, 'a')))
        self.assertEqual(SystemExit(1).__reduce__(), (SystemExit, (1,)))
        self.assertEqual(SyntaxError('m', ('f', 1, 2, 'l')).__reduce__(), (SyntaxError, ('m', ('f', 1, 2, 'l'))))
        error = SystemExit(1)
        error.code = 'x'
        self.assertEqual((error.__dict__, error.code, error.args), ({}, 'x', (1,)))
        del error.code
        self.assertIs(error.code, None)
        error.extra = 2
        self.assertEqual(error.__reduce__(), (SystemExit, (1,), {'extra': 2}))
        self.assertEqual((EnvironmentError().errno, EnvironmentError().filename, str(EnvironmentError())), (None, None, ''))
        self.assertEqual(str(EnvironmentError(None, None)), '[Errno None] None')
        self.assertEqual((SyntaxError().msg, str(SyntaxError()), SyntaxError('m', ('f.py', 3, 1, 'x')).lineno), (None, 'None', 3))
        try:
            compile('x = (', 'parcel.py', 'exec')
        except SyntaxError as error:
            self.assertEqual((error.filename, error.lineno, error.__dict__), ('parcel.py', 1, {}))
        self.assert_message(TypeError, 'state is not a dictionary', ValueError().__setstate__, 1)

        class Returning(Exception):
            def __init__(self):
                return 1

        self.assert_message(TypeError, "__init__() should return None, not 'int'", Returning)
        self.assertEqual(sorted(vars(ValueError)), ['__doc__', '__init__', '__new__'])
        self.assertEqual(sorted(vars(SystemExit)), ['__doc__', '__init__', '__new__', 'code'])
        self.assertEqual(sorted(vars(KeyError)), ['__doc__', '__init__', '__new__', '__str__'])
        self.assertEqual(sorted(vars(EnvironmentError)), ['__doc__', '__init__', '__new__', '__reduce__', '__str__', 'errno', 'filename', 'strerror'])
        self.assertEqual(sorted(vars(SyntaxError)), ['__doc__', '__init__', '__new__', '__str__', 'filename', 'lineno', 'msg', 'offset', 'print_file_and_line', 'text'])
        self.assertEqual(sorted(vars(UnicodeDecodeError)), ['__doc__', '__init__', '__new__', '__str__', 'encoding', 'end', 'object', 'reason', 'start'])
        self.assertEqual(sorted(vars(BaseException)), ['__delattr__', '__dict__', '__doc__', '__getattribute__', '__getitem__', '__getslice__', '__init__', '__new__', '__reduce__', '__repr__', '__setattr__', '__setstate__', '__str__', '__unicode__', 'args', 'message'])
        for kind in (list, int, str, dict, type, slice, type(iter([])), type(type.__dict__)):
            self.assertIn('__getattribute__', vars(kind))
        self.assertNotIn('__getattribute__', vars(bool))

    def test_system_exit_arguments(self):
        def exit_code(*args):
            try:
                sys.exit(*args)
            except SystemExit as error:
                return (error.code, error.args)

        self.assertEqual(exit_code(None), (None, ()))
        self.assertEqual(exit_code(), (None, ()))
        self.assertEqual(exit_code(1), (1, (1,)))
        self.assertEqual(exit_code((1, 2)), ((1, 2), (1, 2)))
        self.assertEqual(exit_code([1]), ([1], ([1],)))

    def test_unicode_of_environment_error(self):
        accent = u'\xe9'
        self.assertEqual(unicode(EnvironmentError(accent, accent, accent)), u"[Errno \xe9] \xe9: u'\\xe9'")
        self.assertEqual(unicode(IOError(1, accent)), u'[Errno 1] \xe9')
        self.assertEqual(u'%s' % EnvironmentError(accent, accent, accent), u"[Errno \xe9] \xe9: u'\\xe9'")
        self.assertEqual(unicode(EnvironmentError(1, 'a', accent)), u"[Errno 1] a: u'\\xe9'")
        self.assert_message(UnicodeEncodeError, "'ascii' codec can't encode character u'\\xe9' in position 7: ordinal not in range(128)", str, EnvironmentError(accent, accent, accent))

    def test_metaclass_bases_prefix(self):
        prefix = 'Error when calling the metaclass bases\n    '

        class Old:
            pass

        def classic_metaclass():
            class Made:
                __metaclass__ = Old

        def builtin_metaclass():
            class Made(object):
                __metaclass__ = len

        def python_new():
            class Meta(type):
                def __new__(meta, name, bases, namespace):
                    return type.__new__(meta, name, bases, namespace, 1)

            class Made(object):
                __metaclass__ = Meta

        def python_raise():
            class Meta(type):
                def __new__(meta, name, bases, namespace):
                    raise TypeError('from python')

            class Made(object):
                __metaclass__ = Meta

        class PropertyBase:
            __class__ = property(lambda self: int)

        def property_metaclass():
            class Made(PropertyBase):
                pass

        class TypeBase:
            __class__ = type

        def type_metaclass():
            class Made(TypeBase):
                pass

        self.assert_message(TypeError, prefix + 'this constructor takes no arguments', classic_metaclass)
        self.assert_message(TypeError, prefix + 'len() takes exactly one argument (3 given)', builtin_metaclass)
        self.assert_message(TypeError, prefix + 'type() takes 1 or 3 arguments', python_new)
        self.assert_message(TypeError, 'from python', python_raise)
        self.assert_message(TypeError, prefix + "'property' object is not callable", property_metaclass)
        self.assert_message(TypeError, prefix + "a new-style class can't have only classic bases", type_metaclass)
        conflict = 'metaclass conflict: the metaclass of a derived class must be a (non-strict) subclass of the metaclasses of all its bases'
        self.assert_message(TypeError, conflict, type, 'Made', (1,), {})
        self.assert_message(TypeError, 'bases must be types', type, 'Made', (object(),), {})

    def test_classic_instance_generic_lookup(self):
        class Classic:
            attribute = 'class'
            def __getattr__(self, name):
                return 'dynamic'

        class Hooked:
            def __getattribute__(self, name):
                return object.__getattribute__(self, name)
            def __getattr__(self, name):
                raise AttributeError(name)

        class Failing:
            def __getattr__(self, name):
                raise AttributeError(name)

        instance = Classic()
        instance.own = 1
        for name in ('attribute', 'own', 'missing', '__dict__'):
            self.assert_message(AttributeError, "'instance' object has no attribute '%s'" % name, object.__getattribute__, instance, name)
        self.assertIs(object.__getattribute__(instance, '__class__'), type(instance))
        hooked = Hooked()
        hooked.own = 1
        self.assertEqual(hooked.own, 1)
        self.assert_message(AttributeError, "'instance' object has no attribute 'own'", Hooked.__getattribute__, hooked, 'own')
        self.assert_message(TypeError, 'instance has no next() method', next, Failing())
        self.assert_message(TypeError, 'iteration over non-sequence', list, Failing())

    def test_readonly_descriptor_errors(self):
        class Plain(object):
            pass

        def closure():
            value = 1
            def inner():
                return value
            return inner

        def empty_cell():
            def inner():
                return later
            return inner.func_closure[0].cell_contents
            later = 1

        plain = Plain()
        unwritable = "attribute '__weakref__' of 'Plain' objects is not writable"
        self.assert_message(AttributeError, unwritable, setattr, plain, '__weakref__', 1)
        self.assert_message(AttributeError, unwritable, delattr, plain, '__weakref__')
        self.assert_message(AttributeError, unwritable, Plain.__dict__['__weakref__'].__set__, plain, 1)
        self.assert_message(AttributeError, unwritable, Plain.__dict__['__weakref__'].__delete__, plain)
        self.assert_message(AttributeError, "attribute 'cell_contents' of 'cell' objects is not writable", setattr, closure().func_closure[0], 'cell_contents', 2)
        self.assert_message(ValueError, 'Cell is empty', empty_cell)
        self.assert_message(TypeError, "can't delete __class__ attribute", delattr, plain, '__class__')
        self.assert_message(TypeError, "can't delete __class__ attribute", object.__delattr__, plain, '__class__')
        self.assert_message(ValueError, 'f_lineno can only be set by a trace function', setattr, sys._getframe(), 'f_lineno', 1)

    def test_bases_assignment_layout(self):
        class Plain(object):
            pass

        class Tuple(tuple):
            pass

        class List(list):
            pass

        class Value(ValueError):
            pass

        class Environment(IOError):
            pass

        self.assert_message(TypeError, "Plain.__bases__ must be tuple of old- or new-style classes, not 'int'", setattr, Plain, '__bases__', (1,))
        self.assert_message(TypeError, "__bases__ assignment: 'object' deallocator differs from 'tuple'", setattr, Tuple, '__bases__', (object,))
        self.assert_message(TypeError, "__bases__ assignment: 'int' deallocator differs from 'tuple'", setattr, Tuple, '__bases__', (int,))
        self.assert_message(TypeError, "__bases__ assignment: 'dict' deallocator differs from 'list'", setattr, List, '__bases__', (dict,))
        self.assert_message(TypeError, 'multiple bases have instance lay-out conflict', setattr, Plain, '__bases__', (int, str))
        self.assert_message(TypeError, "__bases__ assignment: 'exceptions.ValueError' deallocator differs from 'exceptions.IOError'", setattr, Environment, '__bases__', (ValueError,))
        Value.__bases__ = (KeyError,)
        self.assertEqual([kind.__name__ for kind in Value.__mro__], ['Value', 'KeyError', 'LookupError', 'StandardError', 'Exception', 'BaseException', 'object'])

    def test_classic_class_has_no_class_attribute(self):
        class Old:
            pass

        self.assert_message(AttributeError, "class Old has no attribute '__class__'", getattr, Old, '__class__')
        self.assertEqual(getattr(Old, '__class__', 'absent'), 'absent')
        self.assertFalse(hasattr(Old, '__class__'))
        self.assertIs(Old().__class__, Old)
        self.assertEqual(type(Old).__name__, 'classobj')
        self.assertIsInstance(type(Old).__call__(Old), Old)
        Old.__class__ = 5
        self.assertEqual((Old.__dict__['__class__'], Old().__class__ is Old), (5, True))
        del Old.__class__
        self.assert_message(AttributeError, "class Old has no attribute '__class__'", delattr, Old, '__class__')

    def test_assorted_object_messages(self):
        class Plain(object):
            pass

        class Tuple(tuple):
            pass

        class Old:
            pass

        def keywords(**keywords):
            return keywords

        keywords.func_defaults = (9, 9)
        error = KeyError(1, 2)

        def delete_item():
            del error[1]

        self.assert_message(TypeError, 'super() argument 1 must be type, not None', super, None, Plain())
        self.assert_message(TypeError, 'keywords() takes at most 0 arguments (2 given)', keywords, 1, 2)
        self.assertEqual((Tuple.__itemsize__, Plain.__itemsize__), (8, 0))
        self.assert_message(TypeError, 'tuple.__new__(int): int is not a subtype of tuple', Tuple.__new__, int)
        self.assert_message(TypeError, 'tuple() takes at most 1 argument (2 given)', Tuple, 1, 2)
        self.assertEqual(tuple.__new__(Tuple, sequence=[1, 2]), (1, 2))
        self.assert_message(TypeError, 'object() takes no parameters', lambda: object(a=1))
        self.assert_message(TypeError, '__format__() takes exactly 1 argument (0 given)', Plain().__format__)
        self.assert_message(TypeError, 'argument to __format__ must be unicode or str', Plain().__format__, 1)
        self.assert_message(AttributeError, "Old instance has no attribute '__trunc__'", compile, '1', 'parcel', 'exec', Old())
        self.assert_message(TypeError, "'exceptions.KeyError' object doesn't support item deletion", delete_item)

    def test_slot_named_doc_keeps_member(self):
        class Slot(object):
            __slots__ = ('__doc__', 'x')

        self.assertEqual(repr(Slot.__dict__['__doc__']), "<member '__doc__' of 'Slot' objects>")
        self.assertIs(Slot.__doc__, Slot.__dict__['__doc__'])
        instance = Slot()
        instance.__doc__ = 'parcel'
        self.assertEqual(instance.__doc__, 'parcel')

    def test_static_type_doc_is_text(self):
        class Meta(type):
            pass

        self.assertEqual(type.__doc__, "type(object) -> the object's type\ntype(name, bases, dict) -> a new type")
        self.assertEqual(type(type.__dict__['__doc__']).__name__, 'getset_descriptor')
        self.assertIs(Meta.__doc__, None)
        self.assertIs(Meta('Made', (), {}).__doc__, None)
        for kind in (type(lambda: 0), property, type((lambda: 0).__get__(1)), type(sys), classmethod, staticmethod, super):
            self.assertTrue(kind.__doc__.startswith(kind.__name__ + '('), kind.__name__)
        self.assertEqual(repr(object.__doc__), "'The most base type'")
        self.assertEqual(type(type(len).__doc__).__name__, 'getset_descriptor')

    def test_none_type_slot_wrappers(self):
        self.assertEqual(None.__repr__(), 'None')
        self.assertEqual(type(None).__repr__(None), 'None')
        self.assertEqual(sorted(type(None).__dict__.keys()), ['__doc__', '__hash__', '__repr__'])
        self.assertEqual(None.__hash__(), hash(None))
        self.assert_message(TypeError, "descriptor '__hash__' requires a 'NoneType' object but received a 'int'", type(None).__hash__, 1)
        self.assert_message(TypeError, "descriptor '__repr__' of 'NoneType' object needs an argument", type(None).__repr__)

    def test_function_attribute_wrappers(self):
        function = lambda: 0
        kind = type(function)

        self.assertEqual(sorted(name for name in vars(kind) if name in ('__delattr__', '__getattribute__', '__setattr__')), ['__delattr__', '__getattribute__', '__setattr__'])
        kind.__setattr__(function, 'parcel', 1)
        self.assertEqual(function.parcel, 1)
        kind.__delattr__(function, 'parcel')
        self.assertFalse(hasattr(function, 'parcel'))
        self.assert_message(TypeError, "descriptor '__setattr__' requires a 'function' object but received a 'int'", kind.__setattr__, 1, 'parcel', 1)

    def test_unbound_hash_wrapper_falls_back(self):
        Plain = type('Plain', (object,), {'__hash__': int.__hash__})
        Text = type('Text', (object,), {'__hash__': str.__hash__})
        Wide = type('Wide', (int,), {'__hash__': str.__hash__})
        Equal = type('Equal', (object,), {'__hash__': int.__hash__, '__eq__': lambda self, other: True})

        self.assertIsInstance(hash(Plain()), int)
        self.assertIsInstance(hash(Text()), int)
        self.assert_message(TypeError, "unhashable type: 'Wide'", hash, Wide(5))
        self.assert_message(TypeError, "unhashable type: 'Equal'", hash, Equal())

    def test_static_type_attribute_assignment(self):
        self.assert_message(TypeError, "can't set attributes of built-in/extension type 'int'", setattr, int, '__doc__', 'x')
        self.assert_message(TypeError, "can't set attributes of built-in/extension type 'int'", delattr, int, '__doc__')
        self.assert_message(TypeError, "can't set attributes of built-in/extension type 'int'", setattr, int, '__bases__', (object,))
        self.assert_message(TypeError, "can't set attributes of built-in/extension type 'exceptions.ValueError'", setattr, ValueError, '__name__', 'Q')
        self.assert_message(TypeError, "can't set int.__bases__", type.__dict__['__bases__'].__set__, int, (object,))
        self.assert_message(TypeError, "can't set int.__name__", type.__dict__['__name__'].__set__, int, 'x')
        self.assert_message(TypeError, "can't set int.__module__", type.__dict__['__module__'].__delete__, int)
        self.assert_message(TypeError, "can't delete Plain.__module__", type.__dict__['__module__'].__delete__, type('Plain', (object,), {}))
        self.assert_message(AttributeError, "attribute '__doc__' of 'type' objects is not writable", type.__dict__['__doc__'].__set__, int, 'x')
        view = memoryview('a')
        self.assertEqual(type(memoryview.__dict__['format']).__name__, 'getset_descriptor')
        self.assertEqual((view.format, view.itemsize, view.ndim, view.readonly, view.shape, view.strides, view.suboffsets), ('B', 1, 1, True, (1L,), (1L,), None))
        self.assert_message(AttributeError, "attribute 'format' of 'memoryview' objects is not writable", setattr, view, 'format', 1)
        self.assert_message(AttributeError, "attribute 'shape' of 'memoryview' objects is not writable", delattr, view, 'shape')
        self.assert_message(TypeError, "descriptor 'format' for 'memoryview' objects doesn't apply to 'int' object", memoryview.__dict__['format'].__get__, 1, memoryview)

    def test_classic_instance_protocol_lookups(self):
        log = []

        class Dynamic:
            def __getattr__(self, name):
                log.append(name)
                if name == '__getitem__':
                    return lambda index: [10, 20][index]
                if name == '__len__':
                    return lambda: 2
                raise AttributeError(name)

        class Negative:
            def __len__(self):
                return -1
            def __getitem__(self, index):
                return index

        self.assertEqual(list(Dynamic()), [10, 20])
        self.assertEqual(log, ['__iter__', '__getitem__', '__getitem__', '__len__', '__getitem__', '__getitem__', '__getitem__'])
        del log[:]
        self.assertEqual(len(Dynamic()), 2)
        self.assertEqual(20 in Dynamic(), True)
        self.assertEqual(log, ['__len__', '__contains__', '__iter__', '__getitem__', '__getitem__', '__getitem__', '__getitem__'])
        del log[:]
        self.assertEqual(list(reversed(Dynamic())), [20, 10])
        self.assertEqual(log, ['__reversed__', '__getitem__', '__len__', '__len__', '__getitem__', '__getitem__'])
        self.assert_message(ValueError, '__len__() should return >= 0', reversed, Negative())
        self.assert_message(ValueError, '__len__() should return >= 0', list, type('Wide', (object,), {'__len__': lambda self: -1, '__iter__': lambda self: iter([1])})())
        self.assert_message(OverflowError, 'long int too large to convert to int', list, type('Huge', (object,), {'__length_hint__': lambda self: 2 ** 70, '__iter__': lambda self: iter([1])})())

    def test_struct_sequences(self):
        info = sys.version_info
        kind = type(info)

        self.assertEqual((kind.__name__, kind.__module__, repr(kind)), ('version_info', 'sys', "<type 'sys.version_info'>"))
        self.assertEqual(kind.__mro__, (kind, object))
        self.assertFalse(isinstance(info, tuple))
        self.assertEqual(repr(info), "sys.version_info(major=2, minor=7, micro=18, releaselevel='final', serial=0)")
        self.assertEqual(str(info), repr(info))
        self.assertEqual((kind.n_fields, kind.n_sequence_fields, kind.n_unnamed_fields), (5, 5, 0))
        self.assertEqual(sorted(name for name in kind.__dict__ if not name.startswith('__')), ['major', 'micro', 'minor', 'n_fields', 'n_sequence_fields', 'n_unnamed_fields', 'releaselevel', 'serial'])
        self.assertEqual(repr(kind.__dict__['major']), "<member 'major' of 'sys.version_info' objects>")
        self.assertEqual(kind.__doc__, 'sys.version_info\n\nVersion information as a named tuple.')
        major, minor, micro, level, serial = info
        self.assertEqual((major, minor, micro, level, serial), (info[0], info.minor, info[-3], info.releaselevel, info[4]))
        self.assertEqual((info[:2], info[1:3], info[::2], len(info), list(info), tuple(info)), ((2, 7), (7, 18), (2, 18, 0), 5, [2, 7, 18, 'final', 0], (2, 7, 18, 'final', 0)))
        self.assertEqual((info == (2, 7, 18, 'final', 0), info >= (2, 7), info < (3,), (3,) > info, info == 1, info != 1), (True, True, True, True, False, True))
        self.assertEqual((hash(info), 7 in info, info + (1,), info * 2, 2 * info), (hash((2, 7, 18, 'final', 0)), True, (2, 7, 18, 'final', 0, 1), (2, 7, 18, 'final', 0) * 2, (2, 7, 18, 'final', 0) * 2))
        self.assertEqual(info.__reduce__(), (kind, ((2, 7, 18, 'final', 0), {})))
        self.assert_message(TypeError, 'structseq index must be integer', info.__getitem__, 'major')
        self.assert_message(IndexError, 'tuple index out of range', info.__getitem__, 5)
        self.assert_message(TypeError, 'readonly attribute', setattr, info, 'major', 3)
        self.assert_message(AttributeError, "'sys.version_info' object has no attribute 'parcel'", setattr, info, 'parcel', 3)
        self.assert_message(TypeError, "cannot create 'sys.version_info' instances", kind, (1, 2, 3, 'a', 0))
        self.assert_message(TypeError, "can't set attributes of built-in/extension type 'sys.version_info'", setattr, kind, 'n_fields', 1)
        self.assert_message(TypeError, "descriptor 'major' for 'sys.version_info' objects doesn't apply to 'tuple' object", kind.major.__get__, (1, 2, 3, 4, 5), kind)
        self.assertEqual(repr(sys.long_info), 'sys.long_info(bits_per_digit=30, sizeof_digit=4)')
        self.assertEqual((type(sys.long_info).__name__, sys.long_info.bits_per_digit, sys.long_info.sizeof_digit, len(sys.long_info), sys.long_info == (30, 4)), ('long_info', 30, 4, 2, True))
        self.assertEqual(type(sys.float_info).__name__, 'float_info')
        self.assertTrue(repr(sys.float_info).startswith('sys.float_info(max='))
        self.assertEqual((sys.float_info.max, sys.float_info[8], sys.float_info.n_fields), (sys.float_info[0], sys.float_info.epsilon, 11))

    def test_constructor_and_builtin_messages(self):
        method = type((lambda: 0).__get__(1))

        def print_call(source):
            return eval(compile('from __future__ import print_function\n' + source, 'parcel', 'exec'))

        self.assert_message(TypeError, 'instancemethod does not take keyword arguments', lambda: method(len, 1, int, parcel=1))
        self.assert_message(TypeError, 'instancemethod expected at least 2 arguments, got 1', method, len)
        self.assert_message(TypeError, 'instancemethod expected at most 3 arguments, got 4', method, len, 1, int, 2)
        self.assert_message(TypeError, 'first argument must be callable', method, 1, 1)
        self.assert_message(TypeError, 'unbound methods must have non-NULL im_class', method, len, None)
        self.assert_message(TypeError, 'instancemethod expected at least 2 arguments, got 1', method.__new__, method, len)
        self.assert_message(TypeError, 'instancemethod.__new__(int): int is not a subtype of instancemethod', method.__new__, int, len, 1)
        self.assert_message(TypeError, 'exceptions.BaseException.__new__(int): int is not a subtype of exceptions.BaseException', BaseException.__new__, int)
        self.assert_message(TypeError, 'exceptions.ValueError.__new__(exceptions.BaseException): exceptions.BaseException is not a subtype of exceptions.ValueError', ValueError.__new__, BaseException)
        self.assert_message(TypeError, 'exceptions.BaseException.__new__(): not enough arguments', BaseException.__new__)
        self.assert_message(TypeError, 'int.__new__(bool) is not safe, use bool.__new__()', int.__new__, bool, 5)
        self.assert_message(TypeError, 'type.__new__(int): int is not a subtype of type', type.__new__, int, 'X', (), {})
        self.assert_message(TypeError, 'type.__new__(): not enough arguments', type.__new__)
        self.assert_message(TypeError, 'basestring.__new__(str) is not safe, use str.__new__()', basestring.__new__, str, 'a')
        self.assert_message(TypeError, 'basestring.__new__(int): int is not a subtype of basestring', basestring.__new__, int)
        self.assert_message(TypeError, 'The basestring type cannot be instantiated', basestring.__new__, basestring)
        self.assert_message(TypeError, 'property() takes at most 4 arguments (5 given)', property, None, None, None, None, None)
        self.assert_message(TypeError, 'property() takes at most 4 arguments (5 given)', lambda: property(None, None, None, None, doc=1))
        self.assert_message(TypeError, "'bogus' is an invalid keyword argument for this function", lambda: property(bogus=1))
        self.assert_message(TypeError, "Argument given by name ('fget') and position (1)", lambda: property(None, fget=None))
        self.assert_message(TypeError, "'foo' is an invalid keyword argument for this function", print_call, "print('a', foo=1)")
        self.assert_message(TypeError, 'sep must be None, str or unicode, not int', print_call, "print('a', sep=1)")
        self.assert_message(TypeError, 'end must be None, str or unicode, not list', print_call, "print('a', end=[])")
        try:
            type('Made', (object, ValueError), {})
        except TypeError as error:
            prefix = 'Cannot create a consistent method resolution\norder (MRO) for bases '
            self.assertTrue(str(error).startswith(prefix))
            self.assertEqual(sorted(str(error)[len(prefix):].split(', ')), ['ValueError', 'object'])
        else:
            self.fail('an inconsistent MRO must be rejected')

    def test_weak_proxy_slot_wrappers(self):
        class Sequence(object):
            def __init__(self):
                self.items = [1, 2, 3]
            def __len__(self):
                return len(self.items)
            def __getitem__(self, key):
                return self.items[key]
            def __setitem__(self, key, value):
                self.items[key] = value
            def __delitem__(self, key):
                del self.items[key]
            def __contains__(self, item):
                return item in self.items
            def __iter__(self):
                return iter(self.items)
            def __call__(self, *args):
                return args

        class Counter(object):
            def __init__(self):
                self.count = 0
            def __iter__(self):
                return self
            def next(self):
                if self.count:
                    raise StopIteration
                self.count += 1
                return 'parcel'

        for name in ('__contains__', '__delitem__', '__delslice__', '__getitem__', '__getslice__', '__iter__', '__len__', '__setitem__', '__setslice__', 'next'):
            self.assertIn(name, _weakref.ProxyType.__dict__, name)
            self.assertIn(name, _weakref.CallableProxyType.__dict__, name)
        self.assertNotIn('__call__', _weakref.ProxyType.__dict__)
        self.assertEqual(repr(_weakref.CallableProxyType.__dict__['__call__']), "<slot wrapper '__call__' of 'weakcallableproxy' objects>")
        sequence = Sequence()
        proxy = _weakref.proxy(sequence)
        kind = type(proxy)
        self.assertIs(kind, _weakref.CallableProxyType)
        self.assertEqual((kind.__len__(proxy), kind.__getitem__(proxy, 1), kind.__contains__(proxy, 3), list(kind.__iter__(proxy)), kind.__call__(proxy, 1, 2)), (3, 2, True, [1, 2, 3], (1, 2)))
        kind.__setitem__(proxy, 0, 9)
        kind.__delitem__(proxy, 2)
        self.assertEqual((sequence.items, kind.__getslice__(proxy, 0, 1)), ([9, 2], [9]))
        counter = Counter()
        counter_proxy = _weakref.proxy(counter)
        self.assertEqual(_weakref.ProxyType.next(counter_proxy), 'parcel')
        self.assertRaises(StopIteration, _weakref.ProxyType.next, counter_proxy)
        self.assert_message(TypeError, "descriptor '__len__' requires a 'weakproxy' object but received a 'list'", _weakref.ProxyType.__len__, [])

    def test_classic_hook_slots_snapshot(self):
        class Base:
            pass

        class Early(Base):
            pass

        Base.__setattr__ = lambda self, name, value: self.__dict__.__setitem__('set:' + name, value)
        Base.__getattr__ = lambda self, name: ('get', name)
        Base.__delattr__ = lambda self, name: self.__dict__.__setitem__('del:' + name, 1)

        class Late(Base):
            pass

        def exercise(kind):
            instance = kind()
            instance.parcel = 1
            try:
                value = instance.absent
            except AttributeError:
                value = 'AttributeError'
            del instance.parcel
            return sorted(instance.__dict__.items()), value

        hooked = ([('del:parcel', 1), ('set:parcel', 1)], ('get', 'absent'))
        self.assertEqual((exercise(Base), exercise(Early), exercise(Late)), (hooked, ([], 'AttributeError'), hooked))
        Early.__bases__ = (Base,)
        self.assertEqual(exercise(Early), hooked)
        Early.__getattr__ = lambda self, name: 'own'
        self.assertEqual(Early().absent, 'own')
        del Early.__getattr__
        self.assert_message(AttributeError, "Early instance has no attribute 'absent'", getattr, Early(), 'absent')

    def test_filter_text_returns_new_string(self):
        text = 'abc'
        self.assertIsNot(filter(lambda character: True, text), text)
        self.assertIs(filter(None, text), text)
        self.assertEqual(filter(lambda character: character != 'b', text), 'ac')

    def test_length_protocol_of_offsets(self):
        class FloatLength:
            def __len__(self):
                return 1.5
            def __getitem__(self, item):
                return item

        class NegativeLength:
            def __len__(self):
                return -1
            def __getitem__(self, item):
                return item

        calls = []

        class Bytes(bytearray):
            def __len__(self):
                calls.append('len')
                return 2

        class Huge(Exception):
            def __len__(self):
                return 2 ** 70

        self.assert_message(TypeError, '__len__() should return an int', lambda: FloatLength()[-1:-1])
        self.assert_message(ValueError, '__len__() should return >= 0', lambda: NegativeLength()[-1:-1])
        self.assertEqual(list(reversed(Bytes('abcd'))), [98, 97])
        self.assertTrue(calls)
        self.assert_message(OverflowError, 'long int too large to convert to int', lambda: Huge(1, 2)[-1])
        self.assert_message(OverflowError, 'long int too large to convert to int', lambda: Huge(1, 2)[-5:-5])
        self.assertEqual((Huge(1, 2)[1], Huge(1, 2)[0:1]), (2, (1,)))

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


def lowered_recursion_limit(value):
    limit = sys.getrecursionlimit()
    sys.setrecursionlimit(value)
    try:
        return sys.getrecursionlimit()
    finally:
        sys.setrecursionlimit(limit)


def deep_recursion(count):
    if count == 0:
        return 0
    return deep_recursion(count - 1) + 1


def bounded_recursion(value, count):
    limit = sys.getrecursionlimit()
    sys.setrecursionlimit(value)
    try:
        return deep_recursion(count)
    except RuntimeError as error:
        return str(error)[:32]
    finally:
        sys.setrecursionlimit(limit)


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
