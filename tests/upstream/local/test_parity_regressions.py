"""Project-authored CPython 2.7 parity checks for conversions, output,
release order and message precisions."""

import __builtin__
import _weakref
import sys
import unittest


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must raise')


def _decode_error(operation):
    try:
        operation()
    except UnicodeDecodeError as error:
        return error.object, error.start, error.end
    raise AssertionError('operation must raise UnicodeDecodeError')


class Text(str):
    def __str__(self):
        return 'own-str'


class Writer(object):
    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append((type(text), text))


class Released(object):
    def __init__(self, log, name):
        self.log = log
        self.name = name

    def __del__(self):
        self.log.append(self.name)


class TextConversionParity(unittest.TestCase):
    def test_classic_format_formats_str_whatever_the_lookup_raised(self):
        class Classic:
            def __getattr__(self, name):
                if name in ('__format__', '__unicode__'):
                    raise KeyError(name)
                raise AttributeError(name)

            def __str__(self):
                return 'classic'
        self.assertEqual(format(Classic()), 'classic')
        self.assertEqual(format(Classic(), '>9'), '  classic')
        self.assertEqual(format(Classic(), u''), u'classic')
        self.assertEqual('{0}'.format(Classic()), 'classic')
        self.assertEqual(u'{0:^9}'.format(Classic()), u' classic ')
        self.assertEqual(unicode(Classic()), u'classic')

    def test_object_str_of_an_exception_returns_what_repr_returns(self):
        class Plain(Exception):
            __str__ = object.__str__

            def __repr__(self):
                return Text('plain')

        class Key(KeyError):
            __str__ = object.__str__

            def __repr__(self):
                return Text('key')

        class Wide(Exception):
            __str__ = object.__str__

            def __repr__(self):
                return u'wide'

        class Number(Exception):
            __str__ = object.__str__

            def __repr__(self):
                return 5
        for value in (Plain(), Key('k')):
            text = str(value)
            self.assertIs(type(text), Text)
            self.assertEqual(str(text), 'own-str')
        self.assertEqual(format(Plain()), 'own-str')
        self.assertIs(type(str(Wide())), str)
        self.assertEqual('%s' % Wide(), u'wide')
        self.assertIs(type('%s' % Wide()), unicode)
        self.assertEqual(_error(str, Number()), ('TypeError', '__str__ returned non-string (type int)'))

    def test_proxy_str_returns_what_the_referent_str_returns(self):
        class Target(object):
            def __str__(self):
                return Text('target')
        target = Target()
        text = str(_weakref.proxy(target))
        self.assertIs(type(text), Text)
        self.assertEqual(text, 'target')

    def test_print_function_writes_str_of_every_non_unicode_value(self):
        print_function = getattr(__builtin__, 'print')
        writer = Writer()
        print_function(Text('item'), 5, u'wide', sep=Text('-'), end=Text('!'), file=writer)
        self.assertEqual(writer.parts, [(str, 'own-str'), (str, 'own-str'), (str, '5'), (str, 'own-str'),
                                        (unicode, u'wide'), (str, 'own-str')])
        writer = Writer()
        print >>writer, Text('item')
        self.assertEqual(writer.parts, [(str, 'own-str'), (str, '\n')])
        print_function(Text('stdout'), Text('item'), sep=Text('|'))

    def test_stream_write_and_writelines_reset_the_softspace(self):
        print 'x',
        sys.stdout.write('y')
        print 'z'
        print 'a',
        sys.stdout.writelines(['b'])
        print 'c'
        print 'q',
        sys.stdout.writelines([])
        self.assertEqual(sys.stdout.softspace, 1)
        sys.stdout.write('')
        self.assertEqual(sys.stdout.softspace, 0)
        print

    def test_percent_restart_decodes_the_rest_of_the_format_first(self):
        class Wide(object):
            def __repr__(self):
                return u'\xe9'
        self.assertEqual(_error(lambda: '\xe9%s %' % (u'a',)), ('ValueError', 'incomplete format'))
        self.assertEqual(_error(lambda: '\xe9%s %d' % (u'a', 'x')),
                         ('TypeError', '%d format: a number is required, not str'))
        self.assertEqual(_error(lambda: '\xe9%s%r' % (u'a', Wide()))[0], 'UnicodeEncodeError')
        self.assertEqual(_error(lambda: '\xe9%s' % (u'a', 1)),
                         ('TypeError', 'not all arguments converted during string formatting'))
        self.assertEqual(_decode_error(lambda: '\xe9 %s \xe8' % (u'x',)), ('%s \xe8', 3, 4))
        self.assertEqual(_decode_error(lambda: 'ab%sc\xe9' % (u'x',)), ('%sc\xe9', 3, 4))
        self.assertEqual(_decode_error(lambda: 'ab\xe9%s%d' % (u'x', 3)), ('ab\xe9', 2, 3))
        self.assertEqual(_decode_error(lambda: '%5s|%-3s|\xe9' % ('ab', u'x')), ('%-3s|\xe9', 5, 6))
        self.assertEqual(_decode_error(lambda: '%c\xe9' % (u'ab',)), ('%c\xe9', 2, 3))
        self.assertEqual(_error(lambda: '\xe9%c' % (u'ab',)), ('TypeError', '%c requires int or char'))
        self.assertEqual('a%sb%c' % (u'x', u'y'), u'axby')

    def test_percent_restart_after_a_unicode_str_precedes_unicode(self):
        class Wide(object):
            def __str__(self):
                return u'wide'

            def __unicode__(self):
                raise KeyError('unicode')
        self.assertEqual(_decode_error(lambda: 'q%s\xe9' % (Wide(),)), ('%s\xe9', 2, 3))
        self.assertEqual(_error(lambda: 'q\xe9%s' % (Wide(),)), ('KeyError', "'unicode'"))

    def test_byte_replace_decodes_self_before_its_arguments(self):
        self.assertEqual(_decode_error(lambda: '\xe9'.replace(u'abc', None)), ('\xe9', 0, 1))
        self.assertEqual(_decode_error(lambda: '\xe9'.replace('\xe8', u'b')), ('\xe9', 0, 1))
        self.assertEqual(_error(lambda: 'abc'.replace(u'b', None)),
                         ('TypeError', 'coercing to Unicode: need string or buffer, NoneType found'))
        self.assertEqual(_error(lambda: '\xe9'.replace(None, u'b')),
                         ('TypeError', 'expected a string or other character buffer object'))


class ReleaseOrderParity(unittest.TestCase):
    def _released(self, make):
        log = []
        value = make(log)
        del value
        return log

    def test_list_and_tuple_release_items_from_the_end(self):
        self.assertEqual(self._released(lambda log: [Released(log, index) for index in range(4)]), [3, 2, 1, 0])
        self.assertEqual(self._released(lambda log: tuple(Released(log, index) for index in range(4))), [3, 2, 1, 0])
        self.assertEqual(self._released(lambda log: [(Released(log, 0), Released(log, 1)), [Released(log, 2)]]),
                         [2, 1, 0])
        self.assertEqual(self._released(lambda log: dict((index, Released(log, index)) for index in range(4))),
                         [0, 1, 2, 3])

    def test_subclass_members_and_dict_precede_the_contents(self):
        class Items(list):
            pass

        class Pair(tuple):
            pass

        class Mapping(dict):
            pass

        class Slotted(object):
            __slots__ = ('second', 'first', '__dict__')

        class Getter(property):
            pass

        def items(log):
            value = Items([Released(log, 0), Released(log, 1)])
            value.extra = Released(log, 9)
            return value

        def pair(log):
            value = Pair((Released(log, 0), Released(log, 1)))
            value.extra = Released(log, 9)
            return value

        def mapping(log):
            value = Mapping({0: Released(log, 0), 1: Released(log, 1)})
            value.extra = Released(log, 9)
            return value

        def slotted(log):
            value = Slotted()
            value.second = Released(log, 1)
            value.first = Released(log, 0)
            value.extra = Released(log, 9)
            return value

        def getter(log):
            value = Getter(Released(log, 0), Released(log, 1))
            value.extra = Released(log, 9)
            return value
        self.assertEqual(self._released(items), [9, 1, 0])
        self.assertEqual(self._released(pair), [9, 1, 0])
        self.assertEqual(self._released(mapping), [9, 0, 1])
        self.assertEqual(self._released(slotted), [0, 1, 9])
        self.assertEqual(self._released(getter), [9, 0, 1])


class MessagePrecisionParity(unittest.TestCase):
    def test_attribute_messages(self):
        long_type = type('T' * 80, (object,), {})
        slotted = type('S' * 120, (object,), {'__slots__': (), 'fixed': 1})
        namespace = {}
        exec 'class %s: pass' % ('C' * 80) in namespace
        classic = namespace['C' * 80]
        self.assertEqual(_error(getattr, long_type(), 'a' * 500),
                         ('AttributeError', "'%s' object has no attribute '%s'" % ('T' * 50, 'a' * 400)))
        self.assertEqual(_error(getattr, long_type, 'b' * 500),
                         ('AttributeError', "type object '%s' has no attribute '%s'" % ('T' * 50, 'b' * 400)))
        self.assertEqual(_error(getattr, classic(), 'c' * 500),
                         ('AttributeError', "%s instance has no attribute '%s'" % ('C' * 50, 'c' * 400)))
        self.assertEqual(_error(getattr, classic, 'd' * 500),
                         ('AttributeError', "class %s has no attribute '%s'" % ('C' * 50, 'd' * 400)))
        self.assertEqual(_error(delattr, classic(), 'e' * 500),
                         ('AttributeError', "%s instance has no attribute '%s'" % ('C' * 50, 'e' * 400)))
        self.assertEqual(_error(setattr, 1, 'f' * 500, 1),
                         ('AttributeError', "'int' object has no attribute '%s'" % ('f' * 200)))
        self.assertEqual(_error(setattr, slotted(), 'g' * 500, 1),
                         ('AttributeError', "'%s' object has no attribute '%s'" % ('S' * 100, 'g' * 200)))
        self.assertEqual(_error(setattr, slotted(), 'fixed', 2),
                         ('AttributeError', "'%s' object attribute 'fixed' is read-only" % ('S' * 50)))
        self.assertEqual(_error((1).__getattribute__, 'h' * 500),
                         ('AttributeError', "'int' object has no attribute '%s'" % ('h' * 400)))

    def test_name_and_import_messages(self):
        name = 'n' * 300
        namespace = {}
        exec 'def unbound():\n    %s\n    %s = 1\n' % (name, name) in namespace
        exec 'def outer():\n    def inner():\n        return %s\n    return inner\n    %s = 1\n' % (name, name) in namespace
        exec 'def missing():\n    return %s\n' % name in namespace
        unbound, outer, missing = namespace['unbound'], namespace['outer'], namespace['missing']
        # The functions would otherwise keep their namespace in a cycle.
        namespace.clear()
        self.assertEqual(_error(eval, name), ('NameError', "name '%s' is not defined" % name[:200]))
        self.assertEqual(_error(missing), ('NameError', "global name '%s' is not defined" % name[:200]))
        self.assertEqual(_error(unbound),
                         ('UnboundLocalError', "local variable '%s' referenced before assignment" % name[:200]))
        self.assertEqual(_error(outer()),
                         ('NameError', "free variable '%s' referenced before assignment in enclosing scope" % name[:200]))
        self.assertEqual(_error(__import__, 'm' * 300), ('ImportError', 'No module named ' + 'm' * 200))
        self.assertEqual(_error(__import__, 'sys.' + 'm' * 300), ('ImportError', 'No module named ' + 'm' * 200))

        def from_import():
            exec 'from sys import ' + 'q' * 300
        self.assertEqual(_error(from_import), ('ImportError', 'cannot import name ' + 'q' * 230))

    def test_call_messages(self):
        name = 'f' * 250
        namespace = {}
        exec 'def %s(a):\n    return a\n' % name in namespace
        function = namespace[name]
        namespace.clear()
        self.assertEqual(_error(function), ('TypeError', '%s() takes exactly 1 argument (0 given)' % name[:200]))
        self.assertEqual(_error(function, **{'k' * 500: 1}),
                         ('TypeError', "%s() got an unexpected keyword argument '%s'" % (name[:200], 'k' * 400)))
        self.assertEqual(_error(lambda: function(**5)),
                         ('TypeError', '%s() argument after ** must be a mapping, not int' % name[:200]))

    def test_type_name_messages(self):
        long_type = type('T' * 250, (object,), {})
        unhashable = type('U' * 250, (object,), {'__hash__': None})
        short = 'T' * 200
        self.assertEqual(_error(lambda: long_type() + 1),
                         ('TypeError', "unsupported operand type(s) for +: '%s' and 'int'" % ('T' * 100)))
        self.assertEqual(_error(lambda: -long_type()), ('TypeError', "bad operand type for unary -: '%s'" % short))
        self.assertEqual(_error(len, long_type()), ('TypeError', "object of type '%s' has no len()" % short))
        self.assertEqual(_error(iter, long_type()), ('TypeError', "'%s' object is not iterable" % short))
        self.assertEqual(_error(long_type()), ('TypeError', "'%s' object is not callable" % short))
        self.assertEqual(_error(hash, unhashable()), ('TypeError', "unhashable type: '%s'" % ('U' * 200)))
        self.assertEqual(_error(lambda: [][long_type()]),
                         ('TypeError', 'list indices must be integers, not %s' % short))
        self.assertEqual(_error(lambda: 'a' + long_type()),
                         ('TypeError', "cannot concatenate 'str' and '%s' objects" % short))
        self.assertEqual(_error(lambda: '%d' % long_type()),
                         ('TypeError', '%%d format: a number is required, not %s' % short))
        self.assertEqual(_error(lambda: pow(long_type(), 2, 3)),
                         ('TypeError', "unsupported operand type(s) for pow(): '%s', 'int', 'int'" % ('T' * 100)))
        self.assertEqual(_error(str, type('Z', (object,), {'__str__': lambda self: long_type()})()),
                         ('TypeError', '__str__ returned non-string (type %s)' % short))
