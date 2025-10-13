#include <stddef.h>
#include <stdint.h>

#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

/* Parse the input text in the specified mode returning true if it is valid
   and false otherwise. */

static bool
parse (const void *data, size_t size,
       enum json_language language,
       bool streaming)
{
  struct json_stream json[1];
  enum json_type t = JSON_DONE;

  json_open_buffer (json, data, size);
  json_set_streaming (json, streaming);
  json_set_language (json, language);

  do
  {
    t = json_next (json);

    if (streaming && t == JSON_DONE)
    {
      json_reset (json);
      t = json_next (json); /* JSON_DONE if that was the last object. */
    }

    /* Let's get a warning if any new values are added. */
    size_t n;
    switch (t)
    {
    case JSON_ERROR:
      assert (json_get_error (json) != NULL);
      break;
    case JSON_STRING:
      assert (json_get_string (json, NULL) != NULL);
      break;
    case JSON_NUMBER:
      assert (json_get_string (json, &n) != NULL && n != 0);
      break;
    case JSON_OBJECT:
      assert (json_get_context (json, NULL) == JSON_OBJECT);
      break;
    case JSON_ARRAY:
      assert (json_get_context (json, NULL) == JSON_ARRAY);
      break;
    case JSON_DONE:
    case JSON_TRUE:
    case JSON_FALSE:
    case JSON_NULL:
    case JSON_OBJECT_END:
    case JSON_ARRAY_END:
      break;
    }
  }
  while (t != JSON_DONE && t != JSON_ERROR);

  json_close (json);

  return t != JSON_ERROR;
}

int
LLVMFuzzerTestOneInput (const uint8_t* data, size_t size)
{
  // Parse the input in every mode.
  //
  // While it may see that if the input is valid in the stricter mode, then
  // parsing it in the more relaxed one would be a waste of time. However,
  // different modes may apply different parsing logic to the same input
  // (implied object handling in JSON5E is a good example).
  //
  parse (data, size, json_language_json,   false);
  parse (data, size, json_language_json,   true);
  parse (data, size, json_language_json5,  false);
  parse (data, size, json_language_json5,  true);
  parse (data, size, json_language_json5e, false);
  parse (data, size, json_language_json5e, true);

  return 0;
}
