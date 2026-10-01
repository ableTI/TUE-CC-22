#include "fortune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static fortune_db_t g_global_db = {0};
static bool g_seeded = false;

static const char * const DEFAULT_SEARCH_PATHS[] = {
    "fortunes",
    "applications/fortune/fortunes",
    "../fortune/fortunes",
    "/home/student/libpynq-5EWC0-2023-v0.2.6/applications/fortune/fortunes",
    "/mnt/c/Users/abel_/CLionProjects/pynq-project/applications/fortune/fortunes",
    "C:\\Users\\abel_\\CLionProjects\\pynq-project\\applications\\fortune\\fortunes",
    NULL
};

static char* duplicate_string(const char *src)
{
    if (!src) return NULL;
    size_t len = strlen(src);
    char *dst = (char *)malloc(len + 1);
    if (dst) {
        memcpy(dst, src, len + 1);
    }
    return dst;
}

static bool is_separator_line(const char *line)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    return (*line == '%' && (*(line + 1) == '\n' || *(line + 1) == '\r' || *(line + 1) == '\0'));
}

static FILE* open_fortune_file(const char *filepath, char **resolved_path)
{
    if (filepath) {
        FILE *fp = fopen(filepath, "rb");
        if (fp) {
            if (resolved_path) *resolved_path = duplicate_string(filepath);
            return fp;
        }
        return NULL;
    }

    for (size_t i = 0; DEFAULT_SEARCH_PATHS[i] != NULL; ++i) {
        FILE *fp = fopen(DEFAULT_SEARCH_PATHS[i], "rb");
        if (fp) {
            if (resolved_path) *resolved_path = duplicate_string(DEFAULT_SEARCH_PATHS[i]);
            return fp;
        }
    }

    return NULL;
}

bool fortune_db_init(fortune_db_t *db, const char *filepath)
{
    if (!db) {
        return false;
    }

    // Free any existing allocation
    fortune_db_free(db);

    char *actual_path = NULL;
    FILE *fp = open_fortune_file(filepath, &actual_path);
    if (!fp) {
        return false;
    }

    size_t capacity = 512;
    db->offsets = (long *)malloc(capacity * sizeof(long));
    db->lengths = (size_t *)malloc(capacity * sizeof(size_t));
    db->count = 0;
    db->filepath = actual_path;

    if (!db->offsets || !db->lengths || !db->filepath) {
        fclose(fp);
        fortune_db_free(db);
        return false;
    }

    char line_buf[2048];
    long current_quote_start = 0;
    bool in_quote = false;

    while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
        if (is_separator_line(line_buf)) {
            if (in_quote) {
                long current_pos = ftell(fp);
                size_t quote_len = (size_t)(current_pos - current_quote_start - (long)strlen(line_buf));

                if (db->count >= capacity) {
                    capacity *= 2;
                    long *new_offsets = (long *)realloc(db->offsets, capacity * sizeof(long));
                    size_t *new_lengths = (size_t *)realloc(db->lengths, capacity * sizeof(size_t));
                    if (!new_offsets || !new_lengths) {
                        free(new_offsets ? new_offsets : db->offsets);
                        free(new_lengths ? new_lengths : db->lengths);
                        db->offsets = NULL;
                        db->lengths = NULL;
                        fclose(fp);
                        fortune_db_free(db);
                        return false;
                    }
                    db->offsets = new_offsets;
                    db->lengths = new_lengths;
                }

                db->offsets[db->count] = current_quote_start;
                db->lengths[db->count] = quote_len;
                db->count++;
                in_quote = false;
            }
        } else {
            if (!in_quote) {
                current_quote_start = ftell(fp) - (long)strlen(line_buf);
                in_quote = true;
            }
        }
    }

    // Trailing quote if file doesn't end with '%'
    if (in_quote) {
        long end_pos = ftell(fp);
        size_t quote_len = (size_t)(end_pos - current_quote_start);

        if (db->count >= capacity) {
            capacity += 1;
            long *new_offsets = (long *)realloc(db->offsets, capacity * sizeof(long));
            size_t *new_lengths = (size_t *)realloc(db->lengths, capacity * sizeof(size_t));
            if (new_offsets && new_lengths) {
                db->offsets = new_offsets;
                db->lengths = new_lengths;
                db->offsets[db->count] = current_quote_start;
                db->lengths[db->count] = quote_len;
                db->count++;
            }
        } else {
            db->offsets[db->count] = current_quote_start;
            db->lengths[db->count] = quote_len;
            db->count++;
        }
    }

    fclose(fp);

    if (!g_seeded) {
        srand((unsigned int)time(NULL));
        g_seeded = true;
    }

    db->is_initialized = (db->count > 0);
    return db->is_initialized;
}

const char* fortune_db_get_random(fortune_db_t *db, char *buffer, size_t max_len)
{
    if (!db || !db->is_initialized || db->count == 0 || !buffer || max_len == 0) {
        return NULL;
    }

    size_t chosen_index = (size_t)(rand() % db->count);
    long offset = db->offsets[chosen_index];
    size_t len = db->lengths[chosen_index];

    FILE *fp = fopen(db->filepath, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, offset, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_to_read = (len < max_len - 1) ? len : max_len - 1;
    size_t bytes_read = fread(buffer, sizeof(char), bytes_to_read, fp);
    buffer[bytes_read] = '\0';

    // Normalize and trim trailing newlines / carriage returns
    while (bytes_read > 0 && (buffer[bytes_read - 1] == '\n' || buffer[bytes_read - 1] == '\r')) {
        buffer[--bytes_read] = '\0';
    }

    fclose(fp);
    return buffer;
}

void fortune_db_free(fortune_db_t *db)
{
    if (!db) return;
    if (db->offsets) free(db->offsets);
    if (db->lengths) free(db->lengths);
    if (db->filepath) free(db->filepath);
    db->offsets = NULL;
    db->lengths = NULL;
    db->filepath = NULL;
    db->count = 0;
    db->is_initialized = false;
}

bool fortune_init(const char *filepath)
{
    return fortune_db_init(&g_global_db, filepath);
}

const char* fortune_get_random(char *buffer, size_t max_len)
{
    if (!g_global_db.is_initialized) {
        if (!fortune_init(NULL)) {
            return NULL;
        }
    }
    return fortune_db_get_random(&g_global_db, buffer, max_len);
}

void fortune_cleanup(void)
{
    fortune_db_free(&g_global_db);
}
