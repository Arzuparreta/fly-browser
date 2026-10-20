// Fly User Content: copies text sent by the worker (Share menu).
'use strict';
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (!message || message.type !== 'fly-copy' || sender.id !== chrome.runtime.id) return;
  const area = document.getElementById('text');
  area.value = String(message.text);
  area.select();
  respond({ok: document.execCommand('copy')});
});
