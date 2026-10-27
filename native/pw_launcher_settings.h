/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_LAUNCHER_SETTINGS_H
#define PW_LAUNCHER_SETTINGS_H
#include "pw_wine_library.h"
enum { PW_SETTINGS_ROWS = 21 };
typedef struct PwLauncherSettings {
    int open, row, resolution_changed, mode;
    char file[PW_WINE_LIBRARY_NAME];
    PwGameProfile draft;
    PwGameInput input;
    char labels[PW_SETTINGS_ROWS][32], values[PW_SETTINGS_ROWS][64];
    char status[128];
} PwLauncherSettings;
int pw_settings_open(PwLauncherSettings *s, const PwWineLibraryEntry *entry, const char *root);
void pw_settings_change(PwLauncherSettings *s, int direction);
void pw_settings_labels(PwLauncherSettings *s);
int pw_settings_save(PwLauncherSettings *s, const char *root);
/* Bounded, preserving INI edits; also used by host tests. */
int pw_settings_text(const PwLauncherSettings *s, const char *original, char *out, size_t cap);
#endif
