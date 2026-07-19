/* Peephole optimizations for bytecode compiler. */

#include "value_ops.h"

#include "ast_nodes.h"
#include "cst.h"
#include "ast_builder.h"
#include "bytecode_builder.h"
#include "codegen.h"
#include "symbol_table.h"
#include "../../bytecode/opcode.h"

#define TINYPY_OPTIMIZER_GET_ARGUMENT(arr, i) ((int)((arr[i+2]<<8) + arr[i+1]))
#define TINYPY_OPTIMIZER_IS_UNCONDITIONAL_JUMP(op)  (op==TINYPY_OP_JUMP_ABSOLUTE || op==TINYPY_OP_JUMP_FORWARD)
#define TINYPY_OPTIMIZER_IS_CONDITIONAL_JUMP(op) (op==TINYPY_OP_POP_JUMP_IF_FALSE || op==TINYPY_OP_POP_JUMP_IF_TRUE \
    || op==TINYPY_OP_JUMP_IF_FALSE_OR_POP || op==TINYPY_OP_JUMP_IF_TRUE_OR_POP)
#define TINYPY_OPTIMIZER_IS_ABSOLUTE_JUMP(op) (op==TINYPY_OP_JUMP_ABSOLUTE || op==TINYPY_OP_CONTINUE_LOOP \
    || op==TINYPY_OP_POP_JUMP_IF_FALSE || op==TINYPY_OP_POP_JUMP_IF_TRUE \
    || op==TINYPY_OP_JUMP_IF_FALSE_OR_POP || op==TINYPY_OP_JUMP_IF_TRUE_OR_POP)
#define TINYPY_OPTIMIZER_JUMPS_ON_TRUE(op) (op==TINYPY_OP_POP_JUMP_IF_TRUE || op==TINYPY_OP_JUMP_IF_TRUE_OR_POP)
#define TINYPY_OPTIMIZER_JUMP_TARGET(arr, i) (TINYPY_OPTIMIZER_GET_ARGUMENT(arr,i) + (TINYPY_OPTIMIZER_IS_ABSOLUTE_JUMP(arr[i]) ? 0 : i+3))
#define TINYPY_OPTIMIZER_SET_ARGUMENT(arr, i, val) arr[i+2] = val>>8; arr[i+1] = val & 255
#define TINYPY_OPTIMIZER_CODE_SIZE(op)  (TINYPY_COMPILER_OPCODE_HAS_ARGUMENT(op) ? 3 : 1)
#define TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks, start, bytes) \
    (blocks[start]==blocks[start+bytes-1])

/* Replace TINYPY_OP_LOAD_CONST c1. TINYPY_OP_LOAD_CONST c2 ... TINYPY_OP_LOAD_CONST cn TINYPY_OP_BUILD_TUPLE n
   with    TINYPY_OP_LOAD_CONST (c1, c2, ... cn).
   The consts table must still be in list form so that the
   new constant (c1, c2, ... cn) can be appended.
   Called with codestr pointing to the first TINYPY_OP_LOAD_CONST.
   Bails out with no change if one or more of the LOAD_CONSTs is missing.
   Also works for TINYPY_OP_BUILD_LIST when followed by an "in" or "not in" test.
*/
static int
__tuple_of_constants(unsigned char *codestr, tinypy_compiler_size_t n, tinypy_value_t *consts)
{
    tinypy_value_t *newconst, *constant;
    tinypy_compiler_size_t i, arg, len_consts;

    /* Pre-conditions */
    assert(TINYPY_COMPILER_LIST_CHECK_EXACT(consts));
    assert(codestr[n*3] == TINYPY_OP_BUILD_TUPLE || codestr[n*3] == TINYPY_OP_BUILD_LIST);
    assert(TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, (n*3)) == n);
    for (i=0 ; i<n ; i++)
        assert(codestr[i*3] == TINYPY_OP_LOAD_CONST);

    /* Buildup new tuple of constants */
    newconst = __tinypy_frontend_tuple_new(consts, n);
    if (newconst == NULL)
        return 0;
    len_consts = TINYPY_COMPILER_LIST_GET_SIZE(consts);
    for (i=0 ; i<n ; i++) {
        arg = TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, (i*3));
        assert(arg < len_consts);
        constant = TINYPY_COMPILER_LIST_GET_ITEM(consts, arg);
        TINYPY_COMPILER_INCREF(constant);
        TINYPY_COMPILER_TUPLE_SET_ITEM(newconst, i, constant);
    }

    /* Append folded constant onto consts */
    if (TINYPY_COMPILER_LIST_APPEND(consts, newconst)) {
        TINYPY_COMPILER_DECREF(newconst);
        return 0;
    }
    TINYPY_COMPILER_DECREF(newconst);

    /* Write NOPs over old LOAD_CONSTS and
       add a new TINYPY_OP_LOAD_CONST newconst on top of the TINYPY_OP_BUILD_TUPLE n */
    memset(codestr, TINYPY_OP_NOP, n*3);
    codestr[n*3] = TINYPY_OP_LOAD_CONST;
    TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, (n*3), len_consts);
    return 1;
}

/* Replace TINYPY_OP_LOAD_CONST c1. TINYPY_OP_LOAD_CONST c2 BINOP
   with    TINYPY_OP_LOAD_CONST binop(c1,c2)
   The consts table must still be in list form so that the
   new constant can be appended.
   Called with codestr pointing to the first TINYPY_OP_LOAD_CONST.
   Abandons the transformation if the folding fails (i.e.  1+'a').
   If the new constant is a sequence, only folds when the size
   is below a threshold value.  That keeps pyc files from
   becoming large in the presence of code like:  (None,)*1000.
*/
static int
__fold_binops_on_constants(unsigned char *codestr, tinypy_value_t *consts)
{
    tinypy_value_t *newconst, *v, *w;
    tinypy_compiler_size_t len_consts, size;
    int opcode;

    /* Pre-conditions */
    assert(TINYPY_COMPILER_LIST_CHECK_EXACT(consts));
    assert(codestr[0] == TINYPY_OP_LOAD_CONST);
    assert(codestr[3] == TINYPY_OP_LOAD_CONST);

    /* Create new constant */
    v = TINYPY_COMPILER_LIST_GET_ITEM(consts, TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, 0));
    w = TINYPY_COMPILER_LIST_GET_ITEM(consts, TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, 3));
    opcode = codestr[6];
    switch (opcode) {
        case TINYPY_OP_BINARY_POWER:
            newconst = tinypy_power(v, w, NULL);
            break;
        case TINYPY_OP_BINARY_MULTIPLY:
            newconst = TINYPY_COMPILER_NUMBER_MULTIPLY(v, w);
            break;
        case TINYPY_OP_BINARY_DIVIDE:
            /* Cannot fold this operation statically since
               the result can depend on the run-time presence
               of the -Qnew flag */
            return 0;
        case TINYPY_OP_BINARY_TRUE_DIVIDE:
            newconst = TINYPY_COMPILER_NUMBER_TRUE_DIVIDE(v, w);
            break;
        case TINYPY_OP_BINARY_FLOOR_DIVIDE:
            newconst = TINYPY_COMPILER_NUMBER_FLOOR_DIVIDE(v, w);
            break;
        case TINYPY_OP_BINARY_MODULO:
            newconst = TINYPY_COMPILER_NUMBER_REMAINDER(v, w);
            break;
        case TINYPY_OP_BINARY_ADD:
            newconst = TINYPY_COMPILER_NUMBER_ADD(v, w);
            break;
        case TINYPY_OP_BINARY_SUBTRACT:
            newconst = TINYPY_COMPILER_NUMBER_SUBTRACT(v, w);
            break;
        case TINYPY_OP_BINARY_SUBSCR:
            /* #5057: if v is unicode, there might be differences between
               wide and narrow builds in cases like '\U00012345'[0] or
               '\U00012345abcdef'[3], so it's better to skip the optimization
               in order to produce compatible pycs.
            */
            if (TINYPY_COMPILER_UNICODE_CHECK(v))
                return 0;
            newconst = TINYPY_COMPILER_OBJECT_GET_ITEM(v, w);
            break;
        case TINYPY_OP_BINARY_LSHIFT:
            newconst = TINYPY_COMPILER_NUMBER_LSHIFT(v, w);
            break;
        case TINYPY_OP_BINARY_RSHIFT:
            newconst = TINYPY_COMPILER_NUMBER_RSHIFT(v, w);
            break;
        case TINYPY_OP_BINARY_AND:
            newconst = TINYPY_COMPILER_NUMBER_AND(v, w);
            break;
        case TINYPY_OP_BINARY_XOR:
            newconst = TINYPY_COMPILER_NUMBER_XOR(v, w);
            break;
        case TINYPY_OP_BINARY_OR:
            newconst = TINYPY_COMPILER_NUMBER_OR(v, w);
            break;
        default:
            /* Called with an unknown opcode */
            TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                 "unexpected binary operation %d on a constant",
                     opcode);
            return 0;
    }
    if (newconst == NULL) {
        __tinypy_frontend_clear_raised(v);
        return 0;
    }
    size = TINYPY_COMPILER_OBJECT_SIZE(newconst);
    if (size == -1)
        __tinypy_frontend_clear_raised(newconst);
    else if (size > 20) {
        TINYPY_COMPILER_DECREF(newconst);
        return 0;
    }

    /* Append folded constant into consts table */
    len_consts = TINYPY_COMPILER_LIST_GET_SIZE(consts);
    if (TINYPY_COMPILER_LIST_APPEND(consts, newconst)) {
        TINYPY_COMPILER_DECREF(newconst);
        return 0;
    }
    TINYPY_COMPILER_DECREF(newconst);

    /* Write TINYPY_OP_NOP TINYPY_OP_NOP TINYPY_OP_NOP TINYPY_OP_NOP TINYPY_OP_LOAD_CONST newconst */
    memset(codestr, TINYPY_OP_NOP, 4);
    codestr[4] = TINYPY_OP_LOAD_CONST;
    TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, 4, len_consts);
    return 1;
}

static int
__fold_unaryops_on_constants(unsigned char *codestr, tinypy_value_t *consts)
{
    tinypy_value_t *newconst=NULL, *v;
    tinypy_compiler_size_t len_consts;
    int opcode;

    /* Pre-conditions */
    assert(TINYPY_COMPILER_LIST_CHECK_EXACT(consts));
    assert(codestr[0] == TINYPY_OP_LOAD_CONST);

    /* Create new constant */
    v = TINYPY_COMPILER_LIST_GET_ITEM(consts, TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, 0));
    opcode = codestr[3];
    switch (opcode) {
        case TINYPY_OP_UNARY_NEGATIVE:
            /* Preserve the sign of -0.0 */
            if (TINYPY_COMPILER_OBJECT_IS_TRUE(v) == 1)
                newconst = TINYPY_COMPILER_NUMBER_NEGATIVE(v);
            break;
        case TINYPY_OP_UNARY_CONVERT:
            newconst = TINYPY_COMPILER_OBJECT_REPR(v);
            break;
        case TINYPY_OP_UNARY_INVERT:
            newconst = TINYPY_COMPILER_NUMBER_INVERT(v);
            break;
        default:
            /* Called with an unknown opcode */
            TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                 "unexpected unary operation %d on a constant",
                     opcode);
            return 0;
    }
    if (newconst == NULL) {
        __tinypy_frontend_clear_raised(v);
        return 0;
    }

    /* Append folded constant into consts table */
    len_consts = TINYPY_COMPILER_LIST_GET_SIZE(consts);
    if (TINYPY_COMPILER_LIST_APPEND(consts, newconst)) {
        TINYPY_COMPILER_DECREF(newconst);
        return 0;
    }
    TINYPY_COMPILER_DECREF(newconst);

    /* Write TINYPY_OP_NOP TINYPY_OP_LOAD_CONST newconst */
    codestr[0] = TINYPY_OP_NOP;
    codestr[1] = TINYPY_OP_LOAD_CONST;
    TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, 1, len_consts);
    return 1;
}

static unsigned int *
__markblocks(tinypy_compile_ctx_t *arena, unsigned char *code, tinypy_compiler_size_t len)
{
    unsigned int *blocks = (unsigned int *)TINYPY_COMPILER_ARENA_MALLOC(arena, (size_t)len * sizeof(unsigned int));
    int i,j, opcode, blockcnt = 0;

    if (blocks == NULL) {
        TINYPY_COMPILER_ERR_NO_MEMORY();
        return NULL;
    }
    memset(blocks, 0, len*sizeof(int));

    /* Mark labels in the first pass */
    for (i=0 ; i<len ; i+=TINYPY_OPTIMIZER_CODE_SIZE(opcode)) {
        opcode = code[i];
        switch (opcode) {
            case TINYPY_OP_FOR_ITER:
            case TINYPY_OP_JUMP_FORWARD:
            case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
            case TINYPY_OP_JUMP_IF_TRUE_OR_POP:
            case TINYPY_OP_POP_JUMP_IF_FALSE:
            case TINYPY_OP_POP_JUMP_IF_TRUE:
            case TINYPY_OP_JUMP_ABSOLUTE:
            case TINYPY_OP_CONTINUE_LOOP:
            case TINYPY_OP_SETUP_LOOP:
            case TINYPY_OP_SETUP_EXCEPT:
            case TINYPY_OP_SETUP_FINALLY:
            case TINYPY_OP_SETUP_WITH:
                j = TINYPY_OPTIMIZER_JUMP_TARGET(code, i);
                blocks[j] = 1;
                break;
        }
    }
    /* Build block numbers in the second pass */
    for (i=0 ; i<len ; i++) {
        blockcnt += blocks[i];          /* increment blockcnt over labels */
        blocks[i] = blockcnt;
    }
    return blocks;
}

/* Perform basic peephole optimizations to components of a code object.
   The consts object should still be in list form to allow new constants
   to be appended.

   To keep the optimizer simple, it bails out (does nothing) for code
   containing extended arguments or that has a length over 32,700.  That
   allows us to avoid overflow and sign issues.  Likewise, it bails when
   the lineno table has complex encoding for gaps >= 255.

   Optimizations are restricted to simple transformations occurring within a
   single basic block.  All transformations keep the code size the same or
   smaller.  For those that reduce size, the gaps are initially filled with
   NOPs.  Later those NOPs are removed and the jump addresses retargeted in
   a single pass.  Line numbering is adjusted accordingly. */

tinypy_value_t *
__tinypy_bytecode_optimize(tinypy_compile_ctx_t *arena, tinypy_value_t *code, tinypy_value_t* consts, tinypy_value_t *names,
                tinypy_value_t *lineno_obj)
{
    tinypy_compiler_size_t i, j, codelen;
    int nops, h, adj;
    int tgt, tgttgt, opcode;
    unsigned char *codestr = NULL;
    unsigned char *lineno;
    int *addrmap = NULL;
    int new_line, cum_orig_line, last_line, tabsiz;
    int cumlc=0, lastlc=0;      /* Count runs of consecutive LOAD_CONSTs */
    unsigned int *blocks = NULL;
    char *name;

    /* Bypass optimization when the lineno table is too complex */
    assert(TINYPY_COMPILER_STRING_CHECK(lineno_obj));
    lineno = (unsigned char*)TINYPY_COMPILER_STRING_AS_STRING(lineno_obj);
    tabsiz = TINYPY_COMPILER_STRING_GET_SIZE(lineno_obj);
    if (memchr(lineno, 255, tabsiz) != NULL)
        goto exitUnchanged;

    /* Avoid situations where jump retargeting could overflow */
    assert(TINYPY_COMPILER_STRING_CHECK(code));
    codelen = TINYPY_COMPILER_STRING_GET_SIZE(code);
    if (codelen > 32700)
        goto exitUnchanged;

    /* Make a modifiable copy of the code string */
    codestr = (unsigned char *)TINYPY_COMPILER_ARENA_MALLOC(arena, (size_t)codelen);
    if (codestr == NULL)
        goto exitError;
    codestr = (unsigned char *)memcpy(codestr,
                                      TINYPY_COMPILER_STRING_AS_STRING(code), codelen);

    /* Verify that TINYPY_OP_RETURN_VALUE terminates the codestring. This allows
       the various transformation patterns to look ahead several
       instructions without additional checks to make sure they are not
       looking beyond the end of the code string.
    */
    if (codestr[codelen-1] != TINYPY_OP_RETURN_VALUE)
        goto exitUnchanged;

    /* Mapping to new jump targets after NOPs are removed */
    addrmap = (int *)TINYPY_COMPILER_ARENA_MALLOC(arena, (size_t)codelen * sizeof(int));
    if (addrmap == NULL) {
        TINYPY_COMPILER_ERR_NO_MEMORY();
        goto exitError;
    }

    blocks = __markblocks(arena, codestr, codelen);
    if (blocks == NULL)
        goto exitError;
    assert(TINYPY_COMPILER_LIST_CHECK(consts));

    for (i=0 ; i<codelen ; i += TINYPY_OPTIMIZER_CODE_SIZE(codestr[i])) {
      reoptimize_current:
        opcode = codestr[i];

        lastlc = cumlc;
        cumlc = 0;

        switch (opcode) {
            /* Replace TINYPY_OP_UNARY_NOT TINYPY_OP_POP_JUMP_IF_FALSE
               with    TINYPY_OP_POP_JUMP_IF_TRUE */
            case TINYPY_OP_UNARY_NOT:
                if (codestr[i+1] != TINYPY_OP_POP_JUMP_IF_FALSE
                    || !TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks,i,4))
                    continue;
                j = TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i+1);
                codestr[i] = TINYPY_OP_POP_JUMP_IF_TRUE;
                TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, j);
                codestr[i+3] = TINYPY_OP_NOP;
                goto reoptimize_current;

                /* not a is b -->  a is not b
                   not a in b -->  a not in b
                   not a is not b -->  a is b
                   not a not in b -->  a in b
                */
            case TINYPY_OP_COMPARE_OP:
                j = TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i);
                if (j < 6  ||  j > 9  ||
                    codestr[i+3] != TINYPY_OP_UNARY_NOT  ||
                    !TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks,i,4))
                    continue;
                TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, (j^1));
                codestr[i+3] = TINYPY_OP_NOP;
                break;

                /* Replace TINYPY_OP_LOAD_GLOBAL/TINYPY_OP_LOAD_NAME None
                   with TINYPY_OP_LOAD_CONST None */
            case TINYPY_OP_LOAD_NAME:
            case TINYPY_OP_LOAD_GLOBAL:
                j = TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i);
                name = TINYPY_COMPILER_STRING_AS_STRING(TINYPY_COMPILER_TUPLE_GET_ITEM(names, j));
                if (name == NULL  ||  strcmp(name, "None") != 0)
                    continue;
                for (j=0 ; j < TINYPY_COMPILER_LIST_GET_SIZE(consts) ; j++) {
                    if (tinypy_typeof(TINYPY_COMPILER_LIST_GET_ITEM(consts, j)) == TINYPY_VALUE_NONE)
                        break;
                }
                if (j == TINYPY_COMPILER_LIST_GET_SIZE(consts)) {
                    tinypy_value_t *none = tinypy_none_get(tinypy_internal_value_vm(consts));
                    if (TINYPY_COMPILER_LIST_APPEND(consts, none) == -1) {
                        TINYPY_COMPILER_DECREF(none);
                        goto exitError;
                    }
                    TINYPY_COMPILER_DECREF(none);
                }
                assert(tinypy_typeof(TINYPY_COMPILER_LIST_GET_ITEM(consts, j)) == TINYPY_VALUE_NONE);
                codestr[i] = TINYPY_OP_LOAD_CONST;
                TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, j);
                cumlc = lastlc + 1;
                break;

                /* Skip over TINYPY_OP_LOAD_CONST trueconst
                   TINYPY_OP_POP_JUMP_IF_FALSE xx. This improves
                   "while 1" performance. */
            case TINYPY_OP_LOAD_CONST:
                cumlc = lastlc + 1;
                j = TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i);
                if (codestr[i+3] != TINYPY_OP_POP_JUMP_IF_FALSE  ||
                    !TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks,i,6)  ||
                    !TINYPY_COMPILER_OBJECT_IS_TRUE(TINYPY_COMPILER_LIST_GET_ITEM(consts, j)))
                    continue;
                memset(codestr+i, TINYPY_OP_NOP, 6);
                cumlc = 0;
                break;

                /* Try to fold tuples of constants (includes a case for lists
                   which are only used for "in" and "not in" tests).
                   Skip over BUILD_SEQN 1 UNPACK_SEQN 1.
                   Replace BUILD_SEQN 2 UNPACK_SEQN 2 with ROT2.
                   Replace BUILD_SEQN 3 UNPACK_SEQN 3 with ROT3 ROT2. */
            case TINYPY_OP_BUILD_TUPLE:
            case TINYPY_OP_BUILD_LIST:
                j = TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i);
                h = i - 3 * j;
                if (h >= 0 &&
                    j <= lastlc &&
                    ((opcode == TINYPY_OP_BUILD_TUPLE &&
                      TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks, h, 3*(j+1))) ||
                     (opcode == TINYPY_OP_BUILD_LIST &&
                      codestr[i+3]==TINYPY_OP_COMPARE_OP &&
                      TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks, h, 3*(j+2)) &&
                      (TINYPY_OPTIMIZER_GET_ARGUMENT(codestr,i+3)==6 ||
                       TINYPY_OPTIMIZER_GET_ARGUMENT(codestr,i+3)==7))) &&
                    __tuple_of_constants(&codestr[h], j, consts)) {
                    assert(codestr[i] == TINYPY_OP_LOAD_CONST);
                    cumlc = 1;
                    break;
                }
                if (codestr[i+3] != TINYPY_OP_UNPACK_SEQUENCE  ||
                    !TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks,i,6) ||
                    j != TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i+3))
                    continue;
                if (j == 1) {
                    memset(codestr+i, TINYPY_OP_NOP, 6);
                } else if (j == 2) {
                    codestr[i] = TINYPY_OP_ROT_TWO;
                    memset(codestr+i+1, TINYPY_OP_NOP, 5);
                } else if (j == 3) {
                    codestr[i] = TINYPY_OP_ROT_THREE;
                    codestr[i+1] = TINYPY_OP_ROT_TWO;
                    memset(codestr+i+2, TINYPY_OP_NOP, 4);
                }
                break;

                /* Fold binary ops on constants.
                   TINYPY_OP_LOAD_CONST c1 TINYPY_OP_LOAD_CONST c2 BINOP -->  TINYPY_OP_LOAD_CONST binop(c1,c2) */
            case TINYPY_OP_BINARY_POWER:
            case TINYPY_OP_BINARY_MULTIPLY:
            case TINYPY_OP_BINARY_TRUE_DIVIDE:
            case TINYPY_OP_BINARY_FLOOR_DIVIDE:
            case TINYPY_OP_BINARY_MODULO:
            case TINYPY_OP_BINARY_ADD:
            case TINYPY_OP_BINARY_SUBTRACT:
            case TINYPY_OP_BINARY_SUBSCR:
            case TINYPY_OP_BINARY_LSHIFT:
            case TINYPY_OP_BINARY_RSHIFT:
            case TINYPY_OP_BINARY_AND:
            case TINYPY_OP_BINARY_XOR:
            case TINYPY_OP_BINARY_OR:
                if (lastlc >= 2 &&
                    TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks, i-6, 7) &&
                    __fold_binops_on_constants(&codestr[i-6], consts)) {
                    i -= 2;
                    assert(codestr[i] == TINYPY_OP_LOAD_CONST);
                    cumlc = 1;
                }
                break;

                /* Fold unary ops on constants.
                   TINYPY_OP_LOAD_CONST c1  UNARY_OP --> TINYPY_OP_LOAD_CONST unary_op(c) */
            case TINYPY_OP_UNARY_NEGATIVE:
            case TINYPY_OP_UNARY_CONVERT:
            case TINYPY_OP_UNARY_INVERT:
                if (lastlc >= 1 &&
                    TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks, i-3, 4) &&
                    __fold_unaryops_on_constants(&codestr[i-3], consts)) {
                    i -= 2;
                    assert(codestr[i] == TINYPY_OP_LOAD_CONST);
                    cumlc = 1;
                }
                break;

                /* Simplify conditional jump to conditional jump where the
                   result of the first test implies the success of a similar
                   test or the failure of the opposite test.
                   Arises in code like:
                   "if a and b:"
                   "if a or b:"
                   "a and b or c"
                   "(a and b) and c"
                   x:TINYPY_OP_JUMP_IF_FALSE_OR_POP y   y:TINYPY_OP_JUMP_IF_FALSE_OR_POP z
                      -->  x:TINYPY_OP_JUMP_IF_FALSE_OR_POP z
                   x:TINYPY_OP_JUMP_IF_FALSE_OR_POP y   y:TINYPY_OP_JUMP_IF_TRUE_OR_POP z
                      -->  x:TINYPY_OP_POP_JUMP_IF_FALSE y+3
                   where y+3 is the instruction following the second test.
                */
            case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
            case TINYPY_OP_JUMP_IF_TRUE_OR_POP:
                tgt = TINYPY_OPTIMIZER_JUMP_TARGET(codestr, i);
                j = codestr[tgt];
                if (TINYPY_OPTIMIZER_IS_CONDITIONAL_JUMP(j)) {
                    /* NOTE: all possible jumps here are absolute! */
                    if (TINYPY_OPTIMIZER_JUMPS_ON_TRUE(j) == TINYPY_OPTIMIZER_JUMPS_ON_TRUE(opcode)) {
                        /* The second jump will be
                           taken iff the first is. */
                        tgttgt = TINYPY_OPTIMIZER_JUMP_TARGET(codestr, tgt);
                        /* The current opcode inherits
                           its target's stack behaviour */
                        codestr[i] = j;
                        TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, tgttgt);
                        goto reoptimize_current;
                    } else {
                        /* The second jump is not taken if the first is (so
                           jump past it), and all conditional jumps pop their
                           argument when they're not taken (so change the
                           first jump to pop its argument when it's taken). */
                        if (TINYPY_OPTIMIZER_JUMPS_ON_TRUE(opcode))
                            codestr[i] = TINYPY_OP_POP_JUMP_IF_TRUE;
                        else
                            codestr[i] = TINYPY_OP_POP_JUMP_IF_FALSE;
                        TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, (tgt + 3));
                        goto reoptimize_current;
                    }
                }
                /* Intentional fallthrough */

                /* Replace jumps to unconditional jumps */
            case TINYPY_OP_POP_JUMP_IF_FALSE:
            case TINYPY_OP_POP_JUMP_IF_TRUE:
            case TINYPY_OP_FOR_ITER:
            case TINYPY_OP_JUMP_FORWARD:
            case TINYPY_OP_JUMP_ABSOLUTE:
            case TINYPY_OP_CONTINUE_LOOP:
            case TINYPY_OP_SETUP_LOOP:
            case TINYPY_OP_SETUP_EXCEPT:
            case TINYPY_OP_SETUP_FINALLY:
            case TINYPY_OP_SETUP_WITH:
                tgt = TINYPY_OPTIMIZER_JUMP_TARGET(codestr, i);
                /* Replace JUMP_* to a RETURN into just a RETURN */
                if (TINYPY_OPTIMIZER_IS_UNCONDITIONAL_JUMP(opcode) &&
                    codestr[tgt] == TINYPY_OP_RETURN_VALUE) {
                    codestr[i] = TINYPY_OP_RETURN_VALUE;
                    memset(codestr+i+1, TINYPY_OP_NOP, 2);
                    continue;
                }
                if (!TINYPY_OPTIMIZER_IS_UNCONDITIONAL_JUMP(codestr[tgt]))
                    continue;
                tgttgt = TINYPY_OPTIMIZER_JUMP_TARGET(codestr, tgt);
                if (opcode == TINYPY_OP_JUMP_FORWARD) /* JMP_ABS can go backwards */
                    opcode = TINYPY_OP_JUMP_ABSOLUTE;
                if (!TINYPY_OPTIMIZER_IS_ABSOLUTE_JUMP(opcode))
                    tgttgt -= i + 3;        /* Calc relative jump addr */
                if (tgttgt < 0)             /* No backward relative jumps */
                    continue;
                codestr[i] = opcode;
                TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, tgttgt);
                break;

            case TINYPY_OP_EXTENDED_ARG:
                goto exitUnchanged;

                /* Replace RETURN TINYPY_OP_LOAD_CONST None RETURN with just RETURN */
                /* Remove unreachable JUMPs after RETURN */
            case TINYPY_OP_RETURN_VALUE:
                if (i+4 >= codelen)
                    continue;
                if (codestr[i+4] == TINYPY_OP_RETURN_VALUE &&
                    TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks,i,5))
                    memset(codestr+i+1, TINYPY_OP_NOP, 4);
                else if (TINYPY_OPTIMIZER_IS_UNCONDITIONAL_JUMP(codestr[i+1]) &&
                         TINYPY_OPTIMIZER_IS_BASIC_BLOCK(blocks,i,4))
                    memset(codestr+i+1, TINYPY_OP_NOP, 3);
                break;
        }
    }

    /* Fixup linenotab */
    for (i=0, nops=0 ; i<codelen ; i += TINYPY_OPTIMIZER_CODE_SIZE(codestr[i])) {
        addrmap[i] = i - nops;
        if (codestr[i] == TINYPY_OP_NOP)
            nops++;
    }
    cum_orig_line = 0;
    last_line = 0;
    for (i=0 ; i < tabsiz ; i+=2) {
        cum_orig_line += lineno[i];
        new_line = addrmap[cum_orig_line];
        assert (new_line - last_line < 255);
        lineno[i] =((unsigned char)(new_line - last_line));
        last_line = new_line;
    }

    /* Remove NOPs and fixup jump targets */
    for (i=0, h=0 ; i<codelen ; ) {
        opcode = codestr[i];
        switch (opcode) {
            case TINYPY_OP_NOP:
                i++;
                continue;

            case TINYPY_OP_JUMP_ABSOLUTE:
            case TINYPY_OP_CONTINUE_LOOP:
            case TINYPY_OP_POP_JUMP_IF_FALSE:
            case TINYPY_OP_POP_JUMP_IF_TRUE:
            case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
            case TINYPY_OP_JUMP_IF_TRUE_OR_POP:
                j = addrmap[TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i)];
                TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, j);
                break;

            case TINYPY_OP_FOR_ITER:
            case TINYPY_OP_JUMP_FORWARD:
            case TINYPY_OP_SETUP_LOOP:
            case TINYPY_OP_SETUP_EXCEPT:
            case TINYPY_OP_SETUP_FINALLY:
            case TINYPY_OP_SETUP_WITH:
                j = addrmap[TINYPY_OPTIMIZER_GET_ARGUMENT(codestr, i) + i + 3] - addrmap[i] - 3;
                TINYPY_OPTIMIZER_SET_ARGUMENT(codestr, i, j);
                break;
        }
        adj = TINYPY_OPTIMIZER_CODE_SIZE(opcode);
        while (adj--)
            codestr[h++] = codestr[i++];
    }
    assert(h + nops == codelen);

    code = __tinypy_frontend_string_from_owner(code, (char *)codestr, (size_t)h);
    return code;

 exitError:
    code = NULL;

 exitUnchanged:
    TINYPY_COMPILER_XINCREF(code);
    return code;
}
