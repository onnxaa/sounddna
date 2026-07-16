#include "Geno_MorphEngine.h"
#include <algorithm>

MorphEngine::MorphEngine() {
  Reset();
}

void MorphEngine::Reset() {
  mMorphPoints.clear();
  mMorphPosition = 0.0;
}

void MorphEngine::AddPoint(const GenoProfile& profile, double position, double blend) {
  MorphPoint pt;
  pt.profile = profile;
  pt.position = std::clamp(position, 0.0, 1.0);
  pt.blendAmount = std::max(0.0, blend);
  mMorphPoints.push_back(pt);

  std::sort(mMorphPoints.begin(), mMorphPoints.end(),
    [](const MorphPoint& a, const MorphPoint& b) {
      return a.position < b.position;
    });
}

void MorphEngine::Clear() {
  mMorphPoints.clear();
}

void MorphEngine::SetMorphPosition(double position) {
  mMorphPosition = std::clamp(position, 0.0, 1.0);
}

GenoProfile MorphEngine::GetCurrentMorph() const {
  if (mMorphPoints.empty()) return GenoProfile{};

  if (mMorphPoints.size() == 1) return mMorphPoints[0].profile;

  if (mMorphPosition <= mMorphPoints[0].position)
    return mMorphPoints[0].profile;

  if (mMorphPosition >= mMorphPoints.back().position)
    return mMorphPoints.back().profile;

  for (size_t i = 0; i < mMorphPoints.size() - 1; ++i) {
    const auto& a = mMorphPoints[i];
    const auto& b = mMorphPoints[i + 1];

    if (mMorphPosition >= a.position && mMorphPosition <= b.position) {
      double range = b.position - a.position;
      double t = (range > 0.0) ? (mMorphPosition - a.position) / range : 0.0;
      double wA = std::max(0.001, a.blendAmount);
      double wB = std::max(0.001, b.blendAmount);
      double tw = t * wB / (wA * (1.0 - t) + wB * t);
      return InterpolateProfiles(a.profile, b.profile, std::clamp(tw, 0.0, 1.0));
    }
  }

  return mMorphPoints.back().profile;
}

GenoProfile MorphEngine::InterpolateProfiles(const GenoProfile& a,
                                             const GenoProfile& b,
                                             double t) const {
  GenoProfile result;

  auto interpVec = [t](const std::vector<double>& va,
                        const std::vector<double>& vb) {
    std::vector<double> vr;
    size_t n = std::min(va.size(), vb.size());
    vr.resize(n);
    for (size_t i = 0; i < n; ++i)
      vr[i] = va[i] * (1.0 - t) + vb[i] * t;
    return vr;
  };

  auto interp = [t](double a, double b) { return a * (1.0 - t) + b * t; };

  result.spectral.spectralEnvelope = interpVec(
    a.spectral.spectralEnvelope, b.spectral.spectralEnvelope);
  result.spectral.harmonicProfile = interpVec(
    a.spectral.harmonicProfile, b.spectral.harmonicProfile);
  result.spectral.centroid = interp(a.spectral.centroid, b.spectral.centroid);
  result.spectral.brightness = interp(a.spectral.brightness, b.spectral.brightness);

  result.dynamics.rms = interp(a.dynamics.rms, b.dynamics.rms);
  result.dynamics.dynamicRange = interp(a.dynamics.dynamicRange, b.dynamics.dynamicRange);
  result.dynamics.crestFactor = interp(a.dynamics.crestFactor, b.dynamics.crestFactor);

  result.stereo.width = interp(a.stereo.width, b.stereo.width);
  result.stereo.phaseCorrelation = interp(a.stereo.phaseCorrelation, b.stereo.phaseCorrelation);

  result.noise.noiseFloorDb = interp(a.noise.noiseFloorDb, b.noise.noiseFloorDb);

  result.texture.saturationAmount = interp(a.texture.saturationAmount, b.texture.saturationAmount);
  result.texture.harmonicDistortion = interp(a.texture.harmonicDistortion, b.texture.harmonicDistortion);

  return result;
}
