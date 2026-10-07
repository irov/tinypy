#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_intern_resize(tinypy_vm_t *vm, size_t capacity, tinypy_error_t **out_error) {
    tinypy_intern_entry_t *entries;
    size_t index;
    size_t bytes;

    if (capacity > SIZE_MAX / sizeof(*entries)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "intern table is too large", out_error);
        return TINYPY_FALSE;
    }
    bytes = capacity * sizeof(*entries);
    entries = out_error != NULL
        ? (tinypy_intern_entry_t *)tinypy_internal_vm_allocate_checked(vm, bytes, out_error)
        : (tinypy_intern_entry_t *)tinypy_internal_vm_allocate(vm, bytes);
    if (entries == NULL) {
        return TINYPY_FALSE;
    }
    (void)memset(entries, 0, bytes);
    size_t max_size = 0U;
    for (index = 0U; index < vm->intern_capacity; ++index) {
        tinypy_intern_entry_t entry = vm->intern_entries[index];
        size_t slot;

        if (entry.value == NULL) {
            continue;
        }
        slot = (size_t)entry.hash & (capacity - 1U);
        while (entries[slot].value != NULL) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        entries[slot] = entry;
        if (TINYPY_SIZED_SIZE(entry.value) > max_size) {
            max_size = TINYPY_SIZED_SIZE(entry.value);
        }
    }
    if (vm->intern_entries != NULL) {
        tinypy_internal_vm_deallocate(vm, vm->intern_entries, vm->intern_capacity * sizeof(*entries));
    }
    vm->intern_entries = entries;
    vm->intern_capacity = capacity;
    vm->intern_fill = vm->intern_used;
    vm->intern_max_size = max_size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_find(const tinypy_vm_t *vm, const void *bytes, size_t size) {
    if (vm->intern_capacity == 0U || size > vm->intern_max_size || vm->state == TINYPY_VM_STATE_DESTROYING) {
        return NULL;
    }
    tinypy_hash_t hash = tinypy_internal_hash_bytes(vm, (const uint8_t *)bytes, size);
    size_t slot = (size_t)hash & (vm->intern_capacity - 1U);

    for (;;) {
        const tinypy_intern_entry_t *entry = &vm->intern_entries[slot];
        if (entry->value != NULL) {
            if (entry->hash == hash && TINYPY_SIZED_SIZE(entry->value) == size
                && (size == 0U || memcmp(TINYPY_TEXT_BYTES(entry->value), bytes, size) == 0)) {
                return entry->value;
            }
        }
        else if (entry->hash == 0) {
            return NULL;
        }
        slot = (slot + 1U) & (vm->intern_capacity - 1U);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_string_intern(tinypy_value_t **owned_value, tinypy_error_t **out_error) {
    tinypy_value_t *value = *owned_value;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_hash_t hash;
    size_t slot;
    size_t available = SIZE_MAX;

    if (value->type != &vm->types[TINYPY_VALUE_STRING] || vm->state == TINYPY_VM_STATE_DESTROYING) {
        return TINYPY_TRUE;
    }
    hash = tinypy_internal_hash_value(value, NULL);
    if (vm->intern_capacity != 0U) {
        slot = (size_t)hash & (vm->intern_capacity - 1U);
        for (;;) {
            tinypy_intern_entry_t *entry = &vm->intern_entries[slot];

            if (entry->value == NULL) {
                if (available == SIZE_MAX) {
                    available = slot;
                }
                if (entry->hash == 0) {
                    break;
                }
            }
            else if (entry->hash == hash && TINYPY_SIZED_SIZE(entry->value) == TINYPY_SIZED_SIZE(value)
                && memcmp(TINYPY_TEXT_BYTES(entry->value), TINYPY_TEXT_BYTES(value), TINYPY_SIZED_SIZE(value)) == 0) {
                tinypy_value_t *existing = entry->value;

                TINYPY_INCREF(existing);
                *owned_value = existing;
                TINYPY_DECREF(value);
                return TINYPY_TRUE;
            }
            slot = (slot + 1U) & (vm->intern_capacity - 1U);
        }
    }
    if (vm->intern_capacity == 0U || vm->intern_fill + 1U >= vm->intern_capacity - vm->intern_capacity / 4U
        || (vm->intern_capacity > 256U && vm->intern_used < vm->intern_capacity / 8U)) {
        size_t capacity = 256U;

        while (vm->intern_used >= capacity / 2U) {
            if (capacity > SIZE_MAX / 2U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "intern table is too large", out_error);
                return TINYPY_FALSE;
            }
            capacity *= 2U;
        }
        if (__tinypy_intern_resize(vm, capacity, out_error) == 0) {
            return TINYPY_FALSE;
        }
        available = (size_t)hash & (capacity - 1U);
        while (vm->intern_entries[available].value != NULL) {
            available = (available + 1U) & (capacity - 1U);
        }
    }
    if (vm->intern_entries[available].hash == 0 && vm->intern_entries[available].value == NULL) {
        vm->intern_fill += 1U;
    }
    vm->intern_entries[available].value = value;
    vm->intern_entries[available].hash = hash;
    vm->intern_used += 1U;
    if (TINYPY_SIZED_SIZE(value) > vm->intern_max_size) {
        vm->intern_max_size = TINYPY_SIZED_SIZE(value);
    }
    tinypy_internal_string_set_interned(value, TINYPY_TRUE);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_string_unintern(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t slot;

    if (vm->intern_capacity == 0U || TINYPY_STRING_OBJECT(value)->interned == 0) {
        return;
    }
    slot = (size_t)TINYPY_STRING_OBJECT(value)->hash & (vm->intern_capacity - 1U);
    for (;;) {
        tinypy_intern_entry_t *entry = &vm->intern_entries[slot];

        if (entry->value == value) {
            entry->value = NULL;
            entry->hash = 1;
            vm->intern_used -= 1U;
            return;
        }
        if (entry->value == NULL && entry->hash == 0) {
            return;
        }
        slot = (slot + 1U) & (vm->intern_capacity - 1U);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_intern_finalize(tinypy_vm_t *vm) {
    if (vm->intern_entries != NULL) {
        tinypy_internal_vm_deallocate(vm, vm->intern_entries, vm->intern_capacity * sizeof(*vm->intern_entries));
        vm->intern_entries = NULL;
    }
    vm->intern_capacity = 0U;
    vm->intern_used = 0U;
    vm->intern_fill = 0U;
    vm->intern_max_size = 0U;
}
