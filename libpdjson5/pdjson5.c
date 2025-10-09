#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L
#elif _POSIX_C_SOURCE < 200112L
#  error incompatible _POSIX_C_SOURCE level
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef PDJSON5_H
#  include "pdjson5.h"
#endif

// Feature flags.
//
#define JSON_FLAG_STREAMING    0x01U
#define JSON_FLAG_JSON5        0x02U
#define JSON_FLAG_JSON5E       0x04U

// Runtime state flags.
//
#define JSON_FLAG_ERROR        0x08U
#define JSON_FLAG_IMPLIED_END  0x10U

#if defined(_MSC_VER) && (_MSC_VER < 1900)

#define json_error(json, format, ...)                             \
  if (!(json->flags & JSON_FLAG_ERROR))                           \
  {                                                               \
    json->flags |= JSON_FLAG_ERROR;                               \
    _snprintf_s (json->errmsg, sizeof (json->errmsg),             \
                _TRUNCATE,                                        \
                format,                                           \
                __VA_ARGS__);                                     \
  }

#else

#define json_error(json, format, ...)                             \
  if (!(json->flags & JSON_FLAG_ERROR))                           \
  {                                                               \
    json->flags |= JSON_FLAG_ERROR;                               \
    snprintf (json->errmsg, sizeof (json->errmsg),                \
             format,                                              \
             __VA_ARGS__);                                        \
  }

#endif /* _MSC_VER */

/* See also PDJSON5_STACK_MAX below. */
#ifndef PDJSON5_STACK_INC
#  define PDJSON5_STACK_INC 4
#endif

struct json_stack
{
  enum json_type type;
  long count;
};

static enum json_type
push (json_stream *json, enum json_type type)
{
  size_t new_stack_top = json->stack_top + 1;

#ifdef PDJSON5_STACK_MAX
  if (new_stack_top > PDJSON5_STACK_MAX)
  {
    json_error (json, "%s", "maximum depth of nesting reached");
    return JSON_ERROR;
  }
#endif

  if (new_stack_top >= json->stack_size)
  {
    struct json_stack *stack;
    size_t size = (json->stack_size + PDJSON5_STACK_INC) * sizeof (*json->stack);
    stack = (struct json_stack *)json->alloc.realloc (json->stack, size);
    if (stack == NULL)
    {
      json_error (json, "%s", "out of memory");
      return JSON_ERROR;
    }

    json->stack_size += PDJSON5_STACK_INC;
    json->stack = stack;
  }

  json->stack[new_stack_top].type = type;
  json->stack[new_stack_top].count = 0;

  json->stack_top = new_stack_top;

  return type;
}

static enum json_type
pop (json_stream *json, enum json_type type)
{
#if 0
  assert (json->stack != NULL &&
          json->stack[json->stack_top].type == (type == JSON_OBJECT_END
                                                ? JSON_OBJECT
                                                : JSON_ARRAY));
#endif

  json->stack_top--;
  return type;
}

static int
buffer_peek (struct json_source *source)
{
  if (source->position < source->source.buffer.length)
    return source->source.buffer.buffer[source->position];
  else
    return EOF;
}

static int
buffer_get (struct json_source *source)
{
  int c = source->peek (source);
  if (c != EOF)
    source->position++;
  return c;
}

static int
stream_get (struct json_source *source)
{
  int c = fgetc (source->source.stream.stream);
  if (c != EOF)
    source->position++;
  return c;
}

static int
stream_peek (struct json_source *source)
{
  int c = fgetc (source->source.stream.stream);
  ungetc (c, source->source.stream.stream);
  return c;
}

static void
init (json_stream *json)
{
  json->lineno = 1;
  json->linepos = 0;
  json->lineadj = 0;
  json->linecon = 0;
  json->start_lineno = 0;
  json->start_colno = 0;
  json->flags = JSON_FLAG_STREAMING;
  json->errmsg[0] = '\0';
  json->ntokens = 0;
  json->peek = (enum json_type)0;

  json->pending.type = (enum json_type)0;

  json->stack = NULL;
  json->stack_top = (size_t)-1;
  json->stack_size = 0;

  json->data.string = NULL;
  json->data.string_size = 0;
  json->data.string_fill = 0;
  json->source.position = 0;

  json->alloc.malloc = malloc;
  json->alloc.realloc = realloc;
  json->alloc.free = free;
}

static int
pushchar (json_stream *json, int c)
{
  if (json->data.string_fill == json->data.string_size)
  {
    size_t size = json->data.string_size * 2;
    char *buffer = (char *)json->alloc.realloc (json->data.string, size);
    if (buffer == NULL)
    {
      json_error (json, "%s", "out of memory");
      return -1;
    }
    else
    {
      json->data.string_size = size;
      json->data.string = buffer;
    }
  }
  json->data.string[json->data.string_fill++] = c;
  return 0;
}

// Match the remainder of input assuming the first character in pattern
// matched. If copy is true, also copy the remainder to the string buffer.
//
static enum json_type
is_match (json_stream *json,
          const char *pattern,
          bool copy,
          enum json_type type)
{
  int c;
  for (const char *p = pattern + 1; *p; p++)
  {
    if (*p != (c = json->source.get (&json->source)))
    {
      if (c != EOF)
      {
        json_error (json,
                    "expected '%c' instead of byte '%c' in '%s'",
                    *p, c, pattern);
      }
      else
      {
        json_error (json,
                    "expected '%c' instead of end of text in '%s'",
                    *p, pattern);
      }

      return JSON_ERROR;
    }

    if (copy)
    {
      if (pushchar (json, c) != 0)
        return JSON_ERROR;
    }
  }

  if (copy)
  {
    if (pushchar (json, '\0') != 0)
      return JSON_ERROR;
  }

  return type;
}

// Match the remainder of the string buffer assuming the first character in
// pattern matched.
//
static enum json_type
is_match_string (json_stream *json,
                 const char *pattern,
                 int nextc,           // First character after the string.
                 size_t* colno,       // Adjusted in case of an error.
                 enum json_type type)
{
  const char *string = json->data.string + 1;

  int c;
  size_t i = 0;
  for (int p; (p = pattern[i + 1]); i++)
  {
    if (p != (c = string[i]))
    {
      if (c != '\0' || nextc != EOF)
      {
        json_error (json,
                    "expected '%c' instead of byte '%c' in '%s'",
                    p, (c != '\0' ? c : nextc), pattern);

        *colno += i + 1; // Extra for the first character.
      }
      else
      {
        json_error (json,
                    "expected '%c' instead of end of text in '%s'",
                    p, pattern);

        *colno += i; // Don't count EOF.
      }

      return JSON_ERROR;
    }
  }

  if ((c = string[i]) != '\0')
  {
    json_error (json, "expected end of text instead of byte '%c'", c)
    *colno += i + 1;
    return JSON_ERROR;
  }

  return type;
}

static int
init_string (json_stream *json)
{
  json->data.string_size = 1024; // @@ Too large, make configurable?
  json->data.string = (char *)json->alloc.malloc (json->data.string_size);
  if (json->data.string == NULL)
  {
    json_error (json, "%s", "out of memory");
    return -1;
  }
  return 0;
}

static int
encode_utf8 (json_stream *json, unsigned long c)
{
  if (c < 0x80UL)
  {
    return pushchar (json, c);
  }
  else if (c < 0x0800UL)
  {
    return !((pushchar (json, (c >> 6 & 0x1F) | 0xC0) == 0) &&
             (pushchar (json, (c >> 0 & 0x3F) | 0x80) == 0));
  }
  else if (c < 0x010000UL)
  {
    if (c >= 0xd800 && c <= 0xdfff)
    {
      json_error (json, "invalid codepoint %06lx", c);
      return -1;
    }

    return !((pushchar (json, (c >> 12 & 0x0F) | 0xE0) == 0) &&
             (pushchar (json, (c >>  6 & 0x3F) | 0x80) == 0) &&
             (pushchar (json, (c >>  0 & 0x3F) | 0x80) == 0));
  }
  else if (c < 0x110000UL)
  {
    return !((pushchar (json, (c >> 18 & 0x07) | 0xF0) == 0) &&
             (pushchar (json, (c >> 12 & 0x3F) | 0x80) == 0) &&
             (pushchar (json, (c >> 6  & 0x3F) | 0x80) == 0) &&
             (pushchar (json, (c >> 0  & 0x3F) | 0x80) == 0));
  }
  else
  {
    json_error (json, "unable to encode %06lx as UTF-8", c);
    return -1;
  }
}

static int
hexchar (int c)
{
  switch (c)
  {
  case '0': return 0;
  case '1': return 1;
  case '2': return 2;
  case '3': return 3;
  case '4': return 4;
  case '5': return 5;
  case '6': return 6;
  case '7': return 7;
  case '8': return 8;
  case '9': return 9;
  case 'a':
  case 'A': return 10;
  case 'b':
  case 'B': return 11;
  case 'c':
  case 'C': return 12;
  case 'd':
  case 'D': return 13;
  case 'e':
  case 'E': return 14;
  case 'f':
  case 'F': return 15;
  default:
    return -1;
  }
}

// Read 4-digit hex number in \uHHHH.
//
static long
read_unicode_cp (json_stream *json)
{
  long cp = 0;
  int shift = 12;

  for (size_t i = 0; i < 4; i++)
  {
    int c = json->source.get (&json->source);
    int hc;

    if (c == EOF)
    {
      json_error (json, "%s", "unterminated string literal in Unicode escape");
      return -1;
    }
    else if ((hc = hexchar (c)) == -1)
    {
      json_error (json, "invalid escape Unicode escape byte '%c'", c);
      return -1;
    }

    cp += hc * (1 << shift);
    shift -= 4;
  }

  return cp;
}

static int
read_unicode (json_stream *json)
{
  long cp, h, l;

  if ((cp = read_unicode_cp (json)) == -1)
    return -1;

  if (cp >= 0xd800 && cp <= 0xdbff)
  {
    /* This is the high portion of a surrogate pair; we need to read the
     * lower portion to get the codepoint
     */
    h = cp;

    int c = json->source.get (&json->source);
    if (c == EOF)
    {
      json_error (json, "%s", "unterminated string literal in Unicode");
      return -1;
    }
    else if (c != '\\')
    {
      json_error (json, "invalid continuation for surrogate pair '%c', "
                 "expected '\\'", c);
      return -1;
    }

    c = json->source.get (&json->source);
    if (c == EOF)
    {
      json_error (json, "%s", "unterminated string literal in Unicode");
      return -1;
    }
    else if (c != 'u')
    {
      json_error (json, "invalid continuation for surrogate pair '%c', "
                 "expected 'u'", c);
      return -1;
    }

    if ((l = read_unicode_cp (json)) == -1)
      return -1;

    if (l < 0xdc00 || l > 0xdfff)
    {
      json_error (json, "surrogate pair continuation \\u%04lx out "
                 "of range (dc00-dfff)", l);
      return -1;
    }

    cp = ((h - 0xd800) * 0x400) + ((l - 0xdc00) + 0x10000);
  }
  else if (cp >= 0xdc00 && cp <= 0xdfff)
  {
    json_error (json, "dangling surrogate \\u%04lx", cp);
    return -1;
  }

  return encode_utf8 (json, cp);
}

// Read 4-digit hex number in \xHH.
//
static long
read_latin_cp (json_stream *json)
{
  long cp = 0;
  int shift = 4;

  for (size_t i = 0; i < 2; i++)
  {
    int c = json->source.get (&json->source);
    int hc;

    if (c == EOF)
    {
      json_error (json, "%s", "unterminated string literal in Latin escape");
      return -1;
    }
    else if ((hc = hexchar (c)) == -1)
    {
      json_error (json, "invalid escape Latin escape byte '%c'", c);
      return -1;
    }

    cp += hc * (1 << shift);
    shift -= 4;
  }

  return cp;
}

static int
read_latin (json_stream *json)
{
  long cp;

  if ((cp = read_latin_cp (json)) == -1)
    return -1;

  return encode_utf8 (json, cp);
}

static int
read_escaped (json_stream *json)
{
  int c = json->source.get (&json->source);
  if (c == EOF)
  {
    json_error (json, "%s", "unterminated string literal in escape");
    return -1;
  }

  // JSON escapes.
  //

  if (c == 'u') // \xHHHH
    return read_unicode (json);

  int u = -1;
  switch (c)
  {
  case '\\': u = c;    break;
  case 'b':  u = '\b'; break;
  case 'f':  u = '\f'; break;
  case 'n':  u = '\n'; break;
  case 'r':  u = '\r'; break;
  case 't':  u = '\t'; break;
  case '/':  u = c;    break;
  case '"':  u = c;    break;
  }

  // Additional JSON5 escapes.
  //
  if (u == -1 && (json->flags & JSON_FLAG_JSON5))
  {
    if (c == 'x') // \xHH
      return read_latin (json);

    // According to the JSON5 spec (Section 5.1):
    //
    // "A decimal digit must not follow a reverse solidus followed by a
    // zero. [...] If any other character follows a reverse solidus, except
    // for the decimal digits 1 through 9, that character will be included in
    // the string, but the reverse solidus will not."
    //
    // So it appears:
    //
    // 1. \0N is not allowed.
    // 2. \N is not allowed either.
    // 3. Raw control characters can appear after `\`.
    //
    // The reference implementation appears to match this understanding.
    //
    switch (c)
    {
    case '\'': u = c;    break;
    case 'v':  u = '\v'; break;

    case '0':
      // Check that it's not followed by a digit (see above).
      //
      c = json->source.peek (&json->source);
      if (c >= '0' && c <= '9')
      {
        json->source.get (&json->source);
        u = -1;
      }
      else
        u = '\0';
      break;

      // Decimal digits (other that 0) are illegal (see above).
      //
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':  u = -1; break;

      // Line continuations.
      //
    case '\r':
      // Check if it's followed by \n (CRLF).
      //
      if (json->source.peek (&json->source) == '\n')
        json->source.get (&json->source);

      // Fall through.
    case '\n':
    case 0x2028:
    case 0x2029:
      return 0; // No pushchar().

    default:
      // Pass as-is, including the control characters (see above).
      //
      u = c;
      break;
    }
  }

  if (u != -1)
    return pushchar (json, u);

  json_error (json, "invalid escaped byte '%c'", c);
  return -1;
}

static int
utf8_seq_length (char byte)
{
  unsigned char u = (unsigned char) byte;
  if (u < 0x80)
    return 1;

  if (0x80 <= u && u <= 0xBF)
  {
    // second, third or fourth byte of a multi-byte
    // sequence, i.e. a "continuation byte"
    return 0;
  }
  else if (u == 0xC0 || u == 0xC1)
  {
    // overlong encoding of an ASCII byte
    return 0;
  }
  else if (0xC2 <= u && u <= 0xDF)
  {
    // 2-byte sequence
    return 2;
  }
  else if (0xE0 <= u && u <= 0xEF)
  {
    // 3-byte sequence
    return 3;
  }
  else if (0xF0 <= u && u <= 0xF4)
  {
    // 4-byte sequence
    return 4;
  }
  else
  {
    // u >= 0xF5
    // Restricted (start of 4-, 5- or 6-byte sequence) or invalid UTF-8
    return 0;
  }
}

static int
is_legal_utf8 (const unsigned char *bytes, int length)
{
  if (0 == bytes || 0 == length)
    return 0;

  unsigned char a;
  const unsigned char* srcptr = bytes + length;
  switch (length)
  {
  default:
    return 0;
    /* Everything else falls through when true. */
  case 4:
    if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
    /* FALLTHRU */
  case 3:
    if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
    /* FALLTHRU */
  case 2:
    a = (*--srcptr);
    switch (*bytes)
    {
    case 0xE0:
      if (a < 0xA0 || a > 0xBF) return 0;
      break;
    case 0xED:
      if (a < 0x80 || a > 0x9F) return 0;
      break;
    case 0xF0:
      if (a < 0x90 || a > 0xBF) return 0;
      break;
    case 0xF4:
      if (a < 0x80 || a > 0x8F) return 0;
      break;
    default:
      if (a < 0x80 || a > 0xBF) return 0;
      break;
    }
    /* FALLTHRU */
  case 1:
    if (*bytes >= 0x80 && *bytes < 0xC2)
      return 0;
  }
  return *bytes <= 0xF4;
}

static int
read_utf8 (json_stream* json, int next_char)
{
  int count = utf8_seq_length (next_char);
  if (!count)
  {
    json_error (json, "%s", "invalid UTF-8 character");
    return -1;
  }

  char buffer[4];
  buffer[0] = next_char;
  int i;
  for (i = 1; i < count; ++i)
  {
    if ((next_char = json->source.get (&json->source)) == EOF)
      break;

    buffer[i] = next_char;
    json->lineadj++;
  }

  if (i != count || !is_legal_utf8 ((unsigned char*) buffer, count))
  {
    json_error (json, "%s", "invalid UTF-8 text");
    return -1;
  }

  for (i = 0; i < count; ++i)
  {
    if (pushchar (json, buffer[i]) != 0)
      return -1;
  }

  return 0;
}

static enum json_type
read_string (json_stream *json, int quote)
{
  if (json->data.string == NULL && init_string (json) != 0)
    return JSON_ERROR;

  json->data.string_fill = 0;

  while (true)
  {
    int c = json->source.get (&json->source);
    if (c == EOF)
    {
      json_error (json, "%s", "unterminated string literal");
      return JSON_ERROR;
    }
    else if (c == quote)
    {
      return pushchar (json, '\0') == 0 ? JSON_STRING : JSON_ERROR;
    }
    else if (c == '\\')
    {
      if (read_escaped (json) != 0)
        return JSON_ERROR;
    }
    else if ((unsigned int) c >= 0x80)
    {
      if (read_utf8 (json, c) != 0)
        return JSON_ERROR;
    }
    else
    {
      // According to the JSON5 spec (Chapter 5):
      //
      // "All Unicode characters may be placed within the quotation marks,
      // except for the characters that must be escaped: the quotation mark
      // used to begin and end the string, reverse solidus, and line
      // terminators."
      //
      // So it appears this includes the raw control characters (except
      // newlines). The reference implementation appears to match this
      // understanding.
      //
      // Note: quote and backslash are handled above.
      //
      if ((json->flags & JSON_FLAG_JSON5)
          ? (c == '\n' || c == '\r')
          : (c >= 0 && c < 0x20))
      {
        json_error (json, "%s", "unescaped control character in string");
        return JSON_ERROR;
      }

      if (pushchar (json, c) != 0)
        return JSON_ERROR;
    }
  }

  return JSON_ERROR;
}

static bool
is_dec_digit (int c)
{
  return c >= '0' && c <= '9';
}

static int
read_dec_digits (json_stream *json)
{
  int c;
  size_t nread = 0;
  while (is_dec_digit (c = json->source.peek (&json->source)))
  {
    json->source.get (&json->source);
    if (pushchar (json, c) != 0)
      return -1;

    nread++;
  }

  if (nread == 0)
  {
    json->source.get (&json->source); // For column.

    if (c != EOF)
    {
      json_error (json, "expected digit instead of byte '%c'", c);
    }
    else
    {
      json_error (json, "%s", "expected digit instead of end of text");
    }

    return -1;
  }

  return 0;
}

static bool
is_hex_digit (int c)
{
  return ((c >= '0' && c <= '9') ||
          (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'));
}

static int
read_hex_digits (json_stream *json)
{
  int c;
  size_t nread = 0;
  while (is_hex_digit (c = json->source.peek (&json->source)))
  {
    json->source.get (&json->source);
    if (pushchar (json, c) != 0)
      return -1;

    nread++;
  }

  if (nread == 0)
  {
    json->source.get (&json->source); // For column.

    if (c != EOF)
    {
      json_error (json, "expected hex digit instead of byte '%c'", c);
    }
    else
    {
      json_error (json, "%s", "expected hex digit instead of end of text");
    }

    return -1;
  }

  return 0;
}

static enum json_type
read_number (json_stream *json, int c)
{
  if (json->data.string == NULL && init_string (json) != 0)
    return JSON_ERROR;

  json->data.string_fill = 0;

  if (pushchar (json, c) != 0)
    return JSON_ERROR;

  // Note: we can only have '+' here if we are in the JSON5 mode.
  //
  if (c == '-' || c == '+')
  {
    c = json->source.get (&json->source);
    if (is_dec_digit (c) ||
        ((json->flags & JSON_FLAG_JSON5) && (c == 'I' || c == 'N' || c == '.')))
    {
      if (pushchar (json, c) != 0)
        return JSON_ERROR;

      // Fall through.
    }
    else
    {
      if (c != EOF)
      {
        json_error (json, "unexpected byte '%c' in number", c);
      }
      else
      {
        json_error (json, "%s", "unexpected end of text in number");
      }

      return JSON_ERROR;
    }
  }

  if (c >= '1' && c <= '9')
  {
    c = json->source.peek (&json->source);
    if (is_dec_digit (c))
    {
      if (read_dec_digits (json) != 0)
        return JSON_ERROR;
    }
  }
  else if (c == '0')
  {
    // Note that while the JSON5 spec doesn't say whether leading 0 is
    // illegal, the reference implementation appears to reject it. So we
    // assume it is (issue #58 in json5-spec).
    //
    c = json->source.peek (&json->source);

    if (c == '.' ||  c == 'e' || c == 'E')
      ;
    else if ((json->flags & JSON_FLAG_JSON5) && (c == 'x' || c == 'X'))
    {
      json->source.get (&json->source); // Consume `x`/`X'.

      return (pushchar (json, c) == 0     &&
              read_hex_digits (json) == 0 &&
              pushchar (json, '\0') == 0) ? JSON_NUMBER : JSON_ERROR;
    }
    else if (is_dec_digit (c))
    {
      json_error (json, "%s", "leading '0' in number");
      return JSON_ERROR;
    }
  }
  // Note that we can only get `I`, `N`, and `.` here if we are in the JSON5
  // mode.
  //
  else if (c == 'I')
    return is_match (json, "Infinity", true /* copy */, JSON_NUMBER);
  else if (c == 'N')
    return is_match (json, "NaN", true /* copy */, JSON_NUMBER);
  else if (c == '.')
  {
    // It is more straightforward to handle leading dot as a special case. It
    // also takes care of the invalid sole dot case.
    //
    if (read_dec_digits (json) != 0)
      return JSON_ERROR;

    c = json->source.peek (&json->source);
    if (c != 'e' && c != 'E')
      return pushchar (json, '\0') == 0 ? JSON_NUMBER : JSON_ERROR;
  }

  // Up to decimal or exponent has been read.
  //
  c = json->source.peek (&json->source);
  if (c != '.' && c != 'e' && c != 'E')
  {
    return pushchar (json, '\0') == 0 ? JSON_NUMBER : JSON_ERROR;
  }

  if (c == '.')
  {
    json->source.get (&json->source); // Consume `.`.

    if (pushchar (json, c) != 0)
      return JSON_ERROR;

    if ((json->flags & JSON_FLAG_JSON5) &&
        !is_dec_digit (json->source.peek (&json->source)))
      ; // Trailing dot.
    else if (read_dec_digits (json) != 0)
      return JSON_ERROR;
  }

  /* Check for exponent. */
  c = json->source.peek (&json->source);
  if (c == 'e' || c == 'E')
  {
    json->source.get (&json->source); // Consume `e`/`E`.
    if (pushchar (json, c) != 0)
      return JSON_ERROR;

    c = json->source.peek (&json->source);
    if (c == '+' || c == '-')
    {
      json->source.get (&json->source); // Consume `+`/`-`.

      if (pushchar (json, c) != 0)
        return JSON_ERROR;

      if (read_dec_digits (json) != 0)
        return JSON_ERROR;
    }
    else if (is_dec_digit (c))
    {
      if (read_dec_digits (json) != 0)
        return JSON_ERROR;
    }
    else
    {
      json->source.get (&json->source); // Consume (for column).
      if (c != EOF)
      {
        json_error (json, "unexpected byte '%c' in number", c);
      }
      else
      {
        json_error (json, "%s", "unexpected end of text in number");
      }

      return JSON_ERROR;
    }
  }

  return pushchar (json, '\0') == 0 ? JSON_NUMBER : JSON_ERROR;
}

bool
json_isspace (json_stream *json, int c)
{
  switch (c)
  {
  case ' ':
  case '\n':
  case '\t':
  case '\r':
    return true;

    // See Chapter 8, "White Space" in the JSON5 spec.
    //
    // @@ TODO: handle Unicode Zs category.
    //
  case '\f':
  case '\v':
  case 0xa0:
  case 0x2028:
  case 0x2029:
  case 0xFEFF:
    return json->flags & JSON_FLAG_JSON5;

  default:
    return false;
  }
}

static void
newline (json_stream *json)
{
  json->lineno++;
  json->linepos = json->source.position;
  json->lineadj = 0;
  json->linecon = 0;
}

// Given the comment determinant character (`/`, `*`, `#`), skip everything
// until the end of the comment (newline or `*/`) and return the last
// character read (newline, '/', or EOF). This function can fail by returning
// EOF and setting the error flag.
//
static int
skip_comment (json_stream *json, int c)
{
  switch (c)
  {
  case '/':
  case '#':
    {
      // Skip everything until the next newline or EOF.
      //
      while ((c = json->source.get (&json->source)) != EOF)
      {
        if (c == '\n')
        {
          newline (json);
          break;
        }

        if (c == '\r')
          break;
      }

      break;
    }
  case '*':
    {
      // Skip everything until closing `*/` or EOF.
      //
      while ((c = json->source.get (&json->source)) != EOF)
      {
        if (c == '*')
        {
          if (json->source.peek (&json->source) == '/')
          {
            c = json->source.get (&json->source); // Consume closing `/`.
            break;
          }
        }
        else if (c == '\n')
          newline (json);
      }

      if (c == EOF)
        json_error (json, "%s", "unexpected end of text before '*/'");

      break;
    }
  }

  return c;
}

// Returns the next non-whitespace (and non-comment, for JSON5) character in
// the stream. This function can fail by returning EOF and setting the error
// flag.
//
// Note that this and the peek() below are the only functions (besides the
// user-facing json_source_get()) that need to worry about newline
// housekeeping.
//
// Note also that we currently don't count sole \r as a newline. Doing that
// would require counting the \r\n sequence as a single newline. So we keep
// it simple for now.
//
static int
next (json_stream *json)
{
  int c;
  while (true)
  {
    c = json->source.get (&json->source);

    if (json_isspace (json, c))
    {
      if (c == '\n')
        newline (json);

      continue;
    }

    if (c == '/' && (json->flags & JSON_FLAG_JSON5))
    {
      int p = json->source.peek (&json->source);
      if (p == '/' || p == '*')
      {
        json->source.get (&json->source);
        if ((c = skip_comment (json, p)) != EOF)
          continue;
      }
    }
    else if (c == '#' && (json->flags & JSON_FLAG_JSON5E))
    {
      if ((c = skip_comment (json, c)) != EOF)
        continue;
    }

    break;
  }
  return c;
}

static enum json_type
read_value (json_stream *json, int c)
{
  size_t colno = json_get_column (json);

  json->ntokens++;

  enum json_type type = (enum json_type)0;
  switch (c)
  {
  case EOF:
    json_error (json, "%s", "unexpected end of text");
    type = JSON_ERROR;
    break;
  case '{':
    type = push (json, JSON_OBJECT);
    break;
  case '[':
    type = push (json, JSON_ARRAY);
    break;
  case '\'':
    if (!(json->flags & JSON_FLAG_JSON5))
      break;
    // Fall through.
  case '"':
    type = read_string (json, c);
    break;
  case 'n':
    type = is_match (json, "null", false /* copy */, JSON_NULL);
    break;
  case 'f':
    type = is_match (json, "false", false /* copy */, JSON_FALSE);
    break;
  case 't':
    type = is_match (json, "true", false /* copy */, JSON_TRUE);
    break;
  case '+':
  case '.': // Leading dot
  case 'I': // Infinity
  case 'N': // NaN
    if (!(json->flags & JSON_FLAG_JSON5))
      break;
    // Fall through.
  case '-':
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    type = read_number (json, c);
    break;
  default:
    break;
  }

  if (type == 0)
  {
    json_error (json, "unexpected byte '%c' in value", c);
    type = JSON_ERROR;
  }

  if (type != JSON_ERROR)
    json->start_colno = colno;

  return type;
}

// While the JSON5 spec says an identifier can be anything that matches the
// ECMAScript's IdentifierName production, this brings all kinds of Unicode
// complications (and allows `$` anywhere in the identifier). So for now we
// restrict it to the C identifier in the ASCII alphabet plus allow `$` (helps
// to pass reference implementation tests).
//
// For JSON5E we allow `-` and `.` but not as a first character. Both of these
// are valid beginnings of a JSON/JSON5 value (-1, .1) so strictly speaking,
// there is an ambiguity: is true-1 an identifier or two values (true and -1)?
// However, in our context (object member name), two values would be illegal.
// And so we resolve this ambiguity in favor of an identifier. One special
// case is the implied top-level object. But since implied objects are
// incompatible with the streaming mode, two top-level values would still be
// illegal (and, yes, true-1 is a valid two-value input in the streaming
// mode).
//
static bool
is_first_id_char (int c)
{
  return (c == '_'               ||
          (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          c == '$');
}

static bool
is_subseq_id_char (int c, bool extended)
{
  return (c == '_'                             ||
          (c >= 'a' && c <= 'z')               ||
          (c >= 'A' && c <= 'Z')               ||
          (c >= '0' && c <= '9')               ||
          (extended && (c == '-' || c == '.')) ||
          c == '$');
}

// Read the remainder of an identifier given its first character.
//
static enum json_type
read_identifier (json_stream *json, int c)
{
  if (json->data.string == NULL && init_string (json) != 0)
    return JSON_ERROR;

  json->data.string_fill = 0;

  for (bool extended = (json->flags & JSON_FLAG_JSON5E);;)
  {
    if (pushchar (json, c) != 0)
      return JSON_ERROR;

    c = json->source.peek (&json->source);

    if (!is_subseq_id_char (c, extended))
      break;

    json->source.get (&json->source);
  }

  if (pushchar (json, '\0') != 0)
    return JSON_ERROR;

  return JSON_STRING;
}

static enum json_type
read_name (json_stream *json, int c)
{
  size_t colno = json_get_column (json);

  json->ntokens++;

  if (c == '"' || ((json->flags & JSON_FLAG_JSON5) &&
                   c == '\''))
  {
    if (read_string (json, c) == JSON_ERROR)
      return JSON_ERROR;
  }
  // See if this is an unquoted member name.
  //
  else if ((json->flags & JSON_FLAG_JSON5) && is_first_id_char (c))
  {
    if (read_identifier (json, c) == JSON_ERROR)
      return JSON_ERROR;
  }
  else
  {
    json_error (json, "%s", "expected member name");
    return JSON_ERROR;
  }

  json->start_colno = colno;

  return JSON_STRING;
}

enum json_type
json_peek (json_stream *json)
{
  enum json_type peek;
  if (json->peek)
    peek = json->peek;
  else
    peek = json->peek = json_next (json);
  return peek;
}

enum json_type
json_next (json_stream *json)
{
  if (json->flags & JSON_FLAG_ERROR)
    return JSON_ERROR;

  if (json->peek != 0)
  {
    enum json_type next = json->peek;
    json->peek = (enum json_type)0;
    return next;
  }

  if (json->pending.type != 0)
  {
    enum json_type next = json->pending.type;
    json->pending.type = (enum json_type)0;
    json->start_lineno = json->pending.lineno;
    json->start_colno = json->pending.colno;

    if (next == JSON_OBJECT_END || next == JSON_ARRAY_END)
      next = pop (json, next);

    return next;
  }

  json->start_lineno = 0;
  json->start_colno = 0;

  if (json->ntokens > 0 && json->stack_top == (size_t)-1)
  {
    /* In the streaming mode leave any trailing whitespaces in the stream.
     * This allows the user to validate any desired separation between
     * values (such as newlines) using json_source_get/peek() with any
     * remaining whitespaces ignored as leading when we parse the next
     * value. */
    if (!(json->flags & JSON_FLAG_STREAMING))
    {
      // If JSON_FLAG_IMPLIED_END is set here, then it means we have already
      // seen EOF.
      //
      if (!(json->flags & JSON_FLAG_IMPLIED_END))
      {
        int c = next (json);
        if (json->flags & JSON_FLAG_ERROR)
          return JSON_ERROR;

        if (c != EOF)
        {
          json_error (json, "expected end of text instead of byte '%c'", c);
          return JSON_ERROR;
        }
      }
    }

    return JSON_DONE;
  }

  int c = next (json);
  if (json->flags & JSON_FLAG_ERROR)
    return JSON_ERROR;

  if (json->stack_top != (size_t)-1)
  {
    if (json->stack[json->stack_top].type == JSON_OBJECT)
    {
      if (json->stack[json->stack_top].count == 0)
      {
        // No member name/value pairs yet.
        //
        if (c == '}')
          return pop (json, JSON_OBJECT_END);

        json->stack[json->stack_top].count++;

        return read_name (json, c);
      }
      else if ((json->stack[json->stack_top].count % 2) == 0)
      {
        // Expecting comma followed by member name or closing brace.
        //
        // In JSON5 comma can be followed directly by the closing brace.
        //
        if (c == ',')
        {
          c = next (json);
          if (json->flags & JSON_FLAG_ERROR)
            return JSON_ERROR;

          if ((json->flags & JSON_FLAG_JSON5) && c == '}')
            ; // Fall through.
          else
          {
            json->stack[json->stack_top].count++;

            return read_name (json, c);
          }
        }

        if (json->stack_top != 0 || !(json->flags & JSON_FLAG_IMPLIED_END))
        {
          if (c == '}')
            return pop (json, JSON_OBJECT_END);

          json_error (json, "%s", "expected ',' or '}' after member value");
          return JSON_ERROR;
        }

        // Handle implied `}`.
        //
        if (c == EOF)
        {
          json->pending.type = JSON_DONE;
          json->pending.lineno = 0;
          json->pending.colno = 0;

          return pop (json, JSON_OBJECT_END);
        }

        if (c == '}')
        {
          json_error (json, "%s", "explicit '}' in implied object");
        }
        else
        {
          json_error (json, "%s", "expected ',' after member value");
        }

        return JSON_ERROR;
      }
      else
      {
        // Expecting colon followed by value.
        //
        if (c == ':')
        {
          c = next (json);
          if (json->flags & JSON_FLAG_ERROR)
            return JSON_ERROR;

          json->stack[json->stack_top].count++;

          return read_value (json, c);
        }

        json_error (json, "%s", "expected ':' after member name");
        return JSON_ERROR;
      }
    }
    else
    {
#if 0
      assert (json->stack[json->stack_top].type == JSON_ARRAY);
#endif

      if (json->stack[json->stack_top].count == 0)
      {
        // No array values yet.
        //
        if (c == ']')
          return pop (json, JSON_ARRAY_END);

        json->stack[json->stack_top].count++;
        return read_value (json, c);
      }

      // Expecting comma followed by array value or closing brace.
      //
      // In JSON5 comma can be followed directly by the closing brace.
      //
      if (c == ',')
      {
        c = next (json);
        if (json->flags & JSON_FLAG_ERROR)
          return JSON_ERROR;

        if ((json->flags & JSON_FLAG_JSON5) && c == ']')
          ; // Fall through.
        else
        {
          json->stack[json->stack_top].count++;
          return read_value (json, c);
        }
      }

      if (c == ']')
        return pop (json, JSON_ARRAY_END);

      json_error (json, "%s", "expected ',' or ']' after array value");
      return JSON_ERROR;
    }
  }
  else
  {
    if (c == EOF && (json->flags & JSON_FLAG_STREAMING))
      return JSON_DONE;

    // Sniff out implied `{`.
    //
    // See below for the implied `}` injection.
    //
    // @@ TODO: move below to documentation.
    //
    // The object can be empty.
    //
    // Limitations:
    //
    // - Incompatible with the streaming mode.
    // - Line/columns numbers for implied `{` and `}` are of the first
    //   member name and EOF, respectively.
    //
    //
    if ((json->flags & JSON_FLAG_JSON5E) &&
        !(json->flags & JSON_FLAG_STREAMING))
    {
      bool id;
      if ((id = is_first_id_char (c)) || c == '"' || c == '\'')
      {
        size_t lineno = json_get_lineno (json);
        size_t colno = json_get_column (json);

        json->ntokens++;

        if ((id
             ? read_identifier (json, c)
             : read_string (json, c)) == JSON_ERROR)
          return JSON_ERROR;

        enum json_type type;

        // Peek at the next non-whitespace/comment character, similar to
        // next(). Note that skipping comments would require a two-character
        // look-ahead, which we don't have. However, `/` in this context that
        // does not start a comment would be illegal. So we simply diagnose
        // this case here, making sure to recreate exactly the same
        // diagnostics (both message and location-wise) as would be issued in
        // the non-extended mode.
        //
        // Save the next character after the name for diagnostics below.
        //
        int nc = json->source.peek (&json->source);
        for (c = nc; ; c = json->source.peek (&json->source))
        {
          if (!json_isspace (json, c) && c != '/' && c != '#')
            break;

          json->source.get (&json->source);

          if (c == '\n')
            newline (json);
          else if (c == '/')
          {
            int p = json->source.peek (&json->source);
            if (p == '/' || p == '*')
            {
              json->source.get (&json->source);
              if ((c = skip_comment (json, p)) == EOF)
              {
                if (json->flags & JSON_FLAG_ERROR)
                  return JSON_ERROR;

                break;
              }
            }
            else
              break; // Diagnose read '/' below.
          }
          else if (c == '#')
          {
            if ((c = skip_comment (json, c)) == EOF)
            {
              if (json->flags & JSON_FLAG_ERROR)
                return JSON_ERROR;

              break;
            }
          }
        }

        if (c == ':')
        {
          json->pending.type = JSON_STRING;
          json->pending.lineno = lineno;
          json->pending.colno = colno;

          json->flags |= JSON_FLAG_IMPLIED_END;

          json->ntokens++; // For `{`.
          type = push (json, JSON_OBJECT);

          if (type != JSON_ERROR)
            json->stack[json->stack_top].count++; // For pending name.
        }
        else
        {
          // Return as a string or one of the literal values.
          //
          // Note that we have ambiguity between, for example, `true` and
          // `true_value`. But any continuation would be illegal so we resolve
          // it in favor of a member name. However, if not followed by `:`, we
          // need to diagnose identically to read_value () (both message and
          // position-wide), which gets a bit tricky.
          //
          if (id)
          {
            switch (json->data.string[0])
            {
            case 'n':
              type = is_match_string (json, "null", nc , &colno, JSON_NULL);
              break;
            case 't':
              type = is_match_string (json, "true", nc , &colno, JSON_TRUE);
              break;
            case 'f':
              type = is_match_string (json, "false", nc , &colno, JSON_FALSE);
              break;
            case 'I':
              type = is_match_string (json, "Infinity", nc , &colno, JSON_NUMBER);
              break;
            case 'N':
              type = is_match_string (json, "NaN", nc , &colno, JSON_NUMBER);
              break;
            default:
              json_error (json,
                          "unexpected byte '%c' in value",
                          json->data.string[0]);
              type = JSON_ERROR;
            }
          }
          else
            type = JSON_STRING;

          // Per the above comment handling logic, if the character we are
          // looking at is `/`, then it is read, not peeked at, and so we
          // have to diagnose it here.
          //
          if (type != JSON_ERROR && c == '/')
          {
            json_error (json, "expected end of text instead of byte '%c'", c)
            return JSON_ERROR; // Don't override location.
          }
        }

        // Note: set even in case of an error since peek() above moved the
        // position past the name/value.
        //
        json->lineno = lineno;
        json->start_colno = colno;

        return type;
      }
      else if (c == EOF)
      {
        // Allow empty implied objects (for example, all members commented
        // out).
        //
        json->pending.type = JSON_OBJECT_END;
        json->pending.lineno = 0;
        json->pending.colno = 0;

        json->flags |= JSON_FLAG_IMPLIED_END;

        json->start_lineno = 1;
        json->start_colno = 1;

        // Note that we need to push an object entry into the stack to make
        // sure json_get_context() works correctly.
        //
        json->ntokens++; // For `{`.
        return push (json, JSON_OBJECT);
      }
      // Else fall through.
    }

    return read_value (json, c);
  }
}

void
json_reset (json_stream *json)
{
  json->stack_top = (size_t)-1;
  json->ntokens = 0;
  json->flags &= ~(JSON_FLAG_ERROR | JSON_FLAG_IMPLIED_END);
  json->errmsg[0] = '\0';
}

enum json_type
json_skip (json_stream *json)
{
  enum json_type type = json_next (json);
  size_t cnt_arr = 0;
  size_t cnt_obj = 0;

  for (enum json_type skip = type; ; skip = json_next (json))
  {
    if (skip == JSON_ERROR || skip == JSON_DONE)
      return skip;

    if (skip == JSON_ARRAY)
      ++cnt_arr;
    else if (skip == JSON_ARRAY_END && cnt_arr > 0)
      --cnt_arr;
    else if (skip == JSON_OBJECT)
      ++cnt_obj;
    else if (skip == JSON_OBJECT_END && cnt_obj > 0)
      --cnt_obj;

    if (!cnt_arr && !cnt_obj)
      break;
  }

  return type;
}

enum json_type
json_skip_until (json_stream *json, enum json_type type)
{
  while (true)
  {
    enum json_type skip = json_skip (json);

    if (skip == JSON_ERROR || skip == JSON_DONE)
      return skip;

    if (skip == type)
      break;
  }

  return type;
}

const char *
json_get_string (json_stream *json, size_t *length)
{
  if (length != NULL)
    *length = json->data.string_fill;

  if (json->data.string == NULL)
    return "";
  else
    return json->data.string;
}

double
json_get_number (json_stream *json)
{
  char *p = json->data.string;
  return p == NULL ? 0 : strtod (p, NULL);
}

const char *
json_get_error (json_stream *json)
{
  return json->flags & JSON_FLAG_ERROR ? json->errmsg : NULL;
}

size_t
json_get_lineno (json_stream *json)
{
  return json->start_lineno == 0 ? json->lineno : json->start_lineno;
}

size_t
json_get_position (json_stream *json)
{
  return json->source.position;
}

size_t
json_get_column (json_stream *json)
{
  return json->start_colno == 0
    ? (json->source.position == 0
       ? 1
       : json->source.position - json->linepos - json->lineadj)
    : json->start_colno;
}

size_t
json_get_depth (json_stream *json)
{
  return json->stack_top + 1;
}

/* Return the current parsing context, that is, JSON_OBJECT if we are inside
   an object, JSON_ARRAY if we are inside an array, and JSON_DONE if we are
   not yet/no longer in either.

   Additionally, for the first two cases, also return the number of parsing
   events that have already been observed at this level with json_next/peek().
   In particular, inside an object, an odd number would indicate that the just
   observed JSON_STRING event is a member name. @@ Add explicit JSON_MEMBER?
*/
enum json_type
json_get_context (json_stream *json, size_t *count)
{
  if (json->stack_top == (size_t)-1)
    return JSON_DONE;

  if (count != NULL)
    *count = json->stack[json->stack_top].count;

  return json->stack[json->stack_top].type;
}

int
json_source_get (json_stream *json)
{
  // If the caller reads a multi-byte UTF-8 sequence, we expect them to read
  // it in its entirety. We also assume that any invalid bytes within such a
  // sequence belong to the same column (as opposed to starting a new column
  // or some such).
  //
  // In JSON5, if the caller starts reading a comment, we expect them to
  // finish reading it.

  int c = json->source.get (&json->source);
  if (json->linecon > 0)
  {
    /* Expecting a continuation byte within a multi-byte UTF-8 sequence. */
    json->linecon--;
    if (c != EOF)
      json->lineadj++;
  }
  else if (c == '\n')
    newline (json);
  else if (c >= 0xC2 && c <= 0xF4) /* First in multi-byte UTF-8 sequence. */
    json->linecon = utf8_seq_length (c) - 1;

  return c;
}

int
json_source_peek (json_stream *json)
{
  return json->source.peek (&json->source);
}

void
json_open_buffer (json_stream *json, const void *buffer, size_t size)
{
  init (json);
  json->source.get = buffer_get;
  json->source.peek = buffer_peek;
  json->source.source.buffer.buffer = (const char *)buffer;
  json->source.source.buffer.length = size;
}

void
json_open_string (json_stream *json, const char *string)
{
  json_open_buffer (json, string, strlen (string));
}

void
json_open_stream (json_stream *json, FILE * stream)
{
  init (json);
  json->source.get = stream_get;
  json->source.peek = stream_peek;
  json->source.source.stream.stream = stream;
}

static int
user_get (struct json_source *json)
{
  int c = json->source.user.get (json->source.user.ptr);
  if (c != EOF)
    json->position++;
  return c;
}

static int
user_peek (struct json_source *json)
{
  return json->source.user.peek (json->source.user.ptr);
}

void
json_open_user (json_stream *json,
                json_user_io get,
                json_user_io peek,
                void *user)
{
  init (json);
  json->source.get = user_get;
  json->source.peek = user_peek;
  json->source.source.user.ptr = user;
  json->source.source.user.get = get;
  json->source.source.user.peek = peek;
}

void
json_set_allocator (json_stream *json, json_allocator *a)
{
  json->alloc = *a;
}

void
json_set_streaming (json_stream *json, bool mode)
{
  if (mode)
    json->flags |= JSON_FLAG_STREAMING;
  else
    json->flags &= ~JSON_FLAG_STREAMING;
}

void
json_set_language (json_stream *json, enum json_language language)
{
  switch (language)
  {
  case json_language_json:
    json->flags &= ~(JSON_FLAG_JSON5 | JSON_FLAG_JSON5E);
    break;
  case json_language_json5:
    json->flags &= ~JSON_FLAG_JSON5E;
    json->flags |= JSON_FLAG_JSON5;
    break;
  case json_language_json5e:
    json->flags |= JSON_FLAG_JSON5 | JSON_FLAG_JSON5E;
    break;
  }
}

void
json_close (json_stream *json)
{
  json->alloc.free (json->stack);
  json->alloc.free (json->data.string);
}
