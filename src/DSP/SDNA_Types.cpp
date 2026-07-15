#include "SDNA_Types.h"
#include <cmath>
#include <numeric>
#include <algorithm>

std::vector<double> DNAProfile::ToFeatureVector() const {
  std::vector<double> vec;
  auto addVec = [&](const std::vector<double>& v) {
    vec.insert(vec.end(), v.begin(), v.end());
  };

  addVec(spectral.spectralEnvelope);
  addVec(spectral.harmonicProfile);
  vec.push_back(spectral.centroid);
  vec.push_back(spectral.spread);
  vec.push_back(spectral.rolloff);
  vec.push_back(spectral.brightness);
  vec.push_back(spectral.spectralFlatness);

  vec.push_back(dynamics.rms);
  vec.push_back(dynamics.peak);
  vec.push_back(dynamics.crestFactor);
  vec.push_back(dynamics.dynamicRange);

  vec.push_back(stereo.width);
  vec.push_back(stereo.phaseCorrelation);

  vec.push_back(noise.noiseFloorDb);
  for (int i = 0; i < kNumNoiseCoefs; ++i)
    vec.push_back(noise.noiseShape[i]);

  vec.push_back(texture.saturationAmount);
  vec.push_back(texture.harmonicDistortion);
  vec.push_back(texture.transientShape);

  return vec;
}

DNAProfile DNAProfile::FromFeatureVector(const std::vector<double>& vec) {
  DNAProfile profile;
  size_t pos = 0;

  auto readVec = [&](size_t n) {
    std::vector<double> v;
    for (size_t i = 0; i < n && pos + i < vec.size(); ++i)
      v.push_back(vec[pos + i]);
    pos += n;
    return v;
  };

  auto read = [&]() {
    return (pos < vec.size()) ? vec[pos++] : 0.0;
  };

  profile.spectral.spectralEnvelope = readVec(kNumSpectralBands);
  profile.spectral.harmonicProfile = readVec(kNumHarmonics);
  profile.spectral.centroid = read();
  profile.spectral.spread = read();
  profile.spectral.rolloff = read();
  profile.spectral.brightness = read();
  profile.spectral.spectralFlatness = read();

  profile.dynamics.rms = read();
  profile.dynamics.peak = read();
  profile.dynamics.crestFactor = read();
  profile.dynamics.dynamicRange = read();

  profile.stereo.width = read();
  profile.stereo.phaseCorrelation = read();

  profile.noise.noiseFloorDb = read();
  for (int i = 0; i < kNumNoiseCoefs && pos < vec.size(); ++i)
    profile.noise.noiseShape[i] = vec[pos++];

  profile.texture.saturationAmount = read();
  profile.texture.harmonicDistortion = read();
  profile.texture.transientShape = read();

  return profile;
}

double DNAProfile::SimilarityTo(const DNAProfile& other) const {
  auto vecA = ToFeatureVector();
  auto vecB = other.ToFeatureVector();

  size_t n = std::min(vecA.size(), vecB.size());
  if (n == 0) return 0.0;

  double dot = 0.0, normA = 0.0, normB = 0.0;
  for (size_t i = 0; i < n; ++i) {
    dot += vecA[i] * vecB[i];
    normA += vecA[i] * vecA[i];
    normB += vecB[i] * vecB[i];
  }

  double denom = std::sqrt(normA) * std::sqrt(normB);
  return (denom > 0.0) ? std::clamp(dot / denom, 0.0, 1.0) : 0.0;
}

DNAProfile DNAProfile::InterpolateWith(const DNAProfile& other, double t) const {
  DNAProfile result;

  auto interpVec = [t](const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = std::min(a.size(), b.size());
    std::vector<double> r(n);
    for (size_t i = 0; i < n; ++i)
      r[i] = a[i] * (1.0 - t) + b[i] * t;
    return r;
  };

  auto interp = [t](double a, double b) { return a * (1.0 - t) + b * t; };

  result.spectral.spectralEnvelope = interpVec(spectral.spectralEnvelope, other.spectral.spectralEnvelope);
  result.spectral.harmonicProfile = interpVec(spectral.harmonicProfile, other.spectral.harmonicProfile);
  result.spectral.centroid = interp(spectral.centroid, other.spectral.centroid);
  result.spectral.spread = interp(spectral.spread, other.spectral.spread);
  result.spectral.rolloff = interp(spectral.rolloff, other.spectral.rolloff);
  result.spectral.brightness = interp(spectral.brightness, other.spectral.brightness);

  result.dynamics.rms = interp(dynamics.rms, other.dynamics.rms);
  result.dynamics.peak = interp(dynamics.peak, other.dynamics.peak);
  result.dynamics.crestFactor = interp(dynamics.crestFactor, other.dynamics.crestFactor);
  result.dynamics.dynamicRange = interp(dynamics.dynamicRange, other.dynamics.dynamicRange);

  result.stereo.width = interp(stereo.width, other.stereo.width);
  result.stereo.phaseCorrelation = interp(stereo.phaseCorrelation, other.stereo.phaseCorrelation);

  result.noise.noiseFloorDb = interp(noise.noiseFloorDb, other.noise.noiseFloorDb);

  result.texture.saturationAmount = interp(texture.saturationAmount, other.texture.saturationAmount);
  result.texture.harmonicDistortion = interp(texture.harmonicDistortion, other.texture.harmonicDistortion);

  result.sourceName = (t < 0.5) ? sourceName : other.sourceName;
  result.confidence = interp(confidence, other.confidence);

  return result;
}
