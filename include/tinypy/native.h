#ifndef TINYPY_NATIVE_H
#define TINYPY_NATIVE_H

#include "tinypy/comparison.h"
//////////////////////////////////////////////////////////////////////////
#define TINYPY_NATIVE_TYPE_ABI_VERSION UINT32_C(1)

typedef tinypy_value_t *(*tinypy_native_function_callback_t)(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
/* The items convention, like METH_O and METH_VARARGS without an argument
 * tuple: self is the instance a method of a type is called on and NULL for a
 * plain function call, items borrows the count positional arguments after it
 * for the duration of the call, and kwargs is NULL or a dict. */
typedef tinypy_value_t *(*tinypy_native_items_callback_t)(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error);
/* Finalizers run exactly once: when the last reference to the owning value
 * is released, before that release returns however deeply finalizers nest,
 * so a host may hand out a value it keeps a borrowed pointer to until the
 * finalizer clears it; or during tinypy_vm_destroy before any reachable
 * value is freed; see vm.h for the values finalizers create there. A native
 * function raises RuntimeError when called after its finalizer ran, as do
 * the slots of a native instance, the methods registered on its type and the
 * native accessors of its properties once the instance's finalizer ran. The
 * instance finalizer may still hand the instance to Python; an error it
 * leaves raised is reported with the instance named as object.__repr__ names
 * it, as no callback of its type runs after its finalizer. */
typedef void (*tinypy_native_function_finalize_t)(void *user_data);

typedef tinypy_bool_t (*tinypy_native_construct_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
typedef void (*tinypy_native_finalize_t)(tinypy_value_t *instance, void *payload, void *user_data);
typedef tinypy_value_t *(*tinypy_native_call_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_unary_t)(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_binary_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_get_attribute_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_native_set_attribute_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *name, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_get_item_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_native_set_item_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *key, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error);
typedef ptrdiff_t (*tinypy_native_length_t)(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error);
typedef int32_t (*tinypy_native_contains_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *item, void *user_data, tinypy_error_t **out_error);
typedef tinypy_hash_t (*tinypy_native_hash_t)(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_compare_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
/* A host native type dispatches the callbacks of its own spec only: a slot its
 * spec leaves NULL keeps the behaviour of the nearest base that is no host
 * native type, object's by default, even when a native base defines it.
 * construct is the type's tp_new: it receives the call arguments, which
 * object.__init__ then accepts. hash never reports -1, which reads as -2;
 * callbacks report failures through out_error or the VM error alone, which
 * the slot then reports through out_error too. A callback, as a native
 * function, that reports success leaves no exception raised: as for the
 * slots and C functions of CPython 2.7, its result is used unchecked and
 * such an exception stays pending. Without iter, sequence_get
 * makes instances iterable by index until IndexError, ahead of mapping_get,
 * and with sequence_length reversible, as sq_item does in CPython; like
 * PySequence_GetItem, the iteration calls it without out_error and reads a
 * failure from the VM error. sequence_get is the sq_item that apply() asks
 * for, and sequence_length the sq_length of map() and of the negative bounds
 * of __getslice__, __setslice__ and __delslice__ of a Python subclass.
 * get_attribute and set_attribute are the tp_getattro and tp_setattro of
 * the type: get_attribute answers the lookups, __class__ included, and
 * set_attribute receives every assignment, ahead of any descriptor of the
 * type, and every deletion with a NULL value, as mapping_set receives the
 * deletion of an item; object.__setattr__ and object.__delattr__ stay
 * generic. An attribute of object that the dictionary of a Python subclass
 * or of its Python base ahead of the type holds, as __getattribute__ =
 * object.__getattribute__, overrides the slot of the callback the subclass
 * copied, also after an update removed that callback from the type: as
 * update_one_slot, the slot is object's again, and with __getattr__ the
 * generic lookup runs first.
 * As add_operators names the slots of a C type, the type publishes a slot
 * wrapper for each callback of its spec under the name of its special
 * method, __setattr__ and __delattr__ for set_attribute, __lt__ to __ge__
 * for compare and __radd__ or __iadd__ for the reflected and in-place
 * callbacks; the wrapper of a name that a mapping callback shares with a
 * sequence one runs the mapping callback, or without it the sequence one.
 * A name its namespace defines keeps its value, and tinypy_type_set_attr
 * replaces a wrapper. A wrapper runs the callback of the spec of the type
 * that publishes it, so that a Python subclass delegates through super() or
 * the type. As update_one_slot, the operations on an instance read a wrapper
 * found under its own name as the slot of their type when the type runs the
 * same callback with the same user data, as the type itself or a Python
 * subclass that copied its spec, which keeps the slot of a callback it copied
 * after an update replaced or removed that callback of its base, for '%' and
 * eval too.
 * The wrapper of a host native base whose
 * callback the type, or the host native type a Python class copied its spec
 * from, does not dispatch reads as the attribute of the nearest base of that
 * type that is no host native type. Any other wrapper, as one stored under
 * another name or one of a second host native base of a Python class, is a
 * method that the operations call by name. A spec with mapping_get makes
 * instances mappings, as mp_subscript does.
 * payload_alignment accepts any power of two up to alignof(max_align_t). */
struct tinypy_native_type_spec_t {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t payload_size;
    size_t payload_alignment;
    void *user_data;
    tinypy_native_construct_t construct;
    tinypy_native_finalize_t finalize;
    tinypy_native_call_t call;
    tinypy_native_unary_t repr;
    tinypy_native_hash_t hash;
    tinypy_native_compare_t compare;
    tinypy_native_get_attribute_t get_attribute;
    tinypy_native_set_attribute_t set_attribute;
    tinypy_native_get_item_t mapping_get;
    tinypy_native_set_item_t mapping_set;
    tinypy_native_length_t mapping_length;
    tinypy_native_get_item_t sequence_get;
    tinypy_native_set_item_t sequence_set;
    tinypy_native_length_t sequence_length;
    tinypy_native_contains_t contains;
    tinypy_native_unary_t iter;
    tinypy_native_unary_t next;
    tinypy_native_unary_t negative;
    tinypy_native_unary_t absolute;
    tinypy_native_binary_t add;
    tinypy_native_binary_t subtract;
    tinypy_native_binary_t multiply;
    tinypy_native_binary_t divide;
    tinypy_native_binary_t inplace_add;
    tinypy_native_binary_t inplace_subtract;
    tinypy_native_binary_t inplace_multiply;
    tinypy_native_binary_t inplace_divide;
    tinypy_native_binary_t reflected_add;
    tinypy_native_binary_t reflected_subtract;
    tinypy_native_binary_t reflected_multiply;
    tinypy_native_binary_t reflected_divide;
    /* Appended layout switches keep older struct_size-based callers binary
     * compatible. tinypy_native_type_spec_init enables both by default. */
    tinypy_bool_t has_instance_dict;
    tinypy_bool_t has_weakrefs;
};
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
/* Borrows an existing byte-string name and retains it in the new
 * function. No string allocation, lookup or automatic interning is performed. */
tinypy_value_t *tinypy_native_function_new_key(tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
/* An items function checks minimum <= count <= maximum and rejects keyword
 * arguments before its callback runs, reporting both as PyArg_ParseTuple
 * does. Registered fresh on a type through tinypy_type_set_attr, as that
 * function describes, it becomes a method descriptor that calls through an
 * instance without a bound method.
 * The callback reads user_data with tinypy_native_function_user_data, which
 * for a bound method is the user data of its descriptor. */
tinypy_value_t *tinypy_native_function_new_items(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_items_callback_t callback, size_t minimum, size_t maximum, void *user_data, tinypy_native_function_finalize_t finalize);
/* Passes keyword arguments to the callback, which checks all its arguments. */
tinypy_value_t *tinypy_native_function_new_items_keywords(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_items_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
/* The argument tuple a tuple callback would receive: self, unless NULL, then
 * the items. Release it with tinypy_native_arguments_release, which keeps an
 * unshared tuple for reuse by the next call. */
tinypy_value_t *tinypy_native_arguments_acquire(tinypy_vm_t *vm, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_error_t **out_error);
void tinypy_native_arguments_release(tinypy_value_t *args);
tinypy_value_t *tinypy_native_function_name(const tinypy_value_t *function);
void *tinypy_native_function_user_data(const tinypy_value_t *function);

void tinypy_native_type_spec_init(tinypy_native_type_spec_t *spec);
/* The bases belong to the VM and keep the layout of object or of a host
 * native type; another base, such as list, is rejected with TypeError. */
tinypy_type_t *tinypy_native_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, tinypy_value_t *namespace_dict, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error);
/* Borrows and retains an existing byte-string name from the type VM. */
tinypy_type_t *tinypy_native_type_new_key(tinypy_value_t *name, const tinypy_type_t *const *bases, size_t base_count, tinypy_value_t *namespace_dict, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error);
/* Replaces the dispatch callbacks; a callback the new spec leaves NULL resets
 * its slot. The slot wrappers of the added callbacks are published and those
 * of the removed ones withdrawn; the names of the other callbacks and the
 * other values of the namespace of the type stay as they are, and the same
 * spec again changes nothing.
 * The Python subclasses created earlier keep the callbacks they
 * copied from the type, and host native subtypes keep their own spec; the
 * special methods of the subclasses stay the wrappers the type publishes, so
 * the name of a removed callback is gone from them too though the slot they
 * copied still runs it. A call
 * that was already dispatching to a removed callback raises TypeError, or
 * returns NotImplemented from a number or comparison slot. */
tinypy_bool_t tinypy_native_type_update_spec(tinypy_type_t *type, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error);
tinypy_value_t *tinypy_native_instance_new(tinypy_type_t *type);
tinypy_bool_t tinypy_native_instance_construct(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void *tinypy_native_instance_payload(tinypy_value_t *instance);
const void *tinypy_native_instance_const_payload(const tinypy_value_t *instance);
const tinypy_native_type_spec_t *tinypy_native_type_spec(const tinypy_type_t *type);
//////////////////////////////////////////////////////////////////////////
#endif
