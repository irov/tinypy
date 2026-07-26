#include "assertion.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

void tinypy_internal_assertion_failed(const char *condition, const char *file, int32_t line) {
    (void)condition;
    (void)file;
    (void)line;

#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    *(volatile int32_t *)0 = 0;
#endif
}
