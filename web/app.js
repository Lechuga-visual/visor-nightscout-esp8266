(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  let snapshot = null, receivedAt = 0, browserOnline = false, timer = 0, busy = false, modeSaving = false;
  const labels = {normal:'En intervalo',low:'Por debajo de 70',high:'Por encima de 180',old:'Lectura antigua',clock:'Hora sin sincronizar',error:'Última lectura disponible',empty:'Sin lecturas'};
  const elapsed = () => Math.max(0, Math.floor((performance.now() - receivedAt) / 1000));
  const ageText = seconds => seconds < 60 ? 'Hace menos de 1 min' : `Hace ${Math.floor(seconds / 60)} min`;
  function text(id, value) { $(id).textContent = value; }
  function move(id,x,y) { $(id).setAttribute('x',x); $(id).setAttribute('y',y); }
  function layout(mode, digits) {
    const selected = [0,1,2].includes(mode) ? mode : 0;
    const positions = [[104,115,188,108,104,132],[100,148,201,131,100,170],[120,154,155,183,102,179]][selected];
    move('glucose',positions[0],positions[1]);
    move('dial-unit',positions[4],positions[5]);
    $('glucose').style.fontSize = `${(digits>3 ? [48,70,84] : [64,94,112])[selected]}px`;
    $('history-group').style.display = selected===0 ? '' : 'none';
    $('dial').dataset.mode = String(selected);
    if (!modeSaving) $('view-mode').value=String(selected);
    text('view-scope',['Últimos 30 minutos','Número · flecha lateral','Número · flecha debajo'][selected]);
  }
  function drawTrend(symbol, mode, available) {
    const selected=[0,1,2].includes(mode)?mode:0;
    const [x,y,size]=[[188,96,22],[201,118,29],[155,173,20]][selected];
    const direction={'→':[1,0,false],'↑':[0,-1,false],'⇈':[0,-1,true],'↓':[0,1,false],'⇊':[0,1,true],'↗':[1,-1,false],'↘':[1,1,false]}[symbol];
    $('trend-vector').setAttribute('transform',`translate(${x} ${y})`);
    $('trend-vector').style.display=available?'':'none';
    if(!direction){$('trend').setAttribute('d','M-6 0H6');return;}
    const [dx0,dy0,doubled]=direction;
    const dx=dx0*(dy0?Math.trunc(size*7/10):size),dy=dy0*(dx0?Math.trunc(size*7/10):size);
    let path='';
    for(const center of doubled?[-5,5]:[0]) {
      const sx=center-Math.trunc(dx/2),sy=-Math.trunc(dy/2),ex=center+Math.trunc(dx/2),ey=Math.trunc(dy/2);
      path+=`M${sx} ${sy}L${ex} ${ey}M${ex-Math.trunc((dx+dy)/3)} ${ey-Math.trunc((dy-dx)/3)}L${ex} ${ey}L${ex-Math.trunc((dx-dy)/3)} ${ey-Math.trunc((dy+dx)/3)} `;
    }
    $('trend').setAttribute('d',path);
  }
  function drawGraph(plot) {
    const grid = $('graph-grid'), dots = $('history-dots');
    grid.replaceChildren(); dots.replaceChildren();
    let path = '';
    if (plot && plot.high > plot.low) {
      for (const value of [70,180]) {
        if (value < plot.low || value > plot.high) continue;
        const line = document.createElementNS('http://www.w3.org/2000/svg','line');
        const y = 148 + (plot.high - value) * 30 / (plot.high - plot.low);
        for (const [key,v] of Object.entries({x1:38,x2:202,y1:y,y2:y,class:'graph-grid'})) line.setAttribute(key,v);
        grid.append(line);
      }
      for (const point of plot.points || []) {
        const x = 38 + point[0] * 164 / 1000, y = 148 + point[1] * 30 / 1000;
        path += `${point[2] ? 'M':'L'}${x.toFixed(2)} ${y.toFixed(2)} `;
        const dot = document.createElementNS('http://www.w3.org/2000/svg','circle');
        for (const [key,value] of Object.entries({cx:x,cy:y,r:1.1,class:'history-dot'})) dot.setAttribute(key,value);
        dots.append(dot);
      }
    }
    $('history-path').setAttribute('d',path);
    $('graph-empty').hidden = Boolean(path);
    $('graph-empty').style.display = path ? 'none' : '';
    text('graph-empty', snapshot && !snapshot.clock_valid ? 'Hora sin sincronizar' : 'Esperando historial');
  }
  function render() {
    if (!snapshot) return;
    const s = snapshot;
    const hasReading = s.available && Number.isFinite(s.glucose);
    const age = hasReading && Number.isFinite(s.age_seconds) ? s.age_seconds + elapsed() : null;
    const old = age !== null && age >= 600;
    let tone = hasReading ? (labels[s.tone] ? s.tone : 'error') : 'empty';
    if (old) tone = 'old'; else if (!browserOnline && hasReading) tone = 'error';
    $('dial').dataset.tone = tone;
    const number = hasReading ? String(s.glucose) : '---';
    text('glucose',number);
    const mode = s.appearance && Number.isInteger(s.appearance.mode) ? s.appearance.mode : 0;
    layout(mode,number.length);
    if (window.GlucoseAppearance) window.GlucoseAppearance.apply(s.appearance,$('dial'));
    $('view-mode').disabled = modeSaving || !browserOnline || typeof s.csrf !== 'string';
    drawTrend(s.trend_symbol,mode,hasReading);
    text('trend-label',hasReading ? s.trend_label : 'Sin tendencia');
    const ageLabel = !hasReading ? 'Sin lecturas' : age === null ? 'Hora sin sincronizar' : ageText(age);
    text('age',old ? `ANTIGUA · ${ageLabel}` : ageLabel);
    text('age-detail',ageLabel);
    text('reading-state',labels[tone]);
    text('server-status',s.message || 'Esperando consulta');
    const dialStatus=tone==='low' ? 'BAJA · menos de 70' : tone==='high' ? 'ALTA · más de 180' :
      tone==='normal' && s.message==='Conectado' ? 'En intervalo' : s.message || '';
    text('dial-status',browserOnline ? dialStatus.slice(0,25) : 'Sin conexión al visor');
    text('dial-ip',s.ip || 'Sin Wi-Fi');
    text('device-ip',s.ip || '—');
    text('version',s.firmware);
    text('dial-brand',s.patient_name || 'NIGHTSCOUT');
    $('dial-brand').style.letterSpacing = '.6px';
    $('dial-brand').removeAttribute('textLength');
    const nameWidth = $('dial-brand').getComputedTextLength();
    if (nameWidth > 148) {
      $('dial-brand').setAttribute('textLength','148');
      $('dial-brand').setAttribute('lengthAdjust','spacingAndGlyphs');
    }
    text('wifi',s.wifi_connected ? `Conectado · ${s.rssi_dbm} dBm` : 'Desconectado');
    text('checked-age',Number.isFinite(s.checked_seconds_ago) ? ageText(s.checked_seconds_ago + elapsed()) : 'Aún sin consultar');
    text('reading-time',hasReading ? new Date(s.reading_date_ms).toLocaleTimeString('es',{hour:'2-digit',minute:'2-digit'}) : '—');
    $('glucose').removeAttribute('textLength');
    const numberWidth=$('glucose').getComputedTextLength();
    const maximumWidth=[150,164,196][[0,1,2].includes(mode)?mode:0];
    if (numberWidth>maximumWidth) {
      $('glucose').setAttribute('textLength',String(maximumWidth));
      $('glucose').setAttribute('lengthAdjust','spacingAndGlyphs');
    }
    $('dial').setAttribute('aria-label',hasReading ? `${s.glucose} miligramos por decilitro. ${s.trend_label}. ${ageLabel}` : 'Sin lecturas');
    const connection = $('connection');
    connection.className = 'connection-pill ' + (browserOnline ? 'online' : 'offline');
    connection.replaceChildren(Object.assign(document.createElement('i'),{}),document.createTextNode(browserOnline ? 'Conectado al Wemos' : 'Wemos sin respuesta'));
  }
  async function changeMode() {
    if (!snapshot || !browserOnline || modeSaving || typeof snapshot.csrf !== 'string') return;
    const selected=Number($('view-mode').value);
    modeSaving=true; $('view-mode').disabled=true; text('view-feedback','Guardando modo…');
    try {
      const controller=new AbortController(), timeout=setTimeout(()=>controller.abort(),9000);
      let response;
      try {
        response=await fetch('/appearance',{method:'POST',credentials:'same-origin',signal:controller.signal,
          headers:{'Content-Type':'application/x-www-form-urlencoded'},
          body:new URLSearchParams({mode:String(selected),csrf:snapshot.csrf,quick:'1'})});
      } finally {clearTimeout(timeout);}
      if (!response.ok) throw new Error(response.status===403 ? 'La sesión cambió. Recarga el visor para guardar el modo.' : 'No se pudo guardar el modo. Intenta de nuevo.');
      snapshot.appearance={...snapshot.appearance,mode:selected};
      text('view-feedback','Modo guardado en la pantalla y el visor.');
    } catch(error) {
      text('view-feedback',error.name==='AbortError' ? 'El Wemos tardó en responder. Actualiza el visor para comprobar el modo guardado.' : error.message);
    } finally {modeSaving=false;render();}
  }
  async function refresh() {
    if (busy) return;
    clearTimeout(timer);
    if (document.hidden) { timer = setTimeout(refresh,5000); return; }
    busy = true; $('refresh').disabled = true;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(),9000);
    try {
      const response = await fetch('/api/state',{cache:'no-store',credentials:'same-origin',signal:controller.signal});
      if (!response.ok) throw new Error(response.status === 401 ? 'Vuelve a iniciar sesión en el Wemos.' : 'El Wemos no pudo entregar su estado.');
      const data = await response.json();
      if (typeof data.available !== 'boolean') throw new Error('Recarga la página después de actualizar el firmware.');
      snapshot = data; receivedAt = performance.now(); browserOnline = true;
      $('network-notice').hidden = true;
      text('viewer-status','Visor conectado · se actualiza cada 5 s');
      drawGraph(data.plot);
    } catch (error) {
      browserOnline = false;
      text('network-notice',error.name === 'AbortError' ? 'El Wemos tardó en responder. El visor reintentará automáticamente; conserva la última lectura recibida.' : error.message);
      $('network-notice').hidden = false;
      text('viewer-status','Sin respuesta del Wemos · reintentando');
      if (!snapshot) { text('connection','Wemos sin respuesta'); $('connection').className = 'connection-pill offline'; }
    } finally {
      clearTimeout(timeout); busy = false; $('refresh').disabled = false;
      render(); timer = setTimeout(refresh,5000);
    }
  }
  $('refresh').addEventListener('click',refresh);
  $('view-mode').addEventListener('change',changeMode);
  document.addEventListener('visibilitychange',() => { if (!document.hidden) refresh(); });
  setInterval(render,1000);
  refresh();
})();
