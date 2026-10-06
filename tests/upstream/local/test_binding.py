"""Project-authored checks for receiver validation and method dispatch."""

import unittest


class Mailbox:
    def __init__(self, label):
        self.label = label

    def route(self, destination, urgent=False):
        return self.label, destination, urgent

    def acknowledge(self, destination):
        return len(destination)


class ModernMailbox(object):
    def __init__(self, label):
        self.label = label

    def route(self, destination, urgent=False):
        return self.label, destination, urgent

    def acknowledge(self, destination):
        return len(destination)


class Redirect(Mailbox):
    pass


class ModernRedirect(ModernMailbox):
    pass


class Binding(unittest.TestCase):
    def test_receiver_identity(self):
        for constructor in (Mailbox, ModernMailbox):
            first, second = constructor("east"), constructor("west")
            self.assertIs(first.route.im_self, first)
            self.assertIs(second.route.im_self, second)
            self.assertIs(first.route.im_func, constructor.route.im_func)
            self.assertIs(constructor.route.im_self, None)

    def test_descendant_receivers(self):
        for parent, child in ((Mailbox, Redirect), (ModernMailbox, ModernRedirect)):
            self.assertEqual(parent.route(child("north"), "archive"),
                             ("north", "archive", False))

    def test_call_forms(self):
        for constructor in (Mailbox, ModernMailbox):
            receiver = constructor("dispatch")
            self.assertEqual(receiver.route("store", urgent=True),
                             ("dispatch", "store", True))
            self.assertEqual(receiver.route(*("store",), **{"urgent": True}),
                             ("dispatch", "store", True))
            self.assertEqual(constructor.route(receiver, destination="store"),
                             ("dispatch", "store", False))

    def test_instance_function_is_not_bound(self):
        receiver = ModernMailbox("south")
        receiver.route = lambda destination: (destination, "override")
        self.assertEqual(receiver.route("queue"), ("queue", "override"))

    def test_special_methods_and_descriptors(self):
        class Gateway(object):
            def __call__(self, value):
                return value + 17

            @staticmethod
            def normalize(value):
                return value.lower()

            @classmethod
            def construct(cls):
                return cls()

        gateway = Gateway()
        self.assertEqual(gateway(6), 23)
        self.assertEqual(gateway.normalize("QUEUE"), "queue")
        self.assertEqual(Gateway.normalize("ROUTE"), "route")
        self.assertIsInstance(gateway.construct(), Gateway)

    def test_bound_method_equality_and_hash(self):
        for constructor in (Mailbox, ModernMailbox):
            receiver = constructor("local")
            saved = receiver.route
            self.assertEqual(saved, receiver.route)
            self.assertEqual(hash(saved), hash(receiver.route))
            self.assertNotEqual(saved, constructor("remote").route)
            self.assertEqual({saved: 19}[receiver.route], 19)


def invalid_receiver_check(constructor, receiver, receiver_name):
    def check(self):
        with self.assertRaises(TypeError) as captured:
            constructor.route(receiver, "archive")
        expected = (
            "unbound method route() must be called with %s instance as first argument "
            "(got %s instance instead)" % (constructor.__name__, receiver_name))
        self.assertEqual(str(captured.exception), expected)
    return check


def missing_receiver_check(constructor):
    def check(self):
        with self.assertRaises(TypeError) as captured:
            constructor.route()
        self.assertEqual(str(captured.exception),
                         "unbound method route() must be called with %s instance as first "
                         "argument (got nothing instead)" % constructor.__name__)
    return check


def unused_receiver_check(constructor, receiver):
    def check(self):
        # Validation must occur even when the function body never reads self.
        self.assertRaises(TypeError, constructor.acknowledge, receiver, "archive")
    return check


def arity_check(constructor, arguments, keywords):
    def check(self):
        receiver = constructor("origin")
        self.assertRaises(TypeError, receiver.route, *arguments, **keywords)
    return check


def builtin_receiver_check(owner, receiver):
    def check(self):
        # Built-in descriptors must validate the owner, just like user methods.
        self.assertRaises(TypeError, getattr(owner, "__len__"), receiver)
    return check


for owner in (list, tuple, dict, str, unicode):
    for label, receiver in (("number", 47), ("null", None), ("foreign", object())):
        setattr(Binding, "test_builtin_receiver_%s_%s" % (owner.__name__, label),
                builtin_receiver_check(owner, receiver))


for constructor in (Mailbox, ModernMailbox):
    for label, receiver, receiver_name in (
        ("number", 37, "int"), ("text", "address", "str"),
        ("sequence", [8], "list"), ("mapping", {"a": 8}, "dict"),
        ("null", None, "NoneType"), ("foreign", object(), "object"),
    ):
        setattr(Binding, "test_receiver_%s_%s" % (constructor.__name__, label),
                invalid_receiver_check(constructor, receiver, receiver_name))
        setattr(Binding, "test_unused_receiver_%s_%s" % (constructor.__name__, label),
                unused_receiver_check(constructor, receiver))
    setattr(Binding, "test_receiver_%s_missing" % constructor.__name__,
            missing_receiver_check(constructor))
    for label, arguments, keywords in (
        ("missing", (), {}), ("excess", (1, 2, 3), {}),
        ("duplicate", (1,), {"destination": 2}),
        ("unknown", (1,), {"priority": True}),
    ):
        setattr(Binding, "test_arity_%s_%s" % (constructor.__name__, label),
                arity_check(constructor, arguments, keywords))
