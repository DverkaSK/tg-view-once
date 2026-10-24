# tg-view-once desktop

Work in progress: bringing the "send a video as a video message (round video)"
feature of the userscript to the official Telegram Desktop client, without an
API key and without rebuilding Telegram on every update.

## Why this shape

Telegram Desktop is a compiled C++/Qt app with no plugin or script support, so
a feature is added by injecting a DLL that modifies the outgoing request in
memory. The trick for surviving updates is to key on the **TL wire format**, not
on code addresses:

- a round video is an ordinary uploaded document whose `documentAttributeVideo`
  has the `round_message` flag set (`flags` bit 0);
- that attribute travels inside `inputMediaUploadedDocument`, inside
  `messages.sendMedia`, serialized as plain TL **before** MTProto encryption;
- every type above has a constructor id (CRC32 of its schema line). These ids
  change only when Telegram changes the type itself, independently of compiler
  version or code layout, so a transform keyed on them keeps working across
  rebuilds.

Setting `round_message` is a single in-place bit flip; the buffer length does
not change. That makes "send video as round" the cheapest and most robust piece
to do this way. The self-destruct timer (`ttl_seconds`) is a later step: it
appends an int, which means resizing the request buffer, so it is deliberately
out of scope for v1.

### Scope limit found in the source

For a **forwarded** (already uploaded) video, Telegram sends
`inputMediaDocument`, which references the document by id and carries **no
attribute vector** — the attributes live on the server. So round cannot be set
by a flag flip there; it would require re-downloading and re-uploading, as the
userscript does. v1 therefore targets the **upload** path: a video you send from
disk goes out as a video message.

## Layout

- `core/` — the transform and its tests. `round_rewrite.{h,cpp}` finds the
  uploaded document in a request body and sets `round_message`; `tl.h` is a
  minimal TL reader/builder; `tests.cpp` has unit tests plus a fuzz pass. This
  is fully verified offline, no injection and no account needed.
- `cli/roundtool.cpp` — applies the transform to a raw request body on disk, to
  validate the core against a real capture.
- `dll/round_dll.cpp` — injectable scaffold: it scans the loaded Telegram module
  for the send seam by byte signature, logs it, and toggles an "armed" state
  with Ctrl+Alt+R. The one machine-specific step, installing the detour at the
  located address, is marked `INTEGRATION POINT` in the source.

## Build

```sh
cd desktop
./build.sh        # builds + runs core tests, builds cli and the dll scaffold
```

Needs a C++17 compiler; tested with MinGW-w64 g++.

## Status

- [x] Core transform + unit/fuzz tests (round_message, upload path)
- [x] CLI to run the transform on a captured request body
- [x] DLL scaffold: module signature scan, hotkey toggle, core integrated
- [ ] The detour at the send seam (needs the signature for a specific
      Telegram.exe and a trampoline such as MinHook)
- [ ] Injection/loading of the DLL
- [ ] `ttl_seconds` (view once / timer), which requires growing the buffer

## Finding the seam (the one binary-specific step)

The detour must sit where the serialized request body is still plaintext TL,
i.e. inside Telegram's own MTProto session code just before encryption — an
internal, non-exported function, so it is located by a byte signature rather
than an address. Put an IDA-style signature (e.g. `48 8B ?? ?? ?? ?? ?? 48 85`)
in `tgvo_pattern.txt` next to the DLL; the scaffold scans for it and logs the
match to `tgvo.log`. Once the address resolves, wire a trampoline there whose
callback passes the body to `OnOutgoingRequest`.

## Caveats and risk

- Non-square videos are sent as-is: some Telegram apps may crop or stretch the
  circle. No conversion is done (same trade-off the userscript settled on).
- Modifying the client is against Telegram's terms and can in principle risk the
  account; antivirus software often flags DLL injection. Your client, your call.
