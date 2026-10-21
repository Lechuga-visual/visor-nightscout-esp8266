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
    $('setup-counter').textContent = `PASO ${index + 1} DE 3`;
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
        invalid('ssid', 'Escribe el nombre exacto de tu Wi-Fi, de hasta 32 bytes.');
      const password = field('wifi_password').value, length = bytes(password);
      if (length && !(length >= 8 && length <= 63) && !/^[a-f0-9]{64}$/i.test(password))
        invalid('wifi_password', 'Usa entre 8 y 63 bytes, una clave hexadecimal de 64 caracteres o vacío para una red abierta.');
    } else if (index === 1) {
      const url = field('url').value.trim(), name = field('patient_name').value.trim();
      let address;
      try { address = new URL(url); } catch (_) { /* Report the field below. */ }
      if (!address || !/^https?:\/\/[^/]/.test(url) || bytes(url) > 200 || /[\s@?#\\]/.test(url) || address.username || address.password)
        invalid('url', 'Usa la URL base http:// o https:// de tu Nightscout, sin token, API secret ni /api/v1.');
      if (Array.from(name).length > 24 || /[\u0000-\u001f\u007f-\u009f]/.test(name))
        invalid('patient_name', 'Usa hasta 24 caracteres, sin saltos de línea ni controles.');
      const token = field('token').value.trim(), pem = field('custom_ca').value.trim();
      if (token && (!url.startsWith('https://') || /[^\x21-\x7e]/.test(token) || token.length > 256))
        invalid('token', 'El token requiere HTTPS y hasta 256 caracteres sin espacios.');
      if (field('clear_token').checked && token)
        invalid('token', 'Elige borrar el token guardado o escribir uno nuevo.');
      if (bytes(pem) > 3072 || /PRIVATE KEY/.test(pem))
        invalid('custom_ca', 'Usa un certificado CA o una clave pública PEM de hasta 3072 bytes. Nunca una clave privada.');
      if (field('clear_ca').checked && pem)
        invalid('custom_ca', 'Elige borrar el PEM guardado o escribir uno nuevo.');
    } else {
      const username = field('web_username').value.trim();
      if (!username || bytes(username) > 32 || /[\u0000-\u001f\u007f:]/.test(username))
        invalid('web_username', 'Usa un usuario de hasta 32 bytes, sin dos puntos ni controles.');
      const length = bytes(field('web_password').value);
      if (length < 12 || length > 64)
        invalid('web_password', 'Usa una contraseña de administración de entre 12 y 64 bytes.');
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
    $('setup-save').textContent = 'Guardando…';
    $('setup-error').hidden = true;
    try {
      const response = await fetch(form.action, {
        method: 'POST', credentials: 'same-origin', cache: 'no-store',
        body: new URLSearchParams(new FormData(form))
      });
      if (!response.ok) {
        if (response.status === 403) throw new Error('El formulario caducó o el Wemos se reinició. Recarga esta página antes de guardar de nuevo.');
        if (response.status === 401) throw new Error('La sesión de administración terminó. Recarga la página e inicia sesión.');
        const detail = await response.text();
        throw new Error(detail && !detail.includes('<') ? detail.slice(0, 500) : 'No se pudo guardar. Revisa los campos e intenta nuevamente.');
      }
      form.reset();
      form.hidden = true;
      $('setup-success').hidden = false;
      $('setup-success').setAttribute('tabindex', '-1');
      $('setup-success').focus();
    } catch (error) {
      $('setup-error').textContent = error instanceof TypeError
        ? 'No se pudo confirmar el guardado. Revisa si el Wemos se reinició y muestra una nueva IP. Si sigue en configuración, puedes reintentar.'
        : error.message;
      $('setup-error').hidden = false;
      $('setup-error').scrollIntoView({block:'center'});
      saving = false;
      $('setup-save').disabled = false;
      $('setup-back').disabled = false;
      $('setup-save').textContent = 'Guardar y reiniciar';
    }
  });
  form.noValidate = true;
  form.querySelector('.setup-progress').hidden = false;
  form.querySelector('.setup-navigation').hidden = false;
  $('setup-review').hidden = false;
  show(0, false);
})();
