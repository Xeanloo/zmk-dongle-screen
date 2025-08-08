/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include "last_words.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

#define MAX_WORD_LENGTH 20
#define MAX_WORDS 3
#define DISPLAY_CHAR_LIMIT 10
#define FADE_TIMEOUT_MS 3000

struct last_words_state {
    char current_word[MAX_WORD_LENGTH + 1];
    char words[MAX_WORDS][MAX_WORD_LENGTH + 1];
    int current_word_index;
    int word_count;
};

static struct last_words_state state = {0};

static int64_t last_activity_time = 0;
static struct k_timer fade_timer;

static bool is_letter_key(uint16_t keycode) {
    return (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z);
}

static bool is_space_key(uint16_t keycode) {
    return (keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR);
}

static bool is_backspace_key(uint16_t keycode) {
    return (keycode == HID_USAGE_KEY_KEYPAD_BACKSPACE);
}

static char keycode_to_char(uint16_t keycode) {
    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        return 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    }
    return 0;
}

static void add_word_to_history(const char* word) {
    if (strlen(word) == 0) return;
    
    // Shift existing words
    for (int i = MAX_WORDS - 1; i > 0; i--) {
        strcpy(state.words[i], state.words[i-1]);
    }
    
    // Add new word at the beginning
    strncpy(state.words[0], word, MAX_WORD_LENGTH);
    state.words[0][MAX_WORD_LENGTH] = '\0';
    
    if (state.word_count < MAX_WORDS) {
        state.word_count++;
    }
}

static void fade_anim_cb(void *label, int32_t opa) {
    lv_obj_set_style_opa((lv_obj_t *)label, opa, 0);
}

static void fade_out_display(struct k_timer *timer) {
    // Clear all words and current word
    state.current_word[0] = '\0';
    for (int i = 0; i < MAX_WORDS; i++) {
        state.words[i][0] = '\0';
    }
    state.word_count = 0;

    struct zmk_widget_last_words *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, widget->label);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&a, 1000); // 1 second fade duration
        lv_anim_set_exec_cb(&a, fade_anim_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_start(&a);
    }
}

static void update_display(void) {
    struct zmk_widget_last_words *widget;
    char display_text[DISPLAY_CHAR_LIMIT + 2] = "";

    const char *src = NULL;
    if (strlen(state.current_word) > 0) {
        src = state.current_word;
    } else if (state.word_count > 0) {
        src = state.words[0];
    }

    if (src) {
        int len = strlen(src);
        const char *start = len > DISPLAY_CHAR_LIMIT ? src + len - DISPLAY_CHAR_LIMIT : src;
        snprintf(display_text, sizeof(display_text), ">%s", start);
    } else {
        strcpy(display_text, "...");
    }

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->label, display_text);
        lv_obj_set_style_opa(widget->label, LV_OPA_COVER, 0); // Reset opacity to fully visible
    }

    last_activity_time = k_uptime_get();
    k_timer_start(&fade_timer, K_MSEC(FADE_TIMEOUT_MS), K_NO_WAIT);
}

static void fade_timer_init(void) {
    k_timer_init(&fade_timer, fade_out_display, NULL);
}

static int last_words_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    
    if (!ev->state) { // Key released
        return 0;
    }
    
    uint16_t keycode = ev->keycode;
    
    if (is_letter_key(keycode)) {
        // Add letter to current word
        int len = strlen(state.current_word);
        if (len < MAX_WORD_LENGTH) {
            state.current_word[len] = keycode_to_char(keycode);
            state.current_word[len + 1] = '\0';
        }
        update_display();
    } else if (is_space_key(keycode) || keycode == HID_USAGE_KEY_KEYBOARD_RETURN) {
        // Complete current word
        if (strlen(state.current_word) > 0) {
            add_word_to_history(state.current_word);
            state.current_word[0] = '\0';
        }
        update_display();
    } else if (is_backspace_key(keycode)) {
        // Remove last character
        int len = strlen(state.current_word);
        if (len > 0) {
            state.current_word[len - 1] = '\0';
        }
        update_display();
    }
    
    return 0;
}

ZMK_LISTENER(last_words_listener, last_words_listener);
ZMK_SUBSCRIPTION(last_words_listener, zmk_keycode_state_changed);

int zmk_widget_last_words_init(struct zmk_widget_last_words *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 240, 40);

    widget->label = lv_label_create(widget->obj);
    lv_obj_align(widget->label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(widget->label, "");
    lv_obj_set_style_text_font(widget->label, &lv_font_montserrat_20, 0);

    sys_slist_append(&widgets, &widget->node);

    fade_timer_init();

    return 0;
}

lv_obj_t *zmk_widget_last_words_obj(struct zmk_widget_last_words *widget) {
    return widget->obj;
}