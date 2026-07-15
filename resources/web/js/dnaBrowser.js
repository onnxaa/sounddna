class DNABrowserUI {
  constructor() {
    this.visible = false;
    this.results = [];
    this.filters = {
      instrument: '',
      genre: '',
      year: '',
      analog: null,
      brightness: [0, 100],
      aggression: [0, 100],
      width: [0, 100]
    };
  }

  toggle() {
    this.visible = !this.visible;
    if (this.visible) {
      this.show();
    } else {
      this.hide();
    }
  }

  show() {
    const existing = document.getElementById('dnaBrowserPanel');
    if (existing) {
      existing.style.display = 'flex';
      return;
    }

    this.buildPanel();
    this.search('');
  }

  hide() {
    const panel = document.getElementById('dnaBrowserPanel');
    if (panel) panel.style.display = 'none';
  }

  buildPanel() {
    const panel = document.createElement('div');
    panel.id = 'dnaBrowserPanel';
    panel.style.cssText = `
      position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%);
      width: 700px; height: 500px;
      background: var(--bg-secondary);
      border: 1px solid var(--border);
      border-radius: var(--panel-radius);
      display: flex; flex-direction: column;
      z-index: 200;
    `;

    panel.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:center;padding:12px;border-bottom:1px solid var(--border)">
        <span style="font-weight:600;font-size:13px">DNA Browser</span>
        <button class="btn btn-small" onclick="SDNA.browser.hide()">Close</button>
      </div>
      <div style="padding:8px 12px;border-bottom:1px solid var(--border)">
        <input type="text" id="browserSearch" placeholder="Search by instrument, genre, character..."
               style="width:100%;padding:6px 10px;background:var(--bg-input);border:1px solid var(--border);border-radius:4px;color:var(--text-primary);font-size:12px"
               oninput="SDNA.browser.search(this.value)">
      </div>
      <div id="browserResults" style="flex:1;overflow-y:auto;padding:8px;display:flex;flex-direction:column;gap:4px">
        <div style="color:var(--text-muted);text-align:center;padding:40px;font-size:12px">Loading...</div>
      </div>
    `;

    document.body.appendChild(panel);
  }

  search(query) {
    SDNA.Bridge.sendMessage(8, btoa(JSON.stringify({ query, filters: this.filters })));

    const presetData = this.getPresetResults(query);
    this.displayResults(presetData);
  }

  getPresetResults(query) {
    const library = [
      { name: 'Grand Piano', instrument: 'Piano', genre: 'Classical', year: 2020, analog: false, brightness: 65, aggression: 20, width: 70, tags: ['warm', 'clean', 'wide'] },
      { name: 'Moog Model D', instrument: 'Synth Bass', genre: 'Electronic', year: 1974, analog: true, brightness: 40, aggression: 70, width: 50, tags: ['warm', 'aggressive', 'organic'] },
      { name: 'Vintage Tape', instrument: 'Tape', genre: 'Processing', year: 1970, analog: true, brightness: 35, aggression: 30, width: 60, tags: ['warm', 'smooth', 'vintage'] },
      { name: 'Choir', instrument: 'Voice', genre: 'Classical', year: 2015, analog: false, brightness: 70, aggression: 15, width: 80, tags: ['airy', 'wide', 'organic'] },
      { name: 'Electric Guitar', instrument: 'Guitar', genre: 'Rock', year: 1985, analog: true, brightness: 75, aggression: 80, width: 65, tags: ['aggressive', 'bright', 'wide'] },
      { name: 'Analog Synth Pad', instrument: 'Synth', genre: 'Electronic', year: 2022, analog: true, brightness: 50, aggression: 25, width: 85, tags: ['warm', 'wide', 'smooth'] },
      { name: 'Jazz Saxophone', instrument: 'Sax', genre: 'Jazz', year: 1960, analog: true, brightness: 60, aggression: 35, width: 40, tags: ['warm', 'organic', 'vintage'] },
      { name: 'Ambient Texture', instrument: 'Texture', genre: 'Ambient', year: 2023, analog: false, brightness: 45, aggression: 10, width: 90, tags: ['airy', 'wide', 'smooth'] },
      { name: 'Rock Drum Kit', instrument: 'Drums', genre: 'Rock', year: 1990, analog: true, brightness: 80, aggression: 85, width: 75, tags: ['aggressive', 'bright', 'wide'] },
      { name: 'Classic String Section', instrument: 'Strings', genre: 'Classical', year: 1980, analog: true, brightness: 55, aggression: 20, width: 80, tags: ['warm', 'wide', 'organic'] },
    ];

    if (!query) return library;

    const lower = query.toLowerCase();
    return library.filter(p =>
      p.name.toLowerCase().includes(lower) ||
      p.instrument.toLowerCase().includes(lower) ||
      p.genre.toLowerCase().includes(lower) ||
      p.tags.some(t => t.includes(lower))
    );
  }

  displayResults(results) {
    const container = document.getElementById('browserResults');
    if (!container) return;
    container.innerHTML = '';

    if (results.length === 0) {
      container.innerHTML = '<div style="color:var(--text-muted);text-align:center;padding:40px;font-size:12px">No results found</div>';
      return;
    }

    results.forEach(r => {
      const item = document.createElement('div');
      item.style.cssText = `
        display:flex;align-items:center;gap:10px;padding:8px 10px;
        background:var(--bg-input);border-radius:6px;border:1px solid transparent;
        cursor:pointer;transition:all 0.15s;
      `;
      item.onmouseenter = () => { item.style.borderColor = 'var(--accent)'; item.style.background = 'var(--bg-surface)'; };
      item.onmouseleave = () => { item.style.borderColor = 'transparent'; item.style.background = 'var(--bg-input)'; };
      item.onclick = () => this.selectProfile(r);

      const tags = (r.tags || []).map(t =>
        `<span style="font-size:9px;padding:1px 5px;background:var(--accent-soft);color:var(--accent);border-radius:3px">${t}</span>`
      ).join('');

      item.innerHTML = `
        <div>
          <div style="font-weight:600;font-size:12px;color:var(--text-primary)">${r.name}</div>
          <div style="font-size:10px;color:var(--text-muted)">${r.instrument} · ${r.genre} · ${r.year} ${r.analog ? '· Analog' : ''}</div>
        </div>
        <div style="flex:1"></div>
        <div style="display:flex;gap:2px;flex-wrap:wrap;max-width:200px">${tags}</div>
      `;
      container.appendChild(item);
    });
  }

  selectProfile(profile) {
    SDNA.Bridge.sendMessage(9, btoa(JSON.stringify(profile)));
    const badge = document.getElementById('sourceBadge');
    if (badge) badge.textContent = profile.name;
    this.hide();
  }
}

SDNA.browser = new DNABrowserUI();
