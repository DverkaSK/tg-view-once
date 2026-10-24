# tg-view-once

Userscript for [Telegram Web K](https://web.telegram.org/k/) that lets you forward video messages as **view once**, photos or videos with a **self-destruct timer**, and send any video as a **video message** (round video), optionally view once.

Telegram only offers these options for media you send yourself. This script adds them to forwarded media too: it is sent from your name, without the "Forwarded from" header.

## Installation

[![Install](https://img.shields.io/badge/Install-userscript-8774e1?style=for-the-badge)](https://raw.githubusercontent.com/DverkaSK/tg-view-once/main/tg-view-once.user.js)

1. Install [Tampermonkey](https://www.tampermonkey.net/) or [Violentmonkey](https://violentmonkey.github.io/).
2. Click the **Install** button above and confirm the installation.
3. Open [web.telegram.org/k](https://web.telegram.org/k/). Version A is not supported.

In Chrome and Edge you also need to turn on **Allow user scripts** in the extension details.

## Usage

1. Forward a single video message, photo or video to a chat as usual.
2. Right-click the send button.
3. For a video message, choose **Send as View Once**.
4. For a photo or video, choose **Send with Timer…**, pick view once or a timer from 3 seconds to 1 minute, and press **Send**.

To send a video as a video message:

1. Forward a single video, or attach one video from your computer.
2. Right-click the send button in the chat or in the attachment window.
3. Choose **Send as Video Message…**, pick view once or no timer, and press **Send**.

## How it works

The script calls `messages.sendMedia` through Telegram Web's own API client, passing the existing photo or document with `ttl_seconds` set to the chosen timer. The file is not downloaded or uploaded again. If the file reference has expired, the message is reloaded and the request is retried.

Telegram cannot turn an existing video into a video message, so the script uploads the video file again as is, marked as a round video, and sends it with the chosen timer. A forwarded video is downloaded first.

## Limitations

- Works only in Telegram Web K.
- Self-destructing photos and videos are available only in private chats.
- Albums are not supported, forward one item at a time.
- Relies on Telegram Web internals, so it may break after a Telegram Web update.
- Video messages that were sent to you as view once cannot be forwarded.
- Videos are sent as video messages without conversion: a video that is not square may look cropped or stretched in some Telegram apps, and Telegram may reject videos longer than 60 seconds.

## License

[MIT](LICENSE)
