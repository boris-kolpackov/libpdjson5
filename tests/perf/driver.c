// Usage: driver [<options>]
//
// --size <num>       --  input size in KiB to parse
// --iteration <num>  --  number of times to parse
// --stdio            --  use stdio memory stream instead of memory buffer
// --userio           --  use io callbacks instead of memory buffer
// --json5            --  parse as JSON5 input
// --json5e           --  parse as JSON5E input
//
// Note that --stdio is not supported on Windows.
//

// fmemopen() is in POSIX.1-2008. Not available on Windows.
//
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#elif _POSIX_C_SOURCE < 200809L
#  error incompatible _POSIX_C_SOURCE level
#endif

#include <stdio.h>
#include <errno.h>
#include <stddef.h> // size_t
#include <stdlib.h> // strtoull(), malloc()
#include <string.h> // str*()
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h> // PR*

#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

static const char json_fragment[] =
"    \"boolean_value\": true,\n"
"    \"null_value\": null,\n"
"    \"integer_value\": 123456789,\n"
"    \"string_value\": \"77bd6c2ee33172287318170a8c7d357fe03f65bcbbf942e179b2a2ad8202e24f\",\n"
"    \"date_time\": \"2025-10-14T16:49:47Z\",\n"
"    \"array_integer_value\": [-100, -10, -1, 0, 1, 10, 100],\n"
"    \"array_string_value\": [\"memory exceeded\", \"disk exceeded\"],\n"
"    \"object_value\": {\n"
"        \"boolean\": false,\n"
"        \"integer\": 9876543210,\n"
"        \"array\": [123, 234, 345],\n"
"        \"object\": {\"line\":73,\"column\":64,\"position\":123}\n"
"    }";

struct buffer
{
  char *data;
  size_t size;
  size_t pos;
};

static int
io_peek (void *d)
{
  struct buffer *b = (struct buffer *)d;
  return b->pos != b->size ? b->data[b->pos] : EOF;
}

static int
io_get (void *d)
{
  struct buffer *b = (struct buffer *)d;
  return b->pos != b->size ? b->data[b->pos++] : EOF;
}

bool
io_error (void * d)
{
  (void)d;
  return false;
}

int
main (int argc, char *argv[])
{
  assert (sizeof (json_fragment) == 511); // Includes \0, so 510.

  size_t size = 10;
  uint64_t iter = 10;
  bool stdio = false;
  bool userio = false;
  enum pdjson_language language = PDJSON_LANGUAGE_JSON;

  for (int i = 1; i < argc; ++i)
  {
    const char* a = argv[i];

    if (strcmp (a, "--size") == 0)
    {
      if (++i < argc)
      {
        errno = 0;
        size = (size_t)strtoull (argv[i], NULL, 10);
        if (errno == 0 && size != 0)
          continue;
      }

      fprintf (stderr, "error: missing or invalid --size argument\n");
      return 1;
    }
    else if (strcmp (a, "--iteration") == 0)
    {
      if (++i < argc)
      {
        errno = 0;
        iter = strtoull (argv[i], NULL, 10);
        if (errno == 0 && iter != 0)
          continue;
      }

      fprintf (stderr, "error: missing or invalid --iteration argument\n");
      return 1;
    }
    else if (strcmp (a, "--stdio") == 0)
      stdio = true;
    else if (strcmp (a, "--userio") == 0)
      userio = true;
    else if (strcmp (a, "--json5") == 0)
      language = PDJSON_LANGUAGE_JSON5;
    else if (strcmp (a, "--json5e") == 0)
      language = PDJSON_LANGUAGE_JSON5E;
    else
    {
      fprintf (stderr, "error: unexpected argument '%s'\n", a);
      return 1;
    }
  }

  if (stdio && userio)
  {
    fprintf (stderr, "error: both --stdio and --userio specified\n");
    return 1;
  }

  struct buffer buf;

  buf.size = size * 1024 + 2 + 1;
  buf.data = (char *)malloc (buf.size);

  if (buf.data == NULL)
  {
    fprintf (stderr, "error: unable to allocate %zu\n", buf.size);
    return 1;
  }

  // Assemble the input out of the fragments.
  //
  {
    size_t p = 0;

    strcpy (buf.data + p, "{\n");
    p += 2;

    for (size_t i = 0; i != size * 2; ++i)
    {
      if (i != 0)
      {
        strcpy (buf.data + p, ",\n");
        p += 2;
      }

      strcpy (buf.data + p, json_fragment);
      p += sizeof (json_fragment) - 1;
    }

    strcpy (buf.data + p, "\n}");
    p += 3;

    assert (p == buf.size);
    buf.size--; // '\0'
  }

  FILE *mstream = NULL;
  if (stdio)
  {
#ifndef _WIN32
    mstream = fmemopen (buf.data, buf.size, "r");
    if (mstream == NULL)
    {
      fprintf (stderr, "error: unable to open stdio memory stream\n");
      return 1;
    }
#else
    fprintf (stderr, "error: stdio memory stream not supported on Windows\n");
    return 1;
#endif
  }

  pdjson_stream json[1];

  pdjson_open_null (json);
  pdjson_set_language (json, language);

  enum pdjson_type t = PDJSON_ERROR;
  for (uint64_t i = 0; i != iter; ++i)
  {
    if (stdio)
    {
      if (fseek (mstream, 0, SEEK_SET) != 0)
      {
        t = PDJSON_ERROR;
        break;
      }

      pdjson_reopen_stream (json, mstream);
    }
    else if (userio)
    {
      pdjson_user_io io = {&io_peek, &io_get, &io_error};
      buf.pos = 0;
      pdjson_reopen_user (json, &io, &buf);
    }
    else
      pdjson_reopen_buffer (json, buf.data, buf.size);

    while ((t = pdjson_next (json)) != PDJSON_DONE && t != PDJSON_ERROR)
      ;
  }

  int r = 0;
  if (t == PDJSON_ERROR)
  {
    fprintf (stderr,
             "<buffer>:%" PRIu64 ":%" PRIu64 ": error: %s\n",
             pdjson_get_line (json),
             pdjson_get_column (json),
             pdjson_get_error (json));
    r = 1;
  }

  pdjson_close (json);

  if (mstream != NULL)
    fclose (mstream);

  return r;
}
