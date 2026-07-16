#pragma once
#include <vector>
#include <string>
#include <array>
#include <atomic>
#include <cstdint>

constexpr int kNumSpectralBands = 128;
constexpr int kNumHarmonics = 32;
constexpr int kNumNoiseCoefs = 4;
constexpr int kFFTSize = 2048;
constexpr int kFFTHop = 512;
constexpr int kMaxDNALayers = 8;
constexpr int kMaxBlockSize = 8192;
constexpr double kSmoothTimeMs = 20.0;

enum class DNAGene {
  Tone = 0,
  Dynamics,
  Noise,
  Space,
  Movement,
  Stereo,
  Texture,
  Punch,
  Body,
  Resonance,
  Warmth,
  Sparkle,
  Glue,
  Air,
  Count
};

struct SpectralFeatures {
  std::vector<double> spectralEnvelope;
  std::vector<double> harmonicProfile;
  double centroid = 0.0;
  double spread = 0.0;
  double rolloff = 0.0;
  double brightness = 0.0;
  double spectralFlatness = 0.0;
  double pitch = 0.0;
  double pitchConfidence = 0.0;
};

struct DynamicFeatures {
  double rms = 0.0;
  double peak = 0.0;
  double crestFactor = 0.0;
  double dynamicRange = 0.0;
  double attackMs = 0.0;
  double releaseMs = 0.0;
  double compressionRatio = 1.0;
  double envelopeMean = 0.0;
};

struct StereoFeatures {
  double width = 0.0;
  double phaseCorrelation = 0.0;
  double balance = 0.0;
  double phaseDrift = 0.0;
  bool isMono = true;
};

struct NoiseFeatures {
  double noiseFloorDb = -90.0;
  double noiseShape[kNumNoiseCoefs] = {};
  double signalToNoise = 90.0;
  double humContent = 0.0;
  double spectralTilt = 0.0;
};

struct TextureFeatures {
  double saturationAmount = 0.0;
  double harmonicDistortion = 0.0;
  double analogWarmth = 0.0;
  double tapeSaturation = 0.0;
  double transientShape = 0.0;
};

struct SpaceFeatures {
  double decayTime = 0.0;
  double earlyReflections = 0.0;
  double roomSize = 0.5;
  double damping = 0.5;
  double diffusion = 0.5;
};

struct MovementFeatures {
  double modulationRate = 0.0;
  double modulationDepth = 0.0;
  double tremoloAmount = 0.0;
  double vibratoAmount = 0.0;
  double wobbleRate = 0.0;
};

struct DNAProfile {
  SpectralFeatures spectral;
  DynamicFeatures dynamics;
  StereoFeatures stereo;
  NoiseFeatures noise;
  TextureFeatures texture;
  SpaceFeatures space;
  MovementFeatures movement;

  std::string sourceName;
  std::string instrument;
  std::string category;
  double confidence = 0.0;
  int year = 0;
  bool isAnalog = true;

  std::vector<double> ToFeatureVector() const;
  static DNAProfile FromFeatureVector(const std::vector<double>& vec);
  double SimilarityTo(const DNAProfile& other) const;
  DNAProfile InterpolateWith(const DNAProfile& other, double t) const;
};

struct DNATransferParams {
  std::array<double, static_cast<int>(DNAGene::Count)> amounts = {};
  std::array<bool, static_cast<int>(DNAGene::Count)> locks = {};

  DNATransferParams() {
    amounts.fill(0.5);
    locks.fill(false);
  }
};
