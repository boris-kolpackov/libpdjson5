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

All parser state is attached to a `json_stream` struct. Its fields
should not be accessed directly. To initialize, it can be "opened" on
an input `FILE *` stream or memory buffer. It's disposed of by being
"closed."

```c
void json_open_stream(json_stream *json, FILE * stream);
void json_open_string(json_stream *json, const char *string);
void json_open_buffer(json_stream *json, const void *buffer, size_t size);
void json_close(json_stream *json);
```

After opening a stream, custom allocator callbacks can be specified,
in case allocations should not come from a system-supplied malloc.
(When no custom allocator is specified, the system allocator is used.)

```c
struct json_allocator {
    void *(*malloc)(size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
};


void json_set_allocator(json_stream *json, json_allocator *a);
```

By default only one value is read from the stream. The parser can be
reset to read more objects. The overall line number and position are
preserved.

```c
void json_reset(json_stream *json);
```

By default the parser operates in the strict conformance to the JSON standard
and any non-whitespace trailing data will trigger a parsing error. If desired,
the streaming mode can be enabled by calling `json_set_streaming`. This will
cause the non-whitespace trailing data to be parsed and reported as additional
JSON values.

```c
void json_set_streaming(json_stream *json, bool mode);
```

The JSON is parsed as a stream of events (`enum json_type`). The
stream is in the indicated state, during which data can be queried and
retrieved.

```c
enum json_type json_next(json_stream *json);
enum json_type json_peek(json_stream *json);

const char *json_get_name(json_stream *json, size_t *size);
const char *json_get_value(json_stream *json, size_t *size);
```

Both strings and numbers are retrieved with `json_get_value()`. For numbers it
will return the raw text number as it appeared in the JSON input text. If
required, you will need to parse it into a suitable numeric type yourself.

In the case of a parse error, the event will be `JSON_ERROR`. The
stream cannot be used again until it is reset. In the event of an
error, a human-friendly, English error message is available, as well
as the line number and byte position. (The line number and byte
position are always available.)

```c
const char *json_get_error(json_stream *json);
size_t json_get_lineno(json_stream *json);
size_t json_get_position(json_stream *json);
```

Outside of errors, a `JSON_OBJECT` event will always be followed by
zero or more pairs of `JSON_NAME` (member name) events and their
associated value events. That is, the stream of events will always be
logical and consistent.

In the streaming mode the end of the input is indicated by returning a second
`JSON_DONE` event. Note also that in this mode an input consisting of zero
JSON values is valid and is represented by a single `JSON_DONE` event.

JSON values in the stream can be separated by zero or more JSON whitespaces.
Stricter or alternative separation can be implemented by reading and analyzing
characters between values using the following functions.

```c
int json_source_get (json_stream *json);
int json_source_peek (json_stream *json);
bool json_isspace(int c);
```

As an example, the following code fragment makes sure values are separated by
at least one newline.

```c
enum json_type e = json_next(json);

if (e == JSON_DONE) {
    int c = '\0';
    while (json_isspace(c = json_source_peek(json))) {
        json_source_get(json);
        if (c == '\n')
            break;
    }

    if (c != '\n' && c != EOF) {
        /* error */
    }

    json_reset(json);
}
```
