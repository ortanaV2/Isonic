#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "schematic_io.h"
#include "kv_parser.h"
#include "ic_registry.h"
#include "ics/at28c64b.h"

static const char *layer_role_name(LayerRole role) {
    switch (role) {
        case LAYER_ROLE_GND: return "GND";
        case LAYER_ROLE_POWER: return "POWER";
        default: return "NORMAL";
    }
}

static LayerRole parse_layer_role(const char *s) {
    if (strcmp(s, "GND") == 0) return LAYER_ROLE_GND;
    if (strcmp(s, "POWER") == 0) return LAYER_ROLE_POWER;
    return LAYER_ROLE_NORMAL;
}

static const char *wire_kind_name(WireKind kind) {
    switch (kind) {
        case WIRE_KIND_INPUT: return "INPUT";
        case WIRE_KIND_OUTPUT: return "OUTPUT";
        default: return "NORMAL";
    }
}

static WireKind parse_wire_kind(const char *s) {
    if (strcmp(s, "INPUT") == 0) return WIRE_KIND_INPUT;
    if (strcmp(s, "OUTPUT") == 0) return WIRE_KIND_OUTPUT;
    return WIRE_KIND_NORMAL;
}

int schematic_save(const Circuit *circuit, const Camera *camera, const char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) return 0;

    kv_write_line(f, "ISONIC_SCHEMATIC 1");
    char line[KV_MAX_LINE];

    snprintf(line, sizeof(line), "camera zoom=%.6f pan=%.6f,%.6f", camera->zoom, camera->pan_x, camera->pan_y);
    kv_write_line(f, line);

    /* name=... is always the LAST field on a layer line and takes the rest
       of the line verbatim (see schematic_load) - it's the only free-text
       field in the whole format (layer names may contain spaces; every
       other record is numeric/enum-only), so it can't be a plain
       whitespace-delimited token like the rest. */
    for (int i = 0; i < MAX_LAYERS; i++) {
        const Layer *l = &circuit->layers[i];
        if (!l->in_use) continue;
        snprintf(line, sizeof(line), "layer slot=%d color=%d,%d,%d role=%s name=%s",
                 i, l->color_r, l->color_g, l->color_b, layer_role_name(l->role), l->name);
        kv_write_line(f, line);
    }

    {
        char order[128] = "";
        size_t len = 0;
        for (int i = 0; i < circuit->layer_order_count; i++) {
            int n = snprintf(order + len, sizeof(order) - len, "%s%d", i > 0 ? "," : "", circuit->layer_order[i]);
            if (n > 0) len += (size_t)n;
        }
        snprintf(line, sizeof(line), "layer_order %s", order);
        kv_write_line(f, line);
    }

    for (int i = 0; i < circuit->component_high_water; i++) {
        const Component *c = &circuit->components[i];
        if (!c->in_use) continue;

        char seq_hex[IC_SEQ_STATE_BYTES * 2 + 1];
        kv_hex_encode(c->seq_state, IC_SEQ_STATE_BYTES, seq_hex);
        snprintf(line, sizeof(line), "component ic=%s x=%d y=%d rot=%d seq=%s",
                 c->ic_def->name, c->grid_x, c->grid_y, c->rotation, seq_hex);
        kv_write_line(f, line);

        if (ic_at28c64b_is(c)) {
            /* ic_at28c64b_memory wants a non-const Component* since it may
               lazily claim a pool slot on first touch (see at28c64b.c) - a
               harmless bookkeeping side effect, not a change to anything
               being saved, so casting the constness away here is safe. */
            unsigned char *mem = ic_at28c64b_memory((Component *)c);
            int programmed = 0;
            for (int b = 0; b < AT28C64B_SIZE; b++) {
                if (mem[b] != 0xFF) { programmed = 1; break; }
            }
            if (programmed) {
                char *hex = (char *)malloc((size_t)AT28C64B_SIZE * 2 + 1);
                if (hex != NULL) {
                    kv_hex_encode(mem, AT28C64B_SIZE, hex);
                    fputs("eeprom_data ", f);
                    fputs(hex, f);
                    fputc('\n', f);
                    free(hex);
                }
            }
        }
    }

    for (int i = 0; i < circuit->wire_high_water; i++) {
        const Wire *w = &circuit->wires[i];
        if (!w->in_use) continue;
        snprintf(line, sizeof(line), "wire kind=%s from=%d,%d to=%d,%d layer=%d input=%d",
                 wire_kind_name(w->kind), w->from_x, w->from_y, w->to_x, w->to_y, w->layer_slot, w->input_value);
        kv_write_line(f, line);
    }

    for (int i = 0; i < circuit->via_high_water; i++) {
        const Via *v = &circuit->vias[i];
        if (!v->in_use) continue;
        snprintf(line, sizeof(line), "via x=%d,%d layers=%d,%d", v->x, v->y, v->layer_slot_a, v->layer_slot_b);
        kv_write_line(f, line);
    }

    /* label=... is the last field and may contain spaces - same free-text-
       tail convention as layer's own name=..., see schematic_load. */
    for (int i = 0; i < circuit->section_high_water; i++) {
        const Section *s = &circuit->sections[i];
        if (!s->in_use) continue;
        snprintf(line, sizeof(line), "section x0=%d y0=%d x1=%d y1=%d locked=%d label=%s",
                 s->x0, s->y0, s->x1, s->y1, s->locked, s->label);
        kv_write_line(f, line);
    }

    /* text=... is the last field, same convention. */
    for (int i = 0; i < circuit->text_label_high_water; i++) {
        const TextLabel *t = &circuit->text_labels[i];
        if (!t->in_use) continue;
        snprintf(line, sizeof(line), "text_label x=%d y=%d text=%s", t->x, t->y, t->text);
        kv_write_line(f, line);
    }

    fclose(f);
    return 1;
}

int schematic_load(Circuit *circuit, Camera *camera, const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;

    char line[KV_MAX_LINE];
    if (!kv_next_line(f, line, sizeof(line)) || strncmp(line, "ISONIC_SCHEMATIC", 16) != 0) {
        fclose(f);
        return 0;
    }

    circuit_init(circuit); /* clean slate - every record below overwrites its slot/field directly */

    int last_component_id = -1; /* which component an immediately-following eeprom_data line belongs to */

    while (kv_next_line(f, line, sizeof(line))) {
        const char *cursor = line;
        char keyword[32];
        if (!kv_next_token(&cursor, keyword, sizeof(keyword))) continue;

        if (strcmp(keyword, "camera") == 0) {
            char tok[64], key[32], val[64];
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "zoom") == 0) {
                    /* clamped the same as camera_zoom_at already keeps every
                       live zoom change within - guards against a corrupted
                       or hand-edited file setting a degenerate (<=0) zoom,
                       which would otherwise divide-by-zero/blow up the very
                       next camera_screen_to_grid call */
                    float z = (float)atof(val);
                    if (z < CAMERA_MIN_ZOOM) z = CAMERA_MIN_ZOOM;
                    if (z > CAMERA_MAX_ZOOM) z = CAMERA_MAX_ZOOM;
                    camera->zoom = z;
                } else if (strcmp(key, "pan") == 0) {
                    float p[2];
                    if (sscanf(val, "%f,%f", &p[0], &p[1]) == 2) {
                        camera->pan_x = p[0];
                        camera->pan_y = p[1];
                    }
                }
            }

        } else if (strcmp(keyword, "layer") == 0) {
            Layer parsed;
            memset(&parsed, 0, sizeof(parsed));
            parsed.in_use = 1;
            int slot = -1;

            /* name=... is the last field and may contain spaces - sliced
               directly out of the untouched line rather than tokenized,
               see the matching comment in schematic_save. */
            const char *name_eq = strstr(cursor, "name=");
            if (name_eq != NULL) {
                strncpy(parsed.name, name_eq + 5, sizeof(parsed.name) - 1);
                parsed.name[sizeof(parsed.name) - 1] = '\0';
            }

            char tok[64], key[32], val[64];
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (strncmp(tok, "name=", 5) == 0) break; /* reached (a possibly-truncated copy of) the name field - already captured above, stop */
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "slot") == 0) {
                    slot = atoi(val);
                } else if (strcmp(key, "color") == 0) {
                    int rgb[3];
                    if (kv_parse_int_list(val, rgb, 3) == 3) {
                        parsed.color_r = (unsigned char)rgb[0];
                        parsed.color_g = (unsigned char)rgb[1];
                        parsed.color_b = (unsigned char)rgb[2];
                    }
                } else if (strcmp(key, "role") == 0) {
                    parsed.role = parse_layer_role(val);
                }
            }
            if (slot >= 0 && slot < MAX_LAYERS) circuit->layers[slot] = parsed;

        } else if (strcmp(keyword, "layer_order") == 0) {
            int order[MAX_LAYERS];
            int n = kv_parse_int_list(cursor, order, MAX_LAYERS);
            if (n > 0) {
                circuit->layer_order_count = n;
                for (int i = 0; i < n; i++) circuit->layer_order[i] = order[i];
            }

        } else if (strcmp(keyword, "component") == 0) {
            char tok[64], key[32], val[64];
            char ic_name[64] = "";
            int x = 0, y = 0, rot = 0;
            unsigned char seq[IC_SEQ_STATE_BYTES];
            memset(seq, 0, sizeof(seq));
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "ic") == 0) {
                    strncpy(ic_name, val, sizeof(ic_name) - 1);
                    ic_name[sizeof(ic_name) - 1] = '\0';
                }
                else if (strcmp(key, "x") == 0) x = atoi(val);
                else if (strcmp(key, "y") == 0) y = atoi(val);
                /* absent on a file saved before rotation existed - rot stays
                   0, the same "natural" orientation those files always meant */
                else if (strcmp(key, "rot") == 0) rot = atoi(val);
                else if (strcmp(key, "seq") == 0) kv_hex_decode(val, seq, IC_SEQ_STATE_BYTES);
            }

            last_component_id = -1;
            const IC_Def *def = ic_registry_get(ic_name);
            if (def == NULL) {
                fprintf(stderr, "Isonic: skipping unknown IC '%s' while loading %s\n", ic_name, path);
                continue;
            }
            int id = circuit_add_ic(circuit, x, y, def);
            if (id >= 0) {
                Component *c = &circuit->components[id];
                c->rotation = rot & 3;
                memcpy(c->seq_state, seq, IC_SEQ_STATE_BYTES);
                if (ic_at28c64b_is(c)) {
                    /* The AT28C64B memory pool is a process-lifetime global -
                       a slot number saved by a previous process is
                       meaningless here. Force "unclaimed" so
                       ic_at28c64b_memory() below claims a fresh slot
                       (always zero-initialized-to-0xFF) instead of trusting
                       a stale index. */
                    c->seq_state[0] = 0;
                }
                last_component_id = id;
            }

        } else if (strcmp(keyword, "eeprom_data") == 0) {
            const char *hex = cursor;
            while (*hex == ' ' || *hex == '\t') hex++;
            if (last_component_id >= 0) {
                Component *c = &circuit->components[last_component_id];
                if (ic_at28c64b_is(c)) {
                    unsigned char *mem = ic_at28c64b_memory(c);
                    if (mem != NULL) kv_hex_decode(hex, mem, AT28C64B_SIZE);
                }
            }

        } else if (strcmp(keyword, "wire") == 0) {
            char tok[64], key[32], val[64];
            WireKind kind = WIRE_KIND_NORMAL;
            int fx = 0, fy = 0, tx = 0, ty = 0, layer = 0, input_value = 0;
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "kind") == 0) {
                    kind = parse_wire_kind(val);
                } else if (strcmp(key, "from") == 0) {
                    int p[2];
                    if (kv_parse_int_list(val, p, 2) == 2) { fx = p[0]; fy = p[1]; }
                } else if (strcmp(key, "to") == 0) {
                    int p[2];
                    if (kv_parse_int_list(val, p, 2) == 2) { tx = p[0]; ty = p[1]; }
                } else if (strcmp(key, "layer") == 0) {
                    layer = atoi(val);
                } else if (strcmp(key, "input") == 0) {
                    input_value = atoi(val);
                }
            }
            if (layer < 0 || layer >= MAX_LAYERS || !circuit->layers[layer].in_use) layer = circuit->layer_order[0];
            int wid = circuit_add_wire(circuit, fx, fy, tx, ty, kind, layer);
            if (wid >= 0) circuit->wires[wid].input_value = input_value;

        } else if (strcmp(keyword, "via") == 0) {
            char tok[64], key[32], val[64];
            int x = 0, y = 0, la = 0, lb = 0;
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "x") == 0) {
                    int p[2];
                    if (kv_parse_int_list(val, p, 2) == 2) { x = p[0]; y = p[1]; }
                } else if (strcmp(key, "layers") == 0) {
                    int p[2];
                    if (kv_parse_int_list(val, p, 2) == 2) { la = p[0]; lb = p[1]; }
                }
            }
            circuit_add_via(circuit, x, y, la, lb);

        } else if (strcmp(keyword, "section") == 0) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0, locked = 0;
            char label[SECTION_LABEL_MAX_LEN + 1] = "";

            /* label=... is the last field and may contain spaces - sliced
               directly out of the untouched line, same convention as
               layer's own name=, see schematic_save. */
            const char *label_eq = strstr(cursor, "label=");
            if (label_eq != NULL) {
                strncpy(label, label_eq + 6, sizeof(label) - 1);
                label[sizeof(label) - 1] = '\0';
            }

            char tok[64], key[32], val[64];
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (strncmp(tok, "label=", 6) == 0) break;
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "x0") == 0) x0 = atoi(val);
                else if (strcmp(key, "y0") == 0) y0 = atoi(val);
                else if (strcmp(key, "x1") == 0) x1 = atoi(val);
                else if (strcmp(key, "y1") == 0) y1 = atoi(val);
                else if (strcmp(key, "locked") == 0) locked = atoi(val);
            }
            int sec_id = circuit_add_section(circuit, x0, y0, x1, y1, label);
            if (sec_id >= 0) circuit->sections[sec_id].locked = locked;

        } else if (strcmp(keyword, "text_label") == 0) {
            int x = 0, y = 0;
            char text[TEXT_LABEL_MAX_LEN + 1] = "";

            /* text=... is the last field, same free-text-tail convention. */
            const char *text_eq = strstr(cursor, "text=");
            if (text_eq != NULL) {
                strncpy(text, text_eq + 5, sizeof(text) - 1);
                text[sizeof(text) - 1] = '\0';
            }

            char tok[64], key[32], val[64];
            while (kv_next_token(&cursor, tok, sizeof(tok))) {
                if (strncmp(tok, "text=", 5) == 0) break;
                if (!kv_split_kv(tok, key, sizeof(key), val, sizeof(val))) continue;
                if (strcmp(key, "x") == 0) x = atoi(val);
                else if (strcmp(key, "y") == 0) y = atoi(val);
            }
            circuit_add_text_label(circuit, x, y, text);
        }
        /* any other/unrecognized keyword is silently skipped - forward compat */
    }

    fclose(f);
    circuit_rebuild_nets(circuit);
    return 1;
}
