#include <pico/stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rotencoder.h"
#include "../utils/debug.h"
#include "pointerlist.h"

bool callback_registered = false;
pointer_list_t _internal_instaces;

void gpio_callback(uint gpio, uint32_t events)
{
    for (uint i = 0; i < _internal_instaces.element_count; i++)
    {
        if (rotencoder_irq_callback((rotencoder_t *)pointer_list_get(&_internal_instaces, i), gpio, events)) // stop after first handled callback
            return;
    }
}

void rotencoder_register_callback()
{
    gpio_set_irq_callback(gpio_callback);
    callback_registered = true;
    pointer_list_init(&_internal_instaces, 1);
}

void rotencoder_deinit_callback()
{
    callback_registered = false;
    pointer_list_free(&_internal_instaces);
}

void rotencoder_subscribe(rotencoder_t *p)
{
    if (callback_registered)
    {
        p->subscribed = true;
        pointer_list_add(&_internal_instaces, (void *)p);
    }
}

void rotencoder_unsubscribe(rotencoder_t *p)
{
    if (p->subscribed)
    {
        p->subscribed = false;
        pointer_list_remove(&_internal_instaces, (void *)p);
    }
}

void rotencoder_init(rotencoder_t *p, uint clk_pin, uint dt_pin)
{
    p->clk_pin = clk_pin;
    p->dt_pin = dt_pin;

    gpio_init(clk_pin);
    gpio_set_dir(clk_pin, GPIO_IN);

    gpio_init(dt_pin);
    gpio_set_dir(dt_pin, GPIO_IN);

    gpio_set_irq_enabled(clk_pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(dt_pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    p->clk_val = false;
    p->rel_val = 0;

#ifdef ROTENCODER_AUTO_SUBSCRIBE
    p->subscribed = true;
    rotencoder_subscribe(p);
#else
    p->subscribed = false;
#endif
}

void rotencoder_deinit(rotencoder_t *p)
{
    gpio_set_irq_enabled(p->clk_pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    gpio_set_irq_enabled(p->dt_pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);

#ifdef ROTENCODER_AUTO_SUBSCRIBE
    if (p->subscribed)
        rotencoder_unsubscribe(p);
#endif
}

bool rotencoder_irq_callback(rotencoder_t *p, uint gpio, uint32_t events)
{
    if (gpio == p->clk_pin || gpio == p->dt_pin)
    {
        bool clk = gpio_get(p->clk_pin);
        if (clk != p->clk_val)
        {
            bool dt = gpio_get(p->dt_pin);
            if (clk != dt)
            {
                p->rel_val++;
            }
            else
            {
                p->rel_val--;
            }
        }

        p->clk_val = clk;
        return true;
    }

    return false;
}