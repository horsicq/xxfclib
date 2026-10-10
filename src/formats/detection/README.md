# Ordered native reader detection

The three manifests define native reader probes at their existing positions in
the broad detector in `xx_format_detect_device.c`:

* `xx_format_probe_primary.inc`: specific bounded signatures.
* `xx_format_probe_carriers.inc`: executable archive carriers.
* `xx_format_probe_fallback.inc`: weaker or signatureless evidence.

These files are maintained source, not generator output. A row specifies the
reader name, public file type and an ordinary C precondition. Keep rows and
header boundaries in order: an earlier validated reader wins. READ boundaries
perform one bounded read; READ_EXACT boundaries retain the explicitly required
short-read loop. MAGIC reuses the detector's existing signature bytes.

`xx_format_probe_registry.h` includes the primary and fallback manifests once
to define one non-inlined validation function per reader, then expands each
manifest into its detection pass. Carrier rows reuse existing validation
functions. Every validation uses init/check/destroy and the pass restores the
caller's cursor, whether validation succeeds or fails. Each pass has one
4,100-byte header buffer; reader objects live in their individual stack frames.

To add a reader, place its row in the primary or fallback manifest once. Add a
carrier row if it needs a second precondition at the carrier detection stage.
Keep its public header included by `xx_format_detect_device.c`. Choose the smallest adequate
header boundary and retain structural validation in the reader.

The surrounding ordered detector also has explicit `XX_FORMAT_READER_ADAPTER`
records. Their validator method, null-device policy and stack-frame annotation
are independent choices. Nine specialized wrappers remain regular functions.
Do not replace a base-info validator with a validity check merely because a
different reader uses it.

Compile this translation unit with the same detection feature definitions used
by the consumer. Generic properties and naming are separate translation units.
