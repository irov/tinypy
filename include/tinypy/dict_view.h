#ifndef TINYPY_DICT_VIEW_H
#define TINYPY_DICT_VIEW_H

#include "tinypy/types.h"

typedef enum tinypy_dict_view_kind_e {
    TINYPY_DICT_VIEW_KEYS = 0,
    TINYPY_DICT_VIEW_VALUES = 1,
    TINYPY_DICT_VIEW_ITEMS = 2
} tinypy_dict_view_kind_e;

tinypy_value_t *tinypy_dict_view_new(tinypy_value_t *dict, tinypy_dict_view_kind_e kind);

#endif
