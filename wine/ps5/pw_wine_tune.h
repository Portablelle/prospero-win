/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_WINE_TUNE_H
#define PW_WINE_TUNE_H
#include <stddef.h>
#include <string.h>

/* PW_WINE_TUNE, a profile's opt-in runtime tuning: comma-separated
 * key=value items, in the characters a profile value allows (letters,
 * digits and _ + - , . = : /).
 *
 *   cpus=N          processors reported to the game (ntdll, patch 0996)
 *   dmem2m=1        direct memory in 2 MiB pages where a commit spans them
 *   rest=MASK[/P]   threads no name rule matches (wineserver)
 *   NAME=MASK[/P]   threads whose description starts with NAME, '_'
 *                   standing for a space; the first matching item wins
 *
 * MASK is a hexadecimal CPU mask, intersected with the process's own, and P
 * an optional native SCHED_FIFO priority (256 highest .. 767). */

/* The value of key in tune, copied into value: 1, or 0 when absent. */
static inline int pw_wine_tune_get(const char *tune, const char *key, char *value, size_t size)
{
    size_t key_length = strlen(key);

    while (tune && *tune) {
        const char *end = strchr(tune, ','), *equals;
        size_t item = end ? (size_t)(end - tune) : strlen(tune);

        equals = memchr(tune, '=', item);
        if (equals && (size_t)(equals - tune) == key_length && !memcmp(tune, key, key_length)) {
            size_t length = item - key_length - 1;
            if (length >= size) length = size - 1;
            memcpy(value, equals + 1, length);
            value[length] = 0;
            return 1;
        }
        tune = end ? end + 1 : NULL;
    }
    return 0;
}

static inline int pw_wine_tune_reserved(const char *key, size_t length)
{
    static const char *const keys[] = { "cpus", "dmem2m", "rest" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (strlen(keys[i]) == length && !memcmp(keys[i], key, length)) return 1;
    return 0;
}

/* The MASK[/P] of the first name rule matching name, or else of rest:
 * 1 with *mask and *priority (0 when not given), or 0. */
static inline int pw_wine_tune_thread(const char *tune, const char *name,
                                      unsigned long long *mask, int *priority)
{
    const char *found = NULL;
    char text[32];

    for (const char *item = tune; item && *item;) {
        const char *end = strchr(item, ','), *equals;
        size_t length = end ? (size_t)(end - item) : strlen(item), key;

        equals = memchr(item, '=', length);
        key = equals ? (size_t)(equals - item) : 0;
        if (key && !pw_wine_tune_reserved(item, key) && strlen(name) >= key) {
            size_t i = 0;
            while (i < key && (item[i] == '_' ? name[i] == ' ' : item[i] == name[i])) i++;
            if (i == key) { found = equals + 1; break; }
        }
        item = end ? end + 1 : NULL;
    }
    if (!found) {
        if (!pw_wine_tune_get(tune, "rest", text, sizeof(text))) return 0;
    } else {
        size_t length = strcspn(found, ",");
        if (length >= sizeof(text)) length = sizeof(text) - 1;
        memcpy(text, found, length);
        text[length] = 0;
    }
    {
        char *slash = strchr(text, '/'), *rest;
        unsigned long long value = 0;
        long p = 0;

        if (slash) *slash = 0;
        for (rest = text; *rest; rest++) {
            int digit = *rest >= '0' && *rest <= '9' ? *rest - '0' :
                        *rest >= 'a' && *rest <= 'f' ? *rest - 'a' + 10 :
                        *rest >= 'A' && *rest <= 'F' ? *rest - 'A' + 10 : -1;
            if (digit < 0 || value >> 60) return 0;
            value = value << 4 | (unsigned)digit;
        }
        if (slash)
            for (rest = slash + 1; *rest; rest++) {
                if (*rest < '0' || *rest > '9' || p > 1000) return 0;
                p = p * 10 + (*rest - '0');
            }
        if (p && (p < 256 || p > 767)) return 0;
        *mask = value;
        *priority = (int)p;
    }
    return 1;
}

#endif
