# Import machinery follows CPython 2.7: fromlist handling, __all__ naming a
# submodule, "cannot import name", None entries in sys.modules, relative
# import diagnostics and attributes of already loaded modules.
import sys

from import_semantics import pkg
from import_semantics.pkg import relative

module = __import__("import_semantics.pkg", fromlist=["other"])
assert module.__name__ == "import_semantics.pkg"
assert module.other.value == "other"
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

pkg.sub = "replaced"
import import_semantics.pkg.sub
assert pkg.sub == "replaced"

assert reload(sys) is sys
