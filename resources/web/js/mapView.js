class GenoMap {
  constructor(canvasId) {
    this.canvas = document.getElementById(canvasId);
    this.ctx = this.canvas.getContext('2d');
    this.points = [];
    this.selectedIndex = -1;
    this.rotationX = 0;
    this.rotationY = 0;
    this.dragStart = null;
    this.zoom = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.morphPos = 0;
    this.animId = null;

    this.initDefaultPoints();
    this.setupEvents();
    this.resize();
    this.render();
  }

  initDefaultPoints() {
    const pts = [
      { name: 'Piano', x: 0.2, y: 0.2, z: 0.3, color: '#6c5ce7' },
      { name: 'Guitar', x: 0.7, y: 0.25, z: 0.4, color: '#00cec9' },
      { name: 'Brass', x: 0.8, y: 0.7, z: 0.3, color: '#fdcb6e' },
      { name: 'Pad', x: 0.3, y: 0.5, z: 0.8, color: '#74b9ff' },
      { name: 'Vocal', x: 0.5, y: 0.35, z: 0.5, color: '#ff7675' },
      { name: 'Bass', x: 0.25, y: 0.8, z: 0.25, color: '#a29bfe' },
      { name: 'Strings', x: 0.4, y: 0.3, z: 0.7, color: '#55efc4' },
      { name: 'Drums', x: 0.75, y: 0.8, z: 0.1, color: '#ff9ff3' },
    ];
    this.points = pts.map(p => ({ ...p, selected: false }));
  }

  setPoints(pts) {
    this.points = pts;
    this.render();
  }

  setMorphPosition(pos) {
    this.morphPos = pos;
    this.render();
  }

  resize() {
    const rect = this.canvas.parentElement.getBoundingClientRect();
    this.canvas.width = rect.width * window.devicePixelRatio;
    this.canvas.height = rect.height * window.devicePixelRatio;
    this.canvas.style.width = rect.width + 'px';
    this.canvas.style.height = rect.height + 'px';
    this.ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
    this.render();
  }

  setupEvents() {
    window.addEventListener('resize', () => this.resize());

    this.canvas.addEventListener('mousedown', (e) => {
      this.dragStart = { x: e.clientX, y: e.clientY, rotX: this.rotationX, rotY: this.rotationY };
    });

    this.canvas.addEventListener('mousemove', (e) => {
      if (this.dragStart) {
        const dx = e.clientX - this.dragStart.x;
        const dy = e.clientY - this.dragStart.y;
        this.rotationX = this.dragStart.rotX + dy * 0.01;
        this.rotationY = this.dragStart.rotY + dx * 0.01;
        this.render();
      }
    });

    this.canvas.addEventListener('mouseup', () => { this.dragStart = null; });
    this.canvas.addEventListener('mouseleave', () => { this.dragStart = null; });

    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.zoom = GENO.utils.clamp(this.zoom + (e.deltaY > 0 ? -0.1 : 0.1), 0.3, 3);
      this.render();
    });
  }

  project3D(x, y, z) {
    const cx = x - 0.5;
    const cy = y - 0.5;
    const cz = z - 0.5;

    const cosX = Math.cos(this.rotationX);
    const sinX = Math.sin(this.rotationX);
    const cosY = Math.cos(this.rotationY);
    const sinY = Math.sin(this.rotationY);

    let px = cx * cosY + cz * sinY;
    let py = cx * sinX * sinY + cy * cosX - cz * sinX * cosY;
    let pz = -cx * cosX * sinY + cy * sinX + cz * cosX * cosY;

    const w = this.canvas.width / window.devicePixelRatio;
    const h = this.canvas.height / window.devicePixelRatio;
    const scale = Math.min(w, h) * 0.35 * this.zoom;
    const perspective = 2 / (2 + pz);

    return {
      sx: w / 2 + px * scale * perspective + this.offsetX,
      sy: h / 2 + py * scale * perspective + this.offsetY,
      sz: pz,
      perspective
    };
  }

  render() {
    const ctx = this.ctx;
    const w = this.canvas.width / window.devicePixelRatio;
    const h = this.canvas.height / window.devicePixelRatio;

    ctx.clearRect(0, 0, w, h);

    // Background
    const grad = ctx.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, w * 0.7);
    grad.addColorStop(0, '#1a1d28');
    grad.addColorStop(1, '#0a0b0f');
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, w, h);

    // Grid
    ctx.strokeStyle = '#1e2230';
    ctx.lineWidth = 0.5;
    const gridSize = 10;
    for (let i = 0; i <= gridSize; i++) {
      const t = i / gridSize;
      const p1 = this.project3D(t, 0, 0);
      const p2 = this.project3D(t, 1, 0);
      const p3 = this.project3D(0, t, 0);
      const p4 = this.project3D(1, t, 0);
      ctx.beginPath();
      ctx.moveTo(p1.sx, p1.sy);
      ctx.lineTo(p2.sx, p2.sy);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(p3.sx, p3.sy);
      ctx.lineTo(p4.sx, p4.sy);
      ctx.stroke();
    }

    // Lines between nearby points
    ctx.strokeStyle = 'rgba(108,92,231,0.15)';
    ctx.lineWidth = 0.5;
    for (let i = 0; i < this.points.length; i++) {
      for (let j = i + 1; j < this.points.length; j++) {
        const pi = this.project3D(this.points[i].x, this.points[i].y, this.points[i].z);
        const pj = this.project3D(this.points[j].x, this.points[j].y, this.points[j].z);
        const dist = Math.hypot(pi.sx - pj.sx, pi.sy - pj.sy);
        if (dist < 120) {
          ctx.globalAlpha = 1 - dist / 120;
          ctx.beginPath();
          ctx.moveTo(pi.sx, pi.sy);
          ctx.lineTo(pj.sx, pj.sy);
          ctx.stroke();
          ctx.globalAlpha = 1;
        }
      }
    }

    // Points sorted by depth
    const projected = this.points.map((p, i) => ({
      ...p,
      proj: this.project3D(p.x, p.y, p.z),
      idx: i
    }));
    projected.sort((a, b) => a.proj.sz - b.proj.sz);

    // Morph position indicator
    if (this.morphPos > 0) {
      const morphPoint = this.getMorphPoint();
      const mp = this.project3D(morphPoint.x, morphPoint.y, morphPoint.z);
      ctx.beginPath();
      ctx.arc(mp.sx, mp.sy, 8, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(108,92,231,0.3)';
      ctx.fill();
      ctx.strokeStyle = '#6c5ce7';
      ctx.lineWidth = 2;
      ctx.stroke();
    }

    for (const p of projected) {
      const { sx, sy, sz, perspective } = p.proj;
      const size = (6 + sz * 2) * this.zoom * perspective;

      // Glow
      const glow = ctx.createRadialGradient(sx, sy, 0, sx, sy, size * 3);
      glow.addColorStop(0, p.color + '40');
      glow.addColorStop(1, p.color + '00');
      ctx.fillStyle = glow;
      ctx.beginPath();
      ctx.arc(sx, sy, size * 3, 0, Math.PI * 2);
      ctx.fill();

      // Point
      ctx.beginPath();
      ctx.arc(sx, sy, size, 0, Math.PI * 2);
      ctx.fillStyle = p.color;
      ctx.fill();
      ctx.strokeStyle = 'rgba(255,255,255,0.3)';
      ctx.lineWidth = 1;
      ctx.stroke();

      // Label
      ctx.fillStyle = '#e8eaed';
      ctx.font = '10px Inter, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(p.name, sx, sy + size + 14);
    }
  }

  getMorphPoint() {
    const t = this.morphPos / 100;
    const totalPoints = this.points.length;
    if (totalPoints === 0) return { x: 0.5, y: 0.5, z: 0.5 };
    if (totalPoints === 1) return this.points[0];
    const segIdx = Math.min(Math.floor(t * (totalPoints - 1)), totalPoints - 2);
    const segT = (t * (totalPoints - 1)) - segIdx;
    const a = this.points[segIdx];
    const b = this.points[Math.min(segIdx + 1, totalPoints - 1)];
    return {
      x: GENO.utils.lerp(a.x, b.x, segT),
      y: GENO.utils.lerp(a.y, b.y, segT),
      z: GENO.utils.lerp(a.z, b.z, segT),
      name: a.name + ' → ' + b.name
    };
  }
}

GENO.dnaMap = null;

document.addEventListener('DOMContentLoaded', () => {
  GENO.dnaMap = new GenoMap('dnaMapCanvas');
});
