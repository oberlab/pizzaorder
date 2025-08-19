(() => {
  const qs = new URLSearchParams(location.search);
  const token = qs.get('token') || '';

  const state = {
    ws: null,
    pizzas: [],
    orders: {}, // sid -> order
    paypalEmail: '',
    orderingOpen: true,
    authed: false,
  };

  const els = {
    status: document.getElementById('manager-status'),
    auth: document.getElementById('manager-auth'),
    paypal: document.getElementById('paypal-email'),
    savePaypal: document.getElementById('save-paypal'),
    ordersList: document.getElementById('orders-list'),
    toggleOrders: document.getElementById('toggle-orders'),
    summary: document.getElementById('pizza-summary'),
  };

  function euro(v){ return (v||0).toFixed(2).replace('.', ',') + ' €'; }

  function setStatus(text, ok){
    els.status.innerHTML = ok === undefined
      ? text
      : `<span class="badge ${ok? 'good':'bad'}">${ok? 'Manager' : 'Nicht Manager'}</span> ${text}`;
  }

  let retry = 0;
  function wsUrl(){
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const qs = token ? `?token=${encodeURIComponent(token)}` : '';
    const host = location.hostname.includes(':') ? `[${location.hostname}]` : location.hostname;
    return `${proto}://${host}:81/${qs ? qs : ''}`;
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
      setTimeout(connect, delay);
    };
  }

  function send(obj){
    try{ state.ws && state.ws.readyState===1 && state.ws.send(JSON.stringify(obj)); }catch{}
  }

  function renderOrders(){
    els.ordersList.innerHTML = '';
    const pizzasById = Object.fromEntries(state.pizzas.map(p=>[p.id,p]));
    Object.entries(state.orders).forEach(([sid, o]) => {
      const items = Object.entries(o.items||{}).filter(([_,q])=>q>0);
      const total = items.reduce((s,[pid,q]) => s + (pizzasById[pid]?.price||0)*q, 0);
      const card = document.createElement('div');
      card.className = 'order-card';
      const statusText = o.paid ? `Bezahlt (${o.method||''})` : (o.method === 'paypal' || o.method === 'paypal_started' ? 'PayPal gestartet' : 'Offen');
      const nameVal = (o.name || '');
      const editedItems = items.map(([pid,q])=>{
        const p = pizzasById[pid];
        const label = (p?.name)||pid;
        return `<div class="mini-edit">
          <span class="mini-name">${label}</span>
          <button data-act="dec" data-sid="${sid}" data-pid="${pid}">−</button>
          <span class="mini-qty">${q}</span>
          <button data-act="inc" data-sid="${sid}" data-pid="${pid}">+</button>
        </div>`;
      }).join('');
      const options = state.pizzas.map(p=>`<option value="${p.id}">${p.name}</option>`).join('');
      const addCtrl = `
        <div class="add-control">
          <button data-sid="${sid}" data-action="toggle-add">Add</button>
          <span class="add-ui" data-sid="${sid}" hidden>
            <select class="add-select" data-sid="${sid}">${options}</select>
            <button data-sid="${sid}" data-action="confirm-add">Add</button>
            <button data-sid="${sid}" data-action="cancel-add">Cancel</button>
          </span>
        </div>`;
      card.innerHTML = `
        <div class="order-head">
          <input class="mgr-name" data-sid="${sid}" value="${nameVal.replace(/\\/g,'\\\\').replace(/"/g,'&quot;')}" />
          <div>
            <span>${euro(total)}</span>
            <span style="margin-left:8px;${o.paid? 'color:#1ee38f':''}">${statusText}</span>
          </div>
        </div>
        <div class="order-items">${editedItems || '—'}</div>
        ${addCtrl}
        <div class="row" style="margin-top:8px">
          <button data-sid="${sid}" data-method="cash">Bar bezahlt</button>
          <button data-sid="${sid}" data-method="paypal">PayPal bezahlt</button>
          <button data-sid="${sid}" data-action="unpaid">Nicht bezahlt</button>
        </div>`;
      els.ordersList.appendChild(card);
    });
    els.toggleOrders.textContent = state.orderingOpen ? 'Bestellen schließen' : 'Bestellen öffnen';
  }

  function renderSummary(){
    if (!els.summary) return;
    els.summary.innerHTML = '';
    const byId = Object.fromEntries(state.pizzas.map(p=>[p.id,p]));
    const tally = {};
    Object.values(state.orders).forEach(o => {
      Object.entries(o.items||{}).forEach(([pid,q]) => {
        if (q>0){ tally[pid] = (tally[pid]||0) + q; }
      });
    });
    Object.entries(tally).forEach(([pid, qty]) => {
      const p = byId[pid]; if (!p) return;
      const row = document.createElement('div');
      row.className = 'summary-row';
      const amount = (p.price||0) * qty;
      row.textContent = `${qty}× ${p.name} — ${amount.toFixed(2).replace('.', ',')} €`;
      els.summary.appendChild(row);
    });
  }

  function handle(msg){
    if (msg.type === 'auth'){
      state.authed = !!msg.ok;
      setStatus(msg.ok ? 'Als Manager angemeldet' : 'Management-Link mit Token öffnen', msg.ok);
      els.auth.textContent = msg.ok ? '' : 'Nicht berechtigt: Bitte Seite mit Management-Token öffnen.';
    }
    if (msg.type === 'state'){
      const d = msg.data||{};
      state.pizzas = d.pizzas||[];
      state.orders = d.orders||{};
      state.paypalEmail = d.paypal_email||'';
      state.orderingOpen = !!d.ordering_open;
      els.paypal.value = state.paypalEmail;
      renderOrders();
      renderSummary();
    }
    if (msg.type === 'ok' && msg.op === 'set_paypal'){
      setStatus('PayPal gespeichert', true);
    }
    if (msg.type === 'error' && msg.op === 'set_paypal'){
      setStatus('Speichern fehlgeschlagen (keine Berechtigung)', false);
    }
    if (msg.type === 'ok' && msg.op === 'set_pizzas'){
      setStatus('Pizzen gespeichert', true);
    }
    if (msg.type === 'error' && msg.op === 'set_pizzas'){
      setStatus('Speichern der Pizzen fehlgeschlagen (keine Berechtigung)', false);
    }
  }

  els.savePaypal.addEventListener('click', () => {
    const email = (els.paypal.value||'').trim();
    if (!state.authed){ setStatus('Nicht berechtigt: Management-Link mit Token öffnen', false); return; }
    setStatus('Speichern …', true);
    send({type:'set_paypal', email});
  });
  els.ordersList.addEventListener('click', (e) => {
    const btn = e.target.closest('button');
    if (!btn) return;
    const sid = btn.dataset.sid;
    const method = btn.dataset.method;
    const action = btn.dataset.action;
    const act = btn.dataset.act;
    const pid = btn.dataset.pid;
    if (sid && action === 'toggle-add'){
      const ui = els.ordersList.querySelector(`.add-ui[data-sid="${sid}"]`);
      if (ui) ui.hidden = !ui.hidden; return;
    }
    if (sid && action === 'cancel-add'){
      const ui = els.ordersList.querySelector(`.add-ui[data-sid="${sid}"]`);
      if (ui) ui.hidden = true; return;
    }
    if (sid && action === 'confirm-add'){
      const sel = els.ordersList.querySelector(`select.add-select[data-sid="${sid}"]`);
      const val = sel && sel.value;
      if (val){ send({type:'add_item', sid, pizza_id: val, delta: +1}); }
      const ui = els.ordersList.querySelector(`.add-ui[data-sid="${sid}"]`);
      if (ui) ui.hidden = true;
      return;
    }
    if (sid && method){ send({type:'mark_paid', sid, method}); }
    if (sid && action === 'unpaid'){ send({type:'mark_unpaid', sid}); }
    if (sid && act && pid){
      const delta = act === 'inc' ? +1 : -1;
      send({type:'add_item', sid, pizza_id: pid, delta});
    }
  });
  // Name ändern (Blur/Change)
  els.ordersList.addEventListener('change', (e) => {
    const input = e.target.closest('input.mgr-name');
    if (!input) return;
    const sid = input.dataset.sid;
    const name = input.value || '';
    send({type:'set_user', sid, name});
  });
  els.toggleOrders.addEventListener('click', () => {
    send({type: state.orderingOpen ? 'close_orders' : 'open_orders'});
  });

  setStatus('Verbinde …');
  connect();

  // Cookie banner
  const cookieBanner = document.createElement('div');
  cookieBanner.className = 'cookie-banner';
  cookieBanner.innerHTML = '<span>Diese Seite verwendet Cookies für die Management-Session.</span><button id="cookie-accept">Verstanden</button>';
  function hasCookieOk(){ return document.cookie.split(';').some(c=>c.trim().startsWith('cookie_ok=')); }
  if (!hasCookieOk()){
    document.body.prepend(cookieBanner);
    document.getElementById('cookie-accept').addEventListener('click', ()=>{
      document.cookie = 'cookie_ok=1; Path=/; Max-Age=31536000; SameSite=Lax';
      cookieBanner.remove();
    });
  }

  // client-side cookie clear
  const clearLink = document.getElementById('clear-cookies');
  if (clearLink){
    clearLink.addEventListener('click', (e) => {
      e.preventDefault();
      const expire = 'Max-Age=0; Path=/; SameSite=Lax';
      document.cookie = 'cookie_ok=; ' + expire;
      document.cookie = 'name=; ' + expire;
      document.cookie = 'sid=; ' + expire;
      location.reload();
    });
  }
})();
