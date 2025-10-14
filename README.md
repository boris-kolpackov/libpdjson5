# Public Domain JSON Parser for C

A public domain JSON parser focused on correctness, ANSI C99
compliance, full Unicode (UTF-8) support, minimal memory footprint,
and a simple API. As a streaming API, arbitrary large JSON could be
processed with a small amount of memory (the size of the largest
string in the JSON). It seems most C JSON libraries suck in some
significant way: broken string support (what if the string contains
`\u0000`?), broken/missing Unicode support, or crappy software license
(GPL or "do no evil"). This library intends to avoid these flaws.

The parser is intended to support *exactly* the JSON standard, no more, no
less, so that even slightly non-conforming JSON is rejected. The input is
assumed to be UTF-8, and all strings returned by the library are UTF-8 with
possible `nul` characters in the middle, which is why the size output parameter
is important. Encoded characters (`\uxxxx`) are decoded and re-encoded into
UTF-8. UTF-16 surrogate pairs expressed as adjacent encoded characters are
supported.

One exception to this rule is made to support a "streaming" mode. When a JSON
"stream" contains multiple JSON values (optionally separated by JSON
whitespace), if the streaming mode is enabled, the parser will allow the
stream to be "reset" and to continue parsing the subsequent values.

The library is usable and nearly complete, but needs polish.

## API Overview

All parser state is attached to a `pdjson_stream` struct. Its fields
should not be accessed directly. To initialize, it can be "opened" on
an input `FILE *` stream or memory buffer. It's disposed of by being
"closed."

```c
void pdjson_open_stream(pdjson_stream *json, FILE * stream);
void pdjson_open_string(pdjson_stream *json, const char *string);
void pdjson_open_buffer(pdjson_stream *json, const void *buffer, size_t size);
void pdjson_close(pdjson_stream *json);
```

After opening a stream, custom allocator callbacks can be specified,
in case allocations should not come from a system-supplied malloc.
(When no custom allocator is specified, the system allocator is used.)

```c
struct pdjson_allocator {
    void *(*malloc)(size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
};


void pdjson_set_allocator(pdjson_stream *json, pdjson_allocator *a);
```

By default only one value is read from the stream. The parser can be
reset to read more objects. The overall line number and position are
preserved.

```c
void pdjson_reset(pdjson_stream *json);
```

By default the parser operates in the strict conformance to the JSON standard
and any non-whitespace trailing data will trigger a parsing error. If desired,
the streaming mode can be enabled by calling `pdjson_set_streaming`. This will
cause the non-whitespace trailing data to be parsed and reported as additional
JSON values.

```c
void pdjson_set_streaming(pdjson_stream *json, bool mode);
```

The JSON is parsed as a stream of events (`enum pdjson_type`). The
stream is in the indicated state, during which data can be queried and
retrieved.

```c
enum pdjson_type pdjson_next(pdjson_stream *json);
enum pdjson_type pdjson_peek(pdjson_stream *json);

const char *pdjson_get_name(pdjson_stream *json, size_t *size);
const char *pdjson_get_value(pdjson_stream *json, size_t *size);
```

Both strings and numbers are retrieved with `pdjson_get_value()`. For numbers
it will return the raw text number as it appeared in the JSON input text. If
required, you will need to parse it into a suitable numeric type yourself.

In the case of a parse error, the event will be `PDJSON_ERROR`. The
stream cannot be used again until it is reset. In the event of an
error, a human-friendly, English error message is available, as well
as the line number and byte position. (The line number and byte
position are always available.)

```c
const char *pdjson_get_error(pdjson_stream *json);
size_t pdjson_get_line(pdjson_stream *json);
size_t pdjson_get_column(pdjson_stream *json);
size_t pdjson_get_position(pdjson_stream *json);
```

Outside of errors, a `PDJSON_OBJECT` event will always be followed by
zero or more pairs of `PDJSON_NAME` (member name) events and their
associated value events. That is, the stream of events will always be
logical and consistent.

In the streaming mode the end of the input is indicated by returning a second
`PDJSON_DONE` event. Note also that in this mode an input consisting of zero
JSON values is valid and is represented by a single `PDJSON_DONE` event.

JSON values in the stream can be separated by zero or more JSON whitespaces.
Stricter or alternative separation can be implemented by reading and analyzing
characters between values using the following functions.

```c
int pdjson_source_get (pdjson_stream *json);
int pdjson_source_peek (pdjson_stream *json);
bool pdjson_isspace(int c);
```

As an example, the following code fragment makes sure values are separated by
at least one newline.

```c
enum pdjson_type e = pdjson_next(json);

if (e == PDJSON_DONE) {
    int c = '\0';
    while (pdjson_isspace(c = pdjson_source_peek(json))) {
        pdjson_source_get(json);
        if (c == '\n')
            break;
    }

    if (c != '\n' && c != EOF) {
        /* error */
    }

    pdjson_reset(json);
}
```
