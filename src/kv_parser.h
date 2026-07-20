#ifndef ISONIC_KV_PARSER_H
#define ISONIC_KV_PARSER_H

#include <stddef.h>
#include <stdio.h>

/* Long enough for a full 16384-hex-char EEPROM data line plus its
   "eeprom_data " prefix - the longest line either file format ever writes. */
#define KV_MAX_LINE 16448

/* Shared line-based key=value plumbing for both the .isonic schematic format
   (schematic_io.c) and the settings.ini format (settings.c). Deliberately
   the one shared utility between those two otherwise-independent formats -
   a parser has real shared behavior (a bug fixed in one copy but not the
   other is a real risk), unlike the small per-file duplicated UI constants
   elsewhere in this codebase (see layer_panel.c/data_editor.c), which stay
   duplicated on purpose. */

/* Reads the next non-blank, non-'#'-comment line from f into buf (trailing
   \r/\n stripped). Returns 0 at EOF, 1 otherwise. */
int kv_next_line(FILE *f, char *buf, size_t cap);

/* Copies the next whitespace-delimited token from *cursor into tok (advancing
   *cursor past it and any following whitespace); silently truncates a token
   longer than cap-1, same "silently drop past a limit" precedent as
   layer_panel_text_input. Returns 0 once *cursor is exhausted. Used to walk
   a schematic record line's "field=value field=value ..." tail one token at
   a time. */
int kv_next_token(const char **cursor, char *tok, size_t cap);

/* Splits "key=value" (a whole settings.ini line, or one token from a
   schematic record's tail) at the first '='. Returns 0 if there's no '='. */
int kv_split_kv(const char *token, char *key, size_t key_cap, char *val, size_t val_cap);

/* Parses a comma-separated list of up to max_count ints from val (e.g.
   "235,220,40" or "0,1,2,3") into out, returning how many were parsed. */
int kv_parse_int_list(const char *val, int *out, int max_count);

void kv_write_line(FILE *f, const char *line);

/* Encodes len bytes of data as 2*len uppercase hex chars into out (which must
   be at least 2*len+1 bytes, for the NUL) - one contiguous run, no
   separators, matching the single-line eeprom_data record. */
void kv_hex_encode(const unsigned char *data, size_t len, char *out);
/* Decodes exactly out_len*2 hex chars from hex into out. Returns 1 on
   success, 0 if hex is too short or contains a non-hex-digit character. */
int kv_hex_decode(const char *hex, unsigned char *out, size_t out_len);

#endif
