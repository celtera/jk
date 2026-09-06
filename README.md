# jk... unless?

A library for performing jq-ish queries on C++ objects.

## Supported language

The parser compiles directly into coroutine filters; it does not use a VM.
In addition to navigation, construction, arithmetic, comparison, and the
existing collection/math builtins, it supports:

- `if … then … elif … then … else … end`; an omitted `else` is identity.
- `select`, `and`, `or`, and `//` with jq truthiness and stream semantics.
  `//` handles false/null/empty results, not errors.
- `try EXP catch HANDLER`, `try EXP`, optional navigation, `error`, and
  `error(payload)`. Catch handlers receive the original error value.
- `any`/`all` with zero, one, or two arguments; `first`/`last` with zero or
  one; `isempty(filter)`; `nth(index)` and `nth(index; filter)`;
  `limit(count; filter)`, `skip(count; filter)`, and all three `range` arities.
- Semicolon-separated filter arguments, pipelines in object values,
  expression-valued slice bounds, nested string interpolation (`"x=\(.x)"`),
  and `#` comments, including backslash continuation.

Filters preserve output before an error. Early consumers such as `first`
and `limit` do not evaluate the unused remainder of a stream, including
streams produced by object construction. Slices and splitting strings with
an empty separator operate on UTF-8 codepoints.

## Compatibility and execution limits

This is a jq subset, not a replacement for the jq command-line application.
Variables, user-defined functions, reduce/foreach, assignment/update,
modules, regex, and I/O are not implemented. Objects use sorted keys rather
than jq's insertion order. Integer comparisons preserve signed 64-bit
precision; general arithmetic uses binary64, not jq's full decimal-literal
representation. Runtime error wording is not byte-for-byte jq compatible.

Parsing allocates on the normal heap and compiled filters own stable
`smallfun` nodes. Evaluation can use an `evaluation_context`: coroutine frames
and owning temporary values then use its caller-supplied storage, while
pass-through values are borrowed until a collecting or escaping boundary.
The default bounded context throws `std::bad_alloc` when exhausted; an
explicit upstream resource can preserve unbounded compatibility. Results are
valid until their producer advances. Use `yielded::take()` to retain a result
inside the selected allocation context, or `yielded::persist()` to detach it
onto normal heap storage before the context resets.

This removes per-message heap allocation for supported bounded workloads; it
does not impose a work budget, and runtime errors still allocate persistent
exception diagnostics and payloads. Therefore evaluation is not yet
hard-real-time safe.

## Verification

The standalone build requires C++20, Catch2 3, fmt, and SmallFunction:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/cases.txt` is the supported-behavior regression corpus.
`tests/gen-cases.sh` requires the official **jq 1.8.2** executable; set `JQ`
to its path if necessary. Regenerate `tests/conformance_cases.inc` with that
script. The generated oracle compares sorted-key JSON output, stream order,
and compile/runtime failure status, including output emitted before failure.
