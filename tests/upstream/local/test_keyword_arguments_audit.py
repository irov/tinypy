"""Project-authored CPython 2.7 parity checks for keyword arguments of C
functions, which vgetargskeywords parses, and of the copy_reg and hex codec
functions, which are Python functions in CPython.

Expectations are checked on CPython 2.7.18.
"""

import _codecs
import _sre
import _struct
import copy_reg
import sys
import unittest


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must fail')


def _function():
    pass


class _Slotted(object):
    __slots__ = ('a',)

    def __init__(self, *args):
        pass


class _Reduced(object):
    pass


def _reduce(value):
    return _Reduced, ()


_PATTERN = _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1], 0, {}, [None])

# Each parser with a keyword it declares and a value for it.
_PARSERS = (
    (lambda kw: type(sys)('m', **kw).__doc__, 'doc', 'd'),
    (lambda kw: type(_function)(_function.func_code, {}, **kw).__name__, 'name', 'n'),
    (lambda kw: list(enumerate('ab', **kw)), 'start', 3),
    (lambda kw: sorted([2, 1], **kw), 'reverse', True),
    (lambda kw: [2, 1].sort(**kw), 'reverse', True),
    (lambda kw: property(**kw).__doc__, 'doc', 'd'),
    (lambda kw: int('5', **kw), 'base', 10),
    (lambda kw: float(**kw), 'x', '1.5'),
    (lambda kw: unicode('a', **kw), 'errors', 'strict'),
    (lambda kw: str(**kw), 'object', 5),
    (lambda kw: tuple(**kw), 'sequence', 'ab'),
    (lambda kw: bytearray('a', **kw), 'encoding', 'ascii'),
    (lambda kw: type(sys.float_info)((0,) * 11, **kw)[0], 'dict', {}),
    (lambda kw: sys.getsizeof(1, **kw) > 0, 'default', 1),
    (lambda kw: __import__('sys', **kw).__name__, 'level', -1),
    (lambda kw: round(1.25, **kw), 'ndigits', 1),
    (lambda kw: type(compile('1', 'f', 'eval', **kw)).__name__, 'flags', 0),
    (lambda kw: _PATTERN.match('a', **kw).group(), 'pos', 0),
    (lambda kw: _PATTERN.sub('x', 'aba', **kw), 'count', 0),
    (lambda kw: _PATTERN.match('a').groups(**kw), 'default', None),
    (lambda kw: _struct.unpack_from('d', '\0' * 8, **kw), 'offset', 0),
    (lambda kw: 'a'.encode('ascii', **kw), 'errors', 'strict'),
)


class KeywordArgumentsAudit(unittest.TestCase):
    def test_a_key_that_is_not_a_str_is_rejected_before_it_is_read(self):
        for parse, name, value in _PARSERS:
            for key in (123456789, 1.5, (name,), None):
                self.assertEqual(_error(parse, {key: value}), ('TypeError', 'keywords must be strings'), (name, key))

    def test_unicode_keys_name_parameters_and_extra_ones_must_be_str(self):
        for parse, name, value in _PARSERS:
            self.assertEqual(parse({unicode(name): value}), parse({name: value}), name)
            for key in (u'zz', u'zz\xe9', u'z' * 300):
                self.assertEqual(_error(parse, {key: 1}), ('TypeError', 'keywords must be strings'), (name, key))
            self.assertEqual(_error(parse, {'zz': 1}), ('TypeError', "'zz' is an invalid keyword argument for this function"), name)

    def test_extra_key_is_reported_up_to_its_first_nul_and_in_full(self):
        for parse, name, value in _PARSERS:
            self.assertEqual(_error(parse, {'zz\0' + name: 1}), ('TypeError', "'zz' is an invalid keyword argument for this function"), name)
            self.assertEqual(_error(parse, {'z' * 300: 1}), ('TypeError', "'%s' is an invalid keyword argument for this function" % ('z' * 300)), name)

    def test_extra_key_naming_a_parameter_before_its_first_nul_is_ignored(self):
        for parse, name, value in _PARSERS:
            self.assertEqual(parse({name + '\0x': value}), parse({}), name)
        self.assertEqual(sorted([2, 1], **{'reverse\0': True}), [1, 2])
        self.assertEqual(int('7', **{'base\0x': 2}), 7)

    def test_str_subclass_keys_are_str(self):
        class Key(str):
            pass

        self.assertEqual(sorted([1, 2], **{Key('reverse'): True}), [2, 1])
        self.assertEqual(_error(sorted, [], **{Key('zz'): 1}), ('TypeError', "'zz' is an invalid keyword argument for this function"))

    def test_copy_reg_functions_bind_keywords_as_python_functions(self):
        self.assertEqual(copy_reg.pickle(ob_type=_Reduced, pickle_function=_reduce), None)
        self.assertEqual(copy_reg.pickle(_Reduced, _reduce, constructor_ob=None), None)
        self.assertEqual(copy_reg.pickle(_Reduced, **{u'pickle_function': _reduce}), None)
        self.assertEqual(copy_reg.dispatch_table.pop(_Reduced), _reduce)
        self.assertEqual(_error(copy_reg.pickle, _Reduced, _reduce, constructor_ob=1), ('TypeError', 'constructors must be callable'))
        self.assertEqual(copy_reg.constructor(object=_Reduced), None)
        self.assertEqual(copy_reg.pickle_complex(c=1j), (complex, (0.0, 1.0)))
        self.assertEqual(type(copy_reg._reconstructor(cls=_Reduced, base=object, state=None)), _Reduced)
        self.assertEqual(type(copy_reg._reconstructor(_Reduced, object, state=None)), _Reduced)
        self.assertEqual(copy_reg._reduce_ex(self=_Reduced(), proto=0)[0], copy_reg._reconstructor)
        self.assertEqual(type(copy_reg.__newobj__(cls=_Slotted)), _Slotted)
        self.assertEqual(copy_reg._slotnames(cls=_Slotted), ['a'])
        self.assertEqual(copy_reg.add_extension(module='keyword_module', name='keyword_name', code=4242), None)
        self.assertEqual(copy_reg.remove_extension('keyword_module', name='keyword_name', code=4242), None)

    def test_copy_reg_functions_report_keywords_as_python_functions(self):
        self.assertEqual(_error(copy_reg.pickle, _Reduced, _reduce, zz=1), ('TypeError', "pickle() got an unexpected keyword argument 'zz'"))
        self.assertEqual(_error(copy_reg.pickle, _Reduced, _reduce, **{u'zz\xe9': 1}), ('TypeError', "pickle() got an unexpected keyword argument 'zz?'"))
        self.assertEqual(_error(lambda: copy_reg.pickle(_Reduced, _reduce, **{1: 2})), ('TypeError', 'pickle() keywords must be strings'))
        self.assertEqual(_error(copy_reg.pickle, _Reduced, _reduce, ob_type=_Reduced), ('TypeError', "pickle() got multiple values for keyword argument 'ob_type'"))
        self.assertEqual(_error(copy_reg.pickle, _Reduced, constructor_ob=_Reduced), ('TypeError', 'pickle() takes at least 2 arguments (2 given)'))
        self.assertEqual(_error(copy_reg.pickle, constructor_ob=_Reduced), ('TypeError', 'pickle() takes at least 2 arguments (1 given)'))
        self.assertEqual(_error(copy_reg.pickle, _Reduced, _reduce, None, None, zz=1), ('TypeError', 'pickle() takes at most 3 arguments (5 given)'))
        self.assertEqual(_error(copy_reg.pickle, _Reduced, **{'pickle_function\0': _reduce}), ('TypeError', "pickle() got an unexpected keyword argument 'pickle_function'"))
        self.assertEqual(_error(copy_reg._reconstructor, _Reduced, state=None), ('TypeError', '_reconstructor() takes exactly 3 arguments (2 given)'))
        self.assertEqual(_error(copy_reg.__newobj__, _Slotted, 1, cls=_Slotted), ('TypeError', "__newobj__() got multiple values for keyword argument 'cls'"))
        self.assertEqual(_error(copy_reg.__newobj__, zz=_Slotted), ('TypeError', "__newobj__() got an unexpected keyword argument 'zz'"))
        self.assertEqual(_error(copy_reg._slotnames, _Slotted, zz=1), ('TypeError', "_slotnames() got an unexpected keyword argument 'zz'"))
        self.assertEqual(_error(copy_reg.clear_extension_cache, zz=1), ('TypeError', 'clear_extension_cache() takes no arguments (1 given)'))
        self.assertEqual(_error(copy_reg.clear_extension_cache, 1, zz=1), ('TypeError', 'clear_extension_cache() takes no arguments (2 given)'))
        self.assertEqual(_error(copy_reg.remove_extension, 'm', code=1), ('TypeError', 'remove_extension() takes exactly 3 arguments (2 given)'))
        self.assertEqual(_error(copy_reg.pickle, _Reduced, _reduce, **{'k' * 500: 1}), ('TypeError', "pickle() got an unexpected keyword argument '%s'" % ('k' * 400)))

    def test_hex_codec_binds_keywords_as_python_functions(self):
        encode, decode = _codecs.lookup('hex')[:2]
        self.assertEqual(encode(input='ab'), ('6162', 2))
        self.assertEqual(decode('6162', errors='strict'), ('ab', 4))
        self.assertEqual(_error(encode, 'ab', errors='ignore'), ('AssertionError', ''))
        self.assertEqual(_error(encode, errors='strict'), ('TypeError', 'hex_encode() takes at least 1 argument (1 given)'))
        self.assertEqual(_error(encode), ('TypeError', 'hex_encode() takes at least 1 argument (0 given)'))
        self.assertEqual(_error(encode, 'a', 'strict', 1, zz=2), ('TypeError', 'hex_encode() takes at most 2 arguments (4 given)'))
        self.assertEqual(_error(lambda: decode('61', **{1: 2})), ('TypeError', 'hex_decode() keywords must be strings'))
        self.assertEqual(_error(decode, '61', **{u'zz\xe9': 2}), ('TypeError', "hex_decode() got an unexpected keyword argument 'zz?'"))
        self.assertEqual(_error(decode, '61', input='61'), ('TypeError', "hex_decode() got multiple values for keyword argument 'input'"))

    def test_hex_codec_holds_arguments_a_comparison_releases(self):
        class Strict(object):
            def __eq__(self, other):
                keywords.clear()
                return True

        keywords = {'input': ''.join(['ab'] * 50), 'errors': Strict()}
        encode = _codecs.lookup('hex')[0]
        self.assertEqual(apply(encode, (), keywords), ('6162' * 50, 100))
        self.assertEqual(keywords, {})

    def test_subclasshook_rejects_keywords_as_a_c_function(self):
        self.assertEqual(_error(object.__subclasshook__, x=1), ('TypeError', '__subclasshook__() takes no keyword arguments'))
        self.assertIs(object.__subclasshook__(1, 2), NotImplemented)


if __name__ == '__main__':
    unittest.main()
