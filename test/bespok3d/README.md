# Bespok3d contract test

Checks that this firmware's payload is one the [Bespok3d **RFID Spool
Reader**](https://github.com/Bespok3d/u1-enhanced-rfid) plugin will accept, and
that the firmware reads the plugin's answer correctly.

```bash
python3 test/bespok3d/check.py
```

## Why this exists

The two sides of that question are written in different languages and live in
different repositories. That is exactly the seam that rots without anyone
noticing — and it rots in the worst direction: their handler validates the
whole `info` object and refuses the **entire request** over one key it does not
know, so a single stray field silently stops every spool reaching the printer.

So the test runs the real thing on both ends:

```
u1BuildPayload()  ->  their validator  ->  their answer  ->  u1ClassifyReply()
```

`emit_payload.cpp` and `classify.cpp` compile the **shipped** `src/*.cpp`
against the small Arduino stubs in `stubs/`. Only the middle is a
transcription, and it carries a version and a source link.

Add a field to `u1BuildPayload()` that they do not accept and this fails, with
the field named.

## Keeping it honest

`bespok3d_validator.py` is transcribed from a specific plugin version — the
`DESCRIBES` constant says which. When the plugin moves, re-read

    rfid-ntag/files/klipper/klippy/extras/rfid-support/rfid_ntag.py

specifically `_handle_filament_detect_set`, `_build_filament_info`,
`_resolve_channel` and `_should_apply`, update the transcription, and run this.
A green run then means the new version still accepts what we send.

**A transcription is not the real thing.** This catches field-list and
response-shape drift, which is what has actually bitten. It cannot catch
behaviour that only shows up on a printer — whether `set_filament_info`
notifies the hub, for instance, or what the screen does with the result.

## Stubs

`stubs/` is just enough Arduino to compile the networking translation units on
a host: `String`, and empty shells for `WiFiClient`, `HTTPClient`,
`Preferences` and `ESP`. Nothing there talks to hardware, and nothing in it
should grow logic — if a test needs behaviour from a stub, the code under test
probably wants splitting instead, the way `u1_reply.cpp` was.
