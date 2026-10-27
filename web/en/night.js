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
      ? "Start and end must differ." : '');
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
    feedback.textContent = "Applying preview on the display…";
    const body = new URLSearchParams(new FormData(form));
    body.set('preview', '1'); body.set('quick', '1');
    try {
      const response = await fetch(form.action, {method:'POST', body, credentials:'same-origin', cache:'no-store'});
      if (!response.ok) throw new Error(response.status === 403
        ? "The form expired. Reload Settings before trying again."
        : "Could not start the preview. Check the fields or reload Settings.");
      const result = await response.json();
      if (!result.ok) throw new Error("Could not confirm the preview.");
      feedback.textContent = `Night preview at ${field('night').value} % for 5 seconds. Not saved.`;
      timer = setTimeout(() => { feedback.textContent = "Preview finished. The display returns to the saved schedule."; }, 5000);
    } catch (error) {
      feedback.textContent = error instanceof TypeError
        ? "Disconnected from Wemos. A started preview restores itself; no changes are saved." : error.message;
    } finally { preview.disabled = false; save.disabled = false; }
  });
})();
