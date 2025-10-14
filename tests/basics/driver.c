// Usage: driver [<options>]
//
// --streaming      --  enable streaming mode
// --separator      --  handle/print value separors in streaming mode
// --io-error <pos> --  cause input stream error at or after position
// --json5          --  accept JSON5 input
// --json5e         --  accept JSON5E input
//
//

#include <stdio.h>
#include <errno.h>
#include <stdlib.h> // strtoull()
#include <string.h> // str*()
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h> // PR*

#include <libpdjson5/version.h>
#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

int
main (int argc, char *argv[])
{
  bool streaming = false;
  bool separator = false;
  uint64_t io_error = (uint64_t)-1;
  enum json_language language = JSON_LANGUAGE_JSON;

  for (int i = 1; i < argc; ++i)
  {
    const char* a = argv[i];

    if (strcmp (a, "--streaming") == 0)
      streaming = true;
    else if (strcmp (a, "--separator") == 0)
      separator = true;
    else if (strcmp (a, "--io-error") == 0)
    {
      if (++i < argc)
      {
        errno = 0;
        io_error = strtoull (argv[i], NULL, 10);
        if (errno == 0)
          continue;
      }

      fprintf (stderr, "error: missing or invalid --io-error argument\n");
      return 1;
    }
    else if (strcmp (a, "--json5") == 0)
      language = JSON_LANGUAGE_JSON5;
    else if (strcmp (a, "--json5e") == 0)
      language = JSON_LANGUAGE_JSON5E;
    else
    {
      fprintf (stderr, "error: unexpected argument '%s'\n", a);
      return 1;
    }
  }

  if (separator && !streaming)
  {
    fprintf (stderr, "error: --separator specified without --streaming\n");
    return 1;
  }

  json_stream json;
  json_open_stream (&json, stdin);
  json_set_streaming (&json, streaming);
  json_set_language (&json, language);

  size_t ind = 0; // Indentation.

  enum json_type t;
  for (bool first = true;;)
  {
    if (io_error != (uint64_t)-1)
    {
      uint64_t p = json_get_position (&json);

      // Note that we don't observe every position since some of them are
      // passed over inside the parser. This limits the failure points we can
      // test (would need to use custom io for that).
      //
      if (p >= io_error)
      {
        printf ("%3" PRIu64 ",%3" PRIu64 ": <io error at %" PRIu64 ">\n",
                json_get_line (&json),
                json_get_column (&json),
                p);

        fclose (stdin);
      }
    }

    t = json_next (&json);

    if (t == JSON_ERROR)
      break;

    if (t == JSON_DONE)
    {
      // Second JSON_DONE in the streamig mode is the end of multi-value.
      //
      if (!streaming || first)
        break;

      if (separator)
      {
        uint32_t cp;
        int r;
        while ((r = json_skip_if_space (&json, json_source_peek (&json), &cp)))
        {
          if (r == -1)
          {
            t = JSON_ERROR;
            break;
          }

          printf ("%3" PRIu64 ",%3" PRIu64 ": <0x%06" PRIx32 ">\n",
                  json_get_line (&json),
                  json_get_column (&json),
                  cp);
        }
      }

      if (t == JSON_ERROR)
        break;

      json_reset (&json);
      first = true;
      continue;
    }

    if (first)
      first = false;

    printf ("%3" PRIu64 ",%3" PRIu64 ": ",
            json_get_line (&json),
            json_get_column (&json));

    if (t == JSON_ARRAY_END || t == JSON_OBJECT_END)
      ind--;

    for (size_t i = 0; i != ind; ++i)
      fputs ("  ", stdout);

    if (t == JSON_ARRAY || t == JSON_OBJECT)
      ind++;

    switch (t)
    {
    case JSON_NULL:
      printf ("<null>\n");
      break;
    case JSON_TRUE:
      printf ("<true>\n");
      break;
    case JSON_FALSE:
      printf ("<false>\n");
      break;
    case JSON_NAME:
      {
        uint64_t n;
        assert (json_get_context (&json, &n) == JSON_OBJECT && n % 2 != 0);
      }
      // Fall through.
    case JSON_STRING:
    case JSON_NUMBER:
      {
        size_t n;
        const char* s = (t == JSON_NAME
                         ? json_get_name (&json, &n)
                         : json_get_value (&json, &n));
        assert (strlen (s) + 1 == n);

        // Print numbers and object member names without quoted.
        //
        printf (t == JSON_STRING ? "\"%s\"\n" : "%s\n", s);
        break;
      }
    case JSON_ARRAY:
      assert (json_get_context (&json, NULL) == JSON_ARRAY);
      printf ("[\n");
      break;
    case JSON_ARRAY_END:
      printf ("]\n");
      break;
    case JSON_OBJECT:
      assert (json_get_context (&json, NULL) == JSON_OBJECT);
      printf ("{\n");
      break;
    case JSON_OBJECT_END:
      printf ("}\n");
      break;
    case JSON_ERROR:
    case JSON_DONE:
      assert (false);
    }
  }

  int r = 0;
  if (t == JSON_ERROR)
  {
    const char *et;
    switch (json_get_error_subtype (&json))
    {
    case JSON_ERROR_SYNTAX: et = "";         break;
    case JSON_ERROR_MEMORY: et = " (memory)"; break;
    case JSON_ERROR_IO:     et = " (io)";     break;
    default: assert (false);
    }

    fprintf (stderr,
             "<stdin>:%" PRIu64 ":%" PRIu64 ": error: %s%s\n",
             json_get_line (&json),
             json_get_column (&json),
             json_get_error (&json),
             et);
    r = 1;
  }

  json_close(&json);

  return r;
}
