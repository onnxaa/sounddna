class GenoApp {
  constructor() {
    this.params = {};
    this.currentAB = 'a';
    this.currentProfile = null;
    this.history = [];
    this.historyPosition = -1;

    this.init();
  }

  init() {
    GENO.Bridge.requestParameters();

    document.addEventListener('keydown', (e) => {
      if (e.ctrlKey && e.key === 'z') this.undo();
      if (e.ctrlKey && (e.key === 'y' || (e.key === 'Z' && e.shiftKey))) this.redo();
    });

    setInterval(() => {
      if (GENO.dnaMap) GENO.dnaMap.render();
    }, 100);

    setTimeout(() => GENO.Bridge.requestParameters(), 500);
  }

  onParamChange(paramIdx, value) {
    this.params[paramIdx] = value;

    if (paramIdx >= 3 && paramIdx < 17) {
      const geneIdx = GENO.utils.paramToGene[paramIdx - 3];
      if (GENO.transfer) GENO.transfer.setGeneAmount(geneIdx, value);
    }

    if (paramIdx >= 17 && paramIdx < 31) {
      const geneIdx = GENO.utils.paramToGene[paramIdx - 17];
      if (GENO.transfer) GENO.transfer.setGeneLock(geneIdx, value > 0.5);
    }

    if (paramIdx === 31) {
      const pct = Math.round(value * 100);
      const slider = document.getElementById('morphSlider');
      const valEl = document.getElementById('morphValue');
      if (slider) slider.value = pct;
      if (valEl) valEl.textContent = pct + '%';
      if (GENO.dnaMap) GENO.dnaMap.setMorphPosition(pct);
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
      case 14: {
        if (GENO.analyzer) GENO.analyzer.updateReport(json);
        if (json && json.features) {
          if (json.type === 'target') {
            this.onTargetProfile(json);
          } else if (json.type === 'source') {
            this.onSourceProfile(json);
          }
        }
        break;
      }
      case 15: {
        if (GENO.analyzer) GENO.analyzer.updateReport(json);
        break;
      }
      case 13: {
        if (json && json.params) {
          this.params = json.params;
        }
        break;
      }
      case 19: {
        if (json) this.onCaptureStatus(json.capturing);
        break;
      }
      default:
        break;
    }
  }

  onCaptureStatus(capturing) {
    const btn = document.getElementById('recordBtn');
    if (btn) {
      btn.classList.toggle('recording', capturing);
      btn.textContent = capturing ? '■ Stop' : '● Record';
    }
  }

  toggleCapture() {
    GENO.Bridge.sendMessage(18, '');
  }

  openFileDialog(type) {
    console.log('[GenoUI] openFileDialog type=' + type);
    const target = window.webkit?.messageHandlers?.iPlug;
    if (target) {
      target.postMessage(JSON.stringify({msg: "FILE_DIALOG", type: type}));
      console.log('[GenoUI] FILE_DIALOG posted to helper');
    } else {
      console.log('[GenoUI] ERROR: no webkit.messageHandlers.iPlug');
    }
  }

  loadFile(path, type) {
    console.log('[GenoUI] loadFile path=' + path + ' type=' + type);
    const tag = type === 'source' ? 17 : 16;
    // Base64-encode path because OnMessageFromWebView base64-decodes the data field
    const encoded = btoa(path);
    GENO.Bridge.sendMessage(tag, encoded);
    console.log('[GenoUI] sent tag=' + tag + ' encoded=' + encoded + ' to DSP');
    const name = path.split(/[/\\]/).pop();
    if (type === 'target') {
      let el = document.getElementById('targetFileName');
      if (!el) {
        el = document.createElement('div');
        el.id = 'targetFileName';
        el.style.cssText = 'font-size:11px;color:var(--accent);margin-bottom:4px';
        const report = document.getElementById('targetReport');
        if (report) report.parentNode.insertBefore(el, report);
      }
      el.textContent = 'Loaded: ' + name;
    } else if (type === 'source') {
      const badge = document.getElementById('sourceBadge');
      if (badge) badge.textContent = name;
    }
  }

  setAB(mode) {
    this.currentAB = mode;
    document.querySelectorAll('.ab-btn').forEach(b => b.classList.remove('active'));
    const btn = document.querySelector(`.ab-btn[onclick*="${mode}"]`);
    if (btn) btn.classList.add('active');
  }

  onMixChange(value) {
    document.getElementById('mixValue').textContent = value + '%';
    GENO.Bridge.sendParam(2, value / 100);
  }

  onGainChange(value) {
    const db = ((value - 50) / 50) * 18;
    document.getElementById('gainValue').textContent = (db > 0 ? '+' : '') + db.toFixed(1) + ' dB';
    GENO.Bridge.sendParam(1, value / 100);
  }

  onMorphChange(value) {
    document.getElementById('morphValue').textContent = value + '%';
    GENO.Bridge.sendParam(31, value / 100);
    if (GENO.dnaMap) GENO.dnaMap.setMorphPosition(parseFloat(value));
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
    if (GENO.compare) GENO.compare.show(a, b);
  }

  featuresToMap(f) {
    const c = GENO.utils.clamp;
    return {
      x: c(f.brightness || 0.5, 0, 1),
      y: c((f.dynamicRange || 18) / 30, 0, 1),
      z: c(((f.noiseFloor !== undefined ? f.noiseFloor : -90) + 120) / 90, 0, 1)
    };
  }

  applyProfileToGenes(f) {
    if (!GENO.transfer) return;
    const c = GENO.utils.clamp;
    const br = f.brightness || 0.5;
    const dr = f.dynamicRange || 18;
    const nf = f.noiseFloor !== undefined ? f.noiseFloor : -90;
    const ct = f.centroid || 3000;
    const fl = f.flatness || 0.3;
    const cf = f.crestFactor || 10;
    const sw = f.stereoWidth || 0.5;
    const pc = f.phaseCorrelation !== undefined ? f.phaseCorrelation : 0.5;
    const sat = f.saturation || 0.3;
    const dist = f.distortion || 0.2;

    const normCentroid = c(ct / 5000, 0, 1);

    GENO.transfer.setGeneAmount(0,  c(br, 0, 1));
    GENO.transfer.setGeneAmount(1,  c(dr / 30, 0, 1));
    GENO.transfer.setGeneAmount(2,  c((nf + 120) / 90, 0, 1));
    GENO.transfer.setGeneAmount(3,  c(1 - (pc + 1) / 2, 0, 1));
    GENO.transfer.setGeneAmount(4,  c(fl, 0, 1));
    GENO.transfer.setGeneAmount(5,  c(sw, 0, 1));
    GENO.transfer.setGeneAmount(6,  c((sat + dist) / 2, 0, 1));
    GENO.transfer.setGeneAmount(7,  c(cf / 20, 0, 1));
    GENO.transfer.setGeneAmount(8,  c(1 - normCentroid, 0, 1));
    GENO.transfer.setGeneAmount(9,  c(1 - fl, 0, 1));
    GENO.transfer.setGeneAmount(10, c(1 - normCentroid, 0, 1));
    GENO.transfer.setGeneAmount(11, c(normCentroid, 0, 1));
    GENO.transfer.setGeneAmount(12, c(1 - dr / 30, 0, 1));
    GENO.transfer.setGeneAmount(13, c(br, 0, 1));
  }

  onTargetProfile(json) {
    const f = json.features || {};
    const map = GENO.dnaMap;
    if (map) {
      const { x, y, z } = this.featuresToMap(f);
      map.updatePoint('Target', x, y, z, '#ff6b6b');
    }
    this.applyProfileToGenes(f);
  }

  onSourceProfile(json) {
    const badge = document.getElementById('sourceBadge');
    if (badge) badge.textContent = json.name || 'Custom DNA';
    const map = GENO.dnaMap;
    if (map) {
      const { x, y, z } = this.featuresToMap(json.features || {});
      map.updatePoint('Source', x, y, z, '#00cec9');
    }
  }

  closeCompare() {
    if (GENO.compare) GENO.compare.close();
  }

  toggleBrowser() {
    if (GENO.browser) GENO.browser.toggle();
  }

  analyzeSource() {
    console.log('[GenoUI] analyzeSource clicked');
    GENO.Bridge.analyzeSource();
  }

  analyzeTarget() {
    console.log('[GenoUI] analyzeTarget clicked');
    GENO.Bridge.analyzeTarget();
  }

}

window.app = new GenoApp();
