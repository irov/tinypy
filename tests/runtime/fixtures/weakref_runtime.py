from __future__ import division

from _weakref import CallableProxyType, ProxyType, getweakrefcount, getweakrefs, proxy, ref


events = []


class Target(object):
    pass


def removed(reference):
    events.append(reference() is None)


target = Target()
reference = ref(target, removed)
assert reference() is target
assert getweakrefcount(target) == 1
assert getweakrefs(target) == [reference]
assert hash(reference) == hash(target)
del target
assert reference() is None
assert events == [True]


class KeyedRef(ref):
    __slots__ = ("key",)

    def __new__(type, value, callback, key):
        self = ref.__new__(type, value, callback)
        self.key = key
        return self

    def __init__(self, value, callback, key):
        super(KeyedRef, self).__init__(value, callback)
second = Target()
keyed = KeyedRef(second, None, "second")
assert keyed() is second
assert keyed.key == "second"
assert isinstance(keyed, ref)

class Classic:
    pass


classic = Classic()
classic_reference = ref(classic)
assert classic_reference() is classic
del classic
assert classic_reference() is None

type_reference = ref(Target)
assert type_reference() is Target


reuse_target = Target()
assert ref(reuse_target) is ref(reuse_target)
assert proxy(reuse_target) is proxy(reuse_target)


class Callback(object):
    def __init__(self):
        self.called = False

    def __call__(self, reference):
        self.called = reference() is None


callback = Callback()
callback_target = Target()
callback_reference = ref(callback_target, callback)
del callback_target
assert callback.called
assert callback_reference() is None


class EqualTarget(object):
    def __eq__(self, other):
        return isinstance(other, EqualTarget)

    def __hash__(self):
        return 101


equal_left = EqualTarget()
equal_right = EqualTarget()
equal_left_reference = ref(equal_left)
equal_right_reference = ref(equal_right)
assert equal_left_reference == equal_right_reference
assert type(equal_left_reference).__eq__(equal_left_reference, equal_right_reference)
assert hash(equal_left_reference) == hash(equal_right_reference)
assert type(equal_left_reference).__call__(equal_left_reference) is equal_left
equal_left_proxy = proxy(equal_left)
assert not (equal_left_reference == equal_left_proxy)
assert not (equal_left_proxy == equal_left_reference)


class UnhashableTarget(object):
    __hash__ = None


unhashable_target = UnhashableTarget()
unhashable_reference = ref(unhashable_target)
try:
    type(unhashable_reference).__hash__(unhashable_reference)
except TypeError:
    pass
else:
    raise AssertionError("weakref hash must propagate referent unhashability")

dead_unhashed_target = Target()
dead_unhashed_reference = ref(dead_unhashed_target)
del dead_unhashed_target
try:
    hash(dead_unhashed_reference)
except TypeError:
    pass
else:
    raise AssertionError("an unhashed dead weakref must not acquire an identity hash")


class ProxyTarget(object):
    def __init__(self):
        self.value = 7
        self.items = [1, 2, 3]

    def __len__(self):
        return len(self.items)

    def __getitem__(self, index):
        return self.items[index]

    def __setitem__(self, index, value):
        self.items[index] = value

    def __delitem__(self, index):
        del self.items[index]

    def __contains__(self, value):
        return value in self.items

    def __iter__(self):
        return iter(self.items)

    def __nonzero__(self):
        return self.value != 0

    def __str__(self):
        return "proxy-target"

    def __add__(self, value):
        return self.value + value

    def __radd__(self, value):
        return value + self.value

    def __sub__(self, value):
        return self.value - value

    def __rsub__(self, value):
        return value - self.value

    def __mul__(self, value):
        return self.value * value

    def __div__(self, value):
        return self.value / value

    def __truediv__(self, value):
        return self.value / value

    def __floordiv__(self, value):
        return self.value // value

    def __mod__(self, value):
        return self.value % value

    def __divmod__(self, value):
        return divmod(self.value, value)

    def __pow__(self, value, modulus=None):
        powered = self.value ** value
        return powered if modulus is None else powered % modulus

    def __lshift__(self, value):
        return self.value << value

    def __rshift__(self, value):
        return self.value >> value

    def __and__(self, value):
        return self.value & value

    def __xor__(self, value):
        return self.value ^ value

    def __or__(self, value):
        return self.value | value

    def __pos__(self):
        return self.value

    def __neg__(self):
        return -self.value

    def __invert__(self):
        return ~self.value

    def __abs__(self):
        return self.value

    def __int__(self):
        return int(self.value)

    def __long__(self):
        return long(self.value)

    def __float__(self):
        return float(self.value)

    def __index__(self):
        return 1


proxy_target = ProxyTarget()
target_proxy = proxy(proxy_target)
assert type(target_proxy) is ProxyType
assert target_proxy.value == 7
target_proxy.value = 8
assert proxy_target.value == 8
assert len(target_proxy) == 3
assert target_proxy[1] == 2
target_proxy[1] = 4
assert proxy_target.items == [1, 4, 3]
del target_proxy[0]
assert proxy_target.items == [4, 3]
assert 4 in target_proxy
assert list(target_proxy) == [4, 3]
assert bool(target_proxy)
assert str(target_proxy) == "proxy-target"
assert target_proxy + 2 == 10
assert 2 + target_proxy == 10
assert target_proxy - 2 == 6
assert 20 - target_proxy == 12
assert target_proxy * 3 == 24
assert target_proxy / 2 == 4
assert target_proxy // 3 == 2
assert target_proxy % 3 == 2
assert divmod(target_proxy, 3) == (2, 2)
assert target_proxy ** 2 == 64
assert pow(target_proxy, 2, 5) == 4
assert target_proxy << 1 == 16
assert target_proxy >> 1 == 4
assert target_proxy & 3 == 0
assert target_proxy ^ 3 == 11
assert target_proxy | 3 == 11
assert +target_proxy == 8
assert -target_proxy == -8
assert ~target_proxy == -9
assert abs(target_proxy) == 8
assert int(target_proxy) == 8
assert long(target_proxy) == 8L
assert float(target_proxy) == 8.0
assert [10, 20][target_proxy] == 20
try:
    hash(target_proxy)
except TypeError:
    pass
else:
    raise AssertionError("weak proxies must be unhashable")


class CallableTarget(object):
    def __call__(self, value):
        return value + 1


callable_target = CallableTarget()
callable_proxy = proxy(callable_target)
assert type(callable_proxy) is CallableProxyType
assert callable_proxy(6) == 7

del proxy_target
try:
    target_proxy.value
except ReferenceError:
    pass
else:
    raise AssertionError("dead weak proxy access must raise ReferenceError")
