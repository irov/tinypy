class CustomFailure(Exception):
    def __init__(self, value):
        self.value = value


class CustomStringFailure(Exception):
    def __str__(self):
        return "custom string"


try:
    raise CustomFailure(42)
except CustomFailure as error:
    result = error.value

assert str(BaseException()) == ""
assert str(BaseException("one")) == "one"
assert str(BaseException("one", "two")) == "('one', 'two')"
assert unicode(BaseException("one")) == u"one"
assert unicode(CustomStringFailure("ignored")) == u"custom string"
assert repr(BaseException("one")) == "BaseException('one',)"
assert BaseException("one", "two")[0] == "one"
assert BaseException("one", "two").__getslice__(0, 1) == ("one",)
assert BaseException("one").__reduce__() == (BaseException, ("one",))
compact_exception = BaseException("compact")
assert compact_exception.__dict__ == {}
assert compact_exception.args == ("compact",)
assert compact_exception.message == "compact"
args_descriptor = BaseException.__dict__["args"]
message_descriptor = BaseException.__dict__["message"]
assert args_descriptor.__name__ == "args"
assert args_descriptor.__objclass__ is BaseException
for descriptor in (args_descriptor, message_descriptor):
    for descriptor_method in ("__get__", "__set__", "__delete__", "__repr__"):
        assert descriptor_method in type(descriptor).__dict__
assert args_descriptor.__get__(compact_exception, BaseException) == ("compact",)
assert args_descriptor.__repr__() == repr(args_descriptor)
assert args_descriptor.__set__(compact_exception, [1, 2]) is None
assert compact_exception.args == (1, 2)
assert message_descriptor.__set__(compact_exception, "override") is None
assert compact_exception.message == "override"
assert compact_exception.__dict__ == {"message": "override"}
assert message_descriptor.__delete__(compact_exception) is None
try:
    compact_exception.message
except AttributeError:
    pass
else:
    raise AssertionError("deleted BaseException.message remained visible")

blank_exception = BaseException.__new__(BaseException, "ignored")
assert blank_exception.args == ()


class InitWithoutBase(BaseException):
    def __init__(self):
        self.ready = True


custom_initialized_exception = InitWithoutBase()
assert custom_initialized_exception.ready
assert custom_initialized_exception.args == ()

import _weakref
try:
    _weakref.ref(BaseException())
except TypeError:
    pass
else:
    raise AssertionError("BaseException unexpectedly supported weak references")
weak_exception_target = CustomFailure(42)
assert _weakref.ref(weak_exception_target)() is weak_exception_target
stateful_exception = BaseException()
assert stateful_exception.__setstate__(None) is None
stateful_exception.__setstate__({"answer": 42})
assert stateful_exception.answer == 42
assert stateful_exception.__reduce__() == (BaseException, (), {"answer": 42})
try:
    stateful_exception.__setstate__({1: 2})
except TypeError:
    pass
else:
    raise AssertionError("BaseException.__setstate__ accepted a non-string attribute name")
