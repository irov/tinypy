add_library(tinypy_cli STATIC
    src/cli.c
    src/compile.c
)
add_library(tinypy::cli ALIAS tinypy_cli)

target_link_libraries(tinypy_cli PUBLIC tinypy::tinypy)
target_compile_features(tinypy_cli PUBLIC c_std_11)
set_target_properties(tinypy_cli PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_include_directories(tinypy_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
tinypy_enable_strict_warnings(tinypy_cli)

add_executable(tinypy_console tools/main.c)
target_link_libraries(tinypy_console PRIVATE tinypy::cli)
target_compile_features(tinypy_console PRIVATE c_std_11)
set_target_properties(tinypy_console PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES OUTPUT_NAME tinypy)
tinypy_enable_strict_warnings(tinypy_console)

add_executable(tinypy_compile tools/compile_main.c)
target_link_libraries(tinypy_compile PRIVATE tinypy::cli)
target_compile_features(tinypy_compile PRIVATE c_std_11)
set_target_properties(tinypy_compile PROPERTIES C_EXTENSIONS OFF C_STANDARD 11 C_STANDARD_REQUIRED YES)
tinypy_enable_strict_warnings(tinypy_compile)

if(BUILD_TESTING)
    add_test(NAME cli_command COMMAND tinypy_console -c "assert 6 * 7 == 42")

    set(tinypy_runtime_source_fixtures
        buffer_runtime
        bytearray_runtime
        class_method
        codecs_runtime
        constructors_runtime
        container_methods
        dict_view_runtime
        exception_custom
        exception_finally
        exception_minimal
        exception_nested
        exception_none
        exception_override
        exception_reraise
        finalizer_runtime
        functools_runtime
        generator
        metaclass_checks_runtime
        metaclass_runtime
        multislots_runtime
        object_new_runtime
        old_class_runtime
        output_runtime
        set_runtime
        slots_runtime
        string_methods
        struct_runtime
        sys_runtime
        tuple_subclass_runtime
        weakref_runtime
        with_statement
    )

    foreach(fixture IN LISTS tinypy_runtime_source_fixtures)
        add_test(
            NAME cli_runtime_${fixture}
            COMMAND tinypy_console ${PROJECT_SOURCE_DIR}/tests/runtime/fixtures/${fixture}.py)
    endforeach()

    add_test(NAME cli_python_error_format COMMAND ${CMAKE_COMMAND} -DTINYPY_EXECUTABLE=$<TARGET_FILE:tinypy_console> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/error_format.cmake)
    add_test(NAME cli_allocator_accounting COMMAND tinypy_console --stats -c "pass")
    add_test(NAME cli_stress_speed COMMAND tinypy_console --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_stress.py speed 1000)
    add_test(NAME cli_stress_calls COMMAND tinypy_console --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_stress.py calls 1000)
    add_test(NAME cli_stress_attributes COMMAND tinypy_console --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_stress.py attributes 1000)
    add_test(NAME cli_stress_compiler COMMAND tinypy_console --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_stress.py compiler 10)
    add_test(NAME cli_stress_churn COMMAND tinypy_console --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_stress.py churn 10)
    add_test(NAME cli_stress_memory COMMAND tinypy_console --stats ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_stress.py memory 1000)
endif()
