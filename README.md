# Public Domain JSON/JSON5 Parser for C

A public domain JSON, [JSON5](https://json5.org/), and [JSON5E](JSON5E.md)
parser focused on correctness, ANSI C99 compliance, full Unicode (UTF-8)
support, minimal memory footprint, and a simple API. As a streaming API,
arbitrarily large JSON could be processed with a small amount of memory (the
size of the largest string in the JSON). It appears most C JSON libraries lack
in some significant way: broken string support (what if the string contains
`\u0000`?), broken/missing Unicode support, restrictive license, poorly
tested/fuzzed. This library intends to avoid these flaws. Note also that due
to the simple API constraint, it is not the fastest JSON parser our there.

This `libpdjson5` library is a fork of
[`pdjson`](https://github.com/skeeto/pdjson) with a number of changes and
improvements, including:

* JSON5 and JSON5E support.

* Support for reopening parser with new stream (but reusing allocated memory).

* Support for propagating IO errors.

* Distinct `JSON_NAME` event (instead of `JSON_STRING`) for object member names.

* Support for parsing of numbers has been removed and is left to the caller.

* Streaming, multi-document mode is made optional, not the default.

* Various API improvements (`const`, `bool`, `uint32/64_t`, `size_t`, etc).

* Performance improvements.

If you are looking for a C++ version of the same parsing approach, there is
[`libstud-json`](https://github.com/libstud/libstud-json), which is based on
this library for parsing and includes serialization support.

By default the parser expects standard JSON, no more, no less, so that even
slightly non-conforming JSON is rejected. The input is expected to be UTF-8,
and all strings returned by the library are UTF-8 with possible `\0`
characters in the middle, which is why the size output parameter is
important. Encoded characters (`\uxxxx`) are decoded and re-encoded into
UTF-8. UTF-16 surrogate pairs expressed as adjacent encoded characters are
supported.

The same applies to JSON5 and JSON5E parsing, if enabled, except that
currently only the common subset of the Unicode Zs category is recognized as
JSON5 whitespaces.

One exception to this rule is made to support a "streaming" mode. When a JSON
"stream" contains multiple JSON values (optionally separated by JSON
whitespace), if the streaming mode is enabled, the parser will allow the
stream to be "reset" and to continue parsing the subsequent values.

## Usage Overview

To start using `libpdjson5` in your `build2` project, add the following
`depends` value to your `manifest`, adjusting the version constraint as
appropriate:

```
depends: libpdjson5 ^1.0.0
```

Then import the library in your `buildfile`:

```
import libs = libpdjson5%lib{pdjson5}
```

The library provides two headers: `<libpdjson5/pdjson5.h>` which defines
the parser API and `<libpdjson5/version.h>` which provides detailed
library version information. Note that what follows is a high-level
overview and for the complete API details refer to `<libpdjson5/pdjson5.h>`.

All parser state is attached to the `pdjson_stream` structure. Its fields
should not be accessed directly. To initialize, it can be "opened" on
an input `FILE *` stream, memory buffer, C string, or custom IO callbacks.
It's disposed of by being "closed."

```c
struct pdjson_stream
{
 ...
};
typedef struct pdjson_stream pdjson_stream;

struct pdjson_user_io
{
  int (*peek) (void *user_data);
  int (*get) (void *user_data);
  bool (*error) (void *user_data);
};
typedef struct pdjson_user_io pdjson_user_io;

void pdjson_open_buffer (pdjson_stream *json, const void *buffer, size_t size);
void pdjson_open_string (pdjson_stream *json, const char *string);
void pdjson_open_stream (pdjson_stream *json, FILE *stream);
void pdjson_open_user (pdjson_stream *json,
                       const pdjson_user_io *user_io,
                       void *user_data);
void pdjson_close (pdjson_stream *json);
```

By default the parser only accept strict JSON. To also accept JSON5 or JSON5E,
use the `pdjson_set_language()` function to specify the desired language:

```c
enum pdjson_language
{
  PDJSON_LANGUAGE_JSON,   // Strict JSON.
  PDJSON_LANGUAGE_JSON5,  // Strict JSON5
  PDJSON_LANGUAGE_JSON5E, // Extended JSON5.
};
typedef struct pdjson_allocator pdjson_allocator;

void pdjson_set_language (pdjson_stream *json, enum pdjson_language language);
```

After opening a stream, custom allocator callbacks can be specified, in case
allocations should not come from a system-supplied `malloc()`.

```c
struct pdjson_allocator
{
  void *(*malloc) (size_t, void *user_data);
  void *(*realloc) (void *, size_t, void *user_data);
  void (*free) (void *, size_t, void *user_data);
};

void pdjson_set_allocator (pdjson_stream *json,
                           const pdjson_allocator *a,
                           void *user_data);
```

By default the parser operates in the strict conformance to the JSON standard
and any non-whitespace trailing data will trigger a parsing error. If desired,
the streaming mode can be enabled by calling `pdjson_set_streaming()`. This
will cause the non-whitespace trailing data to be parsed and reported as
additional JSON values.

```c
void pdjson_set_streaming (pdjson_stream *json, bool mode);
```

In the streaming mode only one JSON value is read from the stream at a
time. The parser can be reset to read more values. The overall line/column
numbers and position are preserved.

```c
void pdjson_reset (pdjson_stream *json);
```

The JSON is parsed as a stream of events (`enum pdjson_type`). The stream is
in the indicated state, during which relevant data can be queried and
retrieved.

```c
enum pdjson_type
{
  PDJSON_ERROR = 1,
  PDJSON_DONE,
  PDJSON_OBJECT,
  PDJSON_OBJECT_END,
  PDJSON_ARRAY,
  PDJSON_ARRAY_END,
  PDJSON_NAME,       // Object member name.
  PDJSON_STRING,
  PDJSON_NUMBER,
  PDJSON_TRUE,
  PDJSON_FALSE,
  PDJSON_NULL
};

enum pdjson_type pdjson_next (pdjson_stream *json);
enum pdjson_type pdjson_peek (pdjson_stream *json);

const char *pdjson_get_name (const pdjson_stream *json, size_t *size);
const char *pdjson_get_value (const pdjson_stream *json, size_t *size);
```

Both strings and numbers are retrieved with `pdjson_get_value()`. For numbers
it will return the raw text number as it appeared in the JSON input text. If
required, you will need to parse it into a suitable numeric type yourself.

In case of a parse error, the `PDJSON_ERROR` event it returned. The stream
cannot be used again until it is reset. In the event of an error, a
human-friendly, English error message is available, as well as the line/column
numbers and byte position. (The line/column numbers and byte position are
always available.)

```c
const char *pdjson_get_error (const pdjson_stream *json);
uint64_t pdjson_get_line(const pdjson_stream *json);
uint64_t pdjson_get_column (const pdjson_stream *json);
uint64_t pdjson_get_position (const pdjson_stream *json);
size_t pdjson_get_depth (const pdjson_stream *json);
```

Outside of errors, a `PDJSON_OBJECT` event will always be followed by zero or
more pairs of `PDJSON_NAME` (member name) events and their associated value
events. That is, the stream of events will always be logical and consistent.

In the streaming mode the end of the input is indicated by returning a second
`PDJSON_DONE` event. Note also that in this mode an input consisting of zero
JSON values is valid and is represented by a single, immediate `PDJSON_DONE`
event.

JSON values in the stream can be separated by zero or more JSON whitespaces.
Stricter or alternative separation can be implemented by reading and analyzing
characters between values using the following functions.

```c
int pdjson_source_get (pdjson_stream *json);
int pdjson_source_peek (pdjson_stream *json);
bool pdjson_source_error (pdjson_stream *json);
bool pdjson_is_space (const pdjson_stream *json, int byte);
```

As an example, the following code fragment makes sure values are separated by
at least one newline.

```c
enum pdjson_type e = pdjson_next (json);

if (e == PDJSON_DONE)
{
  int c = '\0';
  while (pdjson_is_space (json, c = pdjson_source_peek (json)))
  {
    pdjson_source_get (json);
    if (c == '\n')
      break;
  }

  if (c != '\n' && c != EOF)
  {
    // error
  }

  pdjson_reset (json);
}
```
