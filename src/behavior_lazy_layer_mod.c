/*
 * Copyright (c) 2026 Engin Al
 *
 * SPDX-License-Identifier: MIT
 *
 * Lazy layer-mod: a momentary layer that also acts as a held modifier, but only when it is held on
 * its own. Pressing it activates the layer at once. The modifier (for example Cmd/Win) is pressed
 * only after `delay-ms` passes with no other key used; any non-modifier key or another layer
 * cancels it for the rest of the hold. Released without using another key, it can tap the modifier.
 *
 * The point: Win/Cmd never wraps a key it wasn't meant for. On Windows, releasing a Win key that
 * nothing else touched opens the Start menu, and sending a symbol through a held Win needs Win
 * released first, which does exactly that.
 */

#define DT_DRV_COMPAT zmk_behavior_lazy_layer_mod

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

struct lazy_layer_mod_config {
    uint32_t keycode;
    uint32_t delay_ms;
    bool tap_on_release;
};

struct lazy_layer_mod_data {
    const struct lazy_layer_mod_config *config;
    struct k_work_delayable mod_work;
    uint8_t layer;
    bool active;
    // Another key or layer was used during this hold: the modifier stays out of it.
    bool combined;
    bool mod_down;
};

// The instance being held. Only one is tracked; a second one pressed meanwhile takes over.
static struct lazy_layer_mod_data *held;

static void raise_mod(const struct lazy_layer_mod_config *config, bool pressed) {
    raise_zmk_keycode_state_changed_from_encoded(config->keycode, pressed, k_uptime_get());
}

static void mod_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct lazy_layer_mod_data *data = CONTAINER_OF(dwork, struct lazy_layer_mod_data, mod_work);

    if (!data->active || data->combined || data->mod_down) {
        return;
    }

    LOG_DBG("lazy layer-mod: held alone, pressing the modifier");
    data->mod_down = true;
    raise_mod(data->config, true);
}

static void mark_combined(struct lazy_layer_mod_data *data) {
    if (data->combined) {
        return;
    }
    data->combined = true;
    k_work_cancel_delayable(&data->mod_work);
}

static int lazy_layer_mod_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct lazy_layer_mod_data *data = dev->data;

    data->layer = binding->param1;
    data->combined = false;
    data->mod_down = false;
    data->active = true;
    held = data;

    // Activating the layer raises a layer event for our own layer; the listener ignores it.
    zmk_keymap_layer_activate(data->layer, false);
    k_work_reschedule(&data->mod_work, K_MSEC(data->config->delay_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}

static int lazy_layer_mod_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct lazy_layer_mod_data *data = dev->data;

    k_work_cancel_delayable(&data->mod_work);
    data->active = false;
    if (held == data) {
        held = NULL;
    }

    if (data->mod_down) {
        data->mod_down = false;
        raise_mod(data->config, false);
    } else if (!data->combined && data->config->tap_on_release) {
        // Held alone, but released before the delay: act like a quick press of the modifier.
        raise_mod(data->config, true);
        raise_mod(data->config, false);
    }

    zmk_keymap_layer_deactivate(data->layer, false);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api lazy_layer_mod_driver_api = {
    .binding_pressed = lazy_layer_mod_pressed,
    .binding_released = lazy_layer_mod_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

// Key events and layer changes are always raised fresh (never replayed by hold-taps), so this
// listener sees them wherever it sits in the listener order.
static int lazy_layer_mod_listener(const zmk_event_t *eh) {
    struct lazy_layer_mod_data *data = held;

    if (data == NULL || !data->active || data->combined) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (kc != NULL) {
        // Modifiers (Shift for Cmd+Shift+click, our own modifier) don't count as using a key.
        if (kc->state && !is_mod(kc->usage_page, kc->keycode)) {
            mark_combined(data);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_layer_state_changed *ls = as_zmk_layer_state_changed(eh);
    if (ls != NULL && ls->state && ls->layer != data->layer) {
        mark_combined(data);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_lazy_layer_mod, lazy_layer_mod_listener);
ZMK_SUBSCRIPTION(behavior_lazy_layer_mod, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(behavior_lazy_layer_mod, zmk_layer_state_changed);

static int lazy_layer_mod_init(const struct device *dev) {
    struct lazy_layer_mod_data *data = dev->data;

    data->config = dev->config;
    k_work_init_delayable(&data->mod_work, mod_work_handler);

    return 0;
}

#define LAZY_LAYER_MOD_INST(n)                                                                     \
    static const struct lazy_layer_mod_config lazy_layer_mod_config_##n = {                        \
        .keycode = DT_INST_PROP(n, keycode),                                                       \
        .delay_ms = DT_INST_PROP(n, delay_ms),                                                     \
        .tap_on_release = DT_INST_PROP(n, tap_on_release),                                         \
    };                                                                                             \
    static struct lazy_layer_mod_data lazy_layer_mod_data_##n;                                     \
    BEHAVIOR_DT_INST_DEFINE(n, lazy_layer_mod_init, NULL, &lazy_layer_mod_data_##n,                \
                            &lazy_layer_mod_config_##n, POST_KERNEL,                               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &lazy_layer_mod_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LAZY_LAYER_MOD_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
