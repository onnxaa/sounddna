class DNATransferUI {
  constructor() {
    this.genes = [];
    this.container = null;
  }

  init(containerId) {
    this.container = document.getElementById(containerId);
    if (!this.container) return;
    this.buildControls();
  }

  buildControls() {
    if (!this.container) return;
    this.container.innerHTML = '';

    const names = SDNA.utils.geneNames;
    const colors = SDNA.utils.geneColors;

    for (let i = 0; i < names.length; i++) {
      const group = document.createElement('div');
      group.className = 'slider-group';
      group.dataset.geneIdx = i;

      const row = document.createElement('div');
      row.className = 'slider-row';

      const lock = document.createElement('div');
      lock.className = 'lock-toggle';
      lock.dataset.geneIdx = i;
      lock.textContent = '🔓';
      lock.addEventListener('click', () => this.toggleLock(i));

      const label = document.createElement('span');
      label.className = 'slider-label';
      label.textContent = names[i];
      label.style.color = colors[i % colors.length];

      const slider = document.createElement('input');
      slider.type = 'range';
      slider.className = 'slider-control';
      slider.min = 0;
      slider.max = 100;
      slider.value = 50;
      slider.dataset.geneIdx = i;
      slider.addEventListener('input', (e) => this.onSliderChange(i, e.target.value));

      const value = document.createElement('span');
      value.className = 'slider-value';
      value.id = 'geneVal_' + i;
      value.textContent = '50%';

      row.appendChild(lock);
      row.appendChild(label);
      row.appendChild(slider);
      row.appendChild(value);
      group.appendChild(row);
      this.container.appendChild(group);

      this.genes[i] = {
        name: names[i],
        amount: 50,
        locked: false,
        slider,
        valueEl: value,
        lockEl: lock
      };
    }
  }

  onSliderChange(idx, val) {
    if (!this.genes[idx]) return;
    this.genes[idx].amount = parseInt(val);
    this.genes[idx].valueEl.textContent = val + '%';
    SDNA.Bridge.sendParam(idx + 3, parseInt(val) / 100);
  }

  toggleLock(idx) {
    if (!this.genes[idx]) return;
    const gene = this.genes[idx];
    gene.locked = !gene.locked;
    gene.lockEl.textContent = gene.locked ? '🔒' : '🔓';
    gene.lockEl.classList.toggle('locked', gene.locked);

    const paramIdx = idx + 3 + 13;
    SDNA.Bridge.sendParam(paramIdx, gene.locked ? 1 : 0);

    if (gene.locked) {
      gene.slider.style.opacity = '0.4';
      gene.slider.disabled = true;
    } else {
      gene.slider.style.opacity = '1';
      gene.slider.disabled = false;
    }
  }

  setGeneAmount(idx, value) {
    if (!this.genes[idx]) return;
    this.genes[idx].amount = Math.round(value * 100);
    this.genes[idx].slider.value = this.genes[idx].amount;
    this.genes[idx].valueEl.textContent = this.genes[idx].amount + '%';
  }

  setGeneLock(idx, locked) {
    if (!this.genes[idx]) return;
    this.genes[idx].locked = locked;
    this.genes[idx].lockEl.textContent = locked ? '🔒' : '🔓';
    this.genes[idx].lockEl.classList.toggle('locked', locked);
    if (locked) {
      this.genes[idx].slider.style.opacity = '0.4';
      this.genes[idx].slider.disabled = true;
    } else {
      this.genes[idx].slider.style.opacity = '1';
      this.genes[idx].slider.disabled = false;
    }
  }
}

SDNA.transfer = new DNATransferUI();

document.addEventListener('DOMContentLoaded', () => {
  SDNA.transfer.init('transferControls');
});
