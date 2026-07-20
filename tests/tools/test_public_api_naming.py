#!/usr/bin/env python3
"""Lock the public C ABI naming and its single C++ linkage proxy."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / "include/tinypy"
PUBLIC_HEADERS = tuple(sorted(INCLUDE.glob("*.h")))
STATIC_FUNCTION_ROOTS = ("src", "tests", "tools", "include")
STATIC_FUNCTION_SUFFIXES = {".c", ".h", ".cpp", ".hpp"}
INTERNAL_CORE_UNITS = {
    "codecs",
    "constructors",
    "container_methods",
    "functools",
    "sre",
    "string_methods",
    "struct",
}


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
        self.assertIn('extern "C" {', text)
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
                "tinypy_bool_from_i32",
                "tinypy_integer_from_i64",
                "tinypy_string_from_bytes",
                "tinypy_unicode_from_utf8",
                "tinypy_long_from_i64",
                "tinypy_long_from_base15_digits",
                "tinypy_float_from_double",
                "tinypy_complex_from_doubles",
                "tinypy_tuple_from_items",
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
                "tinypy_vm_modules",
                "tinypy_vm_raised_exception",
            },
        )

    def test_no_public_api_exposes_recoverable_oom(self) -> None:
        for header in PUBLIC_HEADERS:
            text = header.read_text(encoding="utf-8")
            self.assertNotIn("OUT_OF_MEMORY", text, header.name)

    def test_refcount_max_is_an_assertion_not_an_immortal_sentinel(self) -> None:
        value_source = (ROOT / "src/core/value.c").read_text(encoding="utf-8")
        vm_source = (ROOT / "src/core/vm.c").read_text(encoding="utf-8")

        self.assertIn(
            "assert(value->ref != PTRDIFF_MAX);",
            value_source,
        )
        self.assertNotIn(
            "if (value->ref == PTRDIFF_MAX)",
            value_source,
        )
        self.assertNotRegex(
            vm_source,
            r"\bref\s*=\s*PTRDIFF_MAX",
        )

    def test_long_conversion_is_a_direct_debug_contract(self) -> None:
        long_source = (ROOT / "src/core/long.c").read_text(encoding="utf-8")
        value_source = (ROOT / "src/core/value.c").read_text(encoding="utf-8")
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )

        long_body = long_source.split("int64_t tinypy_long_as_i64(", 1)[1]
        long_body = long_body.split("\n}", 1)[0]
        self.assertIn(
            "assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));",
            long_body,
        )
        self.assertIn("assert(magnitude <= (UINT64_MAX >> 15U));", long_body)
        self.assertIn("assert(magnitude <= (uint64_t)INT64_MAX);", long_body)
        self.assertIn("assert(magnitude <= negative_limit);", long_body)
        self.assertNotIn("out_error", long_body)

        release_body = value_source.split("void tinypy_release(tinypy_value_t *value)", 1)[1]
        release_body = release_body.split("\n}\n", 1)[0]
        self.assertLess(
            release_body.index("if (value->ref != 0U)"),
            release_body.index("vm = tinypy_internal_value_vm(value);"),
        )
        self.assertNotIn("tinypy_internal_value_release", value_source)
        self.assertNotIn("tinypy_internal_value_release", internal_header)

    def test_comparison_recursion_is_a_debug_contract(self) -> None:
        public_header = public_api_text()
        internal_header = (ROOT / "src/core/internal.h").read_text(
            encoding="utf-8"
        )
        hash_source = (ROOT / "src/core/hash.c").read_text(
            encoding="utf-8"
        )
        dict_source = (ROOT / "src/core/dict.c").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("TINYPY_RECURSION_ERROR", public_header)
        self.assertNotIn("TINYPY_UNHASHABLE", public_header)
        self.assertNotIn("TINYPY_RECURSION_ERROR", hash_source)
        self.assertNotIn("TINYPY_RECURSION_ERROR", dict_source)
        self.assertIn(
            "int32_t tinypy_equal(const tinypy_value_t *left, const tinypy_value_t *right);",
            public_header,
        )
        self.assertIn(
            "tinypy_hash_t tinypy_hash(const tinypy_value_t *value);",
            public_header,
        )
        self.assertIn(
            "tinypy_hash_t tinypy_internal_hash_value(const tinypy_value_t *value);",
            internal_header,
        )
        self.assertIn(
            "void tinypy_list_extend(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count);",
            public_header,
        )
        self.assertIn(
            "int32_t tinypy_dict_contains(const tinypy_value_t *dict, const tinypy_value_t *key);",
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
            "int32_t tinypy_internal_equal_value(",
            internal_header,
        )
        self.assertIn(
            "assert(vm->hash_depth < TINYPY_COMPARE_RECURSION_LIMIT);",
            hash_source,
        )
        self.assertIn(
            "assert(vm->equality_depth < TINYPY_COMPARE_RECURSION_LIMIT);",
            hash_source,
        )
        self.assertIn(
            "assert(vm->equality_depth < 1000U);",
            dict_source,
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
        self.assertIn("typedef int32_t (*tinypy_init_slot_t)(", internal_header)

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
        self.assertIn("tinypy_type_t integer_type;", internal_header)
        self.assertNotRegex(internal_header, r"\bPY_INT_VALUE\b")
        self.assertNotRegex(internal_header, r"\bint_type\b")
        self.assertIn("TINYPY_BUILD_VALUE_INTEGER", public_header)
        self.assertIn("TINYPY_MARSHAL_TYPE_INTEGER", public_header)
        self.assertIn("tinypy_marshal_integer_value", public_header)
        self.assertIn("tinypy_complex_object_t", internal_header)
        self.assertIn("TINYPY_VALUE_COMPLEX", public_header)
        self.assertIn("tinypy_complex_from_doubles", public_header)
        self.assertIn("TINYPY_VALUE_COMPLEX", core_source)
        self.assertIn("complex_type", core_source)

    def test_internal_helpers_do_not_revalidate_vm(self) -> None:
        functions = (
            ("src/core/vm.c", "void *tinypy_internal_vm_allocate("),
            ("src/core/vm.c", "void *tinypy_internal_vm_reallocate("),
            ("src/core/value.c", "tinypy_type_t *tinypy_internal_type_for_kind("),
            ("src/core/value.c", "tinypy_value_t *tinypy_internal_object_allocate("),
            ("src/core/value.c", "void tinypy_internal_value_destroy("),
            ("src/core/error.c", "void tinypy_internal_make_vm_error("),
            ("src/core/list.c", "static void __tinypy_internal_list_validate("),
            (
                "src/core/tuple.c",
                "tinypy_value_t *tinypy_internal_tuple_from_borrowed_items(",
            ),
            ("src/core/dict.c", "static void __tinypy_internal_dict_validate("),
            (
                "src/core/type.c",
                "static tinypy_instance_object_t *__tinypy_internal_instance_validate(",
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
        self.assertIn("value >= TINYPY_INTEGER_CONSTANT_MIN", value_source)
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
                for match in declaration.finditer(text):
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
