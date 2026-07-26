#ifndef TINYPY_CORE_ASSERTION_H
#define TINYPY_CORE_ASSERTION_H

#include "tinypy/types.h"

void tinypy_internal_assertion_failed(const char *condition, const char *file, int32_t line);

#if defined(TINYPY_ENABLE_ASSERTS)
#define TINYPY_ASSERT(condition) \
    ((condition) ? (void)0 : tinypy_internal_assertion_failed(#condition, __FILE__, __LINE__))
#else
#define TINYPY_ASSERT(condition) ((void)0)
#endif

#endif
