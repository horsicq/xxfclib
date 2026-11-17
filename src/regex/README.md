# Regex implementation

`xx_regex.c` implements the shared byte-oriented regular expression compiler,
matcher, immutable compiled-program references, metadata and match budgets.
Its public API is `include/xxfclib/regex/xx_regex.h`. It depends on the runtime
and atomic/thread helpers, and can be linked without the JavaScript engine.

The adapters in this directory retain their callers' existing behavior:

- `xx_regex_js.c`: JavaScript RegExp construction, objects, test/exec and properties.
- `xx_regex_js_lex.c`: regex literal scanning and slash/division classification.
- `xx_regex_js_strings.c`: JavaScript String match/search/replace/split methods.
- `xx_regex_die.c`: DIE archive record-name matching and scan budgets.

The JavaScript lexer delegates literal scanning here; the parser retains the
grammar nodes and compiled handles in its AST. Pattern parsing and matching
are implemented here.
Application callers include the public header; interpreter and DIE adapters use
the private headers beside their implementations. Third-party engines outside
xxfclib retain their original source layouts.

## Public API example

```c
#include <xxfclib/regex/xx_regex.h>
#include <xxfclib/rt/xx_rt.h>

char *error = NULL;
xx_regex *regex = xx_regex_compile("[0-9]+", "", &error);
if (regex) {
    int32_t captures[2]; /* 2 * (xx_regex_ngroups(regex) + 1) entries */
    int found = xx_regex_exec(regex, "name123", 7, 0, captures);
    /* found: 1; captures: {4, 7}. Offsets and subject lengths are bytes. */
    (void)found;
    xx_regex_free(regex);
}
xx_rt_free(error);
```

`xx_regex_compile` accepts NUL-terminated patterns and flags.
`xx_regex_compile_n` accepts explicit lengths and preserves embedded NUL bytes
in patterns; `xx_regex_source_size` and `xx_regex_flags_size` report the stored
lengths. Only `g`, `i`, and `m` are accepted, each at most once. Subjects are
explicitly counted and may contain zero bytes. Captures use signed 32-bit byte offsets; larger subjects
are rejected. The global flag is metadata for adapters: the core has no shared
lastIndex. Concurrent executions require separate capture arrays and one live
reference per user. Matching retains the byte/Latin-1 interpretation rather
than ECMAScript UTF-16 indexing. ASCII case folding and the existing standalone
`\s` versus class-space compatibility rules are retained.

## Execution and search

`xx_regex_search.c` filters case-sensitive ASCII candidates with two necessary
bytes at a known fixed offset. Sparse inputs first use the runtime's existing
single-byte search. Repeated false candidates use bounded SSE2 or AArch64 NEON
loads when the corresponding runtime feature switch is enabled; other targets
and disabled features use the scalar path. The ordinary matcher verifies every
candidate and determines captures and alternative order. No vector load reads
beyond the supplied subject.

Groups with one deterministic nonempty chain of literal, dot, or class atoms
repeat iteratively. This supports long `(a)+` and `(ab){n}` matches without
growing the C continuation stack. Variable-width class/dot repetitions retain
their exact byte boundaries for backtracking. Complex groups still use bounded
backtracking: parse nesting is limited to 1000, continuation depth to 2000, and
the execution step budget scales with subject size up to its existing ceiling.
Budget/depth exhaustion and execution allocation failure return zero through
the current API, like an ordinary miss. These bounds do not guarantee arbitrary
complex patterns will finish matching every valid long subject.

## Focused tests

`tests/regex` can be configured independently with
`XXFC_REGEX_TEST_LIBRARY` pointing to an existing xxfclib or DIE engine archive.
It compiles the core directly by default and checks captures, binary subjects,
reference lifetime, large searches and concurrent reuse. An integrated build
can enable `XXFC_BUILD_REGEX_TESTS` (or `XXFC_BUILD_TESTS`). JavaScript adapter
regressions remain in `tests/js_performance`. `test_regex_search.c` compares
scalar and enabled SIMD results across alignments, random binary inputs,
partial blocks and protected page boundaries. Allocation-failure injection
covers both compilation and temporary execution state.

The larger sibling pack lives in `../xxfclib_tests/regex` (relative to the
library root). It includes an independent Node reference, exhaustive binary
literal tests, long repeated groups, concurrent reuse and calibrated
compile/execution benchmarks. It complements the JS adapter regressions in
`tests/js_performance`. Its README documents focused
builds, repeatable timing comparisons, and the byte-model compatibility limits.

## Scanner dialect

`xx_regex_compile_n_options` can select `XX_REGEX_RAW_BYTES` and the opt-in
`XX_REGEX_SCANNER_DIALECT`. Every byte then has its own position; scoped and
bare `i/m/s/x` flags and fixed-width lookbehind are available. Existing JS
entrypoints retain their original dialect. Variable-width lookbehind is
rejected.

`xx_regex_exec_controlled` adds anchored/end-anchored matching, explicit
begin/end-of-line context, cancellation, and a path callback that can reject
overlong candidates while allowing ordinary backtracking. Budget exhaustion
and cancellation remain distinct from a miss. `xx_regex_unicode_property`
provides pinned Unicode 17 Alphabetic, Numeric, and Decimal_Number properties
from embedded UCD ranges; see `LICENSE.UNICODE`.

`xx_regex_unicode16.c` (`include/xxfclib/regex/xx_regex_unicode16.h`) keeps
the Unicode 16.0.0 regex tables of the regex-syntax 0.8.11 crate that YARA-X
1.23.0 uses: the deduplicated property, script, age and break-property range
tables with their normalized alias tables, the Perl `\w \d \s` classes and the
simple case-folding classes. They stay apart from the Unicode 17 tables above
and in `src/strings`. `xx_regex_unicode16_data.inc` is generated by
`tools/gen_regex_unicode16.py` from the crate's `src/unicode_tables`;
`--check` verifies that the committed file is reproduced byte for byte.
