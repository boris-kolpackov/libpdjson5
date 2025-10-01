#include <stddef.h>
#include <stdint.h>

#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

/* Parse the data in the specified stream mode returning true if the data is
   valid JSON and false otherwise. */
static bool
parse (const void *data, size_t size, bool stream)
{
  struct json_stream json[1];
  enum json_type t = JSON_DONE;

  json_open_buffer (json, data, size);
  json_set_streaming (json, stream);

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
  /* If it's valid in the strict mode, then don't waste time parsing it in
     relaxed. */
  if (!parse (data, size, false))
    parse (data, size, true);
  return 0;
}
