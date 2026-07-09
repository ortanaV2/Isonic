#include <string.h>
#include "ic_registry.h"

static const IC_Def *g_registry[IC_REGISTRY_MAX];
static int g_registry_count = 0;

void ic_registry_register(const IC_Def *def) {
    if (g_registry_count >= IC_REGISTRY_MAX) {
        return;
    }
    g_registry[g_registry_count++] = def;
}

const IC_Def *ic_registry_get(const char *name) {
    for (int i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i]->name, name) == 0) {
            return g_registry[i];
        }
    }
    return NULL;
}

/* Fixed body width for every standard DIP package - only the height (driven
   by pin count) varies, matching how real DIP chips scale. */
#define IC_DIP_WIDTH 4

void ic_dip_body_size(int pin_count, int *out_w, int *out_h) {
    int per_side = pin_count / 2;
    *out_w = IC_DIP_WIDTH;
    *out_h = per_side + 1;
}
