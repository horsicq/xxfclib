# Private format helpers

These headers contain reusable parsing primitives and component drivers.
Concrete readers stay in `formats/<format>/xx_<format>.c`; a shared helper does
not belong to whichever format happened to use it first.

Use names that describe the data or operation. Do not name production files or
private helpers after the order in which formats were added. Keep format-specific
grammars beside their reader rather than adding a type switch to a shared header.

The main layers are:

- `xx_binary_cursor.h`, `xx_memory_blob.h`, `xx_binary_records.h` and
  `xx_utf8_validation.h`: bounded input and validation primitives.
- `xx_serialized_value_helpers.h`, `xx_asn1_der.h`, protocol and network headers:
  container values and wire framing.
- Scientific text, molecular, sequence and chemistry headers: reusable domain
  primitives; individual file grammars remain in their reader modules.
- Component headers: shared binary/text cursors for audio, images, models, fonts
  and retro resources.
- `xx_component_binary.h`, `xx_component_text.h`, `xx_component_lexer.h`:
  canonical visual parsing primitives. Explicit variants retain line overflow,
  delimiter, numeric precision, comment and memory ownership contracts. A helper
  that calls a different policy variant has a different contract too.
- `xx_music_components.h`: shared RAM input, bounds, work budget and member
  emission, with separate specialized validators where their rules differ.
- `xx_component_parser_driver.h`: parameterized RAM parsing drivers. Single-read
  and chunked-read paths remain distinct; callers retain their allocation limits,
  cancellation policy and successful size updates.
- Carrier helpers: common executable/archive wrapper handling.

Preserve cancellation checks, allocation/work/member limits, string rules and
input cursor semantics when changing a helper. Similar-looking functions can
have different validation contracts and should not be merged without comparing
their complete behavior.

See [the format layout guide](../../../docs/FORMAT_LAYOUT.md) and
[ordered detection manifests](../detection/README.md).
