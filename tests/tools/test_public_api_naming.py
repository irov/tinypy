#!/usr/bin/env python3
"""Lock the public C ABI naming and its single C++ linkage proxy."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / "include/tinypy"
PUBLIC_HEADERS = tuple(sorted(INCLUDE.glob("*.h")))
STATIC_FUNCTION_ROOTS = ("cli", "src", "tests", "tools", "include")
STATIC_FUNCTION_SUFFIXES = {".c", ".h", ".cpp", ".hpp"}
FIXED_WIDTH_TYPE_ROOTS = ("cli", "include", "src", "tests", "tools")
INTERNAL_CORE_UNITS = {
    "codecs",
    "constructors",
    "container_methods",
    "cycle_diagnostics",
    "functools",
    "pool",
    "sre",
    "string_methods",
    "struct",
}
NON_CALL_EXPRESSION_NAMES = {
    "_Alignof",
    "INT8_C",
    "INT16_C",
    "INT32_C",
    "INT64_C",
    "INTMAX_C",
    "UINT8_C",
    "UINT16_C",
    "UINT32_C",
    "UINT64_C",
    "UINTMAX_C",
    "offsetof",
    "sizeof",
}


def mask_c_comments_and_literals(text: str) -> str:
    masked = list(text)
    offset = 0
    while offset < len(text):
        if text.startswith("//", offset):
            end = text.find("\n", offset)
            if end < 0:
                end = len(text)
            for index in range(offset, end):
                masked[index] = " "
            offset = end
            continue

        if text.startswith("/*", offset):
            end = text.find("*/", offset + 2)
            if end < 0:
                raise AssertionError("unterminated C block comment")
            end += 2
            for index in range(offset, end):
                if masked[index] != "\n":
                    masked[index] = " "
            offset = end
            continue

        if text[offset] in {'"', "'"}:
            quote = text[offset]
            end = offset + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == quote:
                    end += 1
                    break
                end += 1
            else:
                raise AssertionError("unterminated C string or character literal")
            for index in range(offset, min(end, len(text))):
                if masked[index] != "\n":
                    masked[index] = " "
            offset = end
            continue

        offset += 1

    return "".join(masked)


def c_expression_contains_call(expression: str) -> bool:
    names = re.findall(r"\b([A-Za-z_]\w*)\s*\(", expression)
    return any(name not in NON_CALL_EXPRESSION_NAMES for name in names)


def c_function_body_ranges(masked: str, return_type: str):
    function_definition = re.compile(
        r"(?m)^[^\n;{{}}]*\b{}\s+([A-Za-z_]\w*)\s*"
        r"\([^;{{}}]*\)\s*\{{".format(re.escape(return_type))
    )

    for match in function_definition.finditer(masked):
        opening = masked.find("{", match.start())
        depth = 0
        for offset in range(opening, len(masked)):
            if masked[offset] == "{":
                depth += 1
            elif masked[offset] == "}":
                depth -= 1
                if depth == 0:
                    yield match.group(1), match.end(), offset
                    break
        else:
            raise AssertionError(
                "unterminated function definition: {}".format(match.group(1))
            )


def public_api_text() -> str:
    return "\n".join(
        header.read_text(encoding="utf-8") for header in PUBLIC_HEADERS
    )


class PublicApiNamingTests(unittest.TestCase):
    def test_c_prefix_and_versioned_struct_names(self) -> None:
        for header in PUBLIC_HEADERS:
            text = header.read_text(encoding="utf-8")
            self.assertNotRegex(text, r"\b(?:py_|tpy_)", header.name)
            self.assertNotRegex(text, r"\b(?:PY_|TPY_)", header.name)

    def test_c_headers_have_no_cpp_linkage_syntax(self) -> None:
        for header in PUBLIC_HEADERS:
            text = header.read_text(encoding="utf-8")
            self.assertNotIn("__cplusplus", text, header.name)
            self.assertNotIn('extern "C"', text, header.name)

    def test_types_header_owns_standard_size_and_integer_includes(self) -> None:
        types_text = (INCLUDE / "types.h").read_text(encoding="utf-8")
        self.assertIn("#include <stddef.h>", types_text)
        self.assertIn("#include <stdint.h>", types_text)

        for root_name in ("include", "src"):
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in {".c", ".h"}:
                    continue
                if path == INCLUDE / "types.h":
                    continue
                text = path.read_text(encoding="utf-8")
                self.assertNotIn("#include <stddef.h>", text, path)
                self.assertNotIn("#include <stdint.h>", text, path)

    def test_unsigned_char_is_not_used(self) -> None:
        for root_name in FIXED_WIDTH_TYPE_ROOTS:
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in STATIC_FUNCTION_SUFFIXES:
                    continue
                text = path.read_text(encoding="utf-8")
                self.assertNotRegex(text, r"\bunsigned\s+char\b", path)

    def test_boolean_api_uses_semantic_fixed_width_type(self) -> None:
        types_text = (INCLUDE / "types.h").read_text(encoding="utf-8")
        public_header = public_api_text()
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )

        self.assertIn("typedef uint8_t tinypy_bool_t;", types_text)
        self.assertIn("#define TINYPY_FALSE ((tinypy_bool_t)0U)", types_text)
        self.assertIn("#define TINYPY_TRUE ((tinypy_bool_t)1U)", types_text)
        self.assertIn(
            "tinypy_bool_t tinypy_equal(const tinypy_value_t *left, const tinypy_value_t *right);",
            public_header,
        )
        self.assertIn(
            "tinypy_bool_t tinypy_dict_contains(const tinypy_value_t *dict, const tinypy_value_t *key);",
            public_header,
        )
        self.assertIn(
            "tinypy_bool_t tinypy_internal_value_belongs_to(",
            internal_header,
        )
        self.assertIn(
            "tinypy_bool_t tinypy_internal_equal_value(",
            internal_header,
        )
        self.assertEqual(
            set(
                re.findall(
                    r"\btinypy_bool_t\s+(tinypy_\w+)\s*\(",
                    public_header,
                )
            ),
            {
                "tinypy_build_profile_find",
                "tinypy_class_is_subclass",
                "tinypy_delete_item",
                "tinypy_dict_contains",
                "tinypy_dict_next",
                "tinypy_equal",
                "tinypy_exception_raise",
                "tinypy_generator_close",
                "tinypy_generator_finished",
                "tinypy_is_callable",
                "tinypy_marshal_bool_value",
                "tinypy_native_instance_construct",
                "tinypy_native_type_update_spec",
                "tinypy_object_delete_attr",
                "tinypy_object_has_attr",
                "tinypy_object_has_attr_value",
                "tinypy_object_set_attr",
                "tinypy_object_set_attr_value",
                "tinypy_preprocessor_name_is_reserved",
                "tinypy_set_add",
                "tinypy_set_discard",
                "tinypy_set_item",
                "tinypy_type_is_subtype",
                "tinypy_vm_has_error",
            },
        )
        self.assertIn(
            "typedef tinypy_bool_t (*tinypy_native_module_initialize_t)(",
            public_header,
        )
        self.assertIn(
            "typedef tinypy_bool_t (*tinypy_native_construct_t)(",
            public_header,
        )
        self.assertIn(
            "tinypy_bool_t (*poll_interrupt)(void *user_data);",
            public_header,
        )

        self.assertIn(
            "int32_t tinypy_truth(tinypy_value_t *value, tinypy_error_t **out_error);",
            public_header,
        )
        self.assertIn(
            "int32_t tinypy_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error);",
            public_header,
        )
        self.assertIn(
            "int32_t tinypy_exception_matches(",
            public_header,
        )
        self.assertIn(
            "int32_t tinypy_set_contains(",
            public_header,
        )

    def test_vm_builtin_types_are_indexed_by_value_kind(self) -> None:
        types_text = (INCLUDE / "types.h").read_text(encoding="utf-8")
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )
        value_source = (ROOT / "src/core/value.c").read_text(encoding="utf-8")
        vm_source = (ROOT / "src/core/vm.c").read_text(encoding="utf-8")

        value_type_enum = re.search(
            r"typedef enum tinypy_value_type_e \{(?P<body>.*?)"
            r"\} tinypy_value_type_e;",
            types_text,
            re.DOTALL,
        )
        self.assertIsNotNone(value_type_enum)
        value_kinds = set(
            re.findall(r"\b(TINYPY_VALUE_[A-Z_]+)\s*=", value_type_enum["body"])
        )
        value_kinds.remove("TINYPY_VALUE_NATIVE_INSTANCE")
        initialized_kinds = set(
            re.findall(
                r"vm,\s*&vm->types\[(TINYPY_VALUE_[A-Z_]+)\],"
                r"\s*&vm->types\[",
                vm_source,
            )
        )

        self.assertIn(
            "#define TINYPY_BUILTIN_TYPE_COUNT "
            "((size_t)TINYPY_VALUE_NATIVE_INSTANCE)",
            internal_header,
        )
        self.assertIn(
            "tinypy_type_t types[TINYPY_BUILTIN_TYPE_COUNT];",
            internal_header,
        )
        self.assertEqual(initialized_kinds, value_kinds)
        self.assertIn(
            'vm, &vm->types[TINYPY_VALUE_INVALID], '
            '&vm->types[TINYPY_VALUE_TYPE], "basestring",',
            vm_source,
        )
        self.assertNotIn("tinypy_internal_type_for_kind", internal_header)
        self.assertNotIn("tinypy_internal_type_for_kind", value_source)
        self.assertIn(
            "tinypy_type_t *object_type = &vm->types[type];",
            value_source,
        )
        self.assertNotIn(
            "tinypy_type_t *types[TINYPY_BUILTIN_TYPE_COUNT]",
            value_source,
        )
        self.assertNotIn(
            "tinypy_type_t *types[TINYPY_BUILTIN_TYPE_COUNT]",
            vm_source,
        )

    def test_header_function_declarations_are_single_line(self) -> None:
        multiline_prototype = re.compile(
            r"^[A-Za-z_][^;{}\n]*\((?:[^;{}\n]*\n[ \t]*)+[^;{}]*?\);",
            re.MULTILINE,
        )
        multiline_function_pointer = re.compile(r"\(\*\w+\)\(\s*\n")

        for root_name in ("include", "src"):
            for path in (ROOT / root_name).rglob("*.h"):
                text = path.read_text(encoding="utf-8")
                self.assertNotRegex(text, multiline_prototype, path)
                self.assertNotRegex(text, multiline_function_pointer, path)

    def test_enum_and_other_typedef_suffixes(self) -> None:
        declaration = re.compile(
            r"typedef\s+(enum|struct|union)\s+(\w+)"
            r"(?:\s*\{.*?\})?\s+(\w+)\s*;",
            re.DOTALL,
        )
        scalar = re.compile(r"typedef\s+(?!enum|struct|union)([^;]+?)\s+(\w+)\s*;")

        for header in PUBLIC_HEADERS:
            text = header.read_text(encoding="utf-8")
            spans: list[tuple[int, int]] = []
            for match in declaration.finditer(text):
                kind, tag, alias = match.groups()
                suffix = "_e" if kind == "enum" else "_t"
                self.assertTrue(tag.endswith(suffix), (header.name, tag))
                self.assertTrue(alias.endswith(suffix), (header.name, alias))
                spans.append(match.span())

            remaining = text
            for start, end in reversed(spans):
                remaining = remaining[:start] + remaining[end:]
            for match in scalar.finditer(remaining):
                alias = match.group(2)
                self.assertTrue(alias.endswith("_t"), (header.name, alias))

    def test_tinypy_is_the_only_cpp_proxy(self) -> None:
        proxies = tuple(sorted(INCLUDE.glob("*.hpp")))
        self.assertEqual(tuple(path.name for path in proxies), ("tinypy.hpp",))
        text = proxies[0].read_text(encoding="utf-8")
        self.assertRegex(text, r'extern\s+"C"\s*\{')
        self.assertIn('#include "tinypy/tinypy.h"', text)

    def test_tinypy_header_aggregates_the_entire_c_api(self) -> None:
        umbrella = (INCLUDE / "tinypy.h").read_text(encoding="utf-8")
        included = set(re.findall(r'#include "tinypy/([^\"]+\.h)"', umbrella))
        expected = {header.name for header in PUBLIC_HEADERS}
        expected.remove("tinypy.h")
        self.assertEqual(included, expected)

    def test_core_sources_have_matching_public_headers(self) -> None:
        source_names = {
            path.stem for path in (ROOT / "src/core").glob("*.c")
        }
        header_names = {path.stem for path in PUBLIC_HEADERS}
        self.assertEqual(source_names - header_names, INTERNAL_CORE_UNITS)

    def test_core_contract_failures_are_not_runtime_statuses(self) -> None:
        text = public_api_text()
        self.assertNotIn("TINYPY_INVALID_ARGUMENT", text)
        self.assertNotIn("TINYPY_WRONG_VM", text)
        self.assertNotIn("TINYPY_INTERNAL_ERROR", text)
        self.assertNotIn("TINYPY_OUT_OF_MEMORY", text)
        self.assertIn("tinypy_value_t *tinypy_none_get(tinypy_vm_t *vm);", text)
        self.assertIn(
            "tinypy_value_t *tinypy_bool_from_i32(tinypy_vm_t *vm, int32_t value);",
            text,
        )
        self.assertNotIn("tinypy_bool_from_int", text)
        self.assertIn(
            "tinypy_value_t *tinypy_integer_from_i64(tinypy_vm_t *vm, int64_t value);",
            text,
        )
        self.assertIn(
            "tinypy_value_t *tinypy_unicode_from_utf8(",
            text,
        )
        self.assertNotIn("TINYPY_INVALID_ENCODING", text)
        self.assertIn(
            "int32_t tinypy_bool_as_i32(const tinypy_value_t *value);",
            text,
        )
        self.assertNotIn("tinypy_bool_as_int", text)
        self.assertIn(
            "int64_t tinypy_integer_as_i64(const tinypy_value_t *value);",
            text,
        )
        self.assertIn(
            "double tinypy_float_as_double(const tinypy_value_t *value);",
            text,
        )
        self.assertIn(
            "size_t tinypy_tuple_size(const tinypy_value_t *value);",
            text,
        )
        self.assertIn(
            "size_t tinypy_list_size(const tinypy_value_t *value);",
            text,
        )
        self.assertIn(
            "size_t tinypy_dict_size(const tinypy_value_t *dict);",
            text,
        )
        self.assertIn("tinypy_value_t *tinypy_dict_new(tinypy_vm_t *vm);", text)
        self.assertIn("void tinypy_retain(tinypy_value_t *value);", text)
        self.assertIn("void tinypy_release(tinypy_value_t *value);", text)

    def test_vm_parameter_only_when_context_cannot_be_recovered(self) -> None:
        text = public_api_text()
        declaration = re.compile(r"\b(tinypy_\w+)\s*\(([^;{}]*)\);", re.DOTALL)
        functions_with_vm = {
            match.group(1)
            for match in declaration.finditer(text)
            if "tinypy_vm_t *vm" in match.group(2)
        }

        self.assertEqual(
            functions_with_vm,
            {
                "tinypy_vm_destroy",
                "tinypy_none_get",
                "tinypy_not_implemented_get",
                "tinypy_bool_from_i32",
                "tinypy_integer_from_i64",
                "tinypy_string_from_bytes",
                "tinypy_unicode_from_utf8",
                "tinypy_long_from_i64",
                "tinypy_long_from_base15_digits",
                "tinypy_float_from_double",
                "tinypy_complex_from_doubles",
                "tinypy_tuple_from_items",
                "tinypy_tuple_new",
                "tinypy_list_from_items",
                "tinypy_dict_new",
                "tinypy_type_new",
                "tinypy_bytearray_from_bytes",
                "tinypy_cell_new",
                "tinypy_ellipsis_get",
                "tinypy_frozenset_new",
                "tinypy_import_module",
                "tinypy_marshal_load_code_v2",
                "tinypy_module_new",
                "tinypy_native_function_new",
                "tinypy_native_type_new",
                "tinypy_output_emit",
                "tinypy_compile_source",
                "tinypy_preprocess_source",
                "tinypy_eval_source",
                "tinypy_exec_source",
                "tinypy_property_new",
                "tinypy_set_new",
                "tinypy_slice_new",
                "tinypy_vm_builtins",
                "tinypy_vm_current_frame",
                "tinypy_vm_handled_exception",
                "tinypy_vm_has_error",
                "tinypy_vm_clear_error",
                "tinypy_vm_raise_error",
                "tinypy_vm_modules",
                "tinypy_vm_module_finder",
                "tinypy_vm_set_module_finder",
                "tinypy_vm_raised_exception",
                "tinypy_vm_raised_exception_type",
                "tinypy_vm_raised_traceback",
                "tinypy_vm_report_cycles",
            },
        )

    def test_no_public_api_exposes_recoverable_oom(self) -> None:
        for header in PUBLIC_HEADERS:
            text = header.read_text(encoding="utf-8")
            self.assertNotIn("OUT_OF_MEMORY", text, header.name)

    def test_refcount_max_is_not_an_immortal_sentinel(self) -> None:
        internal_header = (ROOT / "src/core/internal.h").read_text(encoding="utf-8")
        vm_source = (ROOT / "src/core/vm.c").read_text(encoding="utf-8")

        self.assertIn(
            "TINYPY_REFCNT(__tinypy_incref_value) += 1;",
            internal_header,
        )
        self.assertNotIn(
            "if (TINYPY_REFCNT(__tinypy_incref_value) == PTRDIFF_MAX)",
            internal_header,
        )
        self.assertNotRegex(
            vm_source,
            r"\bref\s*=\s*PTRDIFF_MAX",
        )

    def test_long_conversion_is_a_direct_contract(self) -> None:
        long_source = (ROOT / "src/core/long.c").read_text(encoding="utf-8")
        value_source = (ROOT / "src/core/value.c").read_text(encoding="utf-8")

        long_body = long_source.split("int64_t tinypy_long_as_i64(", 1)[1]
        long_body = long_body.split("\n}", 1)[0]
        self.assertNotIn("out_error", long_body)

        release_body = value_source.split("void tinypy_release(tinypy_value_t *value)", 1)[1]
        release_body = release_body.split("\n}\n", 1)[0]
        self.assertIn("TINYPY_DECREF(value);", release_body)
        self.assertNotIn("tinypy_internal_value_release(value)", value_source)

    def test_comparison_contract_has_no_status_api(self) -> None:
        public_header = public_api_text()
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("TINYPY_RECURSION_ERROR", public_header)
        self.assertNotIn("TINYPY_UNHASHABLE", public_header)
        self.assertIn(
            "tinypy_bool_t tinypy_equal(const tinypy_value_t *left, const tinypy_value_t *right);",
            public_header,
        )
        self.assertIn(
            "tinypy_hash_t tinypy_hash(const tinypy_value_t *value);",
            public_header,
        )
        self.assertIn(
            "tinypy_hash_t tinypy_internal_hash_value(const tinypy_value_t *value, tinypy_error_t **out_error);",
            internal_header,
        )
        self.assertIn(
            "void tinypy_list_extend(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count);",
            public_header,
        )
        self.assertIn(
            "tinypy_bool_t tinypy_dict_contains(const tinypy_value_t *dict, const tinypy_value_t *key);",
            public_header,
        )
        self.assertIn(
            "void tinypy_dict_clear(tinypy_value_t *dict);",
            public_header,
        )
        self.assertIn(
            "tinypy_value_t *tinypy_instance_new(tinypy_type_t *type);",
            public_header,
        )
        self.assertNotIn("tinypy_status_e", public_header)
        self.assertIn(
            "tinypy_bool_t tinypy_internal_equal_value(",
            internal_header,
        )

    def test_production_code_has_no_c_assertions(self) -> None:
        cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("TINYPY_ENABLE_ASSERTS", cmake_text)

        for root_name in ("cli", "include", "src"):
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in {".c", ".h"}:
                    continue
                text = path.read_text(encoding="utf-8")
                self.assertNotIn("TINYPY_ASSERT", text, path)
                self.assertNotIn("TINYPY_ENABLE_ASSERTS", text, path)
                self.assertNotRegex(text, r"\bassert\s*\(", path)

    def test_function_call_results_use_local_variables(self) -> None:
        return_statement = re.compile(r"\breturn\s+([^;]+);")

        for root_name in STATIC_FUNCTION_ROOTS:
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in STATIC_FUNCTION_SUFFIXES:
                    continue
                text = path.read_text(encoding="utf-8")
                masked = mask_c_comments_and_literals(text)
                for match in return_statement.finditer(masked):
                    expression = match.group(1).strip()
                    line = text.count("\n", 0, match.start()) + 1
                    self.assertFalse(
                        c_expression_contains_call(expression),
                        "{}:{}: function call result must use a local variable".format(
                            path.relative_to(ROOT),
                            line,
                        ),
                    )

    def test_return_value_locals_do_not_create_scopes(self) -> None:
        artificial_return_scope = re.compile(
            r"(?ms)^(?P<indent>[ \t]*)\{\n"
            r"(?P=indent)    [^;{}]*?\b"
            r"(?P<name>return_value(?:_[0-9]+)?)\s*=.*?;\n"
            r"(?P=indent)    return (?P=name);\n"
            r"(?P=indent)\}"
        )
        pseudo_else_scope = re.compile(r"}[ \t\r\n]*{")
        function_body_scope = re.compile(r"\)[ \t\r\n]*\{[ \t\r\n]*\{")

        for root_name in STATIC_FUNCTION_ROOTS:
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in STATIC_FUNCTION_SUFFIXES:
                    continue
                text = path.read_text(encoding="utf-8")
                masked = mask_c_comments_and_literals(text)
                for pattern in (
                    artificial_return_scope,
                    pseudo_else_scope,
                    function_body_scope,
                ):
                    match = pattern.search(masked)
                    if match is None:
                        continue
                    line = text.count("\n", 0, match.start()) + 1
                    self.fail(
                        "{}:{}: return local must not create an extra scope".format(
                            path.relative_to(ROOT),
                            line,
                        )
                    )

    def test_boolean_values_use_boolean_constants(self) -> None:
        return_statement = re.compile(r"\breturn\s+([^;]+);")
        boolean_declaration = re.compile(
            r"\btinypy_bool_t\s+([A-Za-z_]\w*)\s*=\s*([^;]+);"
        )
        fixed_width_integer_constant = re.compile(r"^INT32_C\([01]\)$")
        raw_integer_constant = re.compile(r"^(?:0|1)[uUlL]*$")
        fixed_width_integer_ternary = re.compile(
            r"\?\s*INT32_C\([01]\)\s*:\s*INT32_C\([01]\)"
        )
        raw_integer_ternary = re.compile(
            r"\?\s*[01][uUlL]*\s*:\s*[01][uUlL]*\b"
        )

        for root_name in STATIC_FUNCTION_ROOTS:
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in STATIC_FUNCTION_SUFFIXES:
                    continue
                text = path.read_text(encoding="utf-8")
                masked = mask_c_comments_and_literals(text)
                for function_name, body_start, body_end in c_function_body_ranges(
                    masked, "tinypy_bool_t"
                ):
                    for match in return_statement.finditer(
                        masked, body_start, body_end
                    ):
                        expression = match.group(1).strip()
                        line = text.count("\n", 0, match.start()) + 1
                        self.assertIsNone(
                            fixed_width_integer_constant.fullmatch(expression),
                            "{}:{}: {} must return TINYPY_FALSE/TINYPY_TRUE".format(
                                path.relative_to(ROOT),
                                line,
                                function_name,
                            ),
                        )
                        self.assertIsNone(
                            raw_integer_constant.fullmatch(expression),
                            "{}:{}: {} must return TINYPY_FALSE/TINYPY_TRUE".format(
                                path.relative_to(ROOT),
                                line,
                                function_name,
                            ),
                        )
                        self.assertNotRegex(
                            expression,
                            fixed_width_integer_ternary,
                            "{}:{}: {} must return TINYPY_FALSE/TINYPY_TRUE".format(
                                path.relative_to(ROOT),
                                line,
                                function_name,
                            ),
                        )
                        self.assertNotRegex(
                            expression,
                            raw_integer_ternary,
                            "{}:{}: {} must return TINYPY_FALSE/TINYPY_TRUE".format(
                                path.relative_to(ROOT),
                                line,
                                function_name,
                            ),
                        )
                for match in boolean_declaration.finditer(masked):
                    variable_name = match.group(1)
                    initializer = match.group(2).strip()
                    line = text.count("\n", 0, match.start()) + 1
                    message = (
                        "{}:{}: {} must use TINYPY_FALSE/TINYPY_TRUE".format(
                            path.relative_to(ROOT),
                            line,
                            variable_name,
                        )
                    )
                    self.assertNotEqual(
                        initializer,
                        "INT32_C(0)",
                        message,
                    )
                    self.assertNotEqual(
                        initializer,
                        "INT32_C(1)",
                        message,
                    )
                    self.assertIsNone(
                        raw_integer_constant.fullmatch(initializer),
                        message,
                    )
                    self.assertNotRegex(
                        initializer,
                        fixed_width_integer_ternary,
                        message,
                    )
                    self.assertNotRegex(
                        initializer,
                        raw_integer_ternary,
                        message,
                    )

    def test_status_returning_public_api_is_semantic_only(self) -> None:
        public_header = public_api_text()
        self.assertNotIn("tinypy_status_e", public_header)
        self.assertIn("tinypy_error_kind_e tinypy_error_kind(", public_header)

        self.assertIn(
            "tinypy_vm_t *tinypy_vm_create(const tinypy_vm_config_t *config);",
            public_header,
        )
        self.assertIn(
            "int64_t tinypy_long_as_i64(const tinypy_value_t *value);",
            public_header,
        )

        self.assertIn(
            "tinypy_type_t *tinypy_type_new(",
            public_header,
        )
        self.assertIn(
            "tinypy_value_t *tinypy_type_get_attr(",
            public_header,
        )
        self.assertIn(
            "tinypy_value_t *tinypy_instance_get_attr(",
            public_header,
        )
        self.assertNotIn("TINYPY_KEY_ERROR", public_header)

    def test_type_slots_use_direct_results(self) -> None:
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("tinypy_status_e", internal_header)
        self.assertIn("typedef tinypy_value_t *(*tinypy_unary_slot_t)(", internal_header)
        self.assertIn("typedef int32_t (*tinypy_inquiry_slot_t)(", internal_header)
        self.assertIn("typedef ptrdiff_t (*tinypy_length_slot_t)(", internal_header)
        self.assertIn("typedef tinypy_hash_t (*tinypy_hash_slot_t)(", internal_header)
        self.assertIn("typedef tinypy_bool_t (*tinypy_init_slot_t)(", internal_header)

    def test_allocator_api_has_no_categories(self) -> None:
        types_header = (ROOT / "include/tinypy/types.h").read_text(
            encoding="utf-8"
        )
        vm_header = (ROOT / "include/tinypy/vm.h").read_text(encoding="utf-8")
        implementation = "\n".join(
            path.read_text(encoding="utf-8")
            for directory in ("cli", "src", "tests")
            for path in (ROOT / directory).rglob("*")
            if path.suffix in {".c", ".h"}
        )

        self.assertNotIn("tinypy_allocation_tag_e", types_header)
        self.assertNotIn("ALLOC_TAG_", types_header)
        self.assertIn(
            "void *(*allocate)(void *user_data, size_t size, size_t alignment);",
            vm_header,
        )
        self.assertIn(
            "void *(*reallocate)(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment);",
            vm_header,
        )
        self.assertIn(
            "void (*deallocate)(void *user_data, void *memory, size_t size, size_t alignment);",
            vm_header,
        )
        self.assertNotIn("ALLOC_TAG_", implementation)
        self.assertNotIn("tinypy_allocation_tag_e", implementation)

    def test_cycle_diagnostics_implementation_is_isolated(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )
        vm_source = (ROOT / "src/core/vm.c").read_text(encoding="utf-8")
        diagnostics_source = (
            ROOT / "src/core/cycle_diagnostics.c"
        ).read_text(encoding="utf-8")

        self.assertIn("src/core/cycle_diagnostics.c", cmake)
        self.assertIn(
            "struct tinypy_cycle_diagnostics_state_t {",
            diagnostics_source,
        )
        self.assertIn(
            "void tinypy_internal_cycle_diagnostics_initialize(",
            diagnostics_source,
        )
        self.assertIn("size_t tinypy_vm_report_cycles(", diagnostics_source)
        self.assertNotIn("struct tinypy_cycle_diagnostics_state_t {", internal_header)
        self.assertNotIn("struct tinypy_cycle_diagnostics_state_t {", vm_source)
        self.assertNotIn("tinypy_debug_cycle_graph_t", vm_source)
        self.assertNotIn("size_t tinypy_vm_report_cycles(", vm_source)

    def test_integer_and_complex_runtime_layout(self) -> None:
        public_header = public_api_text()
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )
        core_source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (ROOT / "src/core").glob("*.c")
        )

        self.assertIn("tinypy_integer_object_t", internal_header)
        self.assertNotIn("tinypy_int_object_t", internal_header)
        self.assertIn("TINYPY_VALUE_INTEGER", public_header)
        self.assertNotRegex(public_header, r"\bPY_VALUE_INT\b")
        self.assertIn("tinypy_integer_from_i64", public_header)
        self.assertIn("tinypy_integer_as_i64", public_header)
        self.assertNotRegex(public_header, r"\bpy_int_\w+")
        self.assertIn("TINYPY_INTEGER_VALUE", internal_header)
        self.assertIn(
            "tinypy_type_t types[TINYPY_BUILTIN_TYPE_COUNT];",
            internal_header,
        )
        self.assertNotRegex(internal_header, r"\bPY_INT_VALUE\b")
        self.assertNotRegex(internal_header, r"\bint_type\b")
        self.assertIn("TINYPY_BUILD_VALUE_INTEGER", public_header)
        self.assertIn("TINYPY_MARSHAL_TYPE_INTEGER", public_header)
        self.assertIn("tinypy_marshal_integer_value", public_header)
        self.assertIn("tinypy_complex_object_t", internal_header)
        self.assertIn("TINYPY_VALUE_COMPLEX", public_header)
        self.assertIn("tinypy_complex_from_doubles", public_header)
        self.assertIn("TINYPY_VALUE_COMPLEX", core_source)
        self.assertIn("types[TINYPY_VALUE_COMPLEX]", core_source)

    def test_internal_helpers_do_not_revalidate_vm(self) -> None:
        functions = (
            ("src/core/vm.c", "void *tinypy_internal_vm_allocate("),
            ("src/core/vm.c", "void *tinypy_internal_vm_reallocate("),
            ("src/core/value.c", "tinypy_value_t *tinypy_internal_object_allocate("),
            ("src/core/value.c", "void tinypy_internal_value_destroy("),
            ("src/core/error.c", "void tinypy_internal_make_vm_error("),
            (
                "src/core/tuple.c",
                "tinypy_value_t *tinypy_internal_tuple_from_borrowed_items(",
            ),
        )

        for relative_path, signature in functions:
            text = (ROOT / relative_path).read_text(encoding="utf-8")
            start = text.index(signature)
            opening = text.index("{", start)
            depth = 0
            closing = opening
            for closing in range(opening, len(text)):
                if text[closing] == "{":
                    depth += 1
                elif text[closing] == "}":
                    depth -= 1
                    if depth == 0:
                        break
            body = text[opening : closing + 1]
            self.assertNotIn("tinypy_internal_vm_valid", body, signature)

    def test_vm_local_constant_cache_contract(self) -> None:
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )
        value_source = (ROOT / "src/core/value.c").read_text(
            encoding="utf-8"
        )
        numeric_source = (ROOT / "src/core/numeric.c").read_text(
            encoding="utf-8"
        )
        tuple_source = (ROOT / "src/core/tuple.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("TINYPY_INTEGER_CONSTANT_MIN (-INT64_C(1023))", internal_header)
        self.assertIn("TINYPY_INTEGER_CONSTANT_MAX INT64_C(1024)", internal_header)
        self.assertIn("integer_constants[TINYPY_INTEGER_CONSTANT_COUNT]", internal_header)
        self.assertIn("float_zero_object", internal_header)
        self.assertIn("empty_string_object", internal_header)
        self.assertIn("empty_tuple_object", internal_header)
        self.assertIn("value >= TINYPY_INTEGER_CONSTANT_MIN", internal_header)
        self.assertIn("size == 0U", value_source)
        self.assertIn("value == 0.0 && signbit(value) == 0", numeric_source)
        self.assertIn("result = &vm->empty_tuple_object.base.base", tuple_source)

    def test_static_functions_have_double_underscore_prefix(self) -> None:
        declaration = re.compile(
            r"^\s*static\s+[^;=()]*?([A-Za-z_]\w*)\s*\(",
            re.MULTILINE,
        )
        for root_name in STATIC_FUNCTION_ROOTS:
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in STATIC_FUNCTION_SUFFIXES:
                    continue
                text = path.read_text(encoding="utf-8")
                text = re.sub(r"^\s*#define[^\n]*(?:\\\n[^\n]*)*", "", text, flags=re.MULTILINE)
                for match in declaration.finditer(text):
                    if "##" in match.group(0):
                        continue
                    name = match.group(1)
                    self.assertTrue(
                        name.startswith("__"),
                        "{}: static function {} must start with __".format(
                            path.relative_to(ROOT),
                            name,
                        ),
                    )


if __name__ == "__main__":
    unittest.main()
