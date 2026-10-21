/* Run with: CHROMIUM_PATH=/usr/bin/chromium node tests/test_web_views.js
 * Uses request interception with local assets; starts no HTTP server.
 * Verifies browser behavior. ESP8266 HTTP, SPI, EEPROM and OTA need hardware.
 */
const assert=require('assert/strict');
const fs=require('fs');
const path=require('path');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const preset=['#74edbc','#ff675e','#699bff','#f69bd1','#72d9f5'];
const initial=()=>({firmware:'0.6.1',patient_name:'Alex',csrf:'fixture-session-only',available:true,glucose:142,
  reading_date_ms:Date.now()-120000,age_seconds:120,tone:'normal',trend_symbol:'↗',trend_label:'Subiendo',clock_valid:true,
  message:'Conectado',wifi_connected:true,rssi_dbm:-70,ip:'192.0.2.1',checked_seconds_ago:3,
  appearance:{mode:0,theme:0,accent:preset[0],background:'#0b1114',number:preset[0]},
  plot:{low:60,high:200,points:[[0,800,true],[160,750,false],[330,600,false],[500,650,false],[660,500,false],[830,450,false],[1000,400,false]]}});
const form=`<!doctype html><html lang="es"><meta charset="utf-8"><link rel="stylesheet" href="/assets/ui.css"><script src="/assets/appearance.js" defer></script><main class="settings"><form id="appearance-form"><input name="csrf" type="hidden" value="fixture-session-only"><select name="mode"><option value="0">Gráfica</option><option value="1">Lateral</option><option value="2">Debajo</option></select><select name="theme">${['Menta','Rojo','Azul','Rosado','Celeste','Personalizado'].map((x,i)=>`<option value="${i}">${x}</option>`).join('')}</select><input type="color" name="accent" value="#74edbc"><input type="color" name="background" value="#0b1114"><input type="color" name="number" value="#74edbc"><div id="appearance-preview"><strong>142</strong></div><p id="contrast-feedback"></p><button id="save-appearance">Guardar</button><button id="reset-theme" type="button">Menta</button></form></main></html>`;
(async()=>{
  const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  try {
    const page=await browser.newPage({viewport:{width:1365,height:1050},reducedMotion:'reduce',timezoneId:'America/Santiago'});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    let state=initial(),unavailable=false,rejectPosts=false,posts=[];
    await page.route('http://device.test/**',async route=>{
      const request=route.request(),url=new URL(request.url());
      if(url.pathname==='/api/state')return route.fulfill(unavailable?{status:503,body:'Unavailable'}:{json:state});
      if(url.pathname==='/appearance'){
        const fields=new URLSearchParams(request.postData());
        posts.push(Object.fromEntries(fields));
        assert.equal(request.method(),'POST');assert.equal(fields.get('csrf'),'fixture-session-only');
        if(rejectPosts)return route.fulfill({status:403,body:'Session changed'});
        state.appearance.mode=Number(fields.get('mode'));
        return route.fulfill({json:{ok:true}});
      }
      if(url.pathname==='/settings-fixture')return route.fulfill({contentType:'text/html',body:form});
      const files={'/':['index.html','text/html'],'/assets/ui.css':['ui.css','text/css'],'/assets/app.js':['app.js','application/javascript'],'/assets/appearance.js':['appearance.js','application/javascript']};
      const item=files[url.pathname];
      return item?route.fulfill({contentType:item[1],body:fs.readFileSync(path.join(root,'web',item[0]))}):route.fulfill({status:404});
    });
    async function load(){await page.goto('http://device.test/');await page.waitForFunction(()=>!document.getElementById('view-mode').disabled);}
    async function refresh(){await page.locator('#refresh').click();await page.waitForFunction(()=>!document.getElementById('refresh').disabled);}
    for(const viewport of [{width:1365,height:1050},{width:390,height:844}]){
      await page.setViewportSize(viewport);
      for(let mode=0;mode<3;mode++)for(let theme=0;theme<5;theme++){
        state=initial();Object.assign(state.appearance,{mode,theme,accent:preset[theme],number:preset[theme]});await load();
        assert.equal(await page.locator('#dial').getAttribute('data-mode'),String(mode));
        assert.equal(await page.locator('#history-group').evaluate(e=>getComputedStyle(e).display==='none'),mode!==0);
        assert.equal(await page.locator('#dial-brand').textContent(),'Alex');
        assert.equal(await page.locator('#dial-brand').getAttribute('y'),'53');
        assert.equal(await page.locator('#dial-brand').evaluate(e=>getComputedStyle(e).fontSize),'28px');
        assert.equal(await page.locator('#dial').evaluate(e=>e.style.getPropertyValue('--theme-accent')),preset[theme]);
        assert.equal(await page.locator('#history-dots circle').count(),7);
        const bounds=await page.locator('#glucose').evaluate(e=>{const b=e.getBBox(),style=getComputedStyle(e),ctx=document.createElement('canvas').getContext('2d');ctx.font=`${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;const ink=ctx.measureText(e.textContent);return{x:b.x,width:b.width,y:Number(e.getAttribute('y'))-ink.actualBoundingBoxAscent,height:ink.actualBoundingBoxAscent+ink.actualBoundingBoxDescent}});
        assert(bounds.x>=6 && bounds.x+bounds.width<=234,'Number clipped at circle sides');
        assert(bounds.y+bounds.height<184,`Number overlaps age badge: mode=${mode}, bounds=${JSON.stringify(bounds)}`);
        assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Horizontal page overflow');
      }
    }
    await page.setViewportSize({width:1365,height:1050});state=initial();await load();
    for(const mode of [1,2,0]){
      await page.locator('#view-mode').selectOption(String(mode));
      await page.waitForFunction(value=>document.getElementById('dial').dataset.mode===String(value)&&!document.getElementById('view-mode').disabled,mode);
      assert.equal(state.appearance.mode,mode);assert.equal(posts.at(-1).mode,String(mode));
      assert.deepEqual(Object.keys(posts.at(-1)).sort(),['csrf','mode','quick']);
    }
    await load();assert.equal(await page.locator('#view-mode').inputValue(),'0');
    rejectPosts=true;await page.locator('#view-mode').selectOption('2');
    await page.waitForFunction(()=>document.getElementById('view-feedback').textContent.includes('Recarga'));
    assert.equal(await page.locator('#view-mode').inputValue(),'0');rejectPosts=false;
    for(const mode of [0,1,2]){
      for(const tone of ['low','high','old','clock','empty','error']){
        state=initial();state.appearance.mode=mode;state.tone=tone;
        state.appearance.theme=1;state.appearance.accent=preset[1];state.appearance.number=preset[1];
        if(tone==='low')state.glucose=62;
        if(tone==='high')state.glucose=1000;
        if(tone==='old')state.age_seconds=900;
        if(tone==='clock'){state.age_seconds=null;state.clock_valid=false;}
        if(tone==='empty'){state.available=false;state.glucose=null;}
        if(tone==='error')state.message='Respuesta incompleta';
        await load();
        const rendered=await page.locator('#dial').getAttribute('data-tone');assert.equal(rendered,tone);
        if(tone==='low')assert((await page.locator('#dial-status').textContent()).includes('BAJA'));
        if(tone==='high')assert((await page.locator('#dial-status').textContent()).includes('ALTA'));
        if(tone==='old')assert((await page.locator('#age').textContent()).includes('ANTIGUA'));
        if(tone==='low')assert.equal(await page.locator('#glucose').evaluate(e=>getComputedStyle(e).fill),'rgb(255, 130, 139)');
        if(tone==='high')assert.equal(await page.locator('#glucose').evaluate(e=>getComputedStyle(e).fill),'rgb(255, 189, 115)');
      }
    }
    for(const mode of [0,1,2]) {
      for(const symbol of ['→','↑','⇈','↓','⇊','↗','↘','—']) {
        state=initial();state.appearance.mode=mode;state.trend_symbol=symbol;await load();
        const arrow=await page.locator('#trend').evaluate(e=>({length:e.getTotalLength(),width:e.getBBox().width,height:e.getBBox().height}));
        assert(arrow.length>=12 && arrow.width<=35 && arrow.height<=35,'Trend vector does not fit');
        if(symbol==='⇈'||symbol==='⇊')assert(arrow.length>50,'Double trend missing second arrow');
      }
    }
    state=initial();Object.assign(state.appearance,{theme:5,accent:'#72d9f5',number:'#f2f7f6'});await load();
    assert.equal(await page.locator('#glucose').evaluate(e=>getComputedStyle(e).fill),'rgb(242, 247, 246)');
    assert.equal(await page.locator('#trend').evaluate(e=>getComputedStyle(e).stroke),'rgb(114, 217, 245)');
    state.patient_name='<img src=x onerror=bad>';await refresh();
    assert.equal(await page.locator('#dial-brand').getAttribute('aria-label'),state.patient_name);assert.equal(await page.locator('#dial-brand *').count(),0);
    assert((await page.locator('#dial-brand').textContent()).endsWith('…'));
    assert(await page.locator('#dial-brand').evaluate(e=>e.getComputedTextLength()<=128));
    for(const label of ['María Muñoz','WWWWWWWWWWWWWWWWWWWWWWWW','']) {
      state.patient_name=label;await refresh();
      assert.equal(await page.locator('#dial-brand').getAttribute('aria-label'),label||'NIGHTSCOUT');
      assert(await page.locator('#dial-brand').evaluate(e=>e.getComputedTextLength()<=(e.getAttribute('aria-label')==='NIGHTSCOUT'?148:128)));
      if(!label)assert.equal(await page.locator('#dial-brand').textContent(),'NIGHTSCOUT');
    }
    unavailable=true;await refresh();assert.equal(await page.locator('#glucose').textContent(),'142');assert(await page.locator('#view-mode').isDisabled());
    unavailable=false;
    // Save actual static-asset renders for visual review, using example readings.
    fs.mkdirSync(path.join(root,'docs/images'),{recursive:true});
    for(const [mode,file,viewport] of [[0,'visor-grafica.png',{width:1365,height:1050}],[1,'visor-lateral.png',{width:1365,height:1050}],[2,'visor-centrado-movil.png',{width:390,height:844}]]){
      state=initial();state.appearance.mode=mode;await page.setViewportSize(viewport);await load();
      await page.screenshot({path:path.join(root,'docs/images',file),fullPage:true});
    }
    await page.goto('http://device.test/settings-fixture');await page.waitForFunction(()=>document.getElementById('contrast-feedback').textContent.length>0);
    assert(await page.locator('[name=number]').isDisabled());
    await page.locator('[name=theme]').selectOption('5');assert(!(await page.locator('[name=number]').isDisabled()));
    async function color(name,value){await page.locator(`[name=${name}]`).evaluate((e,v)=>{e.value=v;e.dispatchEvent(new Event('input',{bubbles:true}))},value);}
    await color('number','#000000');assert(await page.locator('#save-appearance').isDisabled());
    await color('number','#f2f7f6');await color('accent','#72d9f5');assert(!(await page.locator('#save-appearance').isDisabled()));
    await color('background','#ffffff');assert(await page.locator('#save-appearance').isDisabled());
    await color('background','#0b1114');assert(!(await page.locator('#save-appearance').isDisabled()));
    const fields=await page.locator('#appearance-form').evaluate(e=>Object.fromEntries(new FormData(e)));
    assert.equal(fields.number,'#f2f7f6');assert.equal(fields.theme,'5');assert.equal(fields.csrf,'fixture-session-only');
    await page.locator('#reset-theme').click();assert.equal(await page.locator('[name=theme]').inputValue(),'0');assert(await page.locator('[name=number]').isDisabled());
    assert.deepEqual(errors,[]);
    console.log('Browser checks passed: 3 modes × 5 themes × desktop/mobile; warnings, custom contrast, CSRF mode saves, failed save, safe names, offline retention and screenshots.');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exit(1)});
