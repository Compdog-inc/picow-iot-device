#ifndef _POINTERLIST_H
#define _POINTERLIST_H

#include <pico/stdlib.h>

typedef struct
{
    void **__ptr;
    uint capacity;
    uint element_count;
} pointer_list_t;

/**
 * Inits a new pointer list with an initial capacity (>= 1)
 */
void pointer_list_init(pointer_list_t *p, uint initial_capacity);

/**
 * Frees a pointer list
 */
void pointer_list_free(pointer_list_t *p);

/**
 * Shifts all elements to the left after <start_index> and reduces the size by 1 (the last element gets shifted to the left)
 * @note Overwrites the element at <start_index> with <start_index + 1>
 */
void pointer_list_shift_left(pointer_list_t *p, uint start_index);

void *pointer_list_get(pointer_list_t *p, uint index);

/**
 * Adds new element to pointer list and resizes if necessary
 */
void pointer_list_add(pointer_list_t *p, void *element);

/**
 * Removes element at index
 * @note Preserves list capacity
 */
void pointer_list_removeat(pointer_list_t *p, uint index);

/**
 * Removes pointer from the list
 * @note Preserves list capacity
 */
void pointer_list_remove(pointer_list_t *p, void *element);

/**
 * Removes last <amount> elements
 * @note Preserves list capacity. To change capacity use pointer_list_resize()
 */
void pointer_list_shrink(pointer_list_t *p, uint amount);

/**
 * Resizes list so that the capacity equals the element count
 */
void pointer_list_fit(pointer_list_t *p);

/**
 * Change capacity to <new_size>
 * @note Removes all elements after the new size
 */
void pointer_list_resize(pointer_list_t *p, uint new_size);

#endif