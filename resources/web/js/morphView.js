class DNAMorphUI {
  constructor() {
    this.points = [];
    this.isPlaying = false;
    this.playInterval = null;
    this.playSpeed = 1000;
  }

  addPoint(profile) {
    this.points.push(profile);
    this.updateUI();
    GENO.Bridge.morphAdd(profile);
  }

  clear() {
    this.points = [];
    this.updateUI();
    GENO.Bridge.morphClear();
  }

  togglePlay() {
    if (this.isPlaying) {
      this.stop();
    } else {
      this.play();
    }
  }

  play() {
    if (this.points.length < 2) return;
    this.isPlaying = true;
    let pos = 0;
    this.playInterval = setInterval(() => {
      pos += 0.01 * (1000 / this.playSpeed);
      if (pos > 1) {
        pos = 0;
      }
      const slider = document.getElementById('morphSlider');
      if (slider) {
        slider.value = pos * 100;
        GENO.Bridge.sendParam(29, pos);
        if (GENO.dnaMap) GENO.dnaMap.setMorphPosition(pos * 100);
      }
    }, 10);
    this.updatePlayButton();
  }

  stop() {
    this.isPlaying = false;
    if (this.playInterval) {
      clearInterval(this.playInterval);
      this.playInterval = null;
    }
    this.updatePlayButton();
  }

  updatePlayButton() {
    const btn = document.getElementById('morphPlayBtn');
    if (btn) btn.textContent = this.isPlaying ? 'Stop' : 'Play';
  }

  updateUI() {
    const container = document.getElementById('dnaLayers');
    if (!container) return;

    const profile = GENO.transfer;
    const names = GENO.utils.geneNames;
    const colors = GENO.utils.geneColors;

    container.innerHTML = '';
    if (this.points.length === 0) {
      container.innerHTML = '<div class="dna-layer"><div class="dna-layer-color" style="background:var(--accent)"></div><span class="dna-layer-name">Default</span><span class="dna-layer-pct">100%</span></div>';
      return;
    }

    const totalPct = Math.max(1, this.points.length);
    this.points.forEach((pt, i) => {
      const pct = Math.round((1 / totalPct) * 100);
      const layer = document.createElement('div');
      layer.className = 'dna-layer';
      layer.innerHTML = `
        <div class="dna-layer-color" style="background:${colors[i % colors.length]}"></div>
        <span class="dna-layer-name">Point ${i + 1}</span>
        <span class="dna-layer-pct">${pct}%</span>
      `;
      container.appendChild(layer);
    });
  }
}

GENO.morph = new DNAMorphUI();
