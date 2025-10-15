#include <stddef.h>
#include <stdint.h>

#include <libpdjson5/pdjson5.h>

#undef NDEBUG
#include <assert.h>

// Parse the input text in the specified mode returning true if it is valid
// and false otherwise.
//
static bool
parse (pdjson_stream *json,
       const void *data, size_t size,
       enum pdjson_language language,
       bool streaming)
{
  pdjson_reopen_buffer (json, data, size);
  pdjson_set_streaming (json, streaming);
  pdjson_set_language (json, language);

  enum pdjson_type t;
  do
  {
    t = pdjson_next (json);

    if (streaming && t == PDJSON_DONE)
    {
      pdjson_reset (json);
      t = pdjson_next (json); // PDJSON_DONE if that was the last object.
    }

    // Let's get a warning if any new values are added.
    //
    size_t n;
    switch (t)
    {
    case PDJSON_ERROR:
      assert (pdjson_get_error_subtype (json) == PDJSON_ERROR_SYNTAX &&
              pdjson_get_error (json) != NULL);
      break;
    case PDJSON_NAME:
      assert (pdjson_get_name (json, NULL) != NULL);
      break;
    case PDJSON_STRING:
      assert (pdjson_get_value (json, NULL) != NULL);
      break;
    case PDJSON_NUMBER:
      assert (pdjson_get_value (json, &n) != NULL && n != 0);
      break;
    case PDJSON_OBJECT:
      assert (pdjson_get_context (json, NULL) == PDJSON_OBJECT);
      break;
    case PDJSON_ARRAY:
      assert (pdjson_get_context (json, NULL) == PDJSON_ARRAY);
      break;
    case PDJSON_DONE:
    case PDJSON_TRUE:
    case PDJSON_FALSE:
    case PDJSON_NULL:
    case PDJSON_OBJECT_END:
    case PDJSON_ARRAY_END:
      break;
    }
  }
  while (t != PDJSON_DONE && t != PDJSON_ERROR);

  return t != PDJSON_ERROR;
}

int
LLVMFuzzerTestOneInput (const uint8_t* data, size_t size)
{
  pdjson_stream json[1];

  pdjson_open_null (json);

  // Parse the input in every mode.
  //
  // While it may see that if the input is valid in the stricter mode, then
  // parsing it in the more relaxed one would be a waste of time. However,
  // different modes may apply different parsing logic to the same input
  // (implied object handling in JSON5E is a good example).
  //
  parse (json, data, size, PDJSON_LANGUAGE_JSON,   false);
  parse (json, data, size, PDJSON_LANGUAGE_JSON,   true);
  parse (json, data, size, PDJSON_LANGUAGE_JSON5,  false);
  parse (json, data, size, PDJSON_LANGUAGE_JSON5,  true);
  parse (json, data, size, PDJSON_LANGUAGE_JSON5E, false);
  parse (json, data, size, PDJSON_LANGUAGE_JSON5E, true);

  pdjson_close (json);

  return 0;
}
