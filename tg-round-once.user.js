// ==UserScript==
// @name         Telegram Web K: View Once Video Messages
// @namespace    tg-round-once
// @version      1.0.0
// @description  Adds "Send as View Once" to the send button menu when forwarding a video message
// @homepageURL  https://github.com/DverkaSK/tg-round-once
// @updateURL    https://raw.githubusercontent.com/DverkaSK/tg-round-once/main/tg-round-once.user.js
// @downloadURL  https://raw.githubusercontent.com/DverkaSK/tg-round-once/main/tg-round-once.user.js
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
      z-index: 10000;
      max-width: 90vw;
      padding: 10px 16px;
      border-radius: 10px;
      background: rgba(0, 0, 0, .85);
      color: #fff;
      font-size: 14px;
    }
  `;

  const ICON = `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
      <path d="M12 3a9 9 0 0 1 0 18" />
      <path d="M12 21a9 9 0 0 1 0-18" stroke-dasharray="2.2 2.9" />
      <path d="M10.2 9.3 12.6 8v8.2" stroke-width="2" stroke-linejoin="round" />
    </svg>
  `;

  const LABELS = {
    ru: 'Отправить для однократного просмотра',
    en: 'Send as View Once'
  };

  document.head.appendChild(Object.assign(document.createElement('style'), {textContent: css}));

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

  function roundDoc(message) {
    const doc = message && message.media && message.media.document;
    return doc && doc.type === 'round' && doc.access_hash ? doc : null;
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

  async function loadDoc(source) {
    const message = cachedMessage(source) ||
      await managers().appMessagesManager.getMessageByPeer(source.peerId, source.mid);
    return roundDoc(message);
  }

  async function reloadDoc(source) {
    const [message] = await managers().appMessagesManager.reloadMessages(source.peerId, [source.mid], true);
    const doc = roundDoc(message);
    if(!doc) throw new Error('video message not found');
    return doc;
  }

  function isForwardingRound() {
    const source = forwardedMessage(currentChat());
    return !!(source && roundDoc(cachedMessage(source)));
  }

  function sendRound(peer, doc) {
    return managers().apiManager.invokeApi('messages.sendMedia', {
      peer,
      media: {
        _: 'inputMediaDocument',
        id: {
          _: 'inputDocument',
          id: doc.id,
          access_hash: doc.access_hash,
          file_reference: doc.file_reference
        },
        ttl_seconds: VIEW_ONCE_TTL,
        pFlags: {}
      },
      message: '',
      random_id: randomLong()
    });
  }

  function closeMenus() {
    ['mousedown', 'click'].forEach((type) => {
      document.body.dispatchEvent(new MouseEvent(type, {bubbles: true}));
    });
  }

  async function sendOnce(item) {
    const chat = currentChat();
    const source = forwardedMessage(chat);
    if(!chat || !chat.peerId || !source || busy) return;

    busy = true;
    item.classList.add('is-busy');
    try {
      const doc = await loadDoc(source);
      if(!doc) throw new Error('video message not found');
      closeMenus();

      const peer = await managers().appPeersManager.getInputPeerById(chat.peerId);
      let updates;
      try {
        updates = await sendRound(peer, doc);
      } catch(err) {
        if(err.type !== 'FILE_REFERENCE_EXPIRED' && err.type !== 'FILE_REFERENCE_INVALID') throw err;
        updates = await sendRound(peer, await reloadDoc(source));
      }

      managers().apiUpdatesManager.processUpdateMessage(updates);
      chat.input.clearHelper();
    } catch(err) {
      console.error('[tg-round-once]', err);
      toast('Не удалось отправить кружок: ' + (err.type || err.message || err));
    } finally {
      busy = false;
      item.classList.remove('is-busy');
    }
  }

  function addMenuItem(menu) {
    const template = menu.querySelector('.btn-menu-item');
    if(!template) return;

    const item = template.cloneNode(true);
    item.classList.add('tro-item');
    item.classList.remove('hide', 'danger');

    const icon = document.createElement('span');
    icon.className = 'btn-menu-item-icon tro-icon';
    icon.innerHTML = ICON;
    const templateIcon = item.querySelector('.btn-menu-item-icon');
    if(templateIcon) templateIcon.replaceWith(icon);
    else item.prepend(icon);

    const text = item.querySelector('.btn-menu-item-text');
    if(text) text.textContent = /[а-яё]/i.test(template.textContent) ? LABELS.ru : LABELS.en;

    item.addEventListener('click', (e) => {
      e.preventDefault();
      e.stopPropagation();
      sendOnce(item);
    });

    template.before(item);
  }

  function onMenuAdded(node) {
    if(node.nodeType !== 1) return;

    const menu = node.matches('.btn-menu.menu-send') ? node : node.querySelector('.btn-menu.menu-send');
    if(!menu || menu.querySelector('.tro-item')) return;

    try {
      if(isForwardingRound()) addMenuItem(menu);
    } catch(err) {
      console.error('[tg-round-once]', err);
    }
  }

  new MutationObserver((mutations) => {
    mutations.forEach((mutation) => mutation.addedNodes.forEach(onMenuAdded));
  }).observe(document.body, {childList: true, subtree: true});
})();
