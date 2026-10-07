"""Finite Python 2 module, class binding and directory protocol outcomes.

Namespaces contain at most four authored fields; recursive class directory
sequences contain at most one base. No cyclic GC or unrestricted recursion
behavior is asserted by this matrix.
"""

import sys


Module = type(sys)
events = []


class ModuleChild(Module):
    pass


class Name(str):
    pass


def emit(identity, callback):
    events[:] = []
    try:
        outcome = ('value', callback())
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    print identity + '\t' + repr(outcome) + '\t' + repr(events)
    sys.exc_clear()


module_arguments = ((), ('m',), ('m', None), ('m', 17), ('m', None, 7),
                    (u'm',), (None,), (17,), (Name('m'),), ('m\0tail',),
                    (False,), ('',))
module_keywords = ({}, {'name': 'keyword'}, {'doc': 23},
                   {'name': 'keyword', 'doc': 23}, {'name': 17},
                   {'unknown': 7}, {'unknown': 7, 'doc': 23},
                   {'name': 'keyword', 'unknown': 7}, {u'doc': None})


def module_state(value):
    return (value.__name__, type(value.__name__).__name__, value.__doc__,
            sorted(value.__dict__))


def constructed(factory, arguments, keywords):
    return module_state(factory(*arguments, **keywords))


def initialized(factory, arguments, keywords):
    value = factory('before', 'before-doc')
    value.marker = 29
    try:
        result = ('value', value.__init__(*arguments, **keywords))
    except BaseException as error:
        result = ('error', type(error).__name__, error.args)
    return result, module_state(value), value.marker


def allocated(factory, arguments, keywords):
    value = Module.__new__(factory, *arguments, **keywords)
    descriptor = Module.__dict__['__dict__']
    return (value.__dict__, descriptor.__get__(value, factory),
            hasattr(value, '__name__'), repr(value))


for factory_index, factory in enumerate((Module, ModuleChild)):
    for arguments_index, arguments in enumerate(module_arguments):
        for keywords_index, keywords in enumerate(module_keywords):
            for operation, callback in (('create', constructed),
                                        ('initialize', initialized),
                                        ('allocate', allocated)):
                emit('module/%d/%d/%d/%s' % (factory_index, arguments_index,
                     keywords_index, operation),
                     lambda: callback(factory, arguments, keywords))


class Old:
    pass


ClassFactory = type(Old)
class_arguments = ((), ('Created',), ('Created', ()),
                   ('Created', (), {}), ('Created', (), {}, 7),
                   (17,), (u'Created', (), {}), ('Created', [], {}),
                   ('Created', (), []), (Name('Created'), (Old,), {}))
class_keywords = ({}, {'name': 'Keyword'}, {'bases': ()}, {'dict': {}},
                  {'name': 'Keyword', 'bases': (), 'dict': {}},
                  {'bases': (), 'dict': {}}, {'unknown': 7},
                  {'name': 'Keyword', 'unknown': 7})


def class_constructed(direct, arguments, keywords):
    # Factories modify their namespace with defaults, so every row owns it.
    arguments = tuple(dict(value) if isinstance(value, dict) else value
                      for value in arguments)
    keywords = dict((key, dict(value) if isinstance(value, dict) else value)
                    for key, value in keywords.items())
    if direct:
        value = ClassFactory(*arguments, **keywords)
    else:
        value = ClassFactory.__new__(ClassFactory, *arguments, **keywords)
    return (value.__name__, type(value.__name__).__name__,
            [base.__name__ for base in value.__bases__],
            value.__dict__.get('__doc__'), value.__dict__.get('__module__'))


for direct in (False, True):
    for arguments_index, arguments in enumerate(class_arguments):
        for keywords_index, keywords in enumerate(class_keywords):
            emit('classic/%d/%d/%d' % (direct, arguments_index, keywords_index),
                 lambda: class_constructed(direct, arguments, keywords))


class Reported(object):
    known = 17


directory_settings = [None, None, None, None]


class Directory(object):
    def __getattribute__(self, name):
        events.append(name)
        for index, attribute in enumerate(('__dict__', '__members__',
                                            '__methods__', '__class__')):
            if name == attribute:
                value = directory_settings[index]
                if value == 'error':
                    raise KeyboardInterrupt(attribute)
                return value
        return object.__getattribute__(self, name)


def directory():
    return [name for name in dir(Directory()) if name in
            ('injected', 'known', 'member', 'method', 'unicode-member', 7)]


for dictionary_index, dictionary in enumerate(({}, {'injected': 17},
                                               {7: 17}, [], 'error')):
    for member_index, members in enumerate((None, ['member', u'unicode-member', 7],
                                            ('member',), 'error')):
        for method_index, methods in enumerate((None, ['method'], 'error')):
            for class_index, reported in enumerate((Reported, None, int, 'error')):
                directory_settings[:] = [dictionary, members, methods, reported]
                emit('directory/%d/%d/%d/%d' % (dictionary_index, member_index,
                     method_index, class_index), directory)


class Owner(object):
    def method(self):
        return 'called'


reported_class = Owner


class Receiver(object):
    @property
    def __class__(self):
        events.append('receiver.class')
        if reported_class in (AttributeError, KeyError, KeyboardInterrupt):
            raise reported_class('reported class')
        return reported_class


for reported_index, reported_class in enumerate((Owner, int, None, Old,
                                                 AttributeError, KeyError,
                                                 KeyboardInterrupt)):
    emit('receiver/%d' % reported_index, lambda: Owner.method(Receiver()))


class Dynamic:
    def __getattr__(self, name):
        events.append(name)
        if dynamic_failure is not None:
            raise dynamic_failure('dynamic lookup')
        return dynamic_value


for failure_index, dynamic_failure in enumerate((None, AttributeError,
                                                  KeyError, KeyboardInterrupt)):
    for value_index, dynamic_value in enumerate((None, 17, False, lambda: 23)):
        emit('callable/%d/%d' % (failure_index, value_index),
             lambda: callable(Dynamic()))


class Namespace(object):
    @property
    def __dict__(self):
        events.append('namespace.dict')
        if namespace_failure is not None:
            raise namespace_failure('namespace lookup')
        return namespace_value


for failure_index, namespace_failure in enumerate((None, AttributeError,
                                                   KeyError, KeyboardInterrupt)):
    for value_index, namespace_value in enumerate((None, 17, [], {'marker': 23})):
        emit('vars/%d/%d' % (failure_index, value_index),
             lambda: vars(Namespace()))


for trigger_index, trigger in enumerate(('_private', 'public')):
    for removed_index, removed in enumerate(('_other', 'other')):
        value = Module('temporary')
        namespace = value.__dict__
        class Cleanup(object):
            def __del__(self):
                events.append('cleanup')
                namespace.pop(removed, None)
        namespace[removed] = 17
        namespace[trigger] = Cleanup()
        events[:] = []
        del value
        print 'module-clear/%d/%d\t%s\t%s' % (
            trigger_index, removed_index,
            repr((removed in namespace, namespace[trigger])), repr(events))
        namespace.clear()
