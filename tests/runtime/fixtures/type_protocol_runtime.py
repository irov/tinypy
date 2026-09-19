from _functools import partial
import sys


class MetadataTarget(object):
    pass


metadata = MetadataTarget.__dict__
assert type(metadata).__name__ == "dictproxy"
assert metadata["__module__"] == __name__
assert metadata.get("missing", 42) == 42
assert "__dict__" in metadata
metadata_copy = metadata.copy()
assert type(metadata_copy) is dict
MetadataTarget.live_value = 41
assert metadata["live_value"] == 41
try:
    metadata["forbidden"] = 1
except TypeError:
    pass
else:
    raise AssertionError("dictproxy accepted item assignment")
try:
    metadata.clear()
except AttributeError:
    pass
else:
    raise AssertionError("dictproxy exposed a mutating dict method")
assert MetadataTarget.__bases__ is MetadataTarget.__bases__
assert MetadataTarget.__mro__ is MetadataTarget.__mro__
assert type.__bases__ is type.__bases__
assert type.__mro__ is type.__mro__


class SentinelCallable(object):
    def __init__(self):
        self.value = 0

    def __call__(self):
        self.value += 1
        return self.value


callable_iterator = iter(SentinelCallable(), 3)
assert type(callable_iterator).__name__ == "callable-iterator"
assert not hasattr(callable_iterator, "__length_hint__")
assert list(callable_iterator) == [1, 2]

assert type(iter([])).__name__ == "listiterator"
assert type(iter(())).__name__ == "tupleiterator"
assert type(iter({})).__name__ == "dictionary-keyiterator"
assert type({}.itervalues()).__name__ == "dictionary-valueiterator"
assert type({}.iteritems()).__name__ == "dictionary-itemiterator"
assert type(iter(set())).__name__ == "setiterator"
assert type(iter(xrange(1))).__name__ == "rangeiterator"
assert iter([1, 2]).__length_hint__() == 2
assert type(type(iter([])).next).__name__ == "wrapper_descriptor"
assert type(iter([])).next.__objclass__ is type(iter([]))
assert type(object.__format__).__name__ == "method_descriptor"
assert type(object.__sizeof__).__name__ == "method_descriptor"
assert type(list.__reversed__).__name__ == "method_descriptor"
assert type(object.__repr__).__name__ == "wrapper_descriptor"
assert type(list.__getitem__).__name__ == "method_descriptor"
assert type(dict.__getitem__).__name__ == "method_descriptor"
assert type(dict.__contains__).__name__ == "method_descriptor"
assert type(set.__contains__).__name__ == "method_descriptor"
assert type(frozenset.__contains__).__name__ == "method_descriptor"
assert int.__pow__(2, 3, 5) == 3
assert long.__pow__(2L, 3L, 5L) == 3L
for sized_type in (long, str, unicode, list, dict, set, frozenset, bytearray):
    assert type(sized_type.__sizeof__).__name__ == "method_descriptor"
    assert sized_type.__sizeof__.__objclass__ is sized_type

growing_list = []
empty_list_size = growing_list.__sizeof__()
growing_list.extend(range(20))
assert growing_list.__sizeof__() > empty_list_size

growing_dict = {}
empty_dict_size = growing_dict.__sizeof__()
growing_dict.update((index, index) for index in range(20))
assert growing_dict.__sizeof__() > empty_dict_size

growing_set = set()
empty_set_size = growing_set.__sizeof__()
growing_set.update(range(20))
assert growing_set.__sizeof__() > empty_set_size

growing_bytearray = bytearray()
empty_bytearray_size = growing_bytearray.__sizeof__()
growing_bytearray.extend(range(20))
assert growing_bytearray.__sizeof__() > empty_bytearray_size


def add(a, b=2):
    return a + b


code = add.func_code
code_type = type(code)
code_copy = code_type(
    code.co_argcount,
    code.co_nlocals,
    code.co_stacksize,
    code.co_flags,
    code.co_code,
    code.co_consts,
    code.co_names,
    code.co_varnames,
    code.co_filename,
    code.co_name,
    code.co_firstlineno,
    code.co_lnotab,
    code.co_freevars,
    code.co_cellvars,
)
assert code_copy == code
assert code_type.__eq__(code_copy, code) is True
assert code_type.__cmp__(code_copy, code) == 0
assert hash(code_copy) == hash(code)
assert code_type.__hash__(code_copy) == hash(code_copy)
assert code_type.__repr__(code) == repr(code)
assert repr(code).startswith("<code object add at 0x")


def copy_code(original, arg_count=None, local_count=None, stack_size=None, consts=None):
    if arg_count is None:
        arg_count = original.co_argcount
    if local_count is None:
        local_count = original.co_nlocals
    if stack_size is None:
        stack_size = original.co_stacksize
    if consts is None:
        consts = original.co_consts
    return code_type(
        arg_count,
        local_count,
        stack_size,
        original.co_flags,
        original.co_code,
        consts,
        original.co_names,
        original.co_varnames,
        original.co_filename,
        original.co_name,
        original.co_firstlineno,
        original.co_lnotab,
        original.co_freevars,
        original.co_cellvars,
    )


different_stack = copy_code(code, stack_size=code.co_stacksize + 1)
assert different_stack == code
assert code_type.__cmp__(different_stack, code) == 0
assert hash(different_stack) == hash(code)
assert copy_code(code, consts=(1,)) != copy_code(code, consts=(1.0,))
for invalid_count in (-1,):
    try:
        copy_code(code, arg_count=invalid_count)
    except ValueError:
        pass
    else:
        raise AssertionError("code accepted a negative argument count")
    try:
        copy_code(code, local_count=invalid_count)
    except ValueError:
        pass
    else:
        raise AssertionError("code accepted a negative local count")

function_type = type(add)
function_copy = function_type(code, globals(), "function_copy", (5,))
assert function_copy(7) == 12
assert function_type.__call__(function_copy, 8) == 13
assert function_type.__repr__(function_copy) == repr(function_copy)
assert repr(function_copy).startswith("<function function_copy at 0x")
function_keyword = function_type(code=code, globals=globals(), name=None, argdefs=(6,))
assert function_keyword(7) == 13
assert function_keyword.func_name == "add"


def make_cell(value):
    def get_value():
        return value

    return get_value.func_closure[0]


left_cell = make_cell(41)
right_cell = make_cell(41)
larger_cell = make_cell(42)
cell_type = type(left_cell)
assert left_cell.cell_contents == 41
assert left_cell == right_cell
assert left_cell < larger_cell
assert "__eq__" not in cell_type.__dict__
assert cell_type.__cmp__(left_cell, larger_cell) < 0
assert cell_type.__repr__(left_cell) == repr(left_cell)
assert repr(left_cell).startswith("<cell at 0x")
try:
    hash(left_cell)
except TypeError:
    pass
else:
    raise AssertionError("cell remained hashable")

module_type = type(sys)
module_value = module_type("runtime_module", "runtime doc")
assert module_value.__name__ == "runtime_module"
assert module_value.__doc__ == "runtime doc"
module_value.answer = 42
assert module_value.__dict__["answer"] == 42
assert module_type.__repr__(module_value) == repr(module_value)
assert repr(module_value) == "<module 'runtime_module' (built-in)>"
module_value.__file__ = "/tmp/runtime.py"
assert repr(module_value) == "<module 'runtime_module' from '/tmp/runtime.py'>"
module_value.__name__ = "renamed_module"
assert repr(module_value) == "<module 'renamed_module' from '/tmp/runtime.py'>"
module_value.__name__ = 42
assert repr(module_value) == "<module '?' from '/tmp/runtime.py'>"


class ModuleSubclass(module_type):
    pass


module_subclass = ModuleSubclass("runtime_submodule")
module_subclass.marker = 7
assert type(module_subclass) is ModuleSubclass
assert module_subclass.__dict__["marker"] == 7


class SuperBase(object):
    def marker(self):
        return 42


class SuperChild(SuperBase):
    pass


SuperChild.parent = super(SuperChild)
super_child = SuperChild()
bound_parent = super_child.parent
assert bound_parent.marker() == 42
assert bound_parent.__thisclass__ is SuperChild
assert bound_parent.__self__ is super_child
assert bound_parent.__self_class__ is SuperChild
assert type(bound_parent).__get__(SuperChild.parent, super_child, SuperChild).marker() == 42
assert type(bound_parent).__repr__(bound_parent) == repr(bound_parent)


def combine(a, b, c=0):
    return a + b + c


bound = partial(combine, 10, c=3)
assert type(bound).__name__ == "partial"
assert type(bound).__module__ == "functools"
assert "__dict__" in dir(bound)
assert partial.__call__(bound, 4) == 17
reduction = bound.__reduce__()
assert reduction[0] is partial
assert reduction[1] == (combine,)
assert reduction[2][0] is combine
assert reduction[2][1] == (10,)
assert reduction[2][2] == {"c": 3}
restored = partial(combine)
assert restored.__setstate__(reduction[2]) is None
assert restored(4) == 17


def mutate_keywords(**keywords):
    keywords["mutated"] = True
    return keywords


isolated_keywords = partial(mutate_keywords, saved=1)
assert isolated_keywords() == {"saved": 1, "mutated": True}
assert isolated_keywords.keywords == {"saved": 1}


class PartialSubclass(partial):
    pass


partial_subclass = PartialSubclass(combine, 20, c=2)
partial_subclass.label = "subclass"
assert type(partial_subclass) is PartialSubclass
assert partial_subclass(3) == 25
assert partial_subclass.__dict__ == {"label": "subclass"}
assert partial_subclass.__reduce__()[0] is PartialSubclass
for attribute in ("func", "args", "keywords"):
    try:
        setattr(bound, attribute, None)
    except (AttributeError, TypeError):
        pass
    else:
        raise AssertionError("partial exposed a writable structural field")

exec_globals = {}
exec_locals = {}
exec "created = 42" in exec_globals, exec_locals
assert "__builtins__" in exec_globals
assert "__builtins__" not in exec_locals
assert exec_locals["created"] == 42


# Types defined in Python report their module; built-in and extension types
# keep the bare form, and the built-in exceptions live in their own module.
class RepresentedType(object):
    pass


class RepresentedSubclass(list):
    pass


assert repr(RepresentedType) == "<class '%s.RepresentedType'>" % __name__
assert repr(RepresentedSubclass) == "<class '%s.RepresentedSubclass'>" % __name__
assert str(RepresentedType) == repr(RepresentedType)
assert repr(int) == "<type 'int'>"
assert repr(object) == "<type 'object'>"
assert repr(type) == "<type 'type'>"
assert repr(xrange) == "<type 'xrange'>"
assert repr(Exception) == "<type 'exceptions.Exception'>"
assert repr(ValueError) == "<type 'exceptions.ValueError'>"
assert Exception.__module__ == "exceptions"
assert ValueError.__module__ == "exceptions"
assert int.__module__ == "__builtin__"
assert RepresentedType.__module__ == __name__


class RepresentedError(Exception):
    pass


assert repr(RepresentedError) == "<class '%s.RepresentedError'>" % __name__
assert RepresentedError.__module__ == __name__


# Instances and methods of types defined in Python report the defining module,
# and a bound classmethod carries the metaclass as its owner.
class RepresentedInstance(object):
    def method(self):
        return 1

    @classmethod
    def class_method(cls):
        return cls.__name__


represented = RepresentedInstance()
assert repr(represented).startswith("<%s.RepresentedInstance object at 0x" % __name__)
assert repr(represented).endswith(">")
assert str(represented) == repr(represented)
assert repr(object()).startswith("<object object at 0x")
assert repr(RepresentedInstance.method) == "<unbound method RepresentedInstance.method>"
assert repr(represented.method).startswith("<bound method RepresentedInstance.method of <%s.RepresentedInstance object at 0x" % __name__)
assert repr(RepresentedInstance.class_method) == "<bound method type.class_method of <class '%s.RepresentedInstance'>>" % __name__
assert RepresentedInstance.class_method.im_class is type
assert RepresentedInstance.class_method.im_self is RepresentedInstance
assert RepresentedInstance.class_method() == "RepresentedInstance"

try:
    RepresentedInstance.missing
except AttributeError, missing_error:
    assert str(missing_error) == "type object 'RepresentedInstance' has no attribute 'missing'"
else:
    raise AssertionError("missing type attribute did not raise")
