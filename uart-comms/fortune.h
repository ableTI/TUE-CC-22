#ifndef FORTUNE_H
#define FORTUNE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Context structure for an indexed fortune database.
typedef struct {
    char *filepath;
    long *offsets;
    size_t *lengths;
    size_t count;
    bool is_initialized;
} fortune_db_t;

// Initializes a specific fortune database instance from a file path.
// db: Pointer to fortune_db_t structure.
// filepath: Path to the fortune cookie file (e.g., "fortunes"). If NULL, standard search paths are tried.
// Returns true on successful indexing of at least one quote, false otherwise.
bool fortune_db_init(fortune_db_t *db, const char *filepath);

// Fetches a random quote from the specified database instance into a buffer.
// db: Pointer to an initialized fortune_db_t.
// buffer: Output char buffer.
// max_len: Maximum length of buffer (including null terminator).
// Returns pointer to buffer on success, or NULL on failure.
const char* fortune_db_get_random(fortune_db_t *db, char *buffer, size_t max_len);

// Releases dynamic memory allocated for the fortune database instance.
// db: Pointer to fortune_db_t.
void fortune_db_free(fortune_db_t *db);

// Initializes the global/singleton fortune generator.
// Automatically searches current working directory, adjacent application folders,
// project root, and standard PYNQ installation directories if filepath is NULL.
// filepath: Explicit file path to fortunes file, or NULL for auto-detection.
// Returns true on success, false otherwise.
bool fortune_init(const char *filepath);

// Fetches a random fortune using the global fortune database.
// If fortune_init() was not yet called, automatically attempts default initialization.
// buffer: Destination character buffer.
// max_len: Maximum bytes to write into buffer.
// Returns pointer to buffer on success, or NULL if unavailable.
const char* fortune_get_random(char *buffer, size_t max_len);

// Cleans up memory used by the global fortune generator.
void fortune_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // FORTUNE_H
