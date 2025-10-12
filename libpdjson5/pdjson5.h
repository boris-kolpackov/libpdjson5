#ifndef PDJSON5_H
#define PDJSON5_H

#ifndef PDJSON5_SYMEXPORT
#  define PDJSON5_SYMEXPORT
#endif

#ifdef __cplusplus
extern "C"
{
#else
#  if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#    include <stdbool.h>
#  else
#    ifndef bool
#      define bool int
#      define true 1
#      define false 0
#    endif /* bool */
#  endif /* __STDC_VERSION__ */
#endif /* __cplusplus */

#include <stdio.h>

enum json_type
{
  JSON_ERROR = 1,
  JSON_DONE,
  JSON_OBJECT,
  JSON_OBJECT_END,
  JSON_ARRAY,
  JSON_ARRAY_END,
  JSON_STRING,
  JSON_NUMBER,
  JSON_TRUE,
  JSON_FALSE,
  JSON_NULL
};

// Custom allocator and io functions may throw provided the parser was
// compiled with exceptions support (-fexceptions, etc). In this case, the
// only valid operation on json_stream is to close it with json_close().
//
struct json_allocator
{
  void *(*malloc) (size_t);
  void *(*realloc) (void *, size_t);
  void (*free) (void *);
};

typedef int (*json_user_io) (void *user);

typedef struct json_stream json_stream;
typedef struct json_allocator json_allocator;

PDJSON5_SYMEXPORT void json_open_buffer (json_stream *json, const void *buffer, size_t size);
PDJSON5_SYMEXPORT void json_open_string (json_stream *json, const char *string);
PDJSON5_SYMEXPORT void json_open_stream (json_stream *json, FILE *stream);
PDJSON5_SYMEXPORT void json_open_user (json_stream *json, json_user_io get, json_user_io peek, void *user);
PDJSON5_SYMEXPORT void json_close (json_stream *json);

PDJSON5_SYMEXPORT void json_set_allocator (json_stream *json, json_allocator *a);
PDJSON5_SYMEXPORT void json_set_streaming (json_stream *json, bool mode);

enum json_language
{
  json_language_json,   // Strict JSON.
  json_language_json5,  // Strict JSON5
  json_language_json5e, // Extended JSON5.
};
PDJSON5_SYMEXPORT void json_set_language (json_stream *json,
                                          enum json_language language);

PDJSON5_SYMEXPORT enum json_type json_next (json_stream *json);
PDJSON5_SYMEXPORT enum json_type json_peek (json_stream *json);
PDJSON5_SYMEXPORT void json_reset (json_stream *json);

// Note that the returned length includes trailing `\0`.
//
PDJSON5_SYMEXPORT const char *json_get_string (json_stream *json, size_t *length);
PDJSON5_SYMEXPORT double json_get_number (json_stream *json);

PDJSON5_SYMEXPORT enum json_type json_skip (json_stream *json);
PDJSON5_SYMEXPORT enum json_type json_skip_until (json_stream *json, enum json_type type);

PDJSON5_SYMEXPORT size_t json_get_lineno (json_stream *json);
PDJSON5_SYMEXPORT size_t json_get_position (json_stream *json);
PDJSON5_SYMEXPORT size_t json_get_column (json_stream *json);
PDJSON5_SYMEXPORT size_t json_get_depth (json_stream *json);
PDJSON5_SYMEXPORT enum json_type json_get_context (json_stream *json, size_t *count);

// Return error message if the previously peeked at or consumed even was
// JSON_ERROR and NULL otherwise. Note that the message is UTF-8 encoded.
//
PDJSON5_SYMEXPORT const char *json_get_error (json_stream *json);

PDJSON5_SYMEXPORT int json_source_get (json_stream *json);
PDJSON5_SYMEXPORT int json_source_peek (json_stream *json);

// Note that this function only examines the first byte of a potentially
// multi-byte UTF-8 sequence. As result, it only returns true for whitespaces
// encoded single bytes. Those are the only valid ones for JSON but not for
// JSON5. If you need to detect multi-byte whitespaces, then you will either
// need to do this yourself (and diagnose any non-whitespaces as appropriate)
// or use json_skip_if_space() below.
//
PDJSON5_SYMEXPORT bool json_is_space (json_stream *json, int byte);

// Given a peeked at byte, consume it and any following bytes that are part of
// the same multi-byte UTF-8 sequence if it is a whitespace and return 1. If
// it is a part of a multi-byte UTF-8 sequence but is not a whitespace,
// consume it, trigger an error, and return -1 (a codepoint that requires
// multiple bytes is only valid in JSON strings). Otherwise (single-byte
// non-whitespace), don't consume it and return 0.
//
// If the result is 1 and codepoint is not NULL, then set it to the decoded
// whitespace codepoint.
//
// Note that in the JSON5/JSON5E mode this function also skips comments,
// treating each as a single logical whitespace (but you can omit skipping
// comments by pre-checking the peeked byte for '/' and '#'). In this case,
// codepoint will contain the comment determinant character (`/`, `*`, `#`).
// Note that for the line comments (`//' and `#`), the newline is part of the
// comment.
//
// This function is primarily meant for custom handling of separators between
// values in the streaming mode. Its semantics is rather convoluted due to the
// above get/peek interface operating on bytes, not codepoints.
//
PDJSON5_SYMEXPORT int
json_skip_if_space (json_stream *json, int byte, unsigned long* codepoint);

// Implementation details.
//

struct json_source
{
  int (*get) (struct json_source *);
  int (*peek) (struct json_source *);
  size_t position;
  union
  {
    struct
    {
      FILE *stream;
    } stream;
    struct
    {
      const char *buffer;
      size_t length;
    } buffer;
    struct
    {
      void *ptr;
      json_user_io get;
      json_user_io peek;
    } user;
  } source;
};

struct json_stream
{
  size_t lineno; // @@ uint64_t (and the rest)

  /* While counting lines is straightforward, columns are tricky because we
   * have to count codepoints, not bytes. We could have peppered the code with
   * increments in all the relevant places but that seems inelegant. So
   * instead we calculate the column dynamically, based on the current
   * position.
   *
   * Specifically, we will remember the position at the beginning of each line
   * (linepos) and, assuming only the ASCII characters on the line, the column
   * will be the difference between the current position and linepos. Of
   * course there could also be multi-byte UTF-8 sequences which we will
   * handle by keeping an adjustment (lineadj) -- the number of continuation
   * bytes encountered on this line so far. Finally, for json_source_get() we
   * also have to keep the number of remaining continuation bytes in the
   * current multi-byte UTF-8 sequence (linecon).
   *
   * This is not the end of the story, however: with only the just described
   * approach we will always end up with the column of the latest character
   * read which is not what we want when returning potentially multi-
   * character value events (string, number, etc); in these cases we want to
   * return the column of the first character (note that if the value itself
   * is invalid and we are returning JSON_ERROR, we still want the current
   * column). So to handle this we will cache the start column (colno) for
   * such events.
   */
  size_t linepos; /* Position at the beginning of the current line. */
  size_t lineadj; /* Adjustment for multi-byte UTF-8 sequences. */
  size_t linecon; /* Number of remaining UTF-8 continuation bytes. */

  /* Start line/column for value events or 0. */
  size_t start_lineno;
  size_t start_colno;

  struct json_stack *stack;
  size_t stack_top;
  size_t stack_size;
  enum json_type peek;
  unsigned int flags;

  struct
  {
    enum json_type type;
    size_t lineno;
    size_t colno;
  } pending;

  struct
  {
    char *string;
    size_t string_fill;
    size_t string_size;
  } data;

  size_t ntokens; // Number of values/names read, recursively.

  struct json_source source;
  struct json_allocator alloc;

  char error_message[128];
  char utf8_char[6]; // Up to 4 for UTF-8, 2 for quotes, one for \0.
};

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* PDJSON5_H */
