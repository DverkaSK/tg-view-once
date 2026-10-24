# Finding the send seam

The detour has to run where the request is still **plaintext TL**, i.e. after
Telegram serializes `messages.sendMedia` but before it AES-IGE-encrypts it.
Release builds are stripped and statically linked, so this function has no name
and no export — it is located empirically with a debugger, then captured as a
byte signature for `tgvo_pattern.txt`. You only ever do this per Telegram build.

Marker on the wire: `messages.sendMedia` begins with its constructor id
`0x0330e77f`, which in memory reads as the bytes `7f e7 30 03` (little-endian).
The serialized body also contains `inputMediaUploadedDocument` = `30 93 7c 03`
and `documentAttributeVideo` = `48 7c c5 43`.

## Recipe (x64dbg)

1. Install [x64dbg](https://x64dbg.com/). Attach it to the running `Telegram.exe`
   (x64), or launch Telegram from it.
2. Reproduce once normally to prime: attach, then prepare to send a short video
   from disk to a Saved Messages chat.
3. Set a memory-write or search trigger on the plaintext:
   - Click send, then in x64dbg use **Find Pattern** (`7F E7 30 03`) across the
     committed memory of the Telegram module/heap. You are looking for the
     plaintext request buffer that holds your `sendMedia` body.
   - Confirm it is the right buffer: nearby you should see `30 93 7c 03`
     (uploaded document) and `48 7c c5 43` (video attribute) with a 4-byte flags
     int right after it.
4. Put a hardware **read** breakpoint on the start of that buffer and send
   again. The code that breaks, reading the buffer to hand it to encryption, is
   the seam (or one call below it). Walk up the call stack to the function that
   receives the whole request buffer as an argument.
5. Note that function's first ~16 bytes of code and turn them into an IDA-style
   signature, wildcarding bytes that are relative addresses/offsets, e.g.
   `48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ??`.
6. Also note how the buffer pointer and length reach the function (which
   register/argument), and set `SeamFn` / the detour in `round_dll.cpp` to that
   calling shape. Update `OnOutgoingRequest` call site accordingly.

## Wire it up

1. Put the signature from step 5 into `tgvo_pattern.txt` next to `round.dll`.
2. Inject: `tools/inject.exe Telegram.exe dll/round.dll` (or load it however you
   prefer). `tgvo.log` next to the DLL should say `seam found at ...` and
   `hook installed`.
3. Press Ctrl+Alt+R (arms the next send), send a video from disk; it should
   arrive as a video message. The log records `armed send rewritten to
   round_message`.

## Validate the transform offline first

Before touching the live client, dump a real plaintext body to a file and run
`cli/roundtool.exe body.bin out.bin`. It should report `Modified` and change
exactly the 4-byte video flags int (`00000000` -> `01000000`). That confirms the
core matches the real wire layout of your Telegram version.

## Static leads (found by analysing one build)

These came from scanning `.text` for the constructor-id immediates and mapping
each hit to its function via the `.pdata` table (400k functions). Addresses are
**specific to one Telegram.exe build** (x64, ~250 MB, 2026-10-22) and will move
on any other build — use the method, not the numbers.

- `messages.sendMedia` (`0x0330e77f`) appears as a code immediate in only **two**
  functions:
  - `0x143cdf130` — a ~94 KB function that also contains most other ids: this is
    the generated TL **deserialize** switch, not the send path. Ignore.
  - `0x1406dc8e0` (443 bytes) — a small function with a stack canary that
    allocates a ~0x378-byte object with a vtable and writes the sendMedia id.
    This is the **request construction** site, the best starting point: set a
    breakpoint here, send a video, and step/trace down to where the finished
    request buffer is handed to the MTProto session and then encrypted. The
    generic encryption seam carries no id, so it is reached from here, not found
    by scanning.

Reproduce the scan on a new build with the PE+`.pdata` walk used above (parse
sections, scan `.text` for the 4-byte LE ids, bisect into the `.pdata` function
ranges).

## Runtime findings so far (tgprobe on one build)

`tools/tgprobe.cpp` is a small, safe debugger (hardware breakpoints,
`DebugSetProcessKillOnExit(FALSE)`, clean detach) used to probe the running
client. Results on the build above, sending a video to Saved Messages:

- HW-breakpoint mechanism verified: a BP on `kernel32!Sleep` fired 57× in 6s.
- Module base observed `0x7ff7b80c0000` (same across restarts this session).
- The constructor ids live **only in `.text` as immediates** (nothing in
  `.rdata`/`.data`), so serialization writes them as immediates.
- Ruled out as the outgoing serializer (did not fire on send, or wrong role):
  - `0x1406dc8e0` — writes the sendMedia id into a request descriptor
    (`[rsp+0x40]`), not the wire; never fired on send.
  - `0x143cdf130` — a ~94 KB function that fills a stack **table** of all type
    ids (`[rbp+0x80c]=video`, `[rbp+0x810]=audio`, …): a type registry, not a
    per-request serializer.
  - `0x143a23bf0` — fired on send but **reads** fields from a buffer
    (`mov ecx,[r8]; mov [rdi+0x10],ecx`): it is the deserializer of the server's
    echo of the sent message, not the outgoing path.
  - `0x143a23410` — extremely hot generic attribute helper (1078 hits; `rax`
    held `documentAttributeImageSize`'s id), not specific to the send.

Still open: the exact function that serializes the outgoing
`inputMediaUploadedDocument` / `documentAttributeVideo`. Next techniques to try:
a value-filtered breakpoint on the generic "append primitive" function (filter
the appended u32 == `0x330e77f`/`0x43c57c48`), or find the pre-encryption point
(the AES-IGE call) and edit the plaintext there with the core.

## Risk

Injecting into / debugging the official client is against Telegram's terms and
may be flagged by antivirus or client integrity checks. Do this on your own
account at your own risk.
