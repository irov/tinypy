cmake_minimum_required(VERSION 3.16)

project(tinypy
    VERSION 0.1.0
    DESCRIPTION "Host-embedded Python 2.7 runtime core"
    LANGUAGES C
)

option(TINYPY_BUILD_CLI "Build the optional TinyPy CLI library and executables" OFF)
option(TINYPY_ENABLE_CYCLE_DIAGNOSTICS "Enable TinyPy cycle diagnostics in Debug builds" ON)
option(TINYPY_ENABLE_LTO "Enable supported interprocedural optimization in Release builds" OFF)

if(TINYPY_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT tinypy_ipo_supported OUTPUT tinypy_ipo_error LANGUAGES C)
    if(NOT tinypy_ipo_supported)
        message(FATAL_ERROR "TinyPy LTO is unavailable: ${tinypy_ipo_error}")
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

function(tinypy_enable_strict_warnings target)
    if(MSVC)
        # MSVC reports standard C99 flexible array members as C4200 even with
        # /std:c11 and /std:c17.
        target_compile_options(${target} PRIVATE /W4 /wd4200 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Werror
        )
    endif()
endfunction()

function(tinypy_enable_test_assertions target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
endfunction()

add_library(tinypy STATIC
    src/artifact/artifact.c
    src/artifact/sha256.c
    src/bytecode/bytecode_verify.c
    src/bytecode/opcode.c
    src/compiler/profile.c
    src/compiler/preprocessor.c
    src/compiler/meta.c
    src/compiler/render.c
    src/compiler/arena.c
    src/compiler/source.c
    src/compiler/value_ops.c
    src/compiler/decimal.c
    src/compiler/unicode_name.c
    src/compiler/literal.c
    src/compiler/compiler.c
    src/compiler/frontend/tokenizer.c
    src/compiler/frontend/cst.c
    src/compiler/frontend/parser.c
    src/compiler/frontend/source_parser.c
    src/compiler/generated/ast_nodes.c
    src/compiler/frontend/ast_builder.c
    src/compiler/frontend/future.c
    src/compiler/frontend/symbol_table.c
    src/compiler/frontend/codegen.c
    src/compiler/frontend/optimizer.c
    src/compiler/generated/grammar.c
    src/core/array.c
    src/core/dict.c
    src/core/dictproxy.c
    src/core/dict_view.c
    src/core/buffer.c
    src/core/build_value.c
    src/core/bytearray.c
    src/core/weakref.c
    src/core/set.c
    src/core/string_methods.c
    src/core/struct.c
    src/core/sre.c
    src/core/code.c
    src/core/cell.c
    src/core/class.c
    src/core/codecs.c
    src/core/comparison.c
    src/core/container_methods.c
    src/core/constructors.c
    src/core/cycle_diagnostics.c
    src/core/descriptor.c
    src/core/double_conversion.c
    src/core/error.c
    src/core/exception.c
    src/core/frame.c
    src/core/debugger.c
    src/core/functools.c
    src/core/function.c
    src/core/generator.c
    src/core/iterator.c
    src/core/item.c
    src/core/intern.c
    src/core/method.c
    src/core/module.c
    src/core/native.c
    src/core/object.c
    src/core/output.c
    src/core/pool.c
    src/core/representation.c
    src/core/operator.c
    src/core/slice.c
    src/core/super.c
    src/core/traceback.c
    src/core/hash.c
    src/core/list.c
    src/core/long.c
    src/core/numeric.c
    src/core/tuple.c
    src/core/type.c
    src/core/unicode.c
    src/core/value.c
    src/core/vm.c
    src/marshal/marshal.c
    src/marshal/dump.c
    src/marshal/load.c
    src/runtime/eval.c
    src/runtime/builtin.c
    src/runtime/import.c
)
add_library(tinypy::tinypy ALIAS tinypy)

target_compile_features(tinypy PUBLIC c_std_11)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tinypy PUBLIC TINYPY_DEBUGGER)
elseif(CMAKE_CONFIGURATION_TYPES)
    target_compile_definitions(tinypy PUBLIC $<$<CONFIG:Debug>:TINYPY_DEBUGGER>)
endif()
if(TINYPY_ENABLE_CYCLE_DIAGNOSTICS)
    target_compile_definitions(tinypy
        PUBLIC
            $<$<CONFIG:Debug>:TINYPY_CYCLE_DIAGNOSTICS>
    )
endif()
set_target_properties(tinypy PROPERTIES
    C_EXTENSIONS OFF
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)

target_include_directories(tinypy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/artifact
        ${CMAKE_CURRENT_SOURCE_DIR}/src/bytecode
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler/frontend
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler/generated
)

if(UNIX AND NOT APPLE)
    target_link_libraries(tinypy PUBLIC m)
endif()

tinypy_enable_strict_warnings(tinypy)

include(CTest)

if(TINYPY_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_TESTING)
    find_package(Threads REQUIRED)

    foreach(subsystem IN ITEMS opcode bytecode_verify)
        add_executable(tinypy_${subsystem}_tests tests/bytecode/test_${subsystem}.c)
        target_link_libraries(tinypy_${subsystem}_tests PRIVATE tinypy::tinypy)
        target_include_directories(tinypy_${subsystem}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/bytecode)
        target_compile_features(tinypy_${subsystem}_tests PRIVATE c_std_11)
        set_target_properties(tinypy_${subsystem}_tests PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
        tinypy_enable_test_assertions(tinypy_${subsystem}_tests)
        tinypy_enable_strict_warnings(tinypy_${subsystem}_tests)
        add_test(NAME bytecode_${subsystem} COMMAND tinypy_${subsystem}_tests)
    endforeach()

    foreach(subsystem IN ITEMS marshal artifact)
        add_executable(tinypy_${subsystem}_tests tests/${subsystem}/test_${subsystem}.c)
        target_link_libraries(tinypy_${subsystem}_tests PRIVATE tinypy::tinypy)
        target_include_directories(tinypy_${subsystem}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/artifact)
        target_compile_features(tinypy_${subsystem}_tests PRIVATE c_std_11)
        set_target_properties(tinypy_${subsystem}_tests PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
        tinypy_enable_test_assertions(tinypy_${subsystem}_tests)
        tinypy_enable_strict_warnings(tinypy_${subsystem}_tests)
        add_test(NAME ${subsystem}_codec COMMAND tinypy_${subsystem}_tests)
    endforeach()

    add_executable(tinypy_core_tests
        tests/core/test_core.c
    )

    target_link_libraries(tinypy_core_tests PRIVATE tinypy::tinypy)
    target_compile_features(tinypy_core_tests PRIVATE c_std_11)
    set_target_properties(tinypy_core_tests PROPERTIES
        C_EXTENSIONS OFF
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
    )
    tinypy_enable_test_assertions(tinypy_core_tests)
    tinypy_enable_strict_warnings(tinypy_core_tests)

    add_executable(tinypy_load_code_tests
        tests/runtime/test_load_code.c
    )
    target_link_libraries(tinypy_load_code_tests PRIVATE tinypy::tinypy)
    target_compile_features(tinypy_load_code_tests PRIVATE c_std_11)
    set_target_properties(tinypy_load_code_tests PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
    tinypy_enable_test_assertions(tinypy_load_code_tests)
    tinypy_enable_strict_warnings(tinypy_load_code_tests)

    add_executable(tinypy_compiler_tests
        tests/compiler/test_compiler.c
    )
    target_link_libraries(tinypy_compiler_tests PRIVATE tinypy::tinypy Threads::Threads)
    target_compile_features(tinypy_compiler_tests PRIVATE c_std_11)
    set_target_properties(tinypy_compiler_tests PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
    tinypy_enable_test_assertions(tinypy_compiler_tests)
    tinypy_enable_strict_warnings(tinypy_compiler_tests)

    add_library(tinypy_compiler_fuzz OBJECT tests/fuzz/fuzz_compiler.c)
    target_link_libraries(tinypy_compiler_fuzz PRIVATE tinypy::tinypy)
    target_compile_features(tinypy_compiler_fuzz PRIVATE c_std_11)
    set_target_properties(tinypy_compiler_fuzz PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
    tinypy_enable_test_assertions(tinypy_compiler_fuzz)
    tinypy_enable_strict_warnings(tinypy_compiler_fuzz)

    add_executable(tinypy_compiler_fuzz_standalone tests/fuzz/fuzz_compiler.c)
    target_compile_definitions(tinypy_compiler_fuzz_standalone PRIVATE TINYPY_FUZZ_STANDALONE=1)
    target_link_libraries(tinypy_compiler_fuzz_standalone PRIVATE tinypy::tinypy)
    target_compile_features(tinypy_compiler_fuzz_standalone PRIVATE c_std_11)
    set_target_properties(tinypy_compiler_fuzz_standalone PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
    tinypy_enable_test_assertions(tinypy_compiler_fuzz_standalone)
    tinypy_enable_strict_warnings(tinypy_compiler_fuzz_standalone)

    add_test(NAME runtime_subtype_factory_limits COMMAND tinypy_core_tests subtype_factory_limits)
    add_test(NAME runtime_intern_lifetime COMMAND tinypy_core_tests intern_lifetime)
    add_test(NAME runtime_float_hex_locale COMMAND tinypy_core_tests float_hex_locale)
    add_test(NAME core_allocator_accounting COMMAND tinypy_core_tests allocator)
    add_test(NAME core_vm_create_abi COMMAND tinypy_core_tests vm_create_abi)
    add_test(NAME core_pool_allocator COMMAND tinypy_core_tests pool_allocator)
    add_test(NAME core_independent_vms COMMAND tinypy_core_tests independent_vms)
    add_test(NAME core_value_lifetime COMMAND tinypy_core_tests value_lifetime)
    add_test(NAME core_constant_cache COMMAND tinypy_core_tests constant_cache)
    add_test(NAME core_byte_strings COMMAND tinypy_core_tests byte_strings)
    add_test(NAME core_build_value COMMAND tinypy_core_tests build_value)
    add_test(NAME core_unicode_utf8 COMMAND tinypy_core_tests unicode_utf8)
    add_test(NAME core_buffer_unicode_refusal COMMAND tinypy_core_tests buffer_unicode_refusal)
    add_test(NAME core_buffer_unsupported_owner COMMAND tinypy_core_tests buffer_unsupported_owner)
    add_test(
        NAME core_string_release_contract
        COMMAND tinypy_core_tests string_release)
    add_test(
        NAME core_float_complex_bits
        COMMAND tinypy_core_tests numeric_bits)
    add_test(NAME core_float_free_list COMMAND tinypy_core_tests float_free_list)
    add_test(NAME core_long_canonical COMMAND tinypy_core_tests long_canonical)
    add_test(NAME core_tuple_ownership COMMAND tinypy_core_tests tuple_ownership)
    add_test(NAME core_tuple_deep_release COMMAND tinypy_core_tests tuple_deep)
    add_test(NAME core_tuple_set_subclass COMMAND tinypy_core_tests tuple_set_subclass)
    add_test(NAME runtime_length_hint_past_reserve COMMAND tinypy_core_tests length_hint_past_reserve)
    add_test(NAME core_hash_and_equality COMMAND tinypy_core_tests hash_equal)
    add_test(NAME core_dictionary_runtime COMMAND tinypy_core_tests dict)
    add_test(NAME core_type_class_runtime COMMAND tinypy_core_tests type_class)
    add_test(NAME core_code_object_runtime COMMAND tinypy_core_tests code)
    add_test(NAME runtime_eval_frame COMMAND tinypy_core_tests eval_frame)
    add_test(NAME runtime_function_call COMMAND tinypy_core_tests function_call)
    add_test(NAME runtime_exception_state_lost COMMAND tinypy_core_tests exception_state_lost)
    add_test(NAME runtime_exception_contracts COMMAND tinypy_core_tests exception_contracts)
    add_test(NAME runtime_exception_diagnostics COMMAND tinypy_core_tests exception_diagnostics)
    add_test(NAME runtime_exception_normalization COMMAND tinypy_core_tests exception_normalization)
    add_test(NAME runtime_representation_errors COMMAND tinypy_core_tests representation_errors)
    add_test(NAME runtime_operator_numeric COMMAND tinypy_core_tests operator_numeric)
    add_test(NAME runtime_stack_budget COMMAND tinypy_core_tests stack_budget)
    add_test(NAME runtime_host_hash_native_guard COMMAND tinypy_core_tests host_hash_native_guard)
    add_test(NAME runtime_class_subclass_native_guard COMMAND tinypy_core_tests class_subclass_native_guard)
    add_test(NAME runtime_reduce_import_without_frame COMMAND tinypy_core_tests reduce_import_without_frame)
    add_test(NAME runtime_container_heap_limits COMMAND tinypy_core_tests container_heap_limits)
    add_test(NAME runtime_heap_budget_sweep COMMAND tinypy_core_tests heap_budget_sweep)
    add_test(NAME runtime_text_heap_budget COMMAND tinypy_core_tests text_heap_budget)
    add_test(NAME runtime_array_heap_budget COMMAND tinypy_core_tests array_heap_budget)
    add_test(NAME runtime_computed_heap_budget COMMAND tinypy_core_tests computed_heap_budget)
    add_test(NAME runtime_computed_allocation_refusal COMMAND tinypy_core_tests computed_allocation_refusal)
    add_test(NAME runtime_sre_allocation_refusal COMMAND tinypy_core_tests sre_allocation_refusal)
    add_test(NAME runtime_sre_interrupt COMMAND tinypy_core_tests sre_interrupt)
    add_test(NAME runtime_computed_result_refusal COMMAND tinypy_core_tests computed_result_refusal)
    add_test(NAME core_allocation_refusal COMMAND tinypy_core_tests allocation_refusal)
    add_test(NAME core_build_profile_allocation_refusal COMMAND tinypy_core_tests build_profile_allocation_refusal)
    add_test(NAME core_error_heap_accounting COMMAND tinypy_core_tests error_heap_accounting)
    add_test(NAME core_attribute_native_key_errors COMMAND tinypy_core_tests attribute_native_key_errors)
    add_test(NAME core_native_attribute_hook_class COMMAND tinypy_core_tests native_attribute_hook_class)
    add_test(NAME core_native_embedding COMMAND tinypy_core_tests native_embedding)
    add_test(NAME core_native_items COMMAND tinypy_core_tests native_items)
    add_test(NAME core_native_type_contracts COMMAND tinypy_core_tests native_type_contracts)
    add_test(NAME core_native_destroy_finalizers COMMAND tinypy_core_tests native_destroy_finalizers)
    add_test(NAME core_native_length_errors COMMAND tinypy_core_tests native_length_errors)
    add_test(NAME core_equal_reentrancy COMMAND tinypy_core_tests equal_reentrancy)
    add_test(NAME core_native_destroy_order COMMAND tinypy_core_tests native_destroy_order)
    add_test(NAME core_native_destroy_release COMMAND tinypy_core_tests native_destroy_release)
    add_test(NAME core_native_destroy_finalized COMMAND tinypy_core_tests native_destroy_finalized)
    add_test(NAME core_native_destroy_passes COMMAND tinypy_core_tests native_destroy_passes)
    add_test(NAME core_native_destroy_runtime_values COMMAND tinypy_core_tests native_destroy_runtime_values)
    add_test(NAME core_native_failure_signals COMMAND tinypy_core_tests native_failure_signals)
    add_test(NAME core_native_finalizer_handoff COMMAND tinypy_core_tests native_finalizer_handoff)
    add_test(NAME core_native_finalizer_nested COMMAND tinypy_core_tests native_finalizer_nested)
    add_test(NAME core_error_reentrant_release COMMAND tinypy_core_tests error_reentrant_release)
    add_test(NAME core_error_pending_diagnostics COMMAND tinypy_core_tests error_pending_diagnostics)
    add_test(NAME core_native_slot_dispatch COMMAND tinypy_core_tests native_slot_dispatch)
    add_test(NAME core_native_slot_failures COMMAND tinypy_core_tests native_slot_failures)
    add_test(NAME core_native_slot_wrappers COMMAND tinypy_core_tests native_slot_wrappers)
    add_test(NAME core_native_special_lookups COMMAND tinypy_core_tests native_special_lookups)
    add_test(NAME core_type_versions COMMAND tinypy_core_tests type_versions)
    add_test(NAME core_type_new_metaclass COMMAND tinypy_core_tests type_new_metaclass)
    add_test(NAME core_type_set_attr_sharing COMMAND tinypy_core_tests type_set_attr_sharing)
    add_test(NAME core_compile_pending_reset COMMAND tinypy_core_tests compile_pending_reset)
    add_test(NAME core_generator_embedding COMMAND tinypy_core_tests generator_embedding)
    add_test(NAME runtime_module_finder COMMAND tinypy_core_tests module_finder)
    add_test(NAME runtime_module_finder_protocol COMMAND tinypy_core_tests module_finder_protocol)
    add_test(NAME runtime_module_resolver_contract COMMAND tinypy_core_tests module_resolver_contract)
    add_test(NAME core_cycle_diagnostics COMMAND tinypy_core_tests cycle_diagnostics)
    add_test(NAME core_debugger_frame_slots COMMAND tinypy_core_tests debugger_frame_slots)
    add_test(NAME core_debugger_mapping_locals COMMAND tinypy_core_tests debugger_mapping_locals)
    add_test(NAME core_debugger_return_events COMMAND tinypy_core_tests debugger_return_events)
    add_test(NAME core_debugger_line_events COMMAND tinypy_core_tests debugger_line_events)
    add_test(NAME core_debugger_exception_events COMMAND tinypy_core_tests debugger_exception_events)
    add_test(NAME core_debugger_interrupt COMMAND tinypy_core_tests debugger_interrupt)
    add_test(NAME core_debugger_frame_sync COMMAND tinypy_core_tests debugger_frame_sync)
    add_test(NAME core_debugger_interrupt_scope COMMAND tinypy_core_tests debugger_interrupt_scope)
    add_test(NAME core_debugger_frame_cells COMMAND tinypy_core_tests debugger_frame_cells)
    add_test(NAME core_debugger_generator_close COMMAND tinypy_core_tests debugger_generator_close)
    add_test(NAME runtime_marshal_code_materialization COMMAND tinypy_load_code_tests)
    add_test(NAME runtime_marshal_load_memory COMMAND tinypy_core_tests marshal_load_memory)
    add_test(NAME runtime_marshal_load_unhashable_key COMMAND tinypy_core_tests marshal_load_unhashable_key)
    add_test(NAME runtime_marshal_dump_memory COMMAND tinypy_core_tests marshal_dump_memory)
    add_test(NAME runtime_marshal_one_byte_strings COMMAND tinypy_core_tests marshal_one_byte_strings)
    add_test(NAME core_code_new_validation COMMAND tinypy_core_tests code_new_validation)
    add_test(NAME compiler_memory_api COMMAND tinypy_compiler_tests)
    add_test(NAME compiler_bounded_fuzz COMMAND tinypy_compiler_fuzz_standalone)
endif()
