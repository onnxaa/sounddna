class DNACompareUI {
  constructor() {
    this.visible = false;
    this.profileA = null;
    this.profileB = null;
  }

  show(profileA, profileB) {
    this.profileA = profileA;
    this.profileB = profileB;
    const overlay = document.getElementById('compareOverlay');
    if (overlay) overlay.classList.add('active');
    this.visible = true;
    this.render();
  }

  close() {
    const overlay = document.getElementById('compareOverlay');
    if (overlay) overlay.classList.remove('active');
    this.visible = false;
  }

  render() {
    if (!this.profileA || !this.profileB) return;

    this.drawRadarChart();
    this.renderDiffList();
  }

  drawRadarChart() {
    const canvas = document.getElementById('compareRadar');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width * window.devicePixelRatio;
    canvas.height = rect.height * window.devicePixelRatio;
    canvas.style.width = rect.width + 'px';
    canvas.style.height = rect.height + 'px';
    ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

    const w = rect.width;
    const h = rect.height;
    const cx = w / 2;
    const cy = h / 2;
    const radius = Math.min(w, h) * 0.35;

    const features = [
      { key: 'brightness', label: 'Brightness' },
      { key: 'dynamicRange', label: 'Dynamics' },
      { key: 'stereoWidth', label: 'Stereo' },
      { key: 'noiseFloor', label: 'Noise' },
      { key: 'saturation', label: 'Texture' },
      { key: 'centroid', label: 'Tone' },
    ];

    ctx.clearRect(0, 0, w, h);

    // Background
    ctx.fillStyle = '#0a0b0f';
    ctx.fillRect(0, 0, w, h);

    // Radar grid
    const numAxes = features.length;
    for (let ring = 1; ring <= 4; ring++) {
      const r = (radius / 4) * ring;
      ctx.beginPath();
      for (let i = 0; i <= numAxes; i++) {
        const angle = (Math.PI * 2 * i) / numAxes - Math.PI / 2;
        const x = cx + r * Math.cos(angle);
        const y = cy + r * Math.sin(angle);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.closePath();
      ctx.strokeStyle = '#1e2230';
      ctx.lineWidth = 0.5;
      ctx.stroke();
    }

    // Axes
    for (let i = 0; i < numAxes; i++) {
      const angle = (Math.PI * 2 * i) / numAxes - Math.PI / 2;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + radius * Math.cos(angle), cy + radius * Math.sin(angle));
      ctx.strokeStyle = '#1e2230';
      ctx.lineWidth = 0.5;
      ctx.stroke();

      ctx.fillStyle = '#6b7185';
      ctx.font = '9px Inter, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(features[i].label,
        cx + (radius + 18) * Math.cos(angle),
        cy + (radius + 18) * Math.sin(angle));
    }

    // Normalize values
    const n = (v, def) => v !== undefined && v !== null ? v : def;
    const normA = [
      n(this.profileA.brightness, 0.5),
      n(this.profileA.dynamicRange, 15),
      n(this.profileA.stereoWidth, 0.5),
      n(this.profileA.noiseFloor, -80),
      n(this.profileA.saturation, 0.5),
      n(this.profileA.centroid, 3000),
    ].map((v, i) => {
      const maxVals = [1, 30, 1, 90, 1, 8000];
      return i === 3 ? Math.min(Math.abs(v) / maxVals[i], 1) : Math.min(v / maxVals[i], 1);
    });
    const normB = [
      n(this.profileB.brightness, 0.5),
      n(this.profileB.dynamicRange, 15),
      n(this.profileB.stereoWidth, 0.5),
      n(this.profileB.noiseFloor, -80),
      n(this.profileB.saturation, 0.5),
      n(this.profileB.centroid, 3000),
    ].map((v, i) => {
      const maxVals = [1, 30, 1, 90, 1, 8000];
      return i === 3 ? Math.min(Math.abs(v) / maxVals[i], 1) : Math.min(v / maxVals[i], 1);
    });

    // Draw profile A
    this.drawRadarPolygon(ctx, cx, cy, radius, numAxes, normA, 'rgba(108,92,231,0.3)', '#6c5ce7');
    // Draw profile B
    this.drawRadarPolygon(ctx, cx, cy, radius, numAxes, normB, 'rgba(0,206,201,0.3)', '#00cec9');

    ctx.fillStyle = '#6b7185';
    ctx.font = '10px Inter, sans-serif';
    ctx.textAlign = 'left';
    ctx.fillRect(w - 130, 12, 10, 10);
    ctx.fillStyle = '#6c5ce7';
    ctx.fillRect(w - 130, 12, 10, 10);
    ctx.fillStyle = '#e8eaed';
    ctx.fillText('Source A', w - 116, 21);
    ctx.fillStyle = '#00cec9';
    ctx.fillRect(w - 130, 28, 10, 10);
    ctx.fillStyle = '#e8eaed';
    ctx.fillText('Source B', w - 116, 37);
  }

  drawRadarPolygon(ctx, cx, cy, radius, numAxes, values, fillColor, strokeColor) {
    ctx.beginPath();
    for (let i = 0; i <= numAxes; i++) {
      const idx = i % numAxes;
      const angle = (Math.PI * 2 * idx) / numAxes - Math.PI / 2;
      const r = radius * Math.max(0.05, Math.min(1, values[idx]));
      const x = cx + r * Math.cos(angle);
      const y = cy + r * Math.sin(angle);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.closePath();
    ctx.fillStyle = fillColor;
    ctx.fill();
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1.5;
    ctx.stroke();
  }

  renderDiffList() {
    const container = document.getElementById('compareDiffList');
    if (!container) return;
    container.innerHTML = '';

    const val = (p, key, def) => p[key] !== undefined && p[key] !== null ? p[key] : def;
    const features = [
      { key: 'brightness', label: 'Brightness', a: val(this.profileA, 'brightness', 0), b: val(this.profileB, 'brightness', 0) },
      { key: 'dynamicRange', label: 'Dynamic Range', a: val(this.profileA, 'dynamicRange', 0), b: val(this.profileB, 'dynamicRange', 0) },
      { key: 'stereoWidth', label: 'Stereo Width', a: val(this.profileA, 'stereoWidth', 0), b: val(this.profileB, 'stereoWidth', 0) },
      { key: 'noiseFloor', label: 'Noise Floor', a: val(this.profileA, 'noiseFloor', -90), b: val(this.profileB, 'noiseFloor', -90) },
      { key: 'saturation', label: 'Saturation', a: val(this.profileA, 'saturation', 0), b: val(this.profileB, 'saturation', 0) },
      { key: 'distortion', label: 'Distortion', a: val(this.profileA, 'distortion', 0), b: val(this.profileB, 'distortion', 0) },
    ];

    for (const feat of features) {
      const diff = feat.b - feat.a;
      const pct = diff !== 0 ? ((diff / (Math.abs(feat.a) || 1)) * 100).toFixed(0) : '0';
      const sign = diff > 0 ? '+' : '';

      const row = document.createElement('div');
      row.className = 'analyzer-row';
      row.style.marginBottom = '2px';
      row.innerHTML = `
        <span class="analyzer-label">${feat.label}</span>
        <span class="analyzer-value" style="color:${diff > 0 ? 'var(--success)' : diff < 0 ? 'var(--danger)' : 'var(--text-muted)'}">
          ${sign}${pct}%
        </span>
      `;
      container.appendChild(row);
    }
  }
}

GENO.compare = new DNACompareUI();
