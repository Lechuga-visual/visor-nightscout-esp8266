(() => {
  'use strict';
  const form = document.getElementById('setup-form');
  if (!form) return;
  const $ = id => document.getElementById(id);
  const steps = Array.from(form.querySelectorAll('[data-step]'));
  const field = name => form.elements.namedItem(name);
  const bytes = value => new TextEncoder().encode(value).length;
  let current = 0, saving = false;

  function show(index, focus = true) {
    current = index;
    steps.forEach((step, i) => { step.hidden = i !== index; });
    $('setup-counter').textContent = `STEP ${index + 1} OF 3`;
    form.querySelectorAll('.setup-progress li').forEach((item, i) => {
      item.classList.toggle('complete', i <= index);
      if (i === index) item.setAttribute('aria-current', 'step');
      else item.removeAttribute('aria-current');
    });
    $('setup-next').hidden = index === 2;
    $('setup-back').hidden = index === 0;
    if (index === 2) {
      $('review-ssid').textContent = field('ssid').value;
      $('review-patient').textContent = field('patient_name').value.trim() || 'NIGHTSCOUT';
      $('review-url').textContent = field('url').value.trim();
    }
    if (focus) steps[index].querySelector('h1').focus();
  }

  function validate(index) {
    const controls = Array.from(steps[index].querySelectorAll('input,textarea'));
    controls.forEach(input => input.setCustomValidity(''));
    const invalid = (name, text) => field(name).setCustomValidity(text);
    if (index === 0) {
      if (!field('ssid').value || bytes(field('ssid').value) > 32)
        invalid('ssid', "Enter your exact Wi-Fi name, up to 32 bytes.");
      const password = field('wifi_password').value, length = bytes(password);
      if (length && !(length >= 8 && length <= 63) && !/^[a-f0-9]{64}$/i.test(password))
        invalid('wifi_password', "Use 8 to 63 bytes, a 64-character hexadecimal key, or leave blank for an open network.");
    } else if (index === 1) {
      const url = field('url').value.trim(), name = field('patient_name').value.trim();
      let address;
      try { address = new URL(url); } catch (_) { /* Report the field below. */ }
      if (!address || !/^https?:\/\/[^/]/.test(url) || bytes(url) > 200 || /[\s@?#\\]/.test(url) || address.username || address.password)
        invalid('url', "Use your Nightscout's http:// or https:// base URL, without a token, API secret or /api/v1.");
      if (Array.from(name).length > 24 || /[\u0000-\u001f\u007f-\u009f]/.test(name))
        invalid('patient_name', "Use up to 24 characters, without line breaks or control characters.");
      const token = field('token').value.trim(), pem = field('custom_ca').value.trim();
      if (token && (!url.startsWith('https://') || /[^\x21-\x7e]/.test(token) || token.length > 256))
        invalid('token', "The token requires HTTPS and up to 256 characters without spaces.");
      if (field('clear_token').checked && token)
        invalid('token', "Either clear the saved token or enter a new one.");
      if (bytes(pem) > 3072 || /PRIVATE KEY/.test(pem))
        invalid('custom_ca', "Use a CA certificate or PEM public key up to 3072 bytes. Never a private key.");
      if (field('clear_ca').checked && pem)
        invalid('custom_ca', "Either clear the saved PEM or enter a new one.");
    } else {
      const username = field('web_username').value.trim();
      if (!username || bytes(username) > 32 || /[\u0000-\u001f\u007f:]/.test(username))
        invalid('web_username', "Use a username up to 32 bytes, without colons or control characters.");
      const length = bytes(field('web_password').value);
      if (length < 12 || length > 64)
        invalid('web_password', "Use an admin password between 12 and 64 bytes.");
    }
    const first = controls.find(input => !input.validity.valid);
    if (!first) return true;
    show(index, false);
    let parent = first.parentElement;
    while (parent && parent !== form) {
      if (parent.tagName === 'DETAILS') parent.open = true;
      parent = parent.parentElement;
    }
    first.reportValidity();
    first.focus();
    return false;
  }

  function advance() {
    if (saving || !validate(current)) return;
    $('setup-error').hidden = true;
    show(Math.min(current + 1, 2));
  }
  $('setup-next').addEventListener('click', advance);
  $('setup-back').addEventListener('click', () => {
    if (saving) return;
    $('setup-error').hidden = true;
    show(Math.max(current - 1, 0));
  });
  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (saving) return;
    if (current < 2) { advance(); return; }
    for (let i = 0; i < steps.length; ++i) if (!validate(i)) return;
    saving = true;
    $('setup-save').disabled = true;
    $('setup-back').disabled = true;
    $('setup-save').textContent = "Saving…";
    $('setup-error').hidden = true;
    try {
      const response = await fetch(form.action, {
        method: 'POST', credentials: 'same-origin', cache: 'no-store',
        body: new URLSearchParams(new FormData(form))
      });
      if (!response.ok) {
        if (response.status === 403) throw new Error("The form expired or the Wemos restarted. Reload this page before saving again.");
        if (response.status === 401) throw new Error("The admin session ended. Reload the page and sign in.");
        const detail = await response.text();
        throw new Error(detail && !detail.includes('<') ? detail.slice(0, 500) : "Could not save. Check the fields and try again.");
      }
      form.reset();
      form.hidden = true;
      $('setup-success').hidden = false;
      $('setup-success').setAttribute('tabindex', '-1');
      $('setup-success').focus();
    } catch (error) {
      $('setup-error').textContent = error instanceof TypeError
        ? "Could not confirm saving. Check whether the Wemos restarted and shows a new IP. If it is still in setup mode, you can retry."
        : error.message;
      $('setup-error').hidden = false;
      $('setup-error').scrollIntoView({block:'center'});
      saving = false;
      $('setup-save').disabled = false;
      $('setup-back').disabled = false;
      $('setup-save').textContent = "Save and restart";
    }
  });
  form.noValidate = true;
  form.querySelector('.setup-progress').hidden = false;
  form.querySelector('.setup-navigation').hidden = false;
  $('setup-review').hidden = false;
  show(0, false);
})();
