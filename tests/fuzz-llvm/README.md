This is an [LLVM LibFuzzer](https://llvm.org/docs/LibFuzzer.html)-based test.

A typical setup could look like this:

```
cd libpdjson5
bdep init -C @fuzz cc config.c=clang config.c.coptions='-O3 -fsanitize=address,undefined,fuzzer-no-link'
b ../libpdjson5-fuzz/tests/fuzz-llvm/
cd ../libpdjson5-fuzz/tests/fuzz-llvm/
mkdir corpus
./driver corpus/
```

It is, however, recommended to pre-initialize the corpus with some samples
(both valid and invalid). For example, you could use the `test_parsing/`
directory from [JSONTestSuite](https://github.com/nst/JSONTestSuite) as
a starting corpus directory.
