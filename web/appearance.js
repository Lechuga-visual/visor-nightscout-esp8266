(() => {
  'use strict';
  const presets = ['#74edbc','#ff675e','#699bff','#f69bd1','#72d9f5'];
  const isColor = value => typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value);
  function luminance(color) {
    const rgb = parseInt(color.slice(1),16);
    const channels = [Math.floor(((rgb >> 19) & 31) * 255 / 31),Math.floor(((rgb >> 10) & 63) * 255 / 63),Math.floor(((rgb >> 3) & 31) * 255 / 31)];
    const linear = channels.map(channel => { const x = channel / 255; return x <= .04045 ? x / 12.92 : ((x + .055) / 1.055) ** 2.4; });
    return .2126 * linear[0] + .7152 * linear[1] + .0722 * linear[2];
  }
  function contrast(first, second) {
    const a=luminance(first),b=luminance(second);
    return (Math.max(a,b)+.05)/(Math.min(a,b)+.05);
  }
  function readable(colors) {
    if (![colors.accent,colors.background,colors.number].every(isColor)) return false;
    return contrast(colors.number,colors.background)>=4.5 && contrast(colors.accent,colors.background)>=4.5 &&
      contrast('#d3e0dd',colors.background)>=4.5 && contrast('#9aabb0',colors.background)>=3 &&
      contrast('#ff828b',colors.background)>=3 && contrast('#ffbd73',colors.background)>=3;
  }
  function apply(colors, dial) {
    if (!colors || ![colors.accent,colors.background,colors.number].every(isColor)) return;
    document.documentElement.style.setProperty('--accent',colors.accent);
    document.documentElement.style.setProperty('--button-ink',colors.background);
    if (dial) {
      dial.style.setProperty('--theme-accent',colors.accent);
      dial.style.setProperty('--theme-number',colors.number);
      dial.style.setProperty('--dial-background',colors.background);
    }
  }
  window.GlucoseAppearance={apply,readable};
  const form=document.getElementById('appearance-form');
  if (!form) return;
  const fields=form.elements, preview=document.getElementById('appearance-preview');
  function update() {
    const custom=fields.theme.value==='5';
    const colors=custom ? {accent:fields.accent.value,background:fields.background.value,number:fields.number.value} :
      {accent:presets[Number(fields.theme.value)] || presets[0],background:'#0b1114',number:presets[Number(fields.theme.value)] || presets[0]};
    for (const name of ['accent','background','number']) fields[name].disabled=!custom;
    preview.style.setProperty('--preview-accent',colors.accent);
    preview.style.setProperty('--preview-background',colors.background);
    preview.style.setProperty('--preview-number',colors.number);
    apply(colors);
    const valid=readable(colors);
    document.getElementById('save-appearance').disabled=!valid;
    document.getElementById('contrast-feedback').textContent=valid ? 'Colores legibles. Los avisos mantienen sus colores.' : 'Aumenta el contraste: elige un fondo oscuro y colores más claros para el número y la flecha.';
  }
  form.addEventListener('input',update);
  form.addEventListener('change',update);
  form.addEventListener('submit',event=>{update();if(document.getElementById('save-appearance').disabled)event.preventDefault();});
  document.getElementById('reset-theme').addEventListener('click',()=>{fields.theme.value='0';update();});
  update();
})();
