"""Execute one vendored or project-authored test in an isolated interpreter."""

import sys
import assertions


def install_adapters():
    import support
    sys.modules["unittest"] = assertions
    test = type(sys)("test")
    test.__path__ = []
    test.test_support = test.support = support
    sys.modules["test"] = test
    sys.modules["test.test_support"] = support
    sys.modules["test.support"] = support


def load_module(name, target):
    if target == "tinypy":
        install_adapters()
    else:
        import unittest
    import_name = name if name.startswith("local.") else "vendor." + name
    return __import__(import_name, fromlist=["*"])


def list_cases(module, name):
    import unittest
    cases = []
    for key in sorted(module.__dict__):
        value = getattr(module, key)
        if isinstance(value, type) and issubclass(value, unittest.TestCase):
            for method in sorted(dir(value)):
                if method.startswith("test") and callable(getattr(value, method)):
                    cases.append(key + "." + method)
    return cases


def run_case(module, path):
    import unittest
    if "." in path:
        class_name, method_name = path.split(".", 1)
        instance = getattr(module, class_name)(method_name)
        function = getattr(instance, method_name)
    else:
        instance = None
        function = getattr(module, path)
    if getattr(function, "__unittest_skip__", False):
        raise assertions.SkipTest(function.__unittest_skip_why__)
    try:
        if instance is not None:
            instance.setUp()
        try:
            function()
        finally:
            if instance is not None:
                instance.tearDown()
    except unittest.SkipTest as error:
        raise assertions.SkipTest(str(error))


def main():
    target, name, path = sys.argv[1:]
    module = load_module(name, target)
    if path == "--list":
        for case in list_cases(module, name):
            print case
        return
    if path not in list_cases(module, name):
        raise ValueError("unknown upstream case: " + path)
    try:
        run_case(module, path)
    except assertions.SkipTest as error:
        print "SKIP:", str(error)
        sys.exit(77)


if __name__ == "__main__":
    main()
