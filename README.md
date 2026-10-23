# tg-round-once

Userscript for [Telegram Web K](https://web.telegram.org/k/) that lets you forward any video message (round video) as **view once**.

Telegram only offers the view once option when you record a video message yourself. This script adds it to forwarded ones too: the video message is sent from your name, without the "Forwarded from" header, and can be opened only once.

## Installation

[![Install](https://img.shields.io/badge/Install-userscript-8774e1?style=for-the-badge)](https://raw.githubusercontent.com/DverkaSK/tg-round-once/main/tg-round-once.user.js)

1. Install [Tampermonkey](https://www.tampermonkey.net/) or [Violentmonkey](https://violentmonkey.github.io/).
2. Click the **Install** button above and confirm the installation.
3. Open [web.telegram.org/k](https://web.telegram.org/k/). Version A is not supported.

In Chrome and Edge you also need to turn on **Allow user scripts** in the extension details.

## Usage

1. Forward a single video message to a chat as usual.
2. Right-click the send button.
3. Choose **Send as View Once**.

## How it works

The script calls `messages.sendMedia` through Telegram Web's own API client, passing the existing document with `ttl_seconds` set to the view once value. The file is not downloaded or uploaded again. If the file reference has expired, the message is reloaded and the request is retried.

## Limitations

- Works only in Telegram Web K.
- Relies on Telegram Web internals, so it may break after a Telegram Web update.
- Video messages that were sent to you as view once cannot be forwarded.
