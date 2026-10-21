(() => {
  'use strict';
  const form = document.getElementById('night-display-form');
  if (!form) return;
  const field = name => form.elements.namedItem(name);
  const feedback = document.getElementById('night-feedback');
  const preview = document.getElementById('night-preview');
  const save = document.getElementById('night-save');
  let timer;
  function labels() {
    document.getElementById('night-day-value').textContent = `${field('day').value} %`;
    document.getElementById('night-level-value').textContent = `${field('night').value} %`;
    field('end').setCustomValidity(field('start').value === field('end').value
      ? 'El inicio y el fin deben ser distintos.' : '');
  }
  form.addEventListener('input', labels);
  labels();
  form.addEventListener('submit', async event => {
    if (!form.reportValidity()) { event.preventDefault(); return; }
    if (event.submitter !== preview) return; // Native save keeps authentication/CSRF and the redirect.
    event.preventDefault();
    if (preview.disabled) return;
    clearTimeout(timer);
    preview.disabled = true; save.disabled = true;
    feedback.textContent = 'Aplicando prueba en la pantalla…';
    const body = new URLSearchParams(new FormData(form));
    body.set('preview', '1'); body.set('quick', '1');
    try {
      const response = await fetch(form.action, {method:'POST', body, credentials:'same-origin', cache:'no-store'});
      if (!response.ok) throw new Error(response.status === 403
        ? 'El formulario caducó. Recarga Ajustes antes de volver a probar.'
        : 'No se pudo iniciar la prueba. Revisa los campos o recarga Ajustes.');
      const result = await response.json();
      if (!result.ok) throw new Error('No se pudo confirmar la prueba.');
      feedback.textContent = `Prueba de noche al ${field('night').value} % durante 5 segundos. No se ha guardado.`;
      timer = setTimeout(() => { feedback.textContent = 'Prueba terminada. La pantalla vuelve al horario guardado.'; }, 5000);
    } catch (error) {
      feedback.textContent = error instanceof TypeError
        ? 'Sin conexión con el Wemos. Una prueba iniciada se restaura sola; no se guardan cambios.' : error.message;
    } finally { preview.disabled = false; save.disabled = false; }
  });
})();
