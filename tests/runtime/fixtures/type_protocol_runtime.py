from _functools import partial
from _weakref import ref
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
# Without a cyclic collector a type cannot own a tuple that references it, so
# __mro__ is handed out as an owning copy rather than the shared object.
assert MetadataTarget.__mro__ == MetadataTarget.__mro__
assert MetadataTarget.__mro__ == (MetadataTarget, object)
assert type.__bases__ is type.__bases__
assert type.__mro__ == (type, object)


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


# A key comparison that changes what a lookup reads: the bases of a class, the
# __dict__ of a classic class in a new-style MRO or that of an exception.
# CPython 2.7 goes on reading the MRO or dictionary it freed, so these checks
# are tinypy-only.
key_actions = []


class ActingKey(object):
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        if key_actions:
            key_actions.pop()()
        return False


def arm_acting_key(namespace, name, action):
    namespace[ActingKey(name)] = "key"
    key_actions.append(action)


class RebasedBase(object):
    attr = "base"


class RebasedOther(object):
    attr = "other"


RebasedMiddle = type("RebasedMiddle", (RebasedBase,), {ActingKey("attr"): "key"})


class RebasedDerived(RebasedMiddle):
    pass


# A type lookup finishes on the MRO it started with, which it holds as the
# _PyType_Lookup of CPython 3 does; a nested __bases__ assignment changes the
# lookups that follow.
nested_lookups = []


def rebase_during_lookup():
    nested_lookups.append(RebasedDerived().attr)
    RebasedDerived.__bases__ = (RebasedOther,)


key_actions.append(rebase_during_lookup)
assert RebasedDerived().attr == "base"
assert nested_lookups == ["base"]
assert RebasedDerived.__mro__ == (RebasedDerived, RebasedOther, object)
assert RebasedDerived().attr == "other"


class ReplacedBase(object):
    attr = "base"


class ReplacedClassic:
    pass


class ReplacedDerived(ReplacedClassic, ReplacedBase):
    pass


def replace_classic_dict():
    ReplacedClassic.__dict__ = {}


for replaced_lookup in (lambda: ReplacedDerived().attr, lambda: ReplacedDerived.attr, lambda: super(ReplacedDerived, ReplacedDerived()).attr):
    arm_acting_key(ReplacedClassic.__dict__, "attr", replace_classic_dict)
    assert replaced_lookup() == "base"
    assert ReplacedClassic.__dict__ == {}

replaced_error = Exception("first")


def replace_error_dict():
    replaced_error.__dict__ = {}


arm_acting_key(replaced_error.__dict__, "message", replace_error_dict)
assert replaced_error.message == "first"
arm_acting_key(replaced_error.__dict__, "message", replace_error_dict)
replaced_error.message = "second"
assert (replaced_error.__dict__, replaced_error.message) == ({}, "first")
arm_acting_key(replaced_error.__dict__, "message", replace_error_dict)
del replaced_error.message
assert replaced_error.__dict__ == {}
try:
    replaced_error.message
except AttributeError, message_error:
    assert str(message_error) == "message attribute was deleted"
else:
    raise AssertionError("deleted message did not raise")


# A lookup never restarts: a comparison that rebases the class or replaces a
# classic __dict__ on every probe still lets it finish on what it holds.
renewing_calls = []


class Renewing(object):
    def __init__(self, renew):
        self.renew = renew

    def __hash__(self):
        return hash("attr")

    def __eq__(self, other):
        renewing_calls.append(other)
        if len(renewing_calls) < 1000:
            self.renew()
        return False


class RenewedFirst(object):
    attr = "first"


class RenewedSecond(object):
    attr = "second"


class RenewedMiddle(RenewedFirst):
    pass


def renew_bases():
    RenewedMiddle.__bases__ = (RenewedSecond,) if RenewedMiddle.__bases__[0] is RenewedFirst else (RenewedFirst,)


RenewedDerived = type("RenewedDerived", (RenewedMiddle,), {Renewing(renew_bases): 1})
assert RenewedDerived().attr == "first"
assert len(renewing_calls) == 2
del renewing_calls[:]
assert getattr(RenewedDerived, "missing", "missing") == "missing"
assert len(renewing_calls) == 0


class RenewedClassic:
    pass


def renew_classic_dict():
    RenewedClassic.__dict__ = {Renewing(renew_classic_dict): 1}


class RenewedBase(object):
    attr = "base"


class RenewedMixed(RenewedClassic, RenewedBase):
    pass


renew_classic_dict()
del renewing_calls[:]
assert RenewedMixed().attr == "base"
assert len(renewing_calls) == 1
del renewing_calls[:]
assert getattr(RenewedMixed(), "attr", "missing") == "base"
assert len(renewing_calls) == 1


# The class of the last instance is freed by a comparison of the __getattr__
# probe; the lookup holds it, and its MRO, until the probe is done.
freed_holder = []


class FreedOther(object):
    pass


class FreedBase(object):
    pass


class FreedRebase(object):
    pass


def free_class():
    freed_holder[0].__class__ = FreedOther


def rebase_and_free_class():
    type(freed_holder[0]).__bases__ = (FreedRebase,)
    freed_holder[0].__class__ = FreedOther


for free_action in (free_class, rebase_and_free_class):
    freed_holder.append(type("Freed", (FreedBase,), {ActingKey("__getattr__"): 1})())
    key_actions.append(free_action)
    try:
        freed_holder[0].missing
    except AttributeError:
        pass
    else:
        raise AssertionError("missing attribute of a freed class did not raise")
    assert type(freed_holder.pop()) is FreedOther


# The keyword values of a constructor are owned as soon as they are read: the
# dictionary apply() passes on may be cleared by the comparison of a later
# keyword or of a special method looked up on a value.
cleared_keywords = {}


class ClearingKeyword(object):
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        return hash(self.name)

    def __eq__(self, other):
        cleared_keywords.clear()
        return False


def apply_cleared(function, args, make_keywords):
    cleared_keywords.update(make_keywords())
    try:
        return apply(function, args, cleared_keywords)
    finally:
        cleared_keywords.clear()


def clearing_value(base, special, value):
    return type("Clearing", (base,), {ClearingKeyword(special): 1})(value)


assert apply_cleared(int, (), lambda: {"x": "".join(["1", "2", "3"]), ClearingKeyword("base"): 1}) == 123
assert apply_cleared(long, (), lambda: {"x": "".join(["1", "2", "3"]), ClearingKeyword("base"): 1}) == 123L
assert apply_cleared(complex, (), lambda: {"real": float("1.5") + 0.25, ClearingKeyword("imag"): 1}) == 1.75
assert apply_cleared(unicode, (), lambda: {"string": "".join(["a", "b"]), "encoding": "".join(["as", "cii"]), ClearingKeyword("errors"): 1}) == u"ab"
assert apply_cleared(bytearray, (u"abc",), lambda: {"encoding": "".join(["as", "cii"]), ClearingKeyword("errors"): 1}) == bytearray("abc")
assert apply_cleared(sys.getsizeof, (), lambda: {"object": [1], ClearingKeyword("default"): 1}) > 0
assert apply_cleared("abc".encode, (), lambda: {"encoding": "".join(["as", "cii"]), ClearingKeyword("errors"): 1}) == "abc"
assert apply_cleared(float, (), lambda: {"x": clearing_value(str, "__float__", "1.5")}) == 1.5
assert apply_cleared(int, (), lambda: {"x": clearing_value(str, "__int__", "15")}) == 15
assert apply_cleared(str, (), lambda: {"object": clearing_value(list, "__str__", [1])}) == "[1]"
assert apply_cleared(list, (), lambda: {"sequence": clearing_value(tuple, "__iter__", (1, 2))}) == [1, 2]
assert apply_cleared(tuple, (), lambda: {"sequence": clearing_value(list, "__iter__", [1, 2])}) == (1, 2)


# A class that a custom mro() adds outside the bases is no base, so its
# changes do not reach the class through the subclass lists; tinypy gives
# such a class no version and follows those changes, where the method cache
# of CPython 2.7 may keep the old answer.
class OutsideMroClass(object):
    value = "outside"


class OutsideMroMeta(type):
    def mro(cls):
        return [cls, OutsideMroClass, object]


class OutsideMroUser(object):
    __metaclass__ = OutsideMroMeta


def read_outside_mro(target):
    return getattr(target, "value", None)


outside_user = OutsideMroUser()
assert [(read_outside_mro(outside_user), read_outside_mro(OutsideMroUser)) for _ in range(2)] == [("outside", "outside")] * 2
OutsideMroClass.value = "changed"
assert (read_outside_mro(outside_user), read_outside_mro(OutsideMroUser)) == ("changed", "changed")
del OutsideMroClass.value
assert (read_outside_mro(outside_user), read_outside_mro(OutsideMroUser)) == (None, None)


# Special methods a classic base gains after a new-style class derives from it
# are found by the dynamic lookup of tinypy; CPython 2.7 keeps the slots the
# new-style class filled when it was created (see SPEC.md).
class LateClassic:
    pass


class LateMixed(LateClassic, object):
    pass


class LateOwner(object):
    attribute = LateMixed()


def late_length(target):
    try:
        return len(target)
    except TypeError:
        return None


late_instance = LateMixed()
assert [late_length(late_instance) for _ in range(2)] == [None] * 2
assert [type(LateOwner().attribute) for _ in range(2)] == [LateMixed] * 2
LateClassic.__len__ = lambda self: 5
LateClassic.__get__ = lambda self, instance, owner: "late get"
assert late_length(late_instance) == 5
assert (LateOwner().attribute, LateOwner.attribute) == ("late get", "late get")
LateClassic.__dict__["__len__"] = lambda self: 6
assert late_length(late_instance) == 6
LateClassic.__dict__ = {}
assert late_length(late_instance) is None
assert type(LateOwner().attribute) is LateMixed


# Python code that a failing __bases__ assignment runs may create a class from
# the MRO the assignment then restores; the class keeps its MRO, as in CPython
# 2.7, and its caches follow every class of it, where the method cache of
# CPython 2.7 keeps an attribute of a class that no longer reaches it.
class RollbackBase(object):
    pass


class RollbackFirst(RollbackBase):
    pass


class RollbackSecond(RollbackBase):
    pass


class RollbackPayload(object):
    pass


rollback_state = {"made": None, "armed": False}


class RollbackMeta(type):
    pass


class RollbackFailingMeta(RollbackMeta):
    def mro(cls):
        if rollback_state["armed"] and cls.__name__ == "RollbackChild":
            rollback_state["armed"] = False
            class RollbackNew(RollbackRebased):
                pass
            rollback_state["made"] = RollbackNew
            raise ValueError("fail the rebase")
        return type.mro(cls)


def payload_name(target, name):
    try:
        return type(getattr(target, name)).__name__
    except AttributeError:
        return None


def mro_names(cls):
    return [entry.__name__ for entry in cls.__mro__]


RollbackRebased = RollbackMeta("RollbackRebased", (RollbackSecond,), {})
RollbackChild = RollbackFailingMeta("RollbackChild", (RollbackRebased,), {})
RollbackFirst.value = RollbackPayload()
rollback_state["armed"] = True
try:
    RollbackRebased.__bases__ = (RollbackFirst,)
except ValueError:
    pass
else:
    raise AssertionError("the __bases__ assignment should fail")
rollback_new = rollback_state["made"]()
rollback_state["made"] = None
assert mro_names(RollbackRebased) == ["RollbackRebased", "RollbackSecond", "RollbackBase", "object"]
assert mro_names(type(rollback_new)) == ["RollbackNew", "RollbackRebased", "RollbackFirst", "RollbackBase", "object"]
assert [payload_name(rollback_new, "value") for _ in range(2)] == ["RollbackPayload"] * 2
del RollbackFirst.value
assert [payload_name(rollback_new, "value") for _ in range(2)] == [None] * 2


# A __bases__ assignment that Python code runs while another one computes the
# MROs may change the MRO of a new base that does not list the type yet: the
# type keeps an MRO through the classes that base had, as in CPython 2.7, and
# its caches follow them.
class NestedOld(object):
    pass


class NestedNew(object):
    pass


class NestedBase(NestedOld):
    pass


class NestedOriginal(object):
    pass


nested_state = {"armed": False}


class NestedMeta(type):
    pass


class NestedTriggerMeta(NestedMeta):
    def mro(cls):
        if nested_state["armed"] and cls.__name__ == "NestedChild":
            nested_state["armed"] = False
            NestedBase.__bases__ = (NestedNew,)
        return type.mro(cls)


NestedRebased = NestedMeta("NestedRebased", (NestedOriginal,), {})
NestedChild = NestedTriggerMeta("NestedChild", (NestedRebased,), {})
NestedOld.value = RollbackPayload()
nested_state["armed"] = True
NestedRebased.__bases__ = (NestedBase,)
nested_instance = NestedRebased()
assert mro_names(NestedRebased) == ["NestedRebased", "NestedBase", "NestedOld", "object"]
assert mro_names(NestedBase) == ["NestedBase", "NestedNew", "object"]
assert [payload_name(nested_instance, "value") for _ in range(2)] == ["RollbackPayload"] * 2
del NestedOld.value
assert [payload_name(nested_instance, "value") for _ in range(2)] == [None] * 2


# type_call asks the class of the instance that __new__ returned for
# __init__. A key comparison of that lookup that reassigns the __class__ of
# the instance releases the class, and an __init__ only that class held is
# then not found; CPython 2.7 reaches __init__ through tp_init.
constructor_state = {"instance": None, "armed": False, "calls": []}


class ConstructorOther(object):
    pass


class ConstructorOtherError(Exception):
    pass


class ConstructorKey(object):
    def __hash__(self):
        return hash("__init__")

    def __eq__(self, other):
        if constructor_state["armed"]:
            constructor_state["armed"] = False
            instance = constructor_state["instance"]
            constructor_state["instance"] = None
            instance.__class__ = ConstructorOtherError if isinstance(instance, Exception) else ConstructorOther
        return False


def shared_initializer(self, *args):
    constructor_state["calls"].append("shared")


def constructor_owner(base, initializer):
    class Owner(base):
        def __new__(cls, *args):
            made = type("Made", (Owner,), {ConstructorKey(): 1})
            if initializer == "own":
                made.__init__ = lambda self, *args: constructor_state["calls"].append("own")
            elif initializer == "shared":
                made.__init__ = shared_initializer
            instance = base.__new__(made, *args)
            constructor_state["instance"] = instance
            constructor_state["armed"] = True
            return instance
    return Owner


def construct(base, initializer, *args):
    constructor_state["calls"] = []
    instance = constructor_owner(base, initializer)(*args)
    return type(instance).__name__, constructor_state["calls"], constructor_state["armed"]


assert construct(object, None) == ("ConstructorOther", [], False)
assert construct(object, "own") == ("ConstructorOther", [], False)
assert construct(object, "shared") == ("ConstructorOther", ["shared"], False)
assert construct(Exception, None, "x") == ("ConstructorOtherError", [], False)
assert construct(Exception, "own", "x") == ("ConstructorOtherError", [], False)
assert construct(Exception, "shared", "x") == ("ConstructorOtherError", ["shared"], False)


# A base keeps the cleared references of its dead subclasses until they are
# half of its list, and lists the others in their order.
class Registry(object):
    pass


def registry_names():
    return [cls.__name__ for cls in Registry.__subclasses__()]


registered = [type("Registered%d" % index, (Registry,), {}) for index in range(8)]
for index in (3, 0, 7):
    registered[index] = None
assert registry_names() == ["Registered1", "Registered2", "Registered4", "Registered5", "Registered6"]
registered.append(type("Registered8", (Registry,), {}))
for index in (1, 5, 2):
    registered[index] = None
assert registry_names() == ["Registered4", "Registered6", "Registered8"]
del registered[:]
assert registry_names() == []


# A class keeps the weak references through which its bases list it until the
# __del__ of its metaclass ran, as type_dealloc clears them after
# subtype_dealloc called it: a class that __del__ resurrects stays listed, a
# change to its base still reaches its lookup caches and constructor memos, and
# the callbacks of its weak references run once it dies after all. CPython 2.7
# keeps classes in reference cycles, which never die that way.
resurrection_events = []
resurrected_classes = []


class ResurrectingMeta(type):
    def __del__(cls):
        resurrection_events.append("del")
        if len(resurrection_events) == 1:
            resurrected_classes.append(cls)


class ResurrectionBase(object):
    pass


class ResurrectionPayload(object):
    pass


def resurrection_init(self):
    self.tag = "init"


def resurrection_read(value):
    return type(value.attr).__name__


ResurrectionBase.attr = ResurrectionPayload()
ResurrectionBase.__init__ = resurrection_init
resurrection_reference = ref(ResurrectingMeta("Resurrected", (ResurrectionBase,), {}), lambda reference: resurrection_events.append("callback"))
assert resurrection_events == ["del"]
resurrected = resurrected_classes[0]
assert resurrection_reference() is resurrected
assert ResurrectionBase.__subclasses__() == [resurrected]
resurrected_instance = resurrected()
assert resurrection_read(resurrected_instance) == resurrection_read(resurrected_instance) == "ResurrectionPayload"
assert resurrected_instance.tag == resurrected().tag == "init"
del ResurrectionBase.attr
del ResurrectionBase.__init__
try:
    resurrection_read(resurrected_instance)
except AttributeError:
    pass
else:
    raise AssertionError("a resurrected class kept a deleted base attribute")
assert not hasattr(resurrected(), "tag")
del resurrected_instance, resurrected, resurrected_classes[:]
assert resurrection_events == ["del", "del", "callback"]
assert resurrection_reference() is None
assert ResurrectionBase.__subclasses__() == []


# The default MRO merges what the iteration of the bases gives, which a tuple
# subclass may choose, as mro_implementation does with PySequence_List. A type
# it adds outside the MROs of the bases is no registered base, so lookups in
# that MRO are not cached; CPython 2.7 caches them and keeps a deleted
# attribute. A non-class, which CPython puts in the MRO and then reads as a
# type, is refused.
class IterationAdded(object):
    marker = ResurrectionPayload()


class AddingBases(tuple):
    def __iter__(self):
        return iter((IterationAdded,))


added = type("Added", AddingBases((object,)), {})
assert added.__mro__ == (added, object, IterationAdded)
added_instance = added()


def added_marker(value):
    return type(value.marker).__name__


assert added_marker(added_instance) == added_marker(added_instance) == "ResurrectionPayload"
del IterationAdded.marker
try:
    added_marker(added_instance)
except AttributeError:
    pass
else:
    raise AssertionError("a type added by the bases iteration kept a deleted attribute")


class NonClassBases(tuple):
    def __iter__(self):
        return iter((len,))


class NonClassMeta(type):
    pass


def bases_error(call):
    try:
        call()
    except TypeError as error:
        return str(error)
    raise AssertionError("a non-class entered an MRO")


assert bases_error(lambda: type("Refused", NonClassBases((object,)), {})) == "bases must be types"
assert bases_error(lambda: NonClassMeta("Refused", NonClassBases((object,)), {})) == "bases must be types"


class SwitchingBases(tuple):
    non_class = False

    def __iter__(self):
        if SwitchingBases.non_class:
            return iter((len,))
        return tuple.__iter__(self)


switching = type("Switching", SwitchingBases((object,)), {})
switching_bases = switching.__bases__
SwitchingBases.non_class = True
assert bases_error(lambda: type.mro(switching)) == "bases must be types"
assert bases_error(lambda: setattr(switching, "__bases__", SwitchingBases((object,)))) == "bases must be types"
assert switching.__bases__ is switching_bases
assert switching.__mro__ == (switching, object)
SwitchingBases.non_class = False


# A type the iteration adds must lay its instances out as a base does: a
# dict or a list there would read the object instances of the class.
class LayoutBases(tuple):
    added = dict

    def __iter__(self):
        return iter(list(tuple.__iter__(self)) + [LayoutBases.added])


layout_error = "mro() returned base with unsuitable layout ('%s')"
assert bases_error(lambda: type("Refused", LayoutBases((object,)), {})) == layout_error % "dict"
assert bases_error(lambda: NonClassMeta("Refused", LayoutBases((object,)), {})) == layout_error % "dict"
LayoutBases.added = IterationAdded
laid_out = type("LaidOut", LayoutBases((object,)), {})
assert laid_out.__mro__ == (laid_out, object, IterationAdded)
LayoutBases.added = list
assert bases_error(lambda: type.mro(laid_out)) == layout_error % "list"
assert bases_error(lambda: setattr(laid_out, "__bases__", LayoutBases((object,)))) == layout_error % "list"
assert laid_out.__mro__ == (laid_out, object, IterationAdded)


# After a __bases__ assignment the type and each descendant get their MRO
# once: the descendants breadth-first in subclass order, each after its bases
# that are recomputed, so a custom mro() sees their new MROs. CPython 2.7's
# mro_subclasses walks tp_subclasses depth-first and calls the hook of a
# descendant once per path, the first time before its other base has its new
# MRO: OrderA, OrderB, OrderC, OrderE (stale OrderD), OrderD, OrderE.
order_hooks = []
order_state = {"other": None}


class OrderMeta(type):
    def mro(cls):
        other = order_state["other"]
        if other is not None:
            order_hooks.append((cls.__name__, all(other in base.__mro__ for base in cls.__bases__)))
        return type.mro(cls)


class OrderRoot(object):
    __metaclass__ = OrderMeta


class OrderOther(OrderRoot):
    pass


class OrderA(OrderRoot):
    pass


class OrderB(OrderA):
    pass


class OrderC(OrderB):
    pass


class OrderD(OrderA):
    pass


class OrderE(OrderB, OrderD):
    pass


order_state["other"] = OrderOther
OrderA.__bases__ = (OrderOther,)
assert order_hooks == [("OrderA", True), ("OrderB", True), ("OrderD", True), ("OrderC", True), ("OrderE", True)]
assert mro_names(OrderE) == ["OrderE", "OrderB", "OrderD", "OrderA", "OrderOther", "OrderRoot", "object"]
