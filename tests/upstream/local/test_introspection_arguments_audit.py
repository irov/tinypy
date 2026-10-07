"""Project-authored constructor, reduction and saved-frame state regressions."""

import sys
import unittest


def sample(value=17):
    return value


def stopped_frame():
    return sys._getframe()


def sample_generator():
    yield 17


class IntrospectionArgumentsAudit(unittest.TestCase):
    def test_sizeof_validates_arguments_without_comparing_physical_layout(self):
        frame, generator = stopped_frame(), sample_generator()
        objects = (object(), 17, 17L, 'abc', (), [], {}, sample,
                   sample.func_code, frame, generator, type(sys)('size_sample'))
        try:
            for value in objects:
                size = value.__sizeof__()
                self.assertIs(type(size), int)
                self.assertTrue(size >= 0)
                for arguments in ((1,), (1, 2)):
                    with self.assertRaises(TypeError) as caught:
                        value.__sizeof__(*arguments)
                    self.assertEqual(caught.exception.args,
                                     ('__sizeof__() takes no arguments (%d given)' % len(arguments),))
                with self.assertRaises(TypeError) as caught:
                    value.__sizeof__(argument=1)
                self.assertEqual(caught.exception.args, ('__sizeof__() takes no keyword arguments',))
        finally:
            generator.close()

    def test_sizeof_user_override_and_explicit_base_method(self):
        events = []
        class Subject(object):
            def __sizeof__(self, *arguments, **keywords):
                events.append((arguments, keywords))
                return 901
        value = Subject()
        self.assertEqual(value.__sizeof__(1, mode=2), 901)
        self.assertEqual(events, [((1,), {'mode': 2})])
        self.assertIs(type(object.__sizeof__(value)), int)
        self.assertEqual(len(events), 1)

    def test_function_constructor_required_and_typed_arguments(self):
        Function = type(sample)
        checks = (((), "Required argument 'code' (pos 1) not found"),
                  ((sample.func_code,), "Required argument 'globals' (pos 2) not found"),
                  ((17, {}), 'function() argument 1 must be code, not int'),
                  ((None, {}), 'function() argument 1 must be code, not None'),
                  ((sample.func_code, []), 'function() argument 2 must be dict, not list'),
                  ((sample.func_code, None), 'function() argument 2 must be dict, not None'))
        for arguments, message in checks:
            with self.assertRaises(TypeError) as caught:
                Function(*arguments)
            self.assertEqual(caught.exception.args, (message,))
        self.assertEqual(Function(sample.func_code, {}, argdefs=(23,))(), 23)

    def test_function_constructor_optional_argument_diagnostics(self):
        Function = type(sample)
        checks = (((sample.func_code, {}, u'name'), 'arg 3 (name) must be None or string'),
                  ((sample.func_code, {}, 17), 'arg 3 (name) must be None or string'),
                  ((sample.func_code, {}, None, []), 'arg 4 (defaults) must be None or tuple'),
                  ((sample.func_code, {}, None, (), []), 'arg 5 (closure) must be None or tuple'),
                  ((sample.func_code, {}, None, (), (None,)), 'sample requires closure of length 0, not 1'))
        for arguments, message in checks:
            with self.assertRaises((TypeError, ValueError)) as caught:
                Function(*arguments)
            self.assertEqual(caught.exception.args, (message,))

    def test_function_constructor_duplicate_unknown_and_total_count(self):
        Function = type(sample)
        for arguments, keywords, message in (
                ((sample.func_code, {}), {'code': sample.func_code}, "Argument given by name ('code') and position (1)"),
                ((sample.func_code, {}), {'extra': 1}, "'extra' is an invalid keyword argument for this function"),
                ((sample.func_code, {}, None, None, None, None), {}, 'function() takes at most 5 arguments (6 given)')):
            with self.assertRaises(TypeError) as caught:
                Function(*arguments, **keywords)
            self.assertEqual(caught.exception.args, (message,))

    def test_function_constructor_closure_validation_and_cell_identity(self):
        def enclosing():
            value = 29
            def closed():
                return value
            return closed
        closed = enclosing()
        Function = type(sample)
        for closure, message in ((None, 'arg 5 (closure) must be tuple'),
                                 ((), 'closed requires closure of length 1, not 0'),
                                 ((None,), 'arg 5 (closure) expected cell, found NoneType')):
            with self.assertRaises((TypeError, ValueError)) as caught:
                Function(closed.func_code, {}, None, None, closure)
            self.assertEqual(caught.exception.args, (message,))
        result = Function(closed.func_code, {}, 'copied', None, closed.func_closure)
        self.assertIs(result.func_closure, closed.func_closure)
        self.assertEqual(result(), 29)

    def test_function_final_keyword_validation_uses_nul_terminated_names(self):
        Function = type(sample)
        for field in ('name', 'code', 'closure'):
            result = Function(sample.func_code, {}, **{field + '\0tail': 'ignored'})
            self.assertEqual(result.__name__, 'sample')
            self.assertIs(result.func_defaults, None)
        for key, display in (('extra\0tail', 'extra'), ('x' * 201, 'x' * 201)):
            with self.assertRaises(TypeError) as caught:
                Function(sample.func_code, {}, **{key: 'ignored'})
            self.assertEqual(caught.exception.args, ("'%s' is an invalid keyword argument for this function" % display,))

    def test_function_constructor_preserves_optional_objects(self):
        class Name(str):
            pass
        class Defaults(tuple):
            pass
        name, defaults, namespace = Name('renamed'), Defaults((31,)), {'__name__': 'sample_module'}
        result = type(sample)(sample.func_code, namespace, name, defaults, ())
        self.assertIs(result.__name__, name)
        self.assertIs(result.func_defaults, defaults)
        self.assertIs(result.func_globals, namespace)
        self.assertEqual(result.__module__, 'sample_module')
        self.assertEqual(result(), 31)

    def test_function_keyword_lookup_uses_equality_and_suppresses_errors(self):
        Function = type(sample)
        for mode in ('true', 'false', 'raise'):
            for field in ('code', 'globals', 'name', 'argdefs', 'closure'):
                events = []
                failure = KeyboardInterrupt('keyword')
                class Key(str):
                    def __hash__(self):
                        return str.__hash__(self)
                    def __eq__(self, other):
                        events.append(str(other))
                        if mode == 'raise':
                            raise failure
                        return mode == 'true'
                values = {'code': sample.func_code, 'globals': {}, 'name': 'renamed', 'argdefs': (37,), 'closure': ()}
                keywords = {key: value for key, value in values.items() if key != field}
                keywords[Key(field)] = values[field]
                try:
                    raise KeyError('outer')
                except KeyError:
                    outer = sys.exc_info()[1]
                    if mode != 'true' and field in ('code', 'globals'):
                        def rejected():
                            with self.assertRaises(TypeError) as caught:
                                Function(**keywords)
                            position = 1 if field == 'code' else 2
                            self.assertEqual(caught.exception.args, ("Required argument '%s' (pos %d) not found" % (field, position),))
                        rejected()
                    else:
                        result = Function(**keywords)
                        self.assertEqual(result.__name__, 'sample' if field == 'name' and mode != 'true' else 'renamed')
                        self.assertEqual(result.func_defaults, None if field == 'argdefs' and mode != 'true' else (37,))
                    self.assertIs(sys.exc_info()[1], outer)
                # Unsuccessful pure-False dictionary probes can repeat according
                # to table layout. Check the lookup and result, not that count.
                self.assertTrue(events)
                self.assertEqual(set(events), set([field]))
        sys.exc_clear()

    def test_function_module_name_lookup_suppresses_callback_error(self):
        events = []
        failure = ValueError('module name')
        class Key(str):
            def __hash__(self):
                return str.__hash__(self)
            def __eq__(self, other):
                events.append((str(other), sys.exc_info()[0]))
                raise failure
        namespace = {Key('__name__'): 'ignored'}
        try:
            raise KeyError('outer')
        except KeyError:
            outer = sys.exc_info()[1]
            result = type(sample)(sample.func_code, namespace, argdefs=(41,))
            self.assertIs(result.__module__, None)
            self.assertEqual(result(), 41)
            self.assertIs(sys.exc_info()[1], outer)
        self.assertEqual(events, [('__name__', KeyError)])
        sys.exc_clear()

    def test_function_argument_type_error_precedes_later_keyword_callback(self):
        events = []
        class Key(str):
            def __hash__(self):
                return str.__hash__(self)
            def __eq__(self, other):
                events.append(other)
                return True
        with self.assertRaises(TypeError) as caught:
            type(sample)(17, **{Key('globals'): {}})
        self.assertEqual(caught.exception.args, ('function() argument 1 must be code, not int',))
        self.assertEqual(events, [])

    def test_reduce_methods_validate_optional_protocol_arguments(self):
        value = object()
        for name in ('__reduce__', '__reduce_ex__'):
            with self.assertRaises(TypeError) as caught:
                getattr(value, name)(0, 1)
            self.assertEqual(caught.exception.args, (name + '() takes at most 1 argument (2 given)',))
            with self.assertRaises(TypeError) as caught:
                getattr(value, name)(protocol=2)
            self.assertEqual(caught.exception.args, (name + '() takes no keyword arguments',))
            self.assertIs(type(getattr(value, name)()), tuple)

    def test_reduce_protocol_conversion_precedes_override(self):
        events, marker = [], object()
        class Protocol(long):
            def __int__(self):
                events.append('int')
                return 2
        class Subject(object):
            def __reduce__(self):
                events.append('reduce')
                return marker
        value = Subject()
        self.assertIs(value.__reduce_ex__(Protocol(0)), marker)
        self.assertEqual(events, ['int', 'reduce'])
        events[:] = []
        for protocol, kind, message in ((2.5, TypeError, 'integer argument expected, got float'),
                                        (2 ** 40, OverflowError, 'signed integer is greater than maximum'),
                                        (2 ** 70, OverflowError, 'Python int too large to convert to C long')):
            with self.assertRaises(kind) as caught:
                value.__reduce_ex__(protocol)
            self.assertEqual(caught.exception.args, (message,))
        self.assertEqual(events, [])

    def test_reduce_int_subtype_uses_stored_protocol(self):
        events = []
        class Protocol(int):
            def __int__(self):
                events.append('int')
                return 2
        result = object().__reduce_ex__(Protocol(0))
        self.assertEqual(len(result), 2)
        self.assertEqual(events, [])

    def test_reduce_reads_optional_methods_once(self):
        events = []
        class Subject(object):
            def __getattribute__(self, name):
                if name in ('__reduce__', '__class__', '__getnewargs__', '__getstate__'):
                    events.append(name)
                if name == '__getnewargs__':
                    return lambda: ()
                if name == '__getstate__':
                    return lambda: 43
                return object.__getattribute__(self, name)
        result = Subject().__reduce_ex__(2)
        self.assertEqual(result[2], 43)
        self.assertEqual(events, ['__reduce__', '__class__', '__class__', '__getnewargs__', '__getstate__'])

    def test_reduce_optional_lookup_errors_and_callback_error_identity(self):
        for failure in (ValueError('lookup'), KeyboardInterrupt('lookup')):
            events = []
            class Subject(object):
                def __getattribute__(self, name):
                    if name in ('__getnewargs__', '__getstate__'):
                        events.append((name, sys.exc_info()[0]))
                        raise failure
                    return object.__getattribute__(self, name)
            value = Subject()
            value.member = 47
            try:
                raise KeyError('outer')
            except KeyError:
                outer = sys.exc_info()[1]
                result = value.__reduce_ex__(2)
                self.assertEqual(result[2], {'member': 47})
                self.assertIs(sys.exc_info()[1], outer)
            self.assertEqual(events, [('__getnewargs__', KeyError), ('__getstate__', KeyError)])
        failure = ValueError('call')
        class Called(object):
            def __getnewargs__(self):
                raise failure
        with self.assertRaises(ValueError) as caught:
            Called().__reduce_ex__(2)
        self.assertIs(caught.exception, failure)
        sys.exc_clear()

    def test_reduce_getnewargs_none_and_wrong_result(self):
        class MissingCallable(object):
            __getnewargs__ = None
        class WrongResult(object):
            def __getnewargs__(self):
                return []
        for value, message in ((MissingCallable(), "'NoneType' object is not callable"),
                               (WrongResult(), "__getnewargs__ should return a tuple, not 'list'")):
            with self.assertRaises(TypeError) as caught:
                value.__reduce_ex__(2)
            self.assertEqual(caught.exception.args, (message,))

    def test_reduce_old_protocol_getters_run_once_and_propagate_lookup_errors(self):
        events = []
        class Subject(object):
            def __getattribute__(self, name):
                if name in ('__reduce__', '__class__', '__getstate__'):
                    events.append(name)
                if name == '__getstate__':
                    return lambda: 53
                return object.__getattribute__(self, name)
        for protocol in (0, 1):
            events[:] = []
            self.assertEqual(Subject().__reduce_ex__(protocol)[2], 53)
            self.assertEqual(events, ['__reduce__', '__class__', '__class__', '__class__', '__getstate__'])
        failure = KeyboardInterrupt('state lookup')
        class Raising(object):
            def __getattribute__(self, name):
                if name == '__getstate__':
                    raise failure
                return object.__getattribute__(self, name)
        with self.assertRaises(KeyboardInterrupt) as caught:
            Raising().__reduce_ex__(0)
        self.assertIs(caught.exception, failure)
        sys.exc_clear()

    def test_reduce_slots_keep_generic_dictionary_and_slot_lookup(self):
        events = []
        class Slotted(object):
            __slots__ = ('field',)
            def __getattribute__(self, name):
                if name in ('__getnewargs__', '__getstate__', '__dict__', 'field'):
                    events.append(name)
                return object.__getattribute__(self, name)
        value = Slotted()
        value.field = 59
        result = value.__reduce_ex__(2)
        self.assertEqual(result[2], (None, {'field': 59}))
        self.assertEqual(events, ['__getnewargs__', '__getstate__', '__dict__', 'field'])
        with self.assertRaises(TypeError) as caught:
            value.__reduce_ex__(0)
        self.assertEqual(caught.exception.args, ('a class that defines __slots__ without defining __getstate__ cannot be pickled',))

    def test_reduce_validates_slotnames_result_and_uses_own_list_cache(self):
        import copy_reg
        original = copy_reg._slotnames
        events = []
        class Subject(object):
            pass
        try:
            for returned in (None, [], (), 17, ''):
                def slotnames(cls):
                    events.append(cls)
                    return returned
                copy_reg._slotnames = slotnames
                events[:] = []
                if returned is None or isinstance(returned, list):
                    self.assertEqual(Subject().__reduce_ex__(2)[2], {})
                else:
                    with self.assertRaises(TypeError) as caught:
                        Subject().__reduce_ex__(2)
                    self.assertEqual(caught.exception.args, ("copy_reg._slotnames didn't return a list or None",))
                self.assertEqual(events, [Subject])
            class Cached(object):
                __slotnames__ = ['field']
            value = Cached()
            value.field = 61
            events[:] = []
            result = value.__reduce_ex__(2)
            self.assertEqual(result[2], ({'field': 61}, {'field': 61}))
            self.assertEqual(events, [])
            Cached.__slotnames__ = []
            self.assertEqual(value.__reduce_ex__(2)[2], {'field': 61})
            self.assertEqual(events, [])
        finally:
            copy_reg._slotnames = original

    def test_frame_exception_getters_report_saved_parent_state(self):
        sys.exc_clear()
        outer = KeyError('outer')
        inner = ValueError('inner')
        observed = []
        def inspect():
            frame = sys._getframe()
            try:
                raise inner
            except ValueError:
                observed.append((frame.f_exc_type, frame.f_exc_value, frame.f_exc_traceback is outer_state[2],
                                 sys.exc_info()[1] is inner))
        try:
            raise outer
        except KeyError:
            outer_state = sys.exc_info()
            inspect()
            self.assertIs(sys.exc_info()[1], outer)
            outer_state = None
        self.assertEqual(observed, [(KeyError, outer, True, True)])
        sys.exc_clear()

    def test_stopped_frame_exception_fields_support_identity_none_and_deletion(self):
        frame = stopped_frame()
        marker = object()
        for field in ('f_exc_type', 'f_exc_value', 'f_exc_traceback'):
            setattr(frame, field, marker)
            self.assertIs(getattr(frame, field), marker)
            setattr(frame, field, None)
            self.assertIs(getattr(frame, field), None)
            delattr(frame, field)
            self.assertIs(getattr(frame, field), None)
            descriptor = type(frame).__dict__[field]
            descriptor.__set__(frame, marker)
            self.assertIs(descriptor.__get__(frame), marker)
            descriptor.__delete__(frame)
            self.assertIs(getattr(frame, field), None)

    def test_frame_exception_fields_are_writable_getset_descriptors(self):
        frame = stopped_frame()
        for field in ('f_exc_type', 'f_exc_value', 'f_exc_traceback'):
            descriptor = type(frame).__dict__[field]
            self.assertEqual(type(descriptor).__name__, 'getset_descriptor')
            self.assertEqual(descriptor.__name__, field)
            self.assertIs(descriptor.__objclass__, type(frame))
            descriptor.__set__(frame, 17)
            self.assertEqual(descriptor.__get__(frame), 17)
            descriptor.__delete__(frame)

    def test_stopped_frame_finalizer_observes_cleared_exception_field(self):
        def replace(field, operation, kind):
            target, events = stopped_frame(), []
            class Previous(object):
                def __del__(self):
                    # Read a transient caller-locals snapshot rather than
                    # retaining the stopped frame in a callback closure.
                    namespace = sys._getframe(1).f_locals
                    try:
                        current = getattr(namespace['target'], field)
                        namespace['events'].append(current is None)
                    finally:
                        namespace.clear()
            setattr(target, field, Previous())
            incoming = None if kind == 'none' else object()
            if operation == 'attribute':
                setattr(target, field, incoming)
            else:
                type(target).__dict__[field].__set__(target, incoming)
            result = events[:], getattr(target, field) is incoming
            delattr(target, field)
            return result
        for field in ('f_exc_type', 'f_exc_value', 'f_exc_traceback'):
            for operation in ('attribute', 'descriptor'):
                for kind in ('object', 'none'):
                    self.assertEqual(replace(field, operation, kind), ([True], True))

    def test_frame_saved_state_can_be_cleared_deleted_or_replaced_by_real_triple(self):
        for mode in ('keep', 'none', 'delete', 'caught'):
            sys.exc_clear()
            outer, inner = KeyError('outer'), ValueError('inner')
            def run():
                frame = sys._getframe()
                try:
                    raise inner
                except ValueError:
                    self.assertIs(frame.f_exc_value, outer)
                    if mode == 'none':
                        frame.f_exc_type = frame.f_exc_value = frame.f_exc_traceback = None
                    elif mode == 'delete':
                        del frame.f_exc_type
                    elif mode == 'caught':
                        active = sys.exc_info()
                        frame.f_exc_type, frame.f_exc_value, frame.f_exc_traceback = active
            try:
                raise outer
            except KeyError:
                run()
                self.assertIs(sys.exc_info()[1], outer if mode == 'keep' else inner)
            sys.exc_clear()

    def test_frame_and_generator_readonly_fields_preserve_values(self):
        frame, generator = stopped_frame(), sample_generator()
        try:
            for field in ('f_back', 'f_code', 'f_builtins', 'f_globals', 'f_lasti'):
                original = getattr(frame, field)
                for operation in ('set', 'del'):
                    with self.assertRaises(TypeError) as caught:
                        if operation == 'set':
                            setattr(frame, field, 17)
                        else:
                            delattr(frame, field)
                    self.assertEqual(caught.exception.args, ('readonly attribute',))
                self.assertEqual(getattr(frame, field), original)
            for field in ('f_locals', 'f_restricted'):
                with self.assertRaises(AttributeError) as caught:
                    setattr(frame, field, 17)
                self.assertEqual(caught.exception.args, ("attribute '%s' of 'frame' objects is not writable" % field,))
            for field in ('gi_frame', 'gi_code', 'gi_running'):
                with self.assertRaises(TypeError) as caught:
                    setattr(generator, field, 17)
                self.assertEqual(caught.exception.args, ('readonly attribute',))
            with self.assertRaises(AttributeError) as caught:
                generator.__name__ = 'other'
            self.assertEqual(caught.exception.args, ("attribute '__name__' of 'generator' objects is not writable",))
            self.assertEqual(generator.next(), 17)
        finally:
            generator.close()
