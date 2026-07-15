class SoundDNAApp {
  constructor() {
    this.params = {};
    this.currentAB = 'a';
    this.currentProfile = null;
    this.history = [];
    this.historyPosition = -1;

    this.init();
  }

  init() {
    SDNA.Bridge.requestParameters();

    document.addEventListener('keydown', (e) => {
      if (e.ctrlKey && e.key === 'z') this.undo();
      if (e.ctrlKey && (e.key === 'y' || (e.key === 'Z' && e.shiftKey))) this.redo();
    });

    setInterval(() => {
      if (SDNA.dnaMap) SDNA.dnaMap.render();
    }, 100);

    setTimeout(() => SDNA.Bridge.requestParameters(), 500);
  }

  onParamChange(paramIdx, value) {
    this.params[paramIdx] = value;

    if (paramIdx >= 3 && paramIdx < 16) {
      const geneIdx = paramIdx - 3;
      if (SDNA.transfer) SDNA.transfer.setGeneAmount(geneIdx, value);
    }

    if (paramIdx >= 16 && paramIdx < 29) {
      const lockIdx = paramIdx - 16;
      if (SDNA.transfer) SDNA.transfer.setGeneLock(lockIdx, value > 0.5);
    }

    if (paramIdx === 29) {
      const pct = Math.round(value * 100);
      const slider = document.getElementById('morphSlider');
      const valEl = document.getElementById('morphValue');
      if (slider) slider.value = pct;
      if (valEl) valEl.textContent = pct + '%';
      if (SDNA.dnaMap) SDNA.dnaMap.setMorphPosition(pct);
    }
  }

  onControlChange(ctrlTag, value) {
  }

  onMessage(msgTag, dataSize, data) {
    if (dataSize === 0) return;

    let json;
    try {
      const decoded = window.atob(data);
      json = JSON.parse(decoded);
    } catch (e) {
      return;
    }

    switch (msgTag) {
      case 0: {
        if (SDNA.analyzer) SDNA.analyzer.updateReport(json);
        break;
      }
      case 1: {
        if (SDNA.analyzer) SDNA.analyzer.updateReport(json);
        break;
      }
      case 12: {
        if (json && json.params) {
          this.params = json.params;
        }
        break;
      }
      default:
        break;
    }
  }

  handleDrop(event, type) {
    event.preventDefault();
    const file = event.dataTransfer.files[0];
    if (!file) return;

    if (type === 'target') {
      this.loadAudioFile(file);
    } else if (type === 'source') {
      this.loadDNAFile(file);
    }
  }

  loadAudioFile(file) {
    const validTypes = ['audio/wav', 'audio/aiff', 'audio/flac', 'audio/x-wav', 'audio/x-aiff'];
    if (!validTypes.includes(file.type) && !file.name.match(/\.(wav|aiff?|flac)$/i)) {
      return;
    }

    const reader = new FileReader();
    reader.onload = (e) => {
      const dropzone = document.getElementById('targetDropzone');
      if (dropzone) {
        dropzone.classList.add('has-data');
        dropzone.querySelector('.dropzone-text').textContent = file.name;
      }
    };
    reader.readAsArrayBuffer(file);
  }

  loadDNAFile(file) {
    const reader = new FileReader();
    reader.onload = (e) => {
      const badge = document.getElementById('sourceBadge');
      if (badge) badge.textContent = file.name;
    };
    reader.readAsText(file);
  }

  setAB(mode) {
    this.currentAB = mode;
    document.querySelectorAll('.ab-btn').forEach(b => b.classList.remove('active'));
    const btn = document.querySelector(`.ab-btn[onclick*="${mode}"]`);
    if (btn) btn.classList.add('active');
  }

  onMixChange(value) {
    document.getElementById('mixValue').textContent = value + '%';
    SDNA.Bridge.sendParam(2, value / 100);
  }

  onGainChange(value) {
    const db = ((value - 50) / 50) * 18;
    document.getElementById('gainValue').textContent = (db > 0 ? '+' : '') + db.toFixed(1) + ' dB';
    SDNA.Bridge.sendParam(1, value / 100);
  }

  onMorphChange(value) {
    document.getElementById('morphValue').textContent = value + '%';
    SDNA.Bridge.sendParam(29, value / 100);
    if (SDNA.dnaMap) SDNA.dnaMap.setMorphPosition(parseFloat(value));
  }

  undo() {
    if (this.historyPosition > 0) {
      this.historyPosition--;
      this.restoreState(this.history[this.historyPosition]);
    }
  }

  redo() {
    if (this.historyPosition < this.history.length - 1) {
      this.historyPosition++;
      this.restoreState(this.history[this.historyPosition]);
    }
  }

  restoreState(state) {
    if (state && state.params) {
      for (const [key, val] of Object.entries(state.params)) {
        this.onParamChange(parseInt(key), val);
      }
    }
  }

  compare() {
    const a = {
      brightness: parseFloat(document.getElementById('targetBrightness')?.textContent || '0'),
      dynamicRange: parseFloat(document.getElementById('targetDR')?.textContent || '0'),
      stereoWidth: 0.6,
      noiseFloor: parseFloat(document.getElementById('targetNoise')?.textContent || '-90'),
      saturation: 0.5,
      centroid: 4000,
      distortion: 0.3
    };
    const b = {
      brightness: 0.7,
      dynamicRange: 18,
      stereoWidth: 0.8,
      noiseFloor: -75,
      saturation: 0.6,
      centroid: 5000,
      distortion: 0.4
    };
    if (SDNA.compare) SDNA.compare.show(a, b);
  }

  closeCompare() {
    if (SDNA.compare) SDNA.compare.close();
  }

  toggleBrowser() {
    if (SDNA.browser) SDNA.browser.toggle();
  }

  analyzeSource() {
    SDNA.Bridge.analyzeSource();
  }

  analyzeTarget() {
    SDNA.Bridge.analyzeTarget();
  }
}

window.app = new SoundDNAApp();
