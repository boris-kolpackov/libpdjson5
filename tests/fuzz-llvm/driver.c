#include <stddef.h>
#include <stdint.h>

#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

/* Parse the input text in the specified mode returning true if it is valid
   and false otherwise. */
static bool
parse (const void *data, size_t size, bool stream, bool json5)
{
  struct json_stream json[1];
  enum json_type t = JSON_DONE;

  json_open_buffer (json, data, size);
  json_set_streaming (json, stream);
  json_set_json5 (json, json5);

  do
  {
    t = json_next (json);

    if (stream && t == JSON_DONE)
    {
      json_reset (json);
      t = json_next (json); /* JSON_DONE if that was the last object. */
    }

    /* Let's get a warning if any new values are added. */
    switch (t)
    {
    case JSON_DONE:                                                  break;
    case JSON_ERROR:  assert (json_get_error (json) != NULL);        break;
    case JSON_STRING: assert (json_get_string (json, NULL) != NULL); break;
    case JSON_NUMBER: json_get_number (json);                        break;
    case JSON_TRUE:
    case JSON_FALSE:
    case JSON_NULL:
    case JSON_OBJECT:
    case JSON_OBJECT_END:
    case JSON_ARRAY:
    case JSON_ARRAY_END:                                             break;
    }
  }
  while (t != JSON_DONE && t != JSON_ERROR);

  json_close (json);

  return t != JSON_ERROR;
}

int
LLVMFuzzerTestOneInput (const uint8_t* data, size_t size)
{
  /* If the input is valid in the stricter mode, then don't waste time parsing
     it in more relaxed. */
  if (!parse (data, size, false, false))
  {
    if (!parse (data, size, true, false) &&
        !parse (data, size, false, true))
      {
        parse (data, size, true, true);
      }
  }

  return 0;
}
