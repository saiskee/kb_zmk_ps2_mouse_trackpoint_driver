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

// ZMK includes for mouse button handling
#include <zmk/endpoints.h>
#include <zmk/hid.h>

// Device configuration structure
struct small_movement_detector_config {
    const struct device *tracked_device;
    int16_t movement_threshold;
    uint32_t tap_timeout_ms;  // Timeout for tap detection
    uint32_t double_tap_timeout_ms; // Timeout for double tap detection
};

// Device data structure
struct small_movement_detector_data {
    int16_t x_movement;
    int16_t y_movement;
    bool pending_sync;

    // Debugging helpers
    uint32_t detection_count;

    // Tap detection
    bool potential_tap;               // Flag for potential tap detection
    int64_t last_movement_time_ms;    // Timestamp of last movement
    uint32_t tap_count;               // Count of taps detected

    // Double tap tracking
    int64_t last_tap_time_ms;         // Timestamp of last tap for double-tap detection
    uint32_t consecutive_taps;        // Count of consecutive taps within time window
};

// Initialize the device
static int small_movement_detector_init(const struct device *dev) {
    const struct small_movement_detector_config *config = dev->config;
    struct small_movement_detector_data *data = dev->data;

    // Initialize data structure
    data->x_movement = 0;
    data->y_movement = 0;
    data->pending_sync = false;
    data->detection_count = 0;

    // Initialize tap detection
    data->potential_tap = false;
    data->last_movement_time_ms = 0;
    data->tap_count = 0;

    // Initialize double tap detection
    data->last_tap_time_ms = 0;
    data->consecutive_taps = 0;

    LOG_INF("Small movement detector initialized with threshold %d, tap timeout %d ms, double tap timeout %d ms",
            config->movement_threshold, config->tap_timeout_ms, config->double_tap_timeout_ms);
    return 0;
}

// Helper function to emit mouse button events
static void emit_mouse_button_event(uint16_t button_code, uint16_t state) {
    // Log the mouse button event
    LOG_WRN("Emitting mouse button event: code %d, state %d", button_code, state);

    // Convert button code to ZMK mouse button index (0-based)
    // Button codes from INPUT_BTN_LEFT (0x110) start at 0x110
    int button_idx = button_code - INPUT_BTN_LEFT;

    if (button_idx < 0 || button_idx >= ZMK_HID_MOUSE_NUM_BUTTONS) {
        LOG_ERR("Invalid button index: %d", button_idx);
        return;
    }

    // Press or release the mouse button
    if (state) {
        zmk_hid_mouse_button_press(button_idx);
    } else {
        zmk_hid_mouse_button_release(button_idx);
    }

    // Send the mouse report
    zmk_endpoints_send_mouse_report();
}

// Define data and config structure for each instance
#define SMALL_MOVEMENT_DETECTOR_INIT(n) \
    static struct small_movement_detector_data small_movement_detector_data_##n; \
    \
    static const struct small_movement_detector_config small_movement_detector_config_##n = { \
        .tracked_device = DEVICE_DT_GET(DT_INST_PHANDLE(n, device)), \
        .movement_threshold = DT_INST_PROP_OR(n, movement_threshold, 3), \
        .tap_timeout_ms = DT_INST_PROP_OR(n, tap_timeout_ms, 200), \
        .double_tap_timeout_ms = DT_INST_PROP_OR(n, double_tap_timeout_ms, 300), \
    }; \
    \
    /* Callback function to handle input events */ \
    static void small_movement_handler_##n(struct input_event *evt) { \
        const struct device *dev = DEVICE_DT_INST_GET(n); \
        const struct small_movement_detector_config *config = dev->config; \
        struct small_movement_detector_data *data = dev->data; \
        int64_t current_time_ms = k_uptime_get(); \
        \
        /* Log each event type we receive */ \
        if (evt->type == INPUT_EV_REL) { \
            LOG_DBG("**** TP SMALL DETECTOR: Received REL event, code %d, value %d", evt->code, evt->value); \
            \
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
            LOG_DBG("**** TP SMALL DETECTOR: Processing SYNC event with x=%d, y=%d", \
                   data->x_movement, data->y_movement); \
            /* Set potential tap flag if any movement is detected */ \
            if ((data->x_movement != 0 || data->y_movement != 0) && !data->potential_tap) { \
                data->potential_tap = true; \
                data->last_movement_time_ms = current_time_ms; \
                LOG_DBG("**** POTENTIAL TAP STARTED: Waiting for timeout period ****"); \
            } \
            /* Check if there was a previous potential tap that timed out */ \
            if (data->potential_tap) { \
                int64_t elapsed_ms = current_time_ms - data->last_movement_time_ms; \
                if (elapsed_ms >= config->tap_timeout_ms) { \
                    data->tap_count++; \
                    LOG_WRN("**** TRACKPOINT TAP DETECTED (count: %d) ****", data->tap_count); \
                    \
                    /* Check for double tap */ \
                    int64_t tap_interval_ms = current_time_ms - data->last_tap_time_ms; \
                    \
                    if (tap_interval_ms <= config->double_tap_timeout_ms) { \
                        /* This is a consecutive tap within the double-tap time window */ \
                        data->consecutive_taps++; \
                        \
                        if (data->consecutive_taps == 2) { \
                            /* Double tap detected - emit left mouse button click */ \
                            LOG_WRN("**** DOUBLE TAP DETECTED - TRIGGERING LEFT MOUSE CLICK ****"); \
                            \
                            /* Press and release left mouse button */ \
                            emit_mouse_button_event(INPUT_BTN_LEFT, 1); /* Press */ \
                            emit_mouse_button_event(INPUT_BTN_LEFT, 0); /* Release */ \
                            \
                            /* Reset consecutive taps after handling */ \
                            data->consecutive_taps = 0; \
                        } \
                    } else { \
                        /* Too much time between taps, reset consecutive count */ \
                        LOG_DBG("**** TAP INTERVAL TOO LONG: %lld ms > %d ms ****", \
                               tap_interval_ms, config->double_tap_timeout_ms); \
                        data->consecutive_taps = 1; /* This is the first tap of a potential sequence */ \
                    } \
                    \
                    /* Update last tap time for next double-tap detection */ \
                    data->last_tap_time_ms = current_time_ms; \
                    data->potential_tap = false; \
                } else { \
                    LOG_DBG("**** NOT A TAP: Recent movements too close together (%lld ms < %d ms) ****", \
                           elapsed_ms, config->tap_timeout_ms); \
                } \
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
