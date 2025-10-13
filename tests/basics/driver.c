// Usage: driver [<options>]
//
// --streaming  --  enable streaming mode
// --separator  --  handle/print value separors in streaming mode
// --json5      --  accept JSON5 input
// --json5e     --  accept JSON5E input
//

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <libpdjson5/version.h>
#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

int
main (int argc, char *argv[])
{
  bool streaming = false;
  bool separator = false;
  enum json_language language = json_language_json;

  for (int i = 1; i < argc; ++i)
  {
    const char* a = argv[i];

    if (strcmp (a, "--streaming") == 0)
      streaming = true;
    else if (strcmp (a, "--separator") == 0)
      separator = true;
    else if (strcmp (a, "--json5") == 0)
      language = json_language_json5;
    else if (strcmp (a, "--json5e") == 0)
      language = json_language_json5e;
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
        unsigned long cp;
        int r;
        while ((r = json_skip_if_space (&json, json_source_peek (&json), &cp)))
        {
          if (r == -1)
          {
            t = JSON_ERROR;
            break;
          }

          printf ("%3zu,%3zu: <0x%06lx>\n",
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

    printf ("%3zu,%3zu: ",
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
        size_t n;
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
    fprintf (stderr,
             "<stdin>:%zu:%zu: error: %s\n",
             json_get_line (&json),
             json_get_column (&json),
             json_get_error (&json));
    r = 1;
  }

  json_close(&json);

  return r;
}
