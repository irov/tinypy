#include "opcode.h"

#include <assert.h>

#define TINYPY_OPCODE_ALL_CATEGORIES \
    ((uint32_t)TINYPY_OPCODE_CATEGORY_CONST | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_NAME | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_JREL | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_JABS | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_LOCAL | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_COMPARE | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_FREE)

static const tinypy_opcode_info_t tinypy_opcode_table[TINYPY_OPCODE_COUNT] = {
    {"STOP_CODE", TINYPY_OPCODE_CATEGORY_NONE, 0U, 1U, 0U, 0U},
    {"POP_TOP", TINYPY_OPCODE_CATEGORY_NONE, 1U, 1U, 0U, 0U},
    {"ROT_TWO", TINYPY_OPCODE_CATEGORY_NONE, 2U, 1U, 0U, 0U},
    {"ROT_THREE", TINYPY_OPCODE_CATEGORY_NONE, 3U, 1U, 0U, 0U},
    {"DUP_TOP", TINYPY_OPCODE_CATEGORY_NONE, 4U, 1U, 0U, 0U},
    {"ROT_FOUR", TINYPY_OPCODE_CATEGORY_NONE, 5U, 1U, 0U, 0U},
    {"<6>", TINYPY_OPCODE_CATEGORY_NONE, 6U, 0U, 0U, 0U},
    {"<7>", TINYPY_OPCODE_CATEGORY_NONE, 7U, 0U, 0U, 0U},
    {"<8>", TINYPY_OPCODE_CATEGORY_NONE, 8U, 0U, 0U, 0U},
    {"NOP", TINYPY_OPCODE_CATEGORY_NONE, 9U, 1U, 0U, 0U},
    {"UNARY_POSITIVE", TINYPY_OPCODE_CATEGORY_NONE, 10U, 1U, 0U, 0U},
    {"UNARY_NEGATIVE", TINYPY_OPCODE_CATEGORY_NONE, 11U, 1U, 0U, 0U},
    {"UNARY_NOT", TINYPY_OPCODE_CATEGORY_NONE, 12U, 1U, 0U, 0U},
    {"UNARY_CONVERT", TINYPY_OPCODE_CATEGORY_NONE, 13U, 1U, 0U, 0U},
    {"<14>", TINYPY_OPCODE_CATEGORY_NONE, 14U, 0U, 0U, 0U},
    {"UNARY_INVERT", TINYPY_OPCODE_CATEGORY_NONE, 15U, 1U, 0U, 0U},
    {"<16>", TINYPY_OPCODE_CATEGORY_NONE, 16U, 0U, 0U, 0U},
    {"<17>", TINYPY_OPCODE_CATEGORY_NONE, 17U, 0U, 0U, 0U},
    {"<18>", TINYPY_OPCODE_CATEGORY_NONE, 18U, 0U, 0U, 0U},
    {"BINARY_POWER", TINYPY_OPCODE_CATEGORY_NONE, 19U, 1U, 0U, 0U},
    {"BINARY_MULTIPLY", TINYPY_OPCODE_CATEGORY_NONE, 20U, 1U, 0U, 0U},
    {"BINARY_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE, 21U, 1U, 0U, 0U},
    {"BINARY_MODULO", TINYPY_OPCODE_CATEGORY_NONE, 22U, 1U, 0U, 0U},
    {"BINARY_ADD", TINYPY_OPCODE_CATEGORY_NONE, 23U, 1U, 0U, 0U},
    {"BINARY_SUBTRACT", TINYPY_OPCODE_CATEGORY_NONE, 24U, 1U, 0U, 0U},
    {"BINARY_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE, 25U, 1U, 0U, 0U},
    {"BINARY_FLOOR_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE, 26U, 1U, 0U, 0U},
    {"BINARY_TRUE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE, 27U, 1U, 0U, 0U},
    {"INPLACE_FLOOR_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE, 28U, 1U, 0U, 0U},
    {"INPLACE_TRUE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE, 29U, 1U, 0U, 0U},
    {"SLICE+0", TINYPY_OPCODE_CATEGORY_NONE, 30U, 1U, 0U, 0U},
    {"SLICE+1", TINYPY_OPCODE_CATEGORY_NONE, 31U, 1U, 0U, 0U},
    {"SLICE+2", TINYPY_OPCODE_CATEGORY_NONE, 32U, 1U, 0U, 0U},
    {"SLICE+3", TINYPY_OPCODE_CATEGORY_NONE, 33U, 1U, 0U, 0U},
    {"<34>", TINYPY_OPCODE_CATEGORY_NONE, 34U, 0U, 0U, 0U},
    {"<35>", TINYPY_OPCODE_CATEGORY_NONE, 35U, 0U, 0U, 0U},
    {"<36>", TINYPY_OPCODE_CATEGORY_NONE, 36U, 0U, 0U, 0U},
    {"<37>", TINYPY_OPCODE_CATEGORY_NONE, 37U, 0U, 0U, 0U},
    {"<38>", TINYPY_OPCODE_CATEGORY_NONE, 38U, 0U, 0U, 0U},
    {"<39>", TINYPY_OPCODE_CATEGORY_NONE, 39U, 0U, 0U, 0U},
    {"STORE_SLICE+0", TINYPY_OPCODE_CATEGORY_NONE, 40U, 1U, 0U, 0U},
    {"STORE_SLICE+1", TINYPY_OPCODE_CATEGORY_NONE, 41U, 1U, 0U, 0U},
    {"STORE_SLICE+2", TINYPY_OPCODE_CATEGORY_NONE, 42U, 1U, 0U, 0U},
    {"STORE_SLICE+3", TINYPY_OPCODE_CATEGORY_NONE, 43U, 1U, 0U, 0U},
    {"<44>", TINYPY_OPCODE_CATEGORY_NONE, 44U, 0U, 0U, 0U},
    {"<45>", TINYPY_OPCODE_CATEGORY_NONE, 45U, 0U, 0U, 0U},
    {"<46>", TINYPY_OPCODE_CATEGORY_NONE, 46U, 0U, 0U, 0U},
    {"<47>", TINYPY_OPCODE_CATEGORY_NONE, 47U, 0U, 0U, 0U},
    {"<48>", TINYPY_OPCODE_CATEGORY_NONE, 48U, 0U, 0U, 0U},
    {"<49>", TINYPY_OPCODE_CATEGORY_NONE, 49U, 0U, 0U, 0U},
    {"DELETE_SLICE+0", TINYPY_OPCODE_CATEGORY_NONE, 50U, 1U, 0U, 0U},
    {"DELETE_SLICE+1", TINYPY_OPCODE_CATEGORY_NONE, 51U, 1U, 0U, 0U},
    {"DELETE_SLICE+2", TINYPY_OPCODE_CATEGORY_NONE, 52U, 1U, 0U, 0U},
    {"DELETE_SLICE+3", TINYPY_OPCODE_CATEGORY_NONE, 53U, 1U, 0U, 0U},
    {"STORE_MAP", TINYPY_OPCODE_CATEGORY_NONE, 54U, 1U, 0U, 0U},
    {"INPLACE_ADD", TINYPY_OPCODE_CATEGORY_NONE, 55U, 1U, 0U, 0U},
    {"INPLACE_SUBTRACT", TINYPY_OPCODE_CATEGORY_NONE, 56U, 1U, 0U, 0U},
    {"INPLACE_MULTIPLY", TINYPY_OPCODE_CATEGORY_NONE, 57U, 1U, 0U, 0U},
    {"INPLACE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE, 58U, 1U, 0U, 0U},
    {"INPLACE_MODULO", TINYPY_OPCODE_CATEGORY_NONE, 59U, 1U, 0U, 0U},
    {"STORE_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE, 60U, 1U, 0U, 0U},
    {"DELETE_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE, 61U, 1U, 0U, 0U},
    {"BINARY_LSHIFT", TINYPY_OPCODE_CATEGORY_NONE, 62U, 1U, 0U, 0U},
    {"BINARY_RSHIFT", TINYPY_OPCODE_CATEGORY_NONE, 63U, 1U, 0U, 0U},
    {"BINARY_AND", TINYPY_OPCODE_CATEGORY_NONE, 64U, 1U, 0U, 0U},
    {"BINARY_XOR", TINYPY_OPCODE_CATEGORY_NONE, 65U, 1U, 0U, 0U},
    {"BINARY_OR", TINYPY_OPCODE_CATEGORY_NONE, 66U, 1U, 0U, 0U},
    {"INPLACE_POWER", TINYPY_OPCODE_CATEGORY_NONE, 67U, 1U, 0U, 0U},
    {"GET_ITER", TINYPY_OPCODE_CATEGORY_NONE, 68U, 1U, 0U, 0U},
    {"<69>", TINYPY_OPCODE_CATEGORY_NONE, 69U, 0U, 0U, 0U},
    {"PRINT_EXPR", TINYPY_OPCODE_CATEGORY_NONE, 70U, 1U, 0U, 0U},
    {"PRINT_ITEM", TINYPY_OPCODE_CATEGORY_NONE, 71U, 1U, 0U, 0U},
    {"PRINT_NEWLINE", TINYPY_OPCODE_CATEGORY_NONE, 72U, 1U, 0U, 0U},
    {"PRINT_ITEM_TO", TINYPY_OPCODE_CATEGORY_NONE, 73U, 1U, 0U, 0U},
    {"PRINT_NEWLINE_TO", TINYPY_OPCODE_CATEGORY_NONE, 74U, 1U, 0U, 0U},
    {"INPLACE_LSHIFT", TINYPY_OPCODE_CATEGORY_NONE, 75U, 1U, 0U, 0U},
    {"INPLACE_RSHIFT", TINYPY_OPCODE_CATEGORY_NONE, 76U, 1U, 0U, 0U},
    {"INPLACE_AND", TINYPY_OPCODE_CATEGORY_NONE, 77U, 1U, 0U, 0U},
    {"INPLACE_XOR", TINYPY_OPCODE_CATEGORY_NONE, 78U, 1U, 0U, 0U},
    {"INPLACE_OR", TINYPY_OPCODE_CATEGORY_NONE, 79U, 1U, 0U, 0U},
    {"BREAK_LOOP", TINYPY_OPCODE_CATEGORY_NONE, 80U, 1U, 0U, 0U},
    {"WITH_CLEANUP", TINYPY_OPCODE_CATEGORY_NONE, 81U, 1U, 0U, 0U},
    {"LOAD_LOCALS", TINYPY_OPCODE_CATEGORY_NONE, 82U, 1U, 0U, 0U},
    {"RETURN_VALUE", TINYPY_OPCODE_CATEGORY_NONE, 83U, 1U, 0U, 0U},
    {"IMPORT_STAR", TINYPY_OPCODE_CATEGORY_NONE, 84U, 1U, 0U, 0U},
    {"EXEC_STMT", TINYPY_OPCODE_CATEGORY_NONE, 85U, 1U, 0U, 0U},
    {"YIELD_VALUE", TINYPY_OPCODE_CATEGORY_NONE, 86U, 1U, 0U, 0U},
    {"POP_BLOCK", TINYPY_OPCODE_CATEGORY_NONE, 87U, 1U, 0U, 0U},
    {"END_FINALLY", TINYPY_OPCODE_CATEGORY_NONE, 88U, 1U, 0U, 0U},
    {"BUILD_CLASS", TINYPY_OPCODE_CATEGORY_NONE, 89U, 1U, 0U, 0U},
    {"STORE_NAME", TINYPY_OPCODE_CATEGORY_NAME, 90U, 1U, 1U, 0U},
    {"DELETE_NAME", TINYPY_OPCODE_CATEGORY_NAME, 91U, 1U, 1U, 0U},
    {"UNPACK_SEQUENCE", TINYPY_OPCODE_CATEGORY_NONE, 92U, 1U, 1U, 0U},
    {"FOR_ITER", TINYPY_OPCODE_CATEGORY_JREL, 93U, 1U, 1U, 0U},
    {"LIST_APPEND", TINYPY_OPCODE_CATEGORY_NONE, 94U, 1U, 1U, 0U},
    {"STORE_ATTR", TINYPY_OPCODE_CATEGORY_NAME, 95U, 1U, 1U, 0U},
    {"DELETE_ATTR", TINYPY_OPCODE_CATEGORY_NAME, 96U, 1U, 1U, 0U},
    {"STORE_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME, 97U, 1U, 1U, 0U},
    {"DELETE_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME, 98U, 1U, 1U, 0U},
    {"DUP_TOPX", TINYPY_OPCODE_CATEGORY_NONE, 99U, 1U, 1U, 0U},
    {"LOAD_CONST", TINYPY_OPCODE_CATEGORY_CONST, 100U, 1U, 1U, 0U},
    {"LOAD_NAME", TINYPY_OPCODE_CATEGORY_NAME, 101U, 1U, 1U, 0U},
    {"BUILD_TUPLE", TINYPY_OPCODE_CATEGORY_NONE, 102U, 1U, 1U, 0U},
    {"BUILD_LIST", TINYPY_OPCODE_CATEGORY_NONE, 103U, 1U, 1U, 0U},
    {"BUILD_SET", TINYPY_OPCODE_CATEGORY_NONE, 104U, 1U, 1U, 0U},
    {"BUILD_MAP", TINYPY_OPCODE_CATEGORY_NONE, 105U, 1U, 1U, 0U},
    {"LOAD_ATTR", TINYPY_OPCODE_CATEGORY_NAME, 106U, 1U, 1U, 0U},
    {"COMPARE_OP", TINYPY_OPCODE_CATEGORY_COMPARE, 107U, 1U, 1U, 0U},
    {"IMPORT_NAME", TINYPY_OPCODE_CATEGORY_NAME, 108U, 1U, 1U, 0U},
    {"IMPORT_FROM", TINYPY_OPCODE_CATEGORY_NAME, 109U, 1U, 1U, 0U},
    {"JUMP_FORWARD", TINYPY_OPCODE_CATEGORY_JREL, 110U, 1U, 1U, 0U},
    {"JUMP_IF_FALSE_OR_POP", TINYPY_OPCODE_CATEGORY_JABS, 111U, 1U, 1U, 0U},
    {"JUMP_IF_TRUE_OR_POP", TINYPY_OPCODE_CATEGORY_JABS, 112U, 1U, 1U, 0U},
    {"JUMP_ABSOLUTE", TINYPY_OPCODE_CATEGORY_JABS, 113U, 1U, 1U, 0U},
    {"POP_JUMP_IF_FALSE", TINYPY_OPCODE_CATEGORY_JABS, 114U, 1U, 1U, 0U},
    {"POP_JUMP_IF_TRUE", TINYPY_OPCODE_CATEGORY_JABS, 115U, 1U, 1U, 0U},
    {"LOAD_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME, 116U, 1U, 1U, 0U},
    {"<117>", TINYPY_OPCODE_CATEGORY_NONE, 117U, 0U, 1U, 0U},
    {"<118>", TINYPY_OPCODE_CATEGORY_NONE, 118U, 0U, 1U, 0U},
    {"CONTINUE_LOOP", TINYPY_OPCODE_CATEGORY_JABS, 119U, 1U, 1U, 0U},
    {"SETUP_LOOP", TINYPY_OPCODE_CATEGORY_JREL, 120U, 1U, 1U, 0U},
    {"SETUP_EXCEPT", TINYPY_OPCODE_CATEGORY_JREL, 121U, 1U, 1U, 0U},
    {"SETUP_FINALLY", TINYPY_OPCODE_CATEGORY_JREL, 122U, 1U, 1U, 0U},
    {"<123>", TINYPY_OPCODE_CATEGORY_NONE, 123U, 0U, 1U, 0U},
    {"LOAD_FAST", TINYPY_OPCODE_CATEGORY_LOCAL, 124U, 1U, 1U, 0U},
    {"STORE_FAST", TINYPY_OPCODE_CATEGORY_LOCAL, 125U, 1U, 1U, 0U},
    {"DELETE_FAST", TINYPY_OPCODE_CATEGORY_LOCAL, 126U, 1U, 1U, 0U},
    {"<127>", TINYPY_OPCODE_CATEGORY_NONE, 127U, 0U, 1U, 0U},
    {"<128>", TINYPY_OPCODE_CATEGORY_NONE, 128U, 0U, 1U, 0U},
    {"<129>", TINYPY_OPCODE_CATEGORY_NONE, 129U, 0U, 1U, 0U},
    {"RAISE_VARARGS", TINYPY_OPCODE_CATEGORY_NONE, 130U, 1U, 1U, 0U},
    {"CALL_FUNCTION", TINYPY_OPCODE_CATEGORY_NONE, 131U, 1U, 1U, 0U},
    {"MAKE_FUNCTION", TINYPY_OPCODE_CATEGORY_NONE, 132U, 1U, 1U, 0U},
    {"BUILD_SLICE", TINYPY_OPCODE_CATEGORY_NONE, 133U, 1U, 1U, 0U},
    {"MAKE_CLOSURE", TINYPY_OPCODE_CATEGORY_NONE, 134U, 1U, 1U, 0U},
    {"LOAD_CLOSURE", TINYPY_OPCODE_CATEGORY_FREE, 135U, 1U, 1U, 0U},
    {"LOAD_DEREF", TINYPY_OPCODE_CATEGORY_FREE, 136U, 1U, 1U, 0U},
    {"STORE_DEREF", TINYPY_OPCODE_CATEGORY_FREE, 137U, 1U, 1U, 0U},
    {"<138>", TINYPY_OPCODE_CATEGORY_NONE, 138U, 0U, 1U, 0U},
    {"<139>", TINYPY_OPCODE_CATEGORY_NONE, 139U, 0U, 1U, 0U},
    {"CALL_FUNCTION_VAR", TINYPY_OPCODE_CATEGORY_NONE, 140U, 1U, 1U, 0U},
    {"CALL_FUNCTION_KW", TINYPY_OPCODE_CATEGORY_NONE, 141U, 1U, 1U, 0U},
    {"CALL_FUNCTION_VAR_KW", TINYPY_OPCODE_CATEGORY_NONE, 142U, 1U, 1U, 0U},
    {"SETUP_WITH", TINYPY_OPCODE_CATEGORY_JREL, 143U, 1U, 1U, 0U},
    {"<144>", TINYPY_OPCODE_CATEGORY_NONE, 144U, 0U, 1U, 0U},
    {"EXTENDED_ARG", TINYPY_OPCODE_CATEGORY_NONE, 145U, 1U, 1U, 0U},
    {"SET_ADD", TINYPY_OPCODE_CATEGORY_NONE, 146U, 1U, 1U, 0U},
    {"MAP_ADD", TINYPY_OPCODE_CATEGORY_NONE, 147U, 1U, 1U, 0U},
    {"<148>", TINYPY_OPCODE_CATEGORY_NONE, 148U, 0U, 1U, 0U},
    {"<149>", TINYPY_OPCODE_CATEGORY_NONE, 149U, 0U, 1U, 0U},
    {"<150>", TINYPY_OPCODE_CATEGORY_NONE, 150U, 0U, 1U, 0U},
    {"<151>", TINYPY_OPCODE_CATEGORY_NONE, 151U, 0U, 1U, 0U},
    {"<152>", TINYPY_OPCODE_CATEGORY_NONE, 152U, 0U, 1U, 0U},
    {"<153>", TINYPY_OPCODE_CATEGORY_NONE, 153U, 0U, 1U, 0U},
    {"<154>", TINYPY_OPCODE_CATEGORY_NONE, 154U, 0U, 1U, 0U},
    {"<155>", TINYPY_OPCODE_CATEGORY_NONE, 155U, 0U, 1U, 0U},
    {"<156>", TINYPY_OPCODE_CATEGORY_NONE, 156U, 0U, 1U, 0U},
    {"<157>", TINYPY_OPCODE_CATEGORY_NONE, 157U, 0U, 1U, 0U},
    {"<158>", TINYPY_OPCODE_CATEGORY_NONE, 158U, 0U, 1U, 0U},
    {"<159>", TINYPY_OPCODE_CATEGORY_NONE, 159U, 0U, 1U, 0U},
    {"<160>", TINYPY_OPCODE_CATEGORY_NONE, 160U, 0U, 1U, 0U},
    {"<161>", TINYPY_OPCODE_CATEGORY_NONE, 161U, 0U, 1U, 0U},
    {"<162>", TINYPY_OPCODE_CATEGORY_NONE, 162U, 0U, 1U, 0U},
    {"<163>", TINYPY_OPCODE_CATEGORY_NONE, 163U, 0U, 1U, 0U},
    {"<164>", TINYPY_OPCODE_CATEGORY_NONE, 164U, 0U, 1U, 0U},
    {"<165>", TINYPY_OPCODE_CATEGORY_NONE, 165U, 0U, 1U, 0U},
    {"<166>", TINYPY_OPCODE_CATEGORY_NONE, 166U, 0U, 1U, 0U},
    {"<167>", TINYPY_OPCODE_CATEGORY_NONE, 167U, 0U, 1U, 0U},
    {"<168>", TINYPY_OPCODE_CATEGORY_NONE, 168U, 0U, 1U, 0U},
    {"<169>", TINYPY_OPCODE_CATEGORY_NONE, 169U, 0U, 1U, 0U},
    {"<170>", TINYPY_OPCODE_CATEGORY_NONE, 170U, 0U, 1U, 0U},
    {"<171>", TINYPY_OPCODE_CATEGORY_NONE, 171U, 0U, 1U, 0U},
    {"<172>", TINYPY_OPCODE_CATEGORY_NONE, 172U, 0U, 1U, 0U},
    {"<173>", TINYPY_OPCODE_CATEGORY_NONE, 173U, 0U, 1U, 0U},
    {"<174>", TINYPY_OPCODE_CATEGORY_NONE, 174U, 0U, 1U, 0U},
    {"<175>", TINYPY_OPCODE_CATEGORY_NONE, 175U, 0U, 1U, 0U},
    {"<176>", TINYPY_OPCODE_CATEGORY_NONE, 176U, 0U, 1U, 0U},
    {"<177>", TINYPY_OPCODE_CATEGORY_NONE, 177U, 0U, 1U, 0U},
    {"<178>", TINYPY_OPCODE_CATEGORY_NONE, 178U, 0U, 1U, 0U},
    {"<179>", TINYPY_OPCODE_CATEGORY_NONE, 179U, 0U, 1U, 0U},
    {"<180>", TINYPY_OPCODE_CATEGORY_NONE, 180U, 0U, 1U, 0U},
    {"<181>", TINYPY_OPCODE_CATEGORY_NONE, 181U, 0U, 1U, 0U},
    {"<182>", TINYPY_OPCODE_CATEGORY_NONE, 182U, 0U, 1U, 0U},
    {"<183>", TINYPY_OPCODE_CATEGORY_NONE, 183U, 0U, 1U, 0U},
    {"<184>", TINYPY_OPCODE_CATEGORY_NONE, 184U, 0U, 1U, 0U},
    {"<185>", TINYPY_OPCODE_CATEGORY_NONE, 185U, 0U, 1U, 0U},
    {"<186>", TINYPY_OPCODE_CATEGORY_NONE, 186U, 0U, 1U, 0U},
    {"<187>", TINYPY_OPCODE_CATEGORY_NONE, 187U, 0U, 1U, 0U},
    {"<188>", TINYPY_OPCODE_CATEGORY_NONE, 188U, 0U, 1U, 0U},
    {"<189>", TINYPY_OPCODE_CATEGORY_NONE, 189U, 0U, 1U, 0U},
    {"<190>", TINYPY_OPCODE_CATEGORY_NONE, 190U, 0U, 1U, 0U},
    {"<191>", TINYPY_OPCODE_CATEGORY_NONE, 191U, 0U, 1U, 0U},
    {"<192>", TINYPY_OPCODE_CATEGORY_NONE, 192U, 0U, 1U, 0U},
    {"<193>", TINYPY_OPCODE_CATEGORY_NONE, 193U, 0U, 1U, 0U},
    {"<194>", TINYPY_OPCODE_CATEGORY_NONE, 194U, 0U, 1U, 0U},
    {"<195>", TINYPY_OPCODE_CATEGORY_NONE, 195U, 0U, 1U, 0U},
    {"<196>", TINYPY_OPCODE_CATEGORY_NONE, 196U, 0U, 1U, 0U},
    {"<197>", TINYPY_OPCODE_CATEGORY_NONE, 197U, 0U, 1U, 0U},
    {"<198>", TINYPY_OPCODE_CATEGORY_NONE, 198U, 0U, 1U, 0U},
    {"<199>", TINYPY_OPCODE_CATEGORY_NONE, 199U, 0U, 1U, 0U},
    {"<200>", TINYPY_OPCODE_CATEGORY_NONE, 200U, 0U, 1U, 0U},
    {"<201>", TINYPY_OPCODE_CATEGORY_NONE, 201U, 0U, 1U, 0U},
    {"<202>", TINYPY_OPCODE_CATEGORY_NONE, 202U, 0U, 1U, 0U},
    {"<203>", TINYPY_OPCODE_CATEGORY_NONE, 203U, 0U, 1U, 0U},
    {"<204>", TINYPY_OPCODE_CATEGORY_NONE, 204U, 0U, 1U, 0U},
    {"<205>", TINYPY_OPCODE_CATEGORY_NONE, 205U, 0U, 1U, 0U},
    {"<206>", TINYPY_OPCODE_CATEGORY_NONE, 206U, 0U, 1U, 0U},
    {"<207>", TINYPY_OPCODE_CATEGORY_NONE, 207U, 0U, 1U, 0U},
    {"<208>", TINYPY_OPCODE_CATEGORY_NONE, 208U, 0U, 1U, 0U},
    {"<209>", TINYPY_OPCODE_CATEGORY_NONE, 209U, 0U, 1U, 0U},
    {"<210>", TINYPY_OPCODE_CATEGORY_NONE, 210U, 0U, 1U, 0U},
    {"<211>", TINYPY_OPCODE_CATEGORY_NONE, 211U, 0U, 1U, 0U},
    {"<212>", TINYPY_OPCODE_CATEGORY_NONE, 212U, 0U, 1U, 0U},
    {"<213>", TINYPY_OPCODE_CATEGORY_NONE, 213U, 0U, 1U, 0U},
    {"<214>", TINYPY_OPCODE_CATEGORY_NONE, 214U, 0U, 1U, 0U},
    {"<215>", TINYPY_OPCODE_CATEGORY_NONE, 215U, 0U, 1U, 0U},
    {"<216>", TINYPY_OPCODE_CATEGORY_NONE, 216U, 0U, 1U, 0U},
    {"<217>", TINYPY_OPCODE_CATEGORY_NONE, 217U, 0U, 1U, 0U},
    {"<218>", TINYPY_OPCODE_CATEGORY_NONE, 218U, 0U, 1U, 0U},
    {"<219>", TINYPY_OPCODE_CATEGORY_NONE, 219U, 0U, 1U, 0U},
    {"<220>", TINYPY_OPCODE_CATEGORY_NONE, 220U, 0U, 1U, 0U},
    {"<221>", TINYPY_OPCODE_CATEGORY_NONE, 221U, 0U, 1U, 0U},
    {"<222>", TINYPY_OPCODE_CATEGORY_NONE, 222U, 0U, 1U, 0U},
    {"<223>", TINYPY_OPCODE_CATEGORY_NONE, 223U, 0U, 1U, 0U},
    {"<224>", TINYPY_OPCODE_CATEGORY_NONE, 224U, 0U, 1U, 0U},
    {"<225>", TINYPY_OPCODE_CATEGORY_NONE, 225U, 0U, 1U, 0U},
    {"<226>", TINYPY_OPCODE_CATEGORY_NONE, 226U, 0U, 1U, 0U},
    {"<227>", TINYPY_OPCODE_CATEGORY_NONE, 227U, 0U, 1U, 0U},
    {"<228>", TINYPY_OPCODE_CATEGORY_NONE, 228U, 0U, 1U, 0U},
    {"<229>", TINYPY_OPCODE_CATEGORY_NONE, 229U, 0U, 1U, 0U},
    {"<230>", TINYPY_OPCODE_CATEGORY_NONE, 230U, 0U, 1U, 0U},
    {"<231>", TINYPY_OPCODE_CATEGORY_NONE, 231U, 0U, 1U, 0U},
    {"<232>", TINYPY_OPCODE_CATEGORY_NONE, 232U, 0U, 1U, 0U},
    {"<233>", TINYPY_OPCODE_CATEGORY_NONE, 233U, 0U, 1U, 0U},
    {"<234>", TINYPY_OPCODE_CATEGORY_NONE, 234U, 0U, 1U, 0U},
    {"<235>", TINYPY_OPCODE_CATEGORY_NONE, 235U, 0U, 1U, 0U},
    {"<236>", TINYPY_OPCODE_CATEGORY_NONE, 236U, 0U, 1U, 0U},
    {"<237>", TINYPY_OPCODE_CATEGORY_NONE, 237U, 0U, 1U, 0U},
    {"<238>", TINYPY_OPCODE_CATEGORY_NONE, 238U, 0U, 1U, 0U},
    {"<239>", TINYPY_OPCODE_CATEGORY_NONE, 239U, 0U, 1U, 0U},
    {"<240>", TINYPY_OPCODE_CATEGORY_NONE, 240U, 0U, 1U, 0U},
    {"<241>", TINYPY_OPCODE_CATEGORY_NONE, 241U, 0U, 1U, 0U},
    {"<242>", TINYPY_OPCODE_CATEGORY_NONE, 242U, 0U, 1U, 0U},
    {"<243>", TINYPY_OPCODE_CATEGORY_NONE, 243U, 0U, 1U, 0U},
    {"<244>", TINYPY_OPCODE_CATEGORY_NONE, 244U, 0U, 1U, 0U},
    {"<245>", TINYPY_OPCODE_CATEGORY_NONE, 245U, 0U, 1U, 0U},
    {"<246>", TINYPY_OPCODE_CATEGORY_NONE, 246U, 0U, 1U, 0U},
    {"<247>", TINYPY_OPCODE_CATEGORY_NONE, 247U, 0U, 1U, 0U},
    {"<248>", TINYPY_OPCODE_CATEGORY_NONE, 248U, 0U, 1U, 0U},
    {"<249>", TINYPY_OPCODE_CATEGORY_NONE, 249U, 0U, 1U, 0U},
    {"<250>", TINYPY_OPCODE_CATEGORY_NONE, 250U, 0U, 1U, 0U},
    {"<251>", TINYPY_OPCODE_CATEGORY_NONE, 251U, 0U, 1U, 0U},
    {"<252>", TINYPY_OPCODE_CATEGORY_NONE, 252U, 0U, 1U, 0U},
    {"<253>", TINYPY_OPCODE_CATEGORY_NONE, 253U, 0U, 1U, 0U},
    {"<254>", TINYPY_OPCODE_CATEGORY_NONE, 254U, 0U, 1U, 0U},
    {"<255>", TINYPY_OPCODE_CATEGORY_NONE, 255U, 0U, 1U, 0U},
};

static void __tinypy_opcode_clear_instruction(
    tinypy_decoded_instruction_t *instruction)
{
    instruction->offset = 0U;
    instruction->next_offset = 0U;
    instruction->encoded_size = 0U;
    instruction->extended_arg_count = 0U;
    instruction->argument = UINT64_C(0);
    instruction->opcode = 0U;
    instruction->defined = 0U;
    instruction->has_argument = 0U;
    instruction->reserved = 0U;
}

static int __tinypy_opcode_name_equal(
    const char *candidate,
    const char *name,
    size_t name_size)
{
    size_t index;

    for (index = 0U; index != name_size; ++index) {
        if (candidate[index] == '\0' || candidate[index] != name[index]) {
            return 0;
        }
    }

    return candidate[name_size] == '\0';
}

const tinypy_opcode_info_t *tinypy_opcode_get_info(uint8_t opcode)
{
    return &tinypy_opcode_table[opcode];
}

const char *tinypy_opcode_name(uint8_t opcode)
{
    return tinypy_opcode_table[opcode].name;
}

int tinypy_opcode_is_defined(uint8_t opcode)
{
    return tinypy_opcode_table[opcode].defined != 0U;
}

int tinypy_opcode_has_argument(uint8_t opcode)
{
    return tinypy_opcode_table[opcode].has_argument != 0U;
}

uint32_t tinypy_opcode_categories(uint8_t opcode)
{
    return tinypy_opcode_table[opcode].categories;
}

int tinypy_opcode_has_category(
    uint8_t opcode,
    tinypy_opcode_category_e category)
{
    uint32_t requested = (uint32_t)category;

    assert(requested != 0U);
    assert((requested & ~TINYPY_OPCODE_ALL_CATEGORIES) == 0U);

    return (tinypy_opcode_table[opcode].categories & requested) == requested;
}

int tinypy_opcode_lookup(
    const char *name,
    size_t name_size,
    uint8_t *out_opcode)
{
    size_t opcode;

    assert(out_opcode != NULL);
    assert(name != NULL || name_size == 0U);
    *out_opcode = 0U;

    if (name_size == 0U) {
        return 0;
    }

    for (opcode = 0U; opcode != (size_t)TINYPY_OPCODE_COUNT; ++opcode) {
        const tinypy_opcode_info_t *info = &tinypy_opcode_table[opcode];

        if (info->defined != 0U &&
            __tinypy_opcode_name_equal(info->name, name, name_size)) {
            *out_opcode = (uint8_t)opcode;
            return 1;
        }
    }

    return 0;
}

tinypy_opcode_decode_status_e tinypy_opcode_decode(
    const uint8_t *bytecode,
    size_t bytecode_size,
    size_t offset,
    tinypy_decoded_instruction_t *out_instruction)
{
    size_t cursor;
    size_t extended_arg_count = 0U;
    uint64_t argument = UINT64_C(0);

    assert(out_instruction != NULL);
    assert(bytecode != NULL || bytecode_size == 0U);
    __tinypy_opcode_clear_instruction(out_instruction);

    if (offset > bytecode_size) {
        return TINYPY_OPCODE_DECODE_INVALID_OFFSET;
    }

    if (offset == bytecode_size) {
        return TINYPY_OPCODE_DECODE_EOF;
    }

    cursor = offset;

    for (;;) {
        uint8_t opcode = bytecode[cursor];
        uint16_t argument_word;
        const tinypy_opcode_info_t *info;

        cursor += 1U;

        if (opcode < (uint8_t)TINYPY_OPCODE_HAVE_ARGUMENT) {
            if (extended_arg_count != 0U) {
                return TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG;
            }

            info = &tinypy_opcode_table[opcode];
            out_instruction->offset = offset;
            out_instruction->next_offset = cursor;
            out_instruction->encoded_size = cursor - offset;
            out_instruction->opcode = opcode;
            out_instruction->defined = info->defined;
            out_instruction->has_argument = 0U;
            return TINYPY_OPCODE_DECODE_OK;
        }

        if (bytecode_size - cursor < 2U) {
            return TINYPY_OPCODE_DECODE_TRUNCATED;
        }

        argument_word = (uint16_t)bytecode[cursor];
        argument_word |= (uint16_t)((uint16_t)bytecode[cursor + 1U] << 8U);
        cursor += 2U;

        if (argument > (UINT64_MAX >> 16U)) {
            return TINYPY_OPCODE_DECODE_ARGUMENT_OVERFLOW;
        }

        argument = (argument << 16U) | (uint64_t)argument_word;

        if (opcode == (uint8_t)TINYPY_OPCODE_EXTENDED_ARG) {
            extended_arg_count += 1U;

            if (cursor == bytecode_size) {
                return TINYPY_OPCODE_DECODE_TRUNCATED;
            }

            continue;
        }

        info = &tinypy_opcode_table[opcode];
        out_instruction->offset = offset;
        out_instruction->next_offset = cursor;
        out_instruction->encoded_size = cursor - offset;
        out_instruction->extended_arg_count = extended_arg_count;
        out_instruction->argument = argument;
        out_instruction->opcode = opcode;
        out_instruction->defined = info->defined;
        out_instruction->has_argument = 1U;
        return TINYPY_OPCODE_DECODE_OK;
    }
}

const char *tinypy_opcode_decode_status_name(
    tinypy_opcode_decode_status_e status)
{
    switch (status) {
    case TINYPY_OPCODE_DECODE_OK:
        return "ok";
    case TINYPY_OPCODE_DECODE_EOF:
        return "end of bytecode";
    case TINYPY_OPCODE_DECODE_INVALID_OFFSET:
        return "invalid offset";
    case TINYPY_OPCODE_DECODE_TRUNCATED:
        return "truncated instruction";
    case TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG:
        return "EXTENDED_ARG must prefix an opcode with an argument";
    case TINYPY_OPCODE_DECODE_ARGUMENT_OVERFLOW:
        return "extended argument overflow";
    default:
        return "unknown decode status";
    }
}
