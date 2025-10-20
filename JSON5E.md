# JSON5E - JSON5 for Humans

JSON5E is an extension of the JSON5 format that aims to be even more natural
to write and maintain by hand. Where JSON5 retains the overall JSON shape and
draws on ECMAScript 5 for inspiration, JSON5E tries harder to approximate the
look and feel of a typical configuration file found in `/etc` while retaining
the JSON object model.

Specifically, JSON5E extends JSON5 with the following syntax:

* Implied top-level objects.

  JSON5:

  ```
  {
    delay: 10,
    timeout: 30
  }
  ```

  JSON5E:

  ```
  delay: 10,
  timeout: 30
  ```

  Note that top-level arrays and simple values are still supported (but there
  are no implied top-level arrays).

* Newline in addition to comma as a separator.

  JSON5:

  ```
  {
    delay: 10,
    timeout: 30
  }
  ```

  JSON5E:

  ```
  {
    delay: 10
    timeout: 30
  }
  ```

  Note that it must be a newline, not just a whitespace.

* `-` and `.` are allowed in unquoted object member names.

  But not as a first character.

  JSON5:

  ```
  {
    'connection-delay': 10,
    'connection-timeout': 30
  }
  ```

  JSON5E:

  ```
  {
    connection-delay: 10,
    connection-timeout: 30
  }
  ```


* `#`-style comments in addition to `//` and `/* */`.

  JSON5:

  ```
  {
    // Initial delay before connecting.
	//
    delay: 10,

	// Connection timeout.
	//
    timeout: 30
  }
  ```

  JSON5E:

  ```
  {
    # Initial delay before connecting.
	#
    delay: 10,

	# Connection timeout.
	#
    timeout: 30
  }
  ```

Putting it all together, JSON5:

```
{
  // Initial delay before connecting.
  //
  'connection-delay': 10,

  // Connection timeout.
  //
  'connection-timeout': 30
}
```

JSON5E:

```
# Initial delay before connecting.
#
connection-delay: 10

# Connection timeout.
#
connection-timeout: 30
```

Which looks a lot more like a typical configuration file.

The following parser implementations support JSON5E:

* C: [libpdjson5](https://github.com/boris-kolpackov/libpdjson5/)
* C++: [libstud-json](https://github.com/libstud/libstud-json)
