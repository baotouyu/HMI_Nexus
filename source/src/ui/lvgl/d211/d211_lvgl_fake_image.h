#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline bool hmi_nexus_d211_fake_image_ends_with_ignore_case(const char * value,
                                                                   const char * suffix)
{
    size_t value_len;
    size_t suffix_len;
    size_t i;

    if(value == NULL || suffix == NULL) {
        return false;
    }

    value_len = strlen(value);
    suffix_len = strlen(suffix);
    if(value_len < suffix_len) {
        return false;
    }

    value += value_len - suffix_len;
    for(i = 0; i < suffix_len; ++i) {
        char lhs = value[i];
        char rhs = suffix[i];
        if(lhs >= 'A' && lhs <= 'Z') lhs = (char)(lhs - 'A' + 'a');
        if(rhs >= 'A' && rhs <= 'Z') rhs = (char)(rhs - 'A' + 'a');
        if(lhs != rhs) {
            return false;
        }
    }

    return true;
}

static inline bool hmi_nexus_d211_fake_image_path_looks_like(const char * path)
{
    return hmi_nexus_d211_fake_image_ends_with_ignore_case(path, ".fake");
}

static inline bool hmi_nexus_d211_parse_fake_image_path(const char * path,
                                                        uint16_t * width,
                                                        uint16_t * height,
                                                        bool * blend,
                                                        uint32_t * color)
{
    const char * cursor;
    char * end = NULL;
    unsigned long width_value;
    unsigned long height_value;
    unsigned long blend_value;
    unsigned long color_value;

    if(path == NULL || width == NULL || height == NULL || blend == NULL || color == NULL ||
       !hmi_nexus_d211_fake_image_path_looks_like(path)) {
        return false;
    }

    cursor = path;
    for(const char * it = path; *it != '\0'; ++it) {
        if(*it == '/' || *it == '\\' || *it == ':') {
            cursor = it + 1;
        }
    }

    width_value = strtoul(cursor, &end, 10);
    if(end == cursor || *end != 'x' || width_value == 0U || width_value > UINT16_MAX) {
        return false;
    }

    cursor = end + 1;
    height_value = strtoul(cursor, &end, 10);
    if(end == cursor || *end != '_' || height_value == 0U || height_value > UINT16_MAX) {
        return false;
    }

    cursor = end + 1;
    blend_value = strtoul(cursor, &end, 10);
    if(end == cursor || *end != '_' || blend_value > 1U) {
        return false;
    }

    cursor = end + 1;
    color_value = strtoul(cursor, &end, 16);
    if(end == cursor || !hmi_nexus_d211_fake_image_ends_with_ignore_case(end, ".fake")) {
        return false;
    }

    *width = (uint16_t)width_value;
    *height = (uint16_t)height_value;
    *blend = blend_value != 0U;
    *color = (uint32_t)color_value;
    return true;
}
