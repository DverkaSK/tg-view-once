// ==UserScript==
// @name         Telegram Web: View Once Forward
// @namespace    tg-view-once
// @version      1.2.0
// @description  Forward video messages as view once and photos or videos with a self-destruct timer
// @homepageURL  https://github.com/DverkaSK/tg-view-once
// @updateURL    https://raw.githubusercontent.com/DverkaSK/tg-view-once/main/tg-view-once.user.js
// @downloadURL  https://raw.githubusercontent.com/DverkaSK/tg-view-once/main/tg-view-once.user.js
// @match        https://web.telegram.org/k/*
// @grant        none
// @run-at       document-idle
// ==/UserScript==

(function() {
  'use strict';

  const VIEW_ONCE_TTL = 0x7FFFFFFF;

  const css = `
    .tro-item .tro-icon {
      display: inline-flex;
      align-items: center;
      justify-content: center;
    }
    .tro-item .tro-icon svg {
      width: 24px;
      height: 24px;
    }
    .tro-item.is-busy {
      opacity: .5;
      pointer-events: none;
    }
    .tro-toast {
      position: fixed;
      left: 50%;
      top: 24px;
      transform: translateX(-50%);
      z-index: 10001;
      max-width: 90vw;
      padding: 10px 16px;
      border-radius: 10px;
      background: rgba(0, 0, 0, .85);
      color: #fff;
      font-size: 14px;
    }
    .tro-overlay {
      position: fixed;
      inset: 0;
      z-index: 10000;
      display: flex;
      align-items: center;
      justify-content: center;
      background: rgba(0, 0, 0, .5);
      animation: tro-fade .15s ease-out;
    }
    .tro-popup {
      width: 320px;
      max-width: calc(100vw - 32px);
      padding: 16px 8px 8px;
      border-radius: 12px;
      background: var(--surface-color, #212121);
      color: var(--primary-text-color, #fff);
      box-shadow: 0 4px 24px rgba(0, 0, 0, .35);
      animation: tro-pop .15s ease-out;
    }
    .tro-popup-title {
      padding: 0 16px 12px;
      font-size: 20px;
      font-weight: 500;
    }
    .tro-option {
      display: flex;
      align-items: center;
      gap: 16px;
      width: 100%;
      padding: 12px 16px;
      border: 0;
      border-radius: 8px;
      background: none;
      color: inherit;
      font: inherit;
      font-size: 16px;
      text-align: left;
      cursor: pointer;
    }
    .tro-option:hover {
      background: var(--light-secondary-text-color, rgba(255, 255, 255, .06));
    }
    .tro-radio {
      flex: none;
      width: 20px;
      height: 20px;
      border: 2px solid var(--secondary-text-color, #aaa);
      border-radius: 50%;
      transition: border-color .15s, border-width .15s;
    }
    .tro-option.is-selected .tro-radio {
      border-color: var(--primary-color, #8774e1);
      border-width: 6px;
    }
    .tro-buttons {
      display: flex;
      justify-content: flex-end;
      gap: 4px;
      padding-top: 8px;
    }
    .tro-button {
      padding: 10px 16px;
      border: 0;
      border-radius: 8px;
      background: none;
      color: var(--primary-color, #8774e1);
      font: inherit;
      font-size: 15px;
      font-weight: 500;
      text-transform: uppercase;
      cursor: pointer;
    }
    .tro-button:hover {
      background: var(--light-primary-color, rgba(135, 116, 225, .1));
    }
    @keyframes tro-fade {
      from { opacity: 0; }
    }
    @keyframes tro-pop {
      from { opacity: 0; transform: scale(.95); }
    }
  `;

  const ICON_ONCE = `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
      <path d="M12 3a9 9 0 0 1 0 18" />
      <path d="M12 21a9 9 0 0 1 0-18" stroke-dasharray="2.2 2.9" />
      <path d="M10.2 9.3 12.6 8v8.2" stroke-width="2" stroke-linejoin="round" />
    </svg>
  `;

  const ICON_TIMER = `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
      <circle cx="12" cy="13" r="8" />
      <path d="M12 9v4l2.5 2.5" stroke-linejoin="round" />
      <path d="M10 2.5h4" />
    </svg>
  `;

  const STRINGS = {
    ru: {
      sendOnce: 'Отправить для однократного просмотра',
      sendTimer: 'Отправить с таймером…',
      title: 'Таймер самоуничтожения',
      cancel: 'Отмена',
      send: 'Отправить',
      failed: 'Не удалось отправить',
      timers: ['Один просмотр', '3 секунды', '10 секунд', '30 секунд', '1 минута']
    },
    en: {
      sendOnce: 'Send as View Once',
      sendTimer: 'Send with Timer…',
      title: 'Self-Destruct Timer',
      cancel: 'Cancel',
      send: 'Send',
      failed: 'Failed to send',
      timers: ['View once', '3 seconds', '10 seconds', '30 seconds', '1 minute']
    }
  };

  const TIMERS = [VIEW_ONCE_TTL, 3, 10, 30, 60];

  document.head.appendChild(Object.assign(document.createElement('style'), {textContent: css}));

  let strings = STRINGS.en;
  let busy = false;

  function toast(text, ms = 4000) {
    const el = Object.assign(document.createElement('div'), {className: 'tro-toast', textContent: text});
    document.body.appendChild(el);
    setTimeout(() => el.remove(), ms);
  }

  function randomLong() {
    const parts = new Uint32Array(2);
    crypto.getRandomValues(parts);
    return ((BigInt(parts[0] & 0x7FFFFFFF) << 32n) | BigInt(parts[1])).toString();
  }

  function managers() {
    return window.rootScope && window.rootScope.managers;
  }

  function currentChat() {
    return window.appImManager && window.appImManager.chat;
  }

  function isPrivateChat(chat) {
    return chat && chat.peerId > 0 && chat.peerId !== window.rootScope.myId;
  }

  function extractMedia(message) {
    const media = message && message.media;
    if(!media) return null;

    if(media._ === 'messageMediaPhoto' && media.photo && media.photo.access_hash) {
      return {kind: 'photo', file: media.photo};
    }

    const doc = media.document;
    if(media._ === 'messageMediaDocument' && doc && doc.access_hash) {
      if(doc.type === 'round') return {kind: 'round', file: doc};
      if(doc.type === 'video') return {kind: 'video', file: doc};
    }

    return null;
  }

  function forwardedMessage(chat) {
    const forwarding = chat && chat.input && chat.input.forwarding;
    if(!forwarding) return null;

    const entries = Object.entries(forwarding);
    if(entries.length !== 1 || entries[0][1].length !== 1) return null;

    return {peerId: Number(entries[0][0]), mid: entries[0][1][0]};
  }

  function cachedMessage(source) {
    return window.apiManagerProxy && window.apiManagerProxy.getMessageByPeer(source.peerId, source.mid);
  }

  async function loadMedia(source) {
    const message = cachedMessage(source) ||
      await managers().appMessagesManager.getMessageByPeer(source.peerId, source.mid);
    return extractMedia(message);
  }

  async function reloadMedia(source) {
    const [message] = await managers().appMessagesManager.reloadMessages(source.peerId, [source.mid], true);
    const media = extractMedia(message);
    if(!media) throw new Error('media not found');
    return media;
  }

  function forwardedMediaSync(chat) {
    const source = forwardedMessage(chat);
    return source ? extractMedia(cachedMessage(source)) : null;
  }

  function inputMedia(media, ttl) {
    const id = {
      id: media.file.id,
      access_hash: media.file.access_hash,
      file_reference: media.file.file_reference
    };

    if(media.kind === 'photo') {
      return {_: 'inputMediaPhoto', id: {_: 'inputPhoto', ...id}, ttl_seconds: ttl, pFlags: {}};
    }

    return {_: 'inputMediaDocument', id: {_: 'inputDocument', ...id}, ttl_seconds: ttl, pFlags: {}};
  }

  function sendMedia(peer, media, ttl) {
    return managers().apiManager.invokeApi('messages.sendMedia', {
      peer,
      media: inputMedia(media, ttl),
      message: '',
      random_id: randomLong()
    });
  }

  function closeMenus() {
    ['mousedown', 'click'].forEach((type) => {
      document.body.dispatchEvent(new MouseEvent(type, {bubbles: true}));
    });
  }

  async function sendForwarded(ttl, item) {
    const chat = currentChat();
    const source = forwardedMessage(chat);
    if(!chat || !chat.peerId || !source || busy) return;

    busy = true;
    if(item) item.classList.add('is-busy');
    try {
      const media = await loadMedia(source);
      if(!media) throw new Error('media not found');

      const peer = await managers().appPeersManager.getInputPeerById(chat.peerId);
      let updates;
      try {
        updates = await sendMedia(peer, media, ttl);
      } catch(err) {
        if(err.type !== 'FILE_REFERENCE_EXPIRED' && err.type !== 'FILE_REFERENCE_INVALID') throw err;
        updates = await sendMedia(peer, await reloadMedia(source), ttl);
      }

      managers().apiUpdatesManager.processUpdateMessage(updates);
      chat.input.clearHelper();
    } catch(err) {
      console.error('[tg-view-once]', err);
      toast(strings.failed + ': ' + (err.type || err.message || err));
    } finally {
      busy = false;
      if(item) item.classList.remove('is-busy');
    }
  }

  function openTimerPopup() {
    let selected = 0;

    const overlay = document.createElement('div');
    overlay.className = 'tro-overlay';

    const popup = document.createElement('div');
    popup.className = 'tro-popup';
    overlay.appendChild(popup);

    const title = Object.assign(document.createElement('div'), {className: 'tro-popup-title', textContent: strings.title});
    popup.appendChild(title);

    const options = TIMERS.map((ttl, index) => {
      const option = document.createElement('button');
      option.type = 'button';
      option.className = 'tro-option';
      option.innerHTML = '<span class="tro-radio"></span><span></span>';
      option.lastChild.textContent = strings.timers[index];
      option.addEventListener('click', () => select(index));
      option.addEventListener('dblclick', send);
      popup.appendChild(option);
      return option;
    });

    const buttons = document.createElement('div');
    buttons.className = 'tro-buttons';
    const cancelButton = Object.assign(document.createElement('button'), {className: 'tro-button', type: 'button', textContent: strings.cancel});
    const sendButton = Object.assign(document.createElement('button'), {className: 'tro-button', type: 'button', textContent: strings.send});
    buttons.append(cancelButton, sendButton);
    popup.appendChild(buttons);

    function select(index) {
      selected = index;
      options.forEach((option, i) => option.classList.toggle('is-selected', i === selected));
    }

    function close() {
      overlay.remove();
      document.removeEventListener('keydown', onKeyDown, true);
    }

    function send() {
      close();
      sendForwarded(TIMERS[selected]);
    }

    function onKeyDown(e) {
      if(e.key === 'Escape') close();
      else if(e.key === 'Enter') send();
      else return;
      e.preventDefault();
      e.stopImmediatePropagation();
    }

    cancelButton.addEventListener('click', close);
    sendButton.addEventListener('click', send);
    overlay.addEventListener('mousedown', (e) => {
      if(e.target === overlay) close();
    });
    document.addEventListener('keydown', onKeyDown, true);

    select(0);
    document.body.appendChild(overlay);
    sendButton.focus();
  }

  function addMenuItem(menu, media) {
    const template = menu.querySelector('.btn-menu-item');
    if(!template) return;

    strings = /[а-яё]/i.test(template.textContent) ? STRINGS.ru : STRINGS.en;
    const isRound = media.kind === 'round';

    const item = template.cloneNode(true);
    item.classList.add('tro-item');
    item.classList.remove('hide', 'danger');

    const icon = document.createElement('span');
    icon.className = 'btn-menu-item-icon tro-icon';
    icon.innerHTML = isRound ? ICON_ONCE : ICON_TIMER;
    const templateIcon = item.querySelector('.btn-menu-item-icon');
    if(templateIcon) templateIcon.replaceWith(icon);
    else item.prepend(icon);

    const text = item.querySelector('.btn-menu-item-text');
    if(text) text.textContent = isRound ? strings.sendOnce : strings.sendTimer;

    item.addEventListener('click', (e) => {
      e.preventDefault();
      e.stopPropagation();
      closeMenus();
      if(isRound) sendForwarded(VIEW_ONCE_TTL, item);
      else openTimerPopup();
    });

    template.before(item);
  }

  function onNodeAdded(node) {
    if(node.nodeType !== 1) return;

    const menu = node.matches('.btn-menu.menu-send') ? node : node.querySelector('.btn-menu.menu-send');
    if(!menu || menu.querySelector('.tro-item')) return;

    try {
      const chat = currentChat();
      const media = forwardedMediaSync(chat);
      if(!media) return;
      if(media.kind !== 'round' && !isPrivateChat(chat)) return;
      addMenuItem(menu, media);
    } catch(err) {
      console.error('[tg-view-once]', err);
    }
  }

  new MutationObserver((mutations) => {
    mutations.forEach((mutation) => mutation.addedNodes.forEach(onNodeAdded));
  }).observe(document.body, {childList: true, subtree: true});
})();
