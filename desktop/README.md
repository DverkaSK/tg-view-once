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
  uploaded document in a request body and sets `round_message` in place (raw
  pointer and `std::vector` overloads); `tl.h` is a minimal TL reader/builder;
  `tests.cpp` has unit tests plus a fuzz pass. Verified offline, no injection
  and no account needed.
- `cli/roundtool.cpp` — applies the transform to a raw request body on disk, to
  validate the core against a real capture.
- `dll/round_dll.cpp` — injectable DLL: scans the loaded Telegram module for the
  send seam by byte signature (from `tgvo_pattern.txt`), installs a detour there
  with MinHook that hands the request body to the core, and toggles an "armed"
  state with Ctrl+Alt+R. The `SeamFn`/`ExtractBody` shape is the only thing to
  match per Telegram build once the seam is identified.
- `dll/hook_selftest.cpp` — in-process proof of the whole engine: hooks a local
  stand-in for the seam with MinHook and checks the detour edits the buffer in
  place and the original still runs (trampoline intact). Runs offline.
- `dll/vendor/minhook/` — vendored MinHook (MIT), the x64 hook/trampoline engine.
- `tools/inject.cpp` — DLL injector (CreateRemoteThread + LoadLibraryA).
- `SEAM.md` — how to find the send seam signature with a runtime debugger.

## Build

```sh
cd desktop
./build.sh        # builds + runs core tests, builds cli and the dll scaffold
```

Needs a C++17 compiler; tested with MinGW-w64 g++.

## Status

- [x] Core transform + unit/fuzz tests (round_message, upload path)
- [x] CLI to run the transform on a captured request body
- [x] Hook engine proven end-to-end offline (MinHook trampoline + core,
      `hook_selftest`: detour edits in place, original still runs)
- [x] DLL: module signature scan, MinHook detour install, hotkey toggle
- [x] Injector (CreateRemoteThread + LoadLibraryA), builds
- [ ] The send-seam **signature** for a specific Telegram.exe — a runtime
      debugging step, see `SEAM.md`. Everything else is wired and waiting on it.
- [ ] `ttl_seconds` (view once / timer), which requires growing the buffer

Telegram Desktop release builds statically link Qt, OpenSSL and ffmpeg, so there
is no exported crypto function to hook near encryption; the seam is an internal
function found by the recipe in `SEAM.md`.

## Caveats and risk

- Non-square videos are sent as-is: some Telegram apps may crop or stretch the
  circle. No conversion is done (same trade-off the userscript settled on).
- Modifying the client is against Telegram's terms and can in principle risk the
  account; antivirus software often flags DLL injection. Your client, your call.
