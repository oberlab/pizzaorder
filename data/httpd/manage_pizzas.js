(() => {
  const qs = new URLSearchParams(location.search);
  const token = qs.get('token') || '';

  const state = {
    ws: null,
    pizzas: [],
    authed: false,
  };

  const els = {
    status: document.getElementById('manager-status'),
    addPizza: document.getElementById('add-pizza'),
    savePizzas: document.getElementById('save-pizzas'),
    jsonFile: document.getElementById('json-file'),
    editor: document.getElementById('pizza-editor'),
  };

  function setStatus(text, ok){
    if (!els.status) return;
    els.status.innerHTML = ok === undefined
      ? text
      : `<span class="badge ${ok? 'good':'bad'}">${ok? 'Manager' : 'Nicht Manager'}</span> ${text}`;
  }

  let retry = 0;
  let useAlt = true; // prefer ESP32 port 81
  function wsUrl(){
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const q = token ? `?token=${encodeURIComponent(token)}` : '';
    if (useAlt) return `${proto}://${location.hostname}:81/${q ? q : ''}`;
    return `${proto}://${location.host}/ws${q}`;
  }

  function connect(){
    const ws = new WebSocket(wsUrl());
    state.ws = ws;
    ws.onopen = () => {
      retry = 0;
      send({type:'hello', role:'manager', token});
    };
    ws.onmessage = ev => {
      try{ handle(JSON.parse(ev.data)); }catch{}
    };
    ws.onclose = () => {
      state.ws = null;
      setStatus('Getrennt – verbinde erneut …', false);
      const delay = Math.min(1000, 200 * Math.pow(2, Math.min(retry, 3))) + Math.floor(Math.random()*150);
      retry++;
      if (retry === 2) useAlt = !useAlt; // toggle between endpoints
      setTimeout(connect, delay);
    };
  }

  function send(obj){
    try{ state.ws && state.ws.readyState===1 && state.ws.send(JSON.stringify(obj)); }catch{}
  }

  function renderEditor(){
    if (!els.editor) return;
    els.editor.innerHTML = '';
    state.pizzas.forEach((p, idx) => {
      const row = document.createElement('div');
      row.className = 'pizza-row';
      row.innerHTML = `
        <input placeholder="Name" value="${p.name||''}" />
        <input placeholder="Zutaten, komma-getrennt" value="${(p.ingredients||[]).join(', ')}" />
        <input type="number" step="0.1" min="0" value="${p.price||0}" />
        <button data-i="${idx}" class="del">Löschen</button>`;
      els.editor.appendChild(row);
    });
  }

  function handle(msg){
    if (msg.type === 'auth'){
      state.authed = !!msg.ok;
      setStatus(msg.ok ? 'Als Manager angemeldet' : 'Management-Link mit Token öffnen', msg.ok);
    }
    if (msg.type === 'state'){
      const d = msg.data||{};
      state.pizzas = d.pizzas||[];
      renderEditor();
    }
    if (msg.type === 'ok' && msg.op === 'set_pizzas'){
      setStatus('Pizzen gespeichert', true);
    }
    if (msg.type === 'error' && msg.op === 'set_pizzas'){
      setStatus('Speichern der Pizzen fehlgeschlagen (keine Berechtigung)', false);
    }
  }

  // Events
  if (els.addPizza){
    els.addPizza.addEventListener('click', () => {
      const id = 'p'+Math.random().toString(36).slice(2,8);
      state.pizzas.push({id, name:'Neue Pizza', ingredients:[], price:0});
      renderEditor();
    });
  }
  if (els.savePizzas){
    els.savePizzas.addEventListener('click', () => {
      const rows = Array.from(els.editor?.children || []);
      state.pizzas = rows.map((row, idx) => {
        const [nameI, ingI, priceI] = row.querySelectorAll('input');
        const ing = (ingI.value||'').split(',').map(s=>s.trim()).filter(Boolean);
        const price = parseFloat(priceI.value||'0')||0;
        const p = state.pizzas[idx] || { id: 'p'+Math.random().toString(36).slice(2,8)};
        return { id: p.id, name: nameI.value||'', ingredients: ing, price };
      });
      if (!state.authed){ setStatus('Nicht berechtigt: Management-Link mit Token öffnen', false); return; }
      setStatus('Pizzen speichern …', true);
      send({type:'set_pizzas', pizzas: state.pizzas});
    });
  }
  if (els.editor){
    els.editor.addEventListener('click', (e)=>{
      const btn = e.target.closest('button.del');
      if (!btn) return;
      const i = Array.from(els.editor.children).indexOf(btn.parentElement);
      if (i>=0){ state.pizzas.splice(i,1); renderEditor(); }
    });
  }
  if (els.jsonFile){
    els.jsonFile.addEventListener('change', async (e) => {
      const f = e.target.files?.[0]; if (!f) return;
      const txt = await f.text();
      try {
        const json = JSON.parse(txt);
        state.pizzas = Array.isArray(json) ? json : (json.pizzas||[]);
        renderEditor();
        if (!state.authed){ setStatus('Nicht berechtigt: Management-Link mit Token öffnen', false); return; }
        setStatus('Pizzen speichern …', true);
        send({type:'set_pizzas', pizzas: state.pizzas});
      } catch(err){ alert('Ungültiges JSON'); }
    });
  }

  setStatus('Verbinde …');
  connect();
})();

