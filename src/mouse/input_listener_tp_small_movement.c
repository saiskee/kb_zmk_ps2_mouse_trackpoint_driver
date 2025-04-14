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

// Device configuration structure
struct small_movement_detector_config {
    const struct device *tracked_device;
    int16_t movement_threshold;
};

// Device data structure
struct small_movement_detector_data {
    int16_t x_movement;
    int16_t y_movement;
    bool pending_sync;
};

// Initialize the device
static int small_movement_detector_init(const struct device *dev) {
    const struct small_movement_detector_config *config = dev->config;
    struct small_movement_detector_data *data = dev->data;

    // Initialize data structure
    data->x_movement = 0;
    data->y_movement = 0;
    data->pending_sync = false;

    LOG_INF("Small movement detector initialized with threshold %d", config->movement_threshold);
    return 0;
}

// Define data and config structure for each instance
#define SMALL_MOVEMENT_DETECTOR_INIT(n) \
    static struct small_movement_detector_data small_movement_detector_data_##n; \
    \
    static const struct small_movement_detector_config small_movement_detector_config_##n = { \
        .tracked_device = DEVICE_DT_GET(DT_INST_PHANDLE(n, device)), \
        .movement_threshold = DT_INST_PROP_OR(n, movement_threshold, 3), \
    }; \
    \
    /* Callback function to handle input events */ \
    static void small_movement_handler_##n(struct input_event *evt) { \
        const struct device *dev = DEVICE_DT_INST_GET(n); \
        const struct small_movement_detector_config *config = dev->config; \
        struct small_movement_detector_data *data = dev->data; \
        \
        /* Only process relative movement events */ \
        if (evt->type == INPUT_EV_REL) { \
            /* Track X/Y movement */ \
            if (evt->code == INPUT_REL_X) { \
                data->x_movement = evt->value; \
                data->pending_sync = true; \
            } else if (evt->code == INPUT_REL_Y) { \
                data->y_movement = evt->value; \
                data->pending_sync = true; \
            } \
        } \
        \
        /* Process movement data on sync events */ \
        if (evt->sync && data->pending_sync) { \
            /* Check if the movement is small (non-zero but below threshold) */ \
            if ((abs(data->x_movement) > 0 || abs(data->y_movement) > 0) && \
                abs(data->x_movement) <= config->movement_threshold && \
                abs(data->y_movement) <= config->movement_threshold) { \
                LOG_INF("SMALL TRACKPOINT MOVEMENT DETECTED: x=%d, y=%d", \
                      data->x_movement, data->y_movement); \
            } \
            \
            /* Reset state for next event */ \
            data->x_movement = 0; \
            data->y_movement = 0; \
            data->pending_sync = false; \
        } \
    } \
    \
    /* Register callback for input events */ \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_INST_PHANDLE(n, device)), small_movement_handler_##n); \
    \
    DEVICE_DT_INST_DEFINE(n, \
                     small_movement_detector_init, \
                     NULL, \
                     &small_movement_detector_data_##n, \
                     &small_movement_detector_config_##n, \
                     APPLICATION, \
                     CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                     NULL);

// Create a device instance for each node with this compatible in the device tree
DT_INST_FOREACH_STATUS_OKAY(SMALL_MOVEMENT_DETECTOR_INIT)
