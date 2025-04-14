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
    uint32_t drag_threshold_ms;     // Time threshold to consider continuous movement as dragging
    uint32_t cooldown_timeout_ms;   // Cooldown time to consider movement ended
    uint32_t tap_max_duration_ms;   // Maximum duration for a movement to be considered a tap
};

// Device data structure
struct small_movement_detector_data {
    int16_t x_movement;
    int16_t y_movement;
    bool pending_sync;

    // Movement tracking
    bool is_moving;                  // Currently receiving movement events
    int64_t first_movement_time_ms;  // When movement started
    int64_t last_movement_time_ms;   // When last movement was detected
    bool is_dragging;                // Currently in dragging state
    uint32_t movement_count;         // Count of movement events in current sequence

    // Timer for detecting movement end
    struct k_work_delayable movement_end_timer;
    const struct device *dev;        // Reference to parent device for timer callback
};

// Forward declaration for the timer callback
static void movement_end_timer_callback(struct k_work *work);

// Initialize the device
static int small_movement_detector_init(const struct device *dev) {
    const struct small_movement_detector_config *config = dev->config;
    struct small_movement_detector_data *data = dev->data;

    // Initialize data structure
    data->x_movement = 0;
    data->y_movement = 0;
    data->pending_sync = false;

    // Initialize movement tracking
    data->is_moving = false;
    data->first_movement_time_ms = 0;
    data->last_movement_time_ms = 0;
    data->is_dragging = false;
    data->movement_count = 0;

    // Save device reference for timer callback
    data->dev = dev;

    // Initialize the timer
    k_work_init_delayable(&data->movement_end_timer, movement_end_timer_callback);

    LOG_INF("Movement detector initialized with drag threshold %d ms, cooldown %d ms, tap max duration %d ms",
            config->drag_threshold_ms, config->cooldown_timeout_ms, config->tap_max_duration_ms);
    return 0;
}

// Timer callback to detect end of movement
static void movement_end_timer_callback(struct k_work *work) {
    // Get the work_delayable structure
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    // Get the data structure that contains the timer
    struct small_movement_detector_data *data =
        CONTAINER_OF(dwork, struct small_movement_detector_data, movement_end_timer);

    // Get the device reference that was stored in the data structure
    const struct device *dev = data->dev;
    const struct small_movement_detector_config *config = dev->config;

    // Current time
    int64_t current_time_ms = k_uptime_get();

    // If we're still in moving state
    if (data->is_moving) {
        // Calculate total movement duration
        int64_t total_duration = data->last_movement_time_ms - data->first_movement_time_ms;

        LOG_INF("Movement ended. Duration: %lld ms, Events: %d",
                total_duration, data->movement_count);

        // Check if this was a tap (short duration movement)
        // A tap should be a very short duration movement (≤100ms) with few events
        if (total_duration <= config->tap_max_duration_ms &&
            !data->is_dragging &&
            data->movement_count <= 10) {  // Lower event count for shorter taps

            // IMPORTANT: Every single tap should trigger a mouse click
            // No need to wait for double tap or other conditions
            LOG_WRN("TAP DETECTED with duration %lld ms, %d events - TRIGGERING MOUSE CLICK",
                    total_duration, data->movement_count);

            // Trigger a left mouse button click using ZMK's functions
            // Use mouse button 0 for left click (INPUT_BTN_LEFT - INPUT_BTN_LEFT = 0)
            zmk_hid_mouse_button_press(0); // Press left mouse button
            k_sleep(K_MSEC(30));           // Longer delay to ensure click is registered
            zmk_hid_mouse_button_release(0); // Release left mouse button
            zmk_endpoints_send_mouse_report(); // Send the mouse report
        } else if (data->is_dragging) {
            LOG_WRN("DRAG ENDED after %lld ms, %d events",
                    total_duration, data->movement_count);
        }

        // Reset movement tracking
        data->is_moving = false;
        data->is_dragging = false;
        data->movement_count = 0;
    }
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
        .drag_threshold_ms = DT_INST_PROP_OR(n, drag_threshold_ms, 300), \
        .cooldown_timeout_ms = DT_INST_PROP_OR(n, cooldown_timeout_ms, 80), \
        .tap_max_duration_ms = DT_INST_PROP_OR(n, tap_max_duration_ms, 100), \
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
            LOG_DBG("Received REL event, code %d, value %d", evt->code, evt->value); \
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
            /* If movement is detected */ \
            if (data->x_movement != 0 || data->y_movement != 0) { \
                /* Cancel any pending end timer */ \
                k_work_cancel_delayable(&data->movement_end_timer); \
                \
                /* If this is the start of a new movement sequence */ \
                if (!data->is_moving) { \
                    data->is_moving = true; \
                    data->first_movement_time_ms = current_time_ms; \
                    data->movement_count = 1; \
                    LOG_DBG("Movement started at %lld ms", current_time_ms); \
                } else { \
                    data->movement_count++; \
                    \
                    /* Check if we've been moving long enough to be considered dragging */ \
                    int64_t movement_duration = current_time_ms - data->first_movement_time_ms; \
                    \
                    /* If we're getting rapid movement events or moving for a long time, it's a drag */ \
                    if (!data->is_dragging && \
                        (movement_duration > config->drag_threshold_ms || \
                         data->movement_count > 20)) { \
                        data->is_dragging = true; \
                        LOG_WRN("DRAG DETECTED after %lld ms with %d movements", \
                               movement_duration, data->movement_count); \
                    } \
                } \
                \
                /* Update last movement time */ \
                data->last_movement_time_ms = current_time_ms; \
                \
                /* Schedule the movement end timer - this will fire if no more events are received */ \
                /* Use a shorter timer when still within tap threshold to detect taps quicker */ \
                int64_t movement_duration = current_time_ms - data->first_movement_time_ms; \
                uint32_t timeout = (movement_duration < config->tap_max_duration_ms) ? \
                                  (config->cooldown_timeout_ms / 2) : config->cooldown_timeout_ms; \
                k_work_schedule(&data->movement_end_timer, K_MSEC(timeout)); \
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
