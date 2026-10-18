# Import machinery follows CPython 2.7: fromlist handling, __all__ naming a
# submodule, "cannot import name", None entries in sys.modules, relative
# import diagnostics and attributes of already loaded modules.
import sys

from import_semantics import pkg

# An implicit relative import in a function must publish the submodule on
# its package even though the local import does not assign a package global.
local_sibling = pkg.import_sibling()
from import_semantics.pkg import sibling
assert sibling is local_sibling
assert pkg.sibling is sys.modules["import_semantics.pkg.sibling"]

from import_semantics.pkg import relative

module = __import__("import_semantics.pkg", fromlist=["other"])
assert module.__name__ == "import_semantics.pkg"
assert module.other.value == "other"
assert pkg.import_other_value() == "other"
from import_semantics.pkg import other
assert other is sys.modules["import_semantics.pkg.other"]
module = __import__("import_semantics.pkg", globals(), locals(), [], -1)
assert module.__name__ == "import_semantics"
module = __import__("import_semantics.pkg.sub")
assert module.__name__ == "import_semantics"
assert sys.modules["import_semantics.pkg.sub"].value == "sub"

namespace = {}
exec "from import_semantics.pkg import *" in namespace
assert namespace["CONST"] == 1
assert namespace["sub"].value == "sub"

try:
    from import_semantics.pkg import missing
except ImportError as error:
    assert str(error) == "cannot import name missing"
else:
    raise AssertionError("missing name was imported")

sys.modules["blockedmod"] = None
try:
    import blockedmod
except ImportError as error:
    assert str(error) == "No module named blockedmod"
else:
    raise AssertionError("None in sys.modules did not block the import")

try:
    import import_semantics.plain.sub
except ImportError as error:
    assert str(error) == "No module named sub"
else:
    raise AssertionError("submodule of a plain module was imported")

try:
    __import__("import_semantics.pkg", fromlist=[1])
except TypeError as error:
    assert str(error) == "Item in ``from list'' must be str, not int"
else:
    raise AssertionError("non-string fromlist item was accepted")

try:
    exec "from . import nothing"
except ValueError as error:
    assert str(error) == "Attempted relative import in non-package"
else:
    raise AssertionError("relative import in __main__ was accepted")

assert relative.sibling_value == "sibling"
assert relative.partial_error == "No module named zzz"
assert relative.beyond_error == "Attempted relative import beyond toplevel package"
assert relative.plain_value == "plain"

# A cached implicit import must preserve an explicitly replaced attribute.
pkg.sibling = "replaced"
assert pkg.import_sibling() is sibling
assert pkg.sibling == "replaced"

pkg.sub = "replaced"
import import_semantics.pkg.sub
assert pkg.sub == "replaced"

assert reload(sys) is sys

# Reload uses the registered parent but leaves its replaced attribute alone.
assert reload(sibling) is sibling
assert pkg.sibling == "replaced"
parent_name = "import_semantics.pkg"
parent = sys.modules[parent_name]
del sys.modules[parent_name]
try:
    try:
        reload(sibling)
    except ImportError as error:
        assert str(error) == "reload(): parent import_semantics.pkg not in sys.modules"
    else:
        raise AssertionError("reload accepted an unregistered parent")
finally:
    sys.modules[parent_name] = parent

# The loaders of sys.meta_path and of the host finder: the module a loader
# returns is registered when it registered none, and a loader that fails or
# returns None leaves no entry, as PEP 302 asks of loaders.
class ContractLoader(object):
    def load_module(self, name):
        module = type(sys)(name)
        if name == "contract_unregistered":
            return module
        sys.modules[name] = module
        if name == "contract_failing":
            raise ValueError("loader failed")
        return None

class ContractFinder(object):
    def find_module(self, name, path=None):
        if name.startswith("contract_"):
            return ContractLoader()
        return None

finder = ContractFinder()
sys.meta_path.append(finder)
try:
    module = __import__("contract_unregistered")
    assert sys.modules["contract_unregistered"] is module
    try:
        __import__("contract_failing")
    except ValueError as error:
        assert str(error) == "loader failed"
    else:
        raise AssertionError("the failing loader succeeded")
    assert "contract_failing" not in sys.modules
    try:
        __import__("contract_none")
    except ImportError as error:
        assert str(error) == "No module named contract_none"
    else:
        raise AssertionError("a loader returning None imported a module")
    assert "contract_none" not in sys.modules
finally:
    sys.meta_path.remove(finder)
    sys.modules.pop("contract_unregistered", None)

# Under the None that marks a package missing, the modules the VM provides
# are not found by the last component of the name, as is_builtin in CPython
# finds them (SPEC, imports).
sys.modules["none_package"] = None
try:
    try:
        __import__("sys", {"__package__": "none_package"}, None, [], 1)
    except ImportError as error:
        assert str(error) == "No module named sys"
    else:
        raise AssertionError("a VM module was found under a None package")
finally:
    del sys.modules["none_package"]
