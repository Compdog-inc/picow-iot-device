#ifndef _ROTENCODER_H
#define _ROTENCODER_H

#define ROTENCODER_AUTO_SUBSCRIBE

#include <pico/stdlib.h>

typedef struct
{
    uint clk_pin;
    uint dt_pin;
    bool subscribed;

    bool clk_val;

    int rel_val;
} rotencoder_t;

/**
 * Sets the GPIO IRQ callback to the internal rotencoder callback
 * @warning ONLY CALL IF NOT USING CUSTOM CALLBACK
 * @note call rotencoder_deinit_callback() when done
 */
void rotencoder_register_callback();

/**
 * Deinits the internal callback and instance list
 * @warning Only call after rotencoder_register_callback()
 * @note Always deinit all subscribed instances first
 */
void rotencoder_deinit_callback();

/**
 * Inits a rotencoder instance and subscribes to the callback if it is registered (ROTENCODER_AUTO_SUBSCRIBE)
 */
void rotencoder_init(rotencoder_t *p, uint clk_pin, uint dt_pin);

/**
 * Deinits a rotencoder instance and unsubscribes from the callback if it is registered (ROTENCODER_AUTO_SUBSCRIBE)
 */
void rotencoder_deinit(rotencoder_t *p);

/**
 * Subscribes a rotencoder instance to the internal callback
 * @warning Only call after rotencoder_register_callback()
 * @note There is no need to subscribe if you init after registering the callback (ROTENCODER_AUTO_SUBSCRIBE)
 */
void rotencoder_subscribe(rotencoder_t *p);

/**
 * Unsubscribes a rotencoder instance from the internal callback
 * @warning Only call after rotencoder_register_callback()
 * @note There is no need to unsubscribe if you deinit after registering the callback (ROTENCODER_AUTO_SUBSCRIBE)
 */
void rotencoder_unsubscribe(rotencoder_t *p);

/**
 * Manual irq handle for a rotencoder instance
 * @note Only call if not using the internal callback (rotencoder_register_callback)
 * @return true if callback handled
 */
bool rotencoder_irq_callback(rotencoder_t *p, uint gpio, uint32_t events);

#endif