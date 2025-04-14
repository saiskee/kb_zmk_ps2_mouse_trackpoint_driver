/*
 * Copyright (c) 2023 Your Name
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_listener_tp_small_movement

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/dt-bindings/input/input-event-codes.h>
// #include <zmk/mouse/types.h>

#define VALID_LISTENER_COUNT 1

struct small_movement_listener_config {
    const struct device *tracked_device;
    int16_t movement_threshold;
};

struct small_movement_listener_data {
    const struct device *dev;
    int16_t last_x;
    int16_t last_y;
};

// Helper function to determine if movement is small
static bool is_small_movement(const struct small_movement_listener_config *config,
                             int16_t x, int16_t y) {
    return (abs(x) <= config->movement_threshold &&
            abs(y) <= config->movement_threshold &&
            (abs(x) > 0 || abs(y) > 0));
}

static void input_handler(struct input_event *evt, struct device *dev) {
    const struct small_movement_listener_config *config = dev->config;
    struct small_movement_listener_data *data = dev->data;

    int16_t x_movement = 0;
    int16_t y_movement = 0;

    // Track only relative movement events from the trackpoint
    if (evt->type != INPUT_EV_REL) {
        return;
    }

    // Apply configuration transformations
    int16_t value = evt->value;


    // Handle axis events
    if (evt->code == INPUT_REL_X) {
        x_movement = value;
    } else if (evt->code == INPUT_REL_Y) {
        y_movement = value;
    }

    // Check for small movements when we have synced data
    if (evt->sync && is_small_movement(config, x_movement, y_movement)) {
        LOG_INF("SMALL TRACKPOINT MOVEMENT DETECTED: x=%d, y=%d", x_movement, y_movement);
    }
}

static int small_movement_listener_init(const struct device *dev) {
    const struct small_movement_listener_config *config = dev->config;
    struct small_movement_listener_data *data = dev->data;

    data->dev = dev;
    data->last_x = 0;
    data->last_y = 0;

    // Register this listener to get input events from the tracked device
    input_register_callback(config->tracked_device, input_handler, dev);

    LOG_INF("Small movement listener initialized with threshold %d", config->movement_threshold);
    return 0;
}

#define SMALL_MOVEMENT_LISTENER_INIT(n)                                                         \
    static struct small_movement_listener_data sm_listener_data_##n;                           \
                                                                                               \
    static const struct small_movement_listener_config sm_listener_config_##n = {              \
        .tracked_device = DEVICE_DT_GET(DT_INST_PHANDLE(n, device)),                          \
        .movement_threshold = DT_INST_PROP_OR(n, movement_threshold, 3),                      \
    };                                                                                         \
                                                                                               \
    DEVICE_DT_INST_DEFINE(n, small_movement_listener_init, NULL,                               \
                     &sm_listener_data_##n, &sm_listener_config_##n,                           \
                     APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

DT_INST_FOREACH_STATUS_OKAY(SMALL_MOVEMENT_LISTENER_INIT)s
