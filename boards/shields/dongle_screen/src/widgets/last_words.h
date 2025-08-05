#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_last_words
{
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *label;
};

int zmk_widget_last_words_init(struct zmk_widget_last_words *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_last_words_obj(struct zmk_widget_last_words *widget);