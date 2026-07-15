class DNAAnalyzerUI {
  constructor() {
    this.currentReport = null;
  }

  updateReport(data) {
    this.currentReport = data;

    const setText = (id, val, suffix = '') => {
      const el = document.getElementById(id);
      if (el) el.textContent = val !== undefined && val !== null ? val + suffix : '—';
    };

    if (data.type === 'source') {
      setText('sourceInstrument', data.instrument || 'Unknown');
      setText('sourceConfidence', data.confidence ? (data.confidence * 100).toFixed(0) + '%' : '—');
      const badge = document.getElementById('sourceBadge');
      if (badge) badge.textContent = data.name || 'Custom DNA';
    }

    if (data.instrument) setText('targetInstrument', data.instrument);
    if (data.confidence) setText('targetConfidence', (data.confidence * 100).toFixed(0) + '%');
    if (data.pitch) setText('targetPitch', data.pitch.toFixed(1) + ' Hz');
    if (data.dynamicRange) setText('targetDR', data.dynamicRange.toFixed(1) + ' dB');
    if (data.brightness) setText('targetBrightness', (data.brightness * 100).toFixed(0) + '%');
    if (data.noiseFloor) setText('targetNoise', data.noiseFloor.toFixed(1) + ' dB');

    if (data.spectralEnvelope && data.harmonicProfile) {
      this.drawSpectralPlot(data.spectralEnvelope, data.harmonicProfile);
    }
  }

  drawSpectralPlot(envelope, harmonics) {
    const existingPlot = document.getElementById('spectralPlot');
    if (!existingPlot) {
      const container = document.getElementById('targetReport');
      if (!container) return;
      const canvas = document.createElement('canvas');
      canvas.id = 'spectralPlot';
      canvas.style.width = '100%';
      canvas.style.height = '80px';
      canvas.style.borderRadius = '4px';
      canvas.style.marginTop = '4px';
      container.appendChild(canvas);
    }
    const canvas = document.getElementById('spectralPlot');
    const ctx = canvas.getContext('2d');
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width * window.devicePixelRatio;
    canvas.height = 80 * window.devicePixelRatio;
    canvas.style.height = '80px';
    ctx.scale(window.devicePixelRatio, window.devicePixelRatio);

    const w = canvas.width / window.devicePixelRatio;
    const h = 80;

    ctx.clearRect(0, 0, w, h);

    // Envelope
    if (envelope && envelope.length > 1) {
      ctx.beginPath();
      ctx.moveTo(0, h);
      for (let i = 0; i < envelope.length; i++) {
        const x = (i / (envelope.length - 1)) * w;
        const y = h - (envelope[i] * h * 0.8);
        ctx.lineTo(x, y);
      }
      ctx.lineTo(w, h);
      ctx.closePath();
      ctx.fillStyle = 'rgba(108,92,231,0.15)';
      ctx.fill();
      ctx.strokeStyle = '#6c5ce7';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let i = 0; i < envelope.length; i++) {
        const x = (i / (envelope.length - 1)) * w;
        const y = h - (envelope[i] * h * 0.8);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    // Harmonics as bars
    if (harmonics && harmonics.length > 1) {
      const barW = w / harmonics.length * 0.6;
      const gap = w / harmonics.length * 0.4;
      for (let i = 0; i < harmonics.length; i++) {
        const x = i * (w / harmonics.length) + gap / 2;
        const barH = harmonics[i] * h * 0.7;
        ctx.fillStyle = `rgba(0,206,201,${0.3 + harmonics[i] * 0.7})`;
        ctx.fillRect(x, h - barH, barW, barH);
      }
    }
  }
}

SDNA.analyzer = new DNAAnalyzerUI();
