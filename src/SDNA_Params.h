#pragma once

enum EParams {
  kInputGain = 0,
  kOutputGain,
  kMix,

  kToneAmount,
  kDynamicsAmount,
  kNoiseAmount,
  kStereoSpatialAmount,
  kAirAmount,
  kMovementAmount,
  kSpaceAmount,
  kTextureAmount,
  kPunchAmount,
  kBodyAmount,
  kResonanceAmount,
  kWarmthAmount,
  kSparkleAmount,
  kGlueAmount,

  kToneLock,
  kDynamicsLock,
  kNoiseLock,
  kStereoSpatialLock,
  kAirLock,
  kMovementLock,
  kSpaceLock,
  kTextureLock,
  kPunchLock,
  kBodyLock,
  kResonanceLock,
  kWarmthLock,
  kSparkleLock,
  kGlueLock,

  kMorphPosition,
  kNumParams
};

enum EMsgTags {
  kMsgTagAnalyzeSource = 0,
  kMsgTagAnalyzeTarget,
  kMsgTagLoadDNA,
  kMsgTagSaveDNA,
  kMsgTagDeleteDNA,
  kMsgTagMorphAdd,
  kMsgTagMorphClear,
  kMsgTagMorphPlay,
  kMsgTagBrowserSearch,
  kMsgTagBrowserSelect,
  kMsgTagCompareProfiles,
  kMsgTagUndo,
  kMsgTagRedo,
  kMsgTagRequestParameters,
  kMsgTagSendDNAProfile,
  kMsgTagSendAnalyzerReport
};

static const char* kGeneNames[] = {
  "Tone", "Dynamics", "Noise", "Space", "Movement",
  "Stereo", "Texture", "Punch", "Body", "Resonance",
  "Warmth", "Sparkle", "Glue", "Air"
};

static_assert(sizeof(kGeneNames) / sizeof(kGeneNames[0]) == static_cast<int>(DNAGene::Count),
              "Gene names array must match DNAGene enum count");
