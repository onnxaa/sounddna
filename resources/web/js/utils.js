// ============================================================
// iPlug2 WebView Bridge Functions
// These are called by the native C++ side
// ============================================================

// If IPlugSendMsg is not injected by native side (e.g. helper process),
// define a no-op fallback so app.js init() doesn't throw
if (typeof IPlugSendMsg === 'undefined') {
  window.IPlugSendMsg = function() {};
}

function SPVFD(paramIdx, val) {
  if (window.app) app.onParamChange(paramIdx, val);
}

function SCVFD(ctrlTag, val) {
  if (window.app) app.onControlChange(ctrlTag, val);
}

function SCMFD(ctrlTag, msgTag, msg) {
}

function SAMFD(msgTag, dataSize, msg) {
  if (window.app) app.onMessage(msgTag, dataSize, msg || '');
}

function SMMFD(statusByte, dataByte1, dataByte2) {
}

function SSMFD(offset, size, msg) {
}

// ============================================================
// Functions called by JS to send data to the native C++ side
// ============================================================

function SAMFUI(msgTag, ctrlTag, data) {
  var message = {
    "msg": "SAMFUI",
    "msgTag": msgTag,
    "ctrlTag": ctrlTag !== undefined ? ctrlTag : -1,
    "data": data || ""
  };
  IPlugSendMsg(message);
}

function SMMFUI(statusByte, dataByte1, dataByte2) {
  var message = {
    "msg": "SMMFUI",
    "statusByte": statusByte,
    "dataByte1": dataByte1,
    "dataByte2": dataByte2
  };
  IPlugSendMsg(message);
}

function SSMFUI(data) {
  var message = {
    "msg": "SSMFUI",
    "data": data || ""
  };
  IPlugSendMsg(message);
}

function SPVFUI(paramIdx, value) {
  var message = {
    "msg": "SPVFUI",
    "paramIdx": parseInt(paramIdx),
    "value": value
  };
  IPlugSendMsg(message);
}

function EPCFUI(paramIdx) {
  if (paramIdx < 0) return;
  var message = {
    "msg": "EPCFUI",
    "paramIdx": parseInt(paramIdx)
  };
  IPlugSendMsg(message);
}

function BPCFUI(paramIdx) {
  if (paramIdx < 0) return;
  var message = {
    "msg": "BPCFUI",
    "paramIdx": parseInt(paramIdx)
  };
  IPlugSendMsg(message);
}

// ============================================================
// SoundDNA Application Bridge & Utilities
// ============================================================

window.SDNA = window.SDNA || {};

SDNA.utils = {
  clamp: (v, min, max) => Math.min(Math.max(v, min), max),

  dbToAmp: (db) => Math.pow(10, db / 20),

  ampToDb: (amp) => amp > 0 ? 20 * Math.log10(amp) : -144,

  lerp: (a, b, t) => a + (b - a) * t,

  formatPct: (v) => Math.round(v) + '%',

  formatDb: (v) => (v > 0 ? '+' : '') + v.toFixed(1) + ' dB',

  parseJSON: (str) => {
    try { return JSON.parse(str); } catch(e) { return null; }
  },

  debounce: (fn, delay) => {
    let timer;
    return (...args) => {
      clearTimeout(timer);
      timer = setTimeout(() => fn(...args), delay);
    };
  },

  geneColors: [
    '#6c5ce7', '#00cec9', '#fdcb6e', '#ff7675',
    '#74b9ff', '#a29bfe', '#55efc4', '#ff9ff3',
    '#feca57', '#ff6348', '#7bed9f', '#70a1ff',
    '#5352ed', '#e17055'
  ],

  geneNames: [
    'Tone', 'Dynamics', 'Noise', 'Space',
    'Movement', 'Stereo', 'Texture', 'Punch',
    'Body', 'Resonance', 'Warmth', 'Sparkle', 'Glue', 'Air'
  ],

  // Maps EParams (C++) to DNAGene index
  // param 3..16 are amounts, but EParams order differs from DNAGene order
  paramToGene: [
    0,  // param 3 = kToneAmount → DNAGene::Tone
    1,  // param 4 = kDynamicsAmount → DNAGene::Dynamics
    2,  // param 5 = kNoiseAmount → DNAGene::Noise
    5,  // param 6 = kStereoSpatialAmount → DNAGene::Stereo
    13, // param 7 = kAirAmount → DNAGene::Air
    4,  // param 8 = kMovementAmount → DNAGene::Movement
    3,  // param 9 = kSpaceAmount → DNAGene::Space
    6,  // param 10 = kTextureAmount → DNAGene::Texture
    7,  // param 11 = kPunchAmount → DNAGene::Punch
    8,  // param 12 = kBodyAmount → DNAGene::Body
    9,  // param 13 = kResonanceAmount → DNAGene::Resonance
    10, // param 14 = kWarmthAmount → DNAGene::Warmth
    11, // param 15 = kSparkleAmount → DNAGene::Sparkle
    12, // param 16 = kGlueAmount → DNAGene::Glue
  ],

  geneToParam: [
    3,  // DNAGene::Tone (0) → kToneAmount
    4,  // DNAGene::Dynamics (1) → kDynamicsAmount
    5,  // DNAGene::Noise (2) → kNoiseAmount
    9,  // DNAGene::Space (3) → kSpaceAmount
    8,  // DNAGene::Movement (4) → kMovementAmount
    6,  // DNAGene::Stereo (5) → kStereoSpatialAmount
    10, // DNAGene::Texture (6) → kTextureAmount
    11, // DNAGene::Punch (7) → kPunchAmount
    12, // DNAGene::Body (8) → kBodyAmount
    13, // DNAGene::Resonance (9) → kResonanceAmount
    14, // DNAGene::Warmth (10) → kWarmthAmount
    15, // DNAGene::Sparkle (11) → kSparkleAmount
    16, // DNAGene::Glue (12) → kGlueAmount
    7,  // DNAGene::Air (13) → kAirAmount
  ]
};

SDNA.Bridge = {
  sendMessage: (msgTag, data) => {
    if (typeof SAMFUI !== 'undefined') {
      SAMFUI(msgTag, -1, data || '');
    }
  },

  sendParam: (paramIdx, value) => {
    if (typeof SPVFUI !== 'undefined') {
      SPVFUI(paramIdx, value);
    }
  },

  requestParameters: () => {
    SDNA.Bridge.sendMessage(13, '');
  },

  analyzeSource: () => {
    SDNA.Bridge.sendMessage(0, '');
  },

  analyzeTarget: () => {
    SDNA.Bridge.sendMessage(1, '');
  },

  loadDNA: (json) => {
    SDNA.Bridge.sendMessage(2, btoa(JSON.stringify(json)));
  },

  saveDNA: (json) => {
    SDNA.Bridge.sendMessage(3, btoa(JSON.stringify(json)));
  },

  morphAdd: (profile) => {
    SDNA.Bridge.sendMessage(6, btoa(JSON.stringify(profile)));
  },

  morphClear: () => {
    SDNA.Bridge.sendMessage(7, '');
  },

  searchBrowser: (query) => {
    SDNA.Bridge.sendMessage(8, btoa(JSON.stringify(query)));
  },

  compareProfiles: (profileA, profileB) => {
    SDNA.Bridge.sendMessage(10, btoa(JSON.stringify({ a: profileA, b: profileB })));
  }
};
