(() => {
  const state = {
    ws: null,
    pizzas: [],
    orderingOpen: true,
    paypalEmail: null,
    name: '',
    items: {}, // pizzaId -> qty for this user
    managerLink: null,
  };

  const els = {
    name: document.getElementById('who-name'),
    list: document.getElementById('pizza-list'),
    total: document.getElementById('grand-total'),
    payBtn: document.getElementById('pay-btn'),
    payHint: document.getElementById('pay-hint'),
    status: document.getElementById('order-status'),
    open: document.getElementById('ordering-open'),
    banner: document.getElementById('manager-banner'),
    bannerLink: document.getElementById('manager-link'),
    consentOverlay: document.getElementById('consent-overlay'),
    consentName: document.getElementById('consent-name'),
    consentAccept: document.getElementById('consent-accept'),
  };

  // cookie utils
  function getCookie(name){
    return document.cookie.split(';').map(c=>c.trim()).find(c=>c.startsWith(name+'='))?.split('=')[1] || null;
  }
  function setCookie(name,val,days=365){
    const max = days*24*3600;
    document.cookie = `${name}=${val}; Path=/; Max-Age=${max}; SameSite=Lax`;
  }

  function euro(v){
    return (v || 0).toFixed(2).replace('.', ',') + ' €';
  }

  function calcTotal(){
    const map = Object.fromEntries(state.pizzas.map(p=>[p.id, p]));
    let sum = 0;
    for (const [pid, qty] of Object.entries(state.items)){
      const p = map[pid];
      if (p) sum += (p.price || 0) * qty;
    }
    return sum;
  }

  function renderList(){
    els.list.innerHTML = '';
    state.pizzas.forEach(p => {
      const card = document.createElement('div');
      card.className = 'card';
      card.innerHTML = `
        <div class="title">${p.name}</div>
        <div class="desc">${(p.ingredients||[]).join(', ')}</div>
        <div class="qty">
          <button data-act="dec" data-id="${p.id}" ${!state.orderingOpen?'disabled':''}>−</button>
          <span id="qty-${p.id}">${state.items[p.id]||0}</span>
          <button data-act="inc" data-id="${p.id}" ${!state.orderingOpen?'disabled':''}>+</button>
        </div>
        <div class="price">${euro(p.price)}</div>`;
      els.list.appendChild(card);
    });
  }

  function renderTotals(){
    const total = calcTotal();
    els.total.textContent = euro(total);
    const canPay = !!state.paypalEmail && total > 0 && state.name.trim().length > 0;
    els.payBtn.disabled = !canPay;
    els.payHint.style.display = state.paypalEmail ? 'none' : 'block';
    els.open.textContent = state.orderingOpen ? 'Bestellung geöffnet' : 'Bestellung geschlossen';
    els.status.textContent = state.name ? `Für: ${state.name}` : 'Bitte Name eingeben';
    renderCart();
  }

  function updateQty(pid, delta){
    const next = Math.max(0, (state.items[pid]||0) + delta);
    state.items[pid] = next;
    const span = document.getElementById(`qty-${pid}`);
    if (span) span.textContent = String(next);
    renderTotals();
    if (state.ws){
      send({type:'add_item', pizza_id: pid, delta});
    }
  }

  function bindListClicks(){
    els.list.addEventListener('click', (e) => {
      const btn = e.target.closest('button');
      if (!btn) return;
      const act = btn.dataset.act;
      const pid = btn.dataset.id;
      if (!state.orderingOpen) return;
      if (act === 'inc') updateQty(pid, +1);
      if (act === 'dec') updateQty(pid, -1);
    });
  }

  function setName(n){
    state.name = (n||'').trim();
    renderTotals();
    if (state.ws) send({type:'set_user', name: state.name});
    if (state.name){ setCookie('name', encodeURIComponent(state.name)); }
  }

  function buildPaypalLink(){
    const amount = calcTotal().toFixed(2);
    const payto = (state.paypalEmail||'').trim();
    // Support PayPal.Me shorthand: me:username
    if (payto.startsWith('me:')){
      const handle = encodeURIComponent(payto.slice(3));
      return `https://www.paypal.com/paypalme/${handle}/${amount}`;
    }
    const to = encodeURIComponent(payto);
    const item = encodeURIComponent(`Pizza ${state.name}`);
    return `https://www.paypal.com/cgi-bin/webscr?cmd=_xclick&business=${to}&amount=${amount}&currency_code=EUR&item_name=${item}`;
  }

  function pay(){
    const url = buildPaypalLink();
    window.open(url, '_blank');
    // Signal only that PayPal was started; manager confirms payment
    if (state.ws){ send({type:'payment_started', method:'paypal'}); }
  }

  let retry = 0;
  let useAlt = true; // prefer ESP32 WS on :81 first
  function wsUrl(){
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    if (useAlt) return `${proto}://${location.hostname}:81/`;
    return `${proto}://${location.host}/ws`;
  }
  function connect(){
    const url = wsUrl();
    const ws = new WebSocket(url);
    state.ws = ws;
    ws.onopen = () => {
      retry = 0;
      send({type:'hello', role:'user'});
      if (state.name) send({type:'set_user', name: state.name});
    };
    ws.onmessage = (ev) => {
      try{ const msg = JSON.parse(ev.data); handle(msg); }catch{}
    };
    ws.onclose = () => {
      state.ws = null;
      const delay = Math.min(1000, 200 * Math.pow(2, Math.min(retry, 3))) + Math.floor(Math.random()*150);
      retry++;
      if (retry === 2) useAlt = !useAlt; // toggle between 81 and /ws after a couple retries
      setTimeout(connect, delay);
    };
  }

  async function primeState(){
    try{
      const res = await fetch('/api/state', {cache:'no-store'});
      if (!res.ok) return;
      const msg = await res.json();
      handle(msg);
    }catch{}
  }

  function send(obj){
    try{ state.ws && state.ws.readyState===1 && state.ws.send(JSON.stringify(obj)); }catch{}
  }

  function handle(msg){
    if (msg.type === 'state'){
      const cur = msg.data || {};
      state.pizzas = cur.pizzas || [];
      state.orderingOpen = !!cur.ordering_open;
      state.paypalEmail = cur.paypal_email || null;
      state.managerLink = cur.manager_link || null;
      if (cur.my && cur.my.items){
        state.items = Object.assign({}, cur.my.items);
      }
      if (cur.my && cur.my.name && !state.name){
        state.name = cur.my.name;
        els.name.value = state.name;
      }
      renderList();
      renderTotals();
      const banner = document.getElementById('closed-banner');
      if (banner) banner.hidden = !!state.orderingOpen;
      // show manager banner if unclaimed
      if (state.managerLink){
        els.banner.hidden = false;
        els.bannerLink.href = state.managerLink;
      } else {
        els.banner.hidden = true;
      }
    }
  }

  // init
  bindListClicks();
  els.name.addEventListener('change', e=> setName(e.target.value));
  els.name.addEventListener('keyup', e=> setName(e.target.value));
  els.payBtn.addEventListener('click', pay);
  connect();
  primeState();

  // Consent + name modal
  function needConsent(){ return !getCookie('cookie_ok'); }
  function getStoredName(){ const v = getCookie('name'); return v? decodeURIComponent(v):''; }
  function maybePrompt(){
    const storedName = getStoredName();
    if (storedName){ els.name.value = storedName; setName(storedName); }
    if (needConsent() || !storedName){
      els.consentOverlay.hidden = false;
      els.consentName.value = storedName || '';
      els.consentAccept.disabled = !(els.consentName.value.trim().length>0);
      const updateBtn = () => { els.consentAccept.disabled = !(els.consentName.value.trim().length>0); };
      els.consentName.addEventListener('input', updateBtn);
      els.consentAccept.addEventListener('click', () => {
        const nm = els.consentName.value.trim();
        if (!nm) return;
        setCookie('cookie_ok','1');
        setCookie('name', encodeURIComponent(nm));
        // remove overlay to avoid any CSS specificity issues
        els.consentOverlay.remove();
        els.name.value = nm;
        setName(nm);
      }, { once:true });
    }
  }
  maybePrompt();

  function renderCart(){
    const c = els.cart || (els.cart = document.getElementById('cart-list'));
    if (!c) return;
    c.innerHTML = '';
    const map = Object.fromEntries(state.pizzas.map(p=>[p.id,p]));
    Object.entries(state.items).filter(([_,q])=>q>0).forEach(([pid, q]) => {
      const p = map[pid]; if (!p) return;
      const div = document.createElement('div');
      div.className = 'cart-item';
      div.innerHTML = `
        <button data-act="dec" data-id="${pid}" ${!state.orderingOpen?'disabled':''}>−</button>
        <span class="qty">${q}×</span>
        <span class="name">${p.name}</span>
        <button data-act="inc" data-id="${pid}" ${!state.orderingOpen?'disabled':''}>+</button>`;
      c.appendChild(div);
    });
  }
  // cart +/-
  document.getElementById('cart-list').addEventListener('click', (e) => {
    const btn = e.target.closest('button'); if (!btn) return;
    if (!state.orderingOpen) return;
    const act = btn.dataset.act; const pid = btn.dataset.id;
    if (act === 'inc') updateQty(pid, +1);
    if (act === 'dec') updateQty(pid, -1);
  });

  // client-side cookie clear
  const clearLink = document.getElementById('clear-cookies');
  if (clearLink){
    clearLink.addEventListener('click', (e) => {
      e.preventDefault();
      const expire = 'Max-Age=0; Path=/; SameSite=Lax';
      // Best-effort: clear client-managed cookies
      document.cookie = 'cookie_ok=; ' + expire;
      document.cookie = 'name=; ' + expire;
      // Try to clear sid if it's not HttpOnly (on ESP it isn't set)
      document.cookie = 'sid=; ' + expire;
      location.reload();
    });
  }
})();
