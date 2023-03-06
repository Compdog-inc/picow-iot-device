#include <pico/stdlib.h>
#include <pico/rand.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "random.h"

float get_rand_float()
{
    return (((float)get_rand_32()) / (float)(UINT32_MAX));
}

int get_rand_int(int min, int max)
{
    return min + (int)(get_rand_float() * (float)(max - min));
}