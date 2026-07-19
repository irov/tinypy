import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "audit_core_symbols.py"
SPEC = importlib.util.spec_from_file_location("audit_core_symbols", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class SymbolAuditTests(unittest.TestCase):
    def test_parses_macho_and_elf_undefined_symbols(self):
        output = """
object.o:
                 U _malloc
                 U ___memcpy_chk
                 U pthread_mutex_lock
"""
        self.assertEqual(
            AUDIT.parse_nm_undefined(output),
            {"malloc", "__memcpy_chk", "pthread_mutex_lock"},
        )

    def test_subtracts_symbols_defined_inside_archive(self):
        undefined = AUDIT.parse_nm_undefined(" U _py_helper\n U _malloc\n")
        defined = AUDIT.parse_nm_defined("000 T _py_helper\n")
        self.assertEqual(undefined.difference(defined), {"malloc"})

    def test_rejects_allocator_io_and_thread_dependencies(self):
        self.assertEqual(
            AUDIT.forbidden_symbols(
                {"memcpy", "malloc", "write", "pthread_mutex_lock"}
            ),
            ["malloc", "pthread_mutex_lock", "write"],
        )

    def test_allows_minimal_memory_primitives(self):
        self.assertEqual(
            AUDIT.forbidden_symbols(
                {"memcpy", "memmove", "memset", "memcmp", "__memcpy_chk"}
            ),
            [],
        )

    def test_rejects_cpython_named_defined_symbols(self):
        self.assertEqual(
            AUDIT.forbidden_defined_symbols(
                {
                    "tinypy_compile_source",
                    "__tinypy_ast_compile",
                    "py_compile_source",
                    "__py_ast_compile",
                    "PyAST_Compile",
                    "_PyParser_Grammar",
                }
            ),
            ["PyAST_Compile", "_PyParser_Grammar", "__py_ast_compile", "py_compile_source"],
        )


if __name__ == "__main__":
    unittest.main()
