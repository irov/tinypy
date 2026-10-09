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
