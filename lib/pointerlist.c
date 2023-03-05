#include <pico/stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pointerlist.h"

void pointer_list_init(pointer_list_t *p, uint initial_capacity)
{
    p->capacity = MAX(1, initial_capacity);
    p->element_count = 0;
    p->__ptr = (void **)calloc(p->capacity, sizeof(void *));
}

void pointer_list_free(pointer_list_t *p)
{
    free(p->__ptr);
    p->capacity = 0;
    p->element_count = 0;
}

void *pointer_list_get(pointer_list_t *p, uint index)
{
    if (index < p->element_count)
        return p->__ptr[index];
    return NULL;
}

void pointer_list_add(pointer_list_t *p, void *element)
{
    p->element_count++; // add element
    if (p->element_count > p->capacity)
    {
        p->capacity *= 2; // resize if necessary
        p->__ptr = (void **)reallocarray(
            p->__ptr,
            p->capacity,
            sizeof(void *));
    }
    p->__ptr[p->element_count - 1] = element; // set new element
}

void pointer_list_shift_left(pointer_list_t *p, uint start_index)
{
    for (uint i = start_index; i < p->element_count - 1; i++)
    {
        p->__ptr[i] = p->__ptr[i + 1];
    }
    p->element_count--;
}

void pointer_list_removeat(pointer_list_t *p, uint index)
{
    pointer_list_shift_left(p, index);
}

void pointer_list_remove(pointer_list_t *p, void *element)
{
    for (uint i = 0; i < p->element_count; i++)
    {
        if (p->__ptr[i] == element)
        {
            pointer_list_removeat(p, i);
            return;
        }
    }
}

void pointer_list_shrink(pointer_list_t *p, uint amount)
{
    if (amount <= p->element_count)
        p->element_count -= amount;
    else
        p->element_count = 0;
}

void pointer_list_fit(pointer_list_t *p)
{
    pointer_list_resize(p, p->element_count);
}

void pointer_list_resize(pointer_list_t *p, uint new_size)
{
    p->capacity = new_size;
    if (p->element_count > p->capacity)
        p->element_count = p->capacity;

    p->__ptr = (void **)reallocarray(
        p->__ptr,
        p->capacity,
        sizeof(void *));
}