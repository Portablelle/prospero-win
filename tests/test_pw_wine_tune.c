/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../wine/ps5/pw_wine_tune.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const char *tune = "cpus=11,dmem2m=1,Main_Thread=3/699,Main=c,Load_Game_Pool=1ff0,rest=1ffc";
    unsigned long long mask;
    int priority;
    char value[8];

    assert(pw_wine_tune_get(tune, "cpus", value, sizeof(value)) && !strcmp(value, "11"));
    assert(pw_wine_tune_get(tune, "dmem2m", value, sizeof(value)) && !strcmp(value, "1"));
    assert(!pw_wine_tune_get(tune, "dmem", value, sizeof(value)));
    assert(!pw_wine_tune_get(NULL, "cpus", value, sizeof(value)));
    assert(pw_wine_tune_get("cpus=123456789", "cpus", value, 4) && !strcmp(value, "123"));

    /* first matching name rule, '_' for a space */
    assert(pw_wine_tune_thread(tune, "Main Thread", &mask, &priority) && mask == 3 && priority == 699);
    assert(pw_wine_tune_thread(tune, "Main Launcher 1", &mask, &priority) && mask == 0xc && priority == 0);
    assert(pw_wine_tune_thread(tune, "Load Game Pool 2", &mask, &priority) && mask == 0x1ff0);
    /* no rule: rest; reserved keys are not names */
    assert(pw_wine_tune_thread(tune, "", &mask, &priority) && mask == 0x1ffc && priority == 0);
    assert(pw_wine_tune_thread(tune, "cpus", &mask, &priority) && mask == 0x1ffc);
    assert(!pw_wine_tune_thread("cpus=4", "Main Thread", &mask, &priority));
    /* malformed rules apply nothing */
    assert(!pw_wine_tune_thread("Main=xyz", "Main", &mask, &priority));
    assert(!pw_wine_tune_thread("Main=3/100", "Main", &mask, &priority));
    assert(!pw_wine_tune_thread("Main=3/9999", "Main", &mask, &priority));
    assert(pw_wine_tune_thread("Main=/700", "Main", &mask, &priority) && mask == 0 && priority == 700);
    puts("wine tune passed: keys, truncation, name rules with spaces, rest, reserved keys and malformed rules");
    return 0;
}
