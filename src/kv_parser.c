#include <string.h>
#include <stdlib.h>
#include "kv_parser.h"

int kv_next_line(FILE *f, char *buf, size_t cap) {
    while (fgets(buf, (int)cap, f) != NULL) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';

        const char *p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue; /* blank or comment line - keep reading */
        return 1;
    }
    return 0;
}

int kv_next_token(const char **cursor, char *tok, size_t cap) {
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') {
        *cursor = p;
        return 0;
    }
    size_t i = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (i + 1 < cap) tok[i++] = *p;
        p++;
    }
    tok[i] = '\0';
    *cursor = p;
    return 1;
}

int kv_split_kv(const char *token, char *key, size_t key_cap, char *val, size_t val_cap) {
    const char *eq = strchr(token, '=');
    if (eq == NULL) return 0;
    size_t klen = (size_t)(eq - token);
    if (klen >= key_cap) klen = key_cap - 1;
    memcpy(key, token, klen);
    key[klen] = '\0';
    strncpy(val, eq + 1, val_cap - 1);
    val[val_cap - 1] = '\0';
    return 1;
}

int kv_parse_int_list(const char *val, int *out, int max_count) {
    int n = 0;
    const char *p = val;
    while (n < max_count && *p != '\0') {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = end;
        if (*p == ',') p++;
        else break;
    }
    return n;
}

void kv_write_line(FILE *f, const char *line) {
    fputs(line, f);
    fputc('\n', f);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void kv_hex_encode(const unsigned char *data, size_t len, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = digits[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = digits[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

int kv_hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = (hi >= 0) ? hex_val(hex[i * 2 + 1]) : -1;
        if (hi < 0 || lo < 0) return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}
