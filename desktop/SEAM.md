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

## Risk

Injecting into / debugging the official client is against Telegram's terms and
may be flagged by antivirus or client integrity checks. Do this on your own
account at your own risk.
