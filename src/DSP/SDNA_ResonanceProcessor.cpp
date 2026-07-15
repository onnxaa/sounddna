#include "SDNA_ResonanceProcessor.h"
#include <cmath>
#include <algorithm>
#include <numeric>

ResonanceProcessor::ResonanceProcessor() { Reset(); }

void ResonanceProcessor::Reset() {
  for (auto& f : mFilters) f.Reset();
  for (auto& g : mGains) g = 0.0;
  mSmoothAmount = mTransferAmount;
}

void ResonanceProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}
void ResonanceProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}
void ResonanceProcessor::SetSourceProfile(const SpectralFeatures& source) {
  mSource = source; mProfilesLoaded = true;
}
void ResonanceProcessor::SetTargetProfile(const SpectralFeatures& target) {
  mTarget = target; mProfilesLoaded = true;
}

void ResonanceProcessor::Biquad::Bandpass(double freq, double q, double sr) {
  double w0 = 2.0 * M_PI * freq / sr;
  double alpha = std::sin(w0) / (2.0 * q);
  double a0 = 1.0 + alpha;
  b0 = alpha / a0;
  b1 = 0.0;
  b2 = -alpha / a0;
  a1 = -2.0 * std::cos(w0) / a0;
  a2 = (1.0 - alpha) / a0;
}

void ResonanceProcessor::Process(const float* input, float* output, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(input, input + numSamples, output);
    return;
  }

  double freqs[6] = {100, 300, 600, 1200, 2400, 4800};
  double envDiffSum = 0.0;

  auto getEnvValue = [&](const SpectralFeatures& sf, int i) -> double {
    if (i < (int)sf.spectralEnvelope.size())
      return sf.spectralEnvelope[i];
    return 1.0;
  };

  for (int i = 0; i < 6; ++i) {
    int band = std::min(i, 5);
    double srcVal = getEnvValue(mSource, band);
    double tgtVal = getEnvValue(mTarget, band);
    double ratio = (srcVal > 1e-10) ? tgtVal / srcVal : 1.0;
    ratio = std::clamp(ratio, 0.1, 10.0);
    double freqMod = std::log2(ratio) * 200.0;
  mSmoothAmount += (mTransferAmount - mSmoothAmount) * (1.0 - mRampCoef);
    double modFreq = freqs[i] + freqMod * mSmoothAmount;
    modFreq = std::clamp(modFreq, 20.0, mSampleRate * 0.45);
    double Q = 2.0 + std::abs(std::log2(ratio)) * 3.0;
    Q = std::clamp(Q, 0.5, 20.0);
    mFilters[i].Bandpass(modFreq, Q, mSampleRate);
    mGains[i] = std::max(0.0, 1.0 - std::abs(std::log2(ratio)) * 0.1) * mSmoothAmount;
    envDiffSum += std::abs(std::log2(ratio));
  }

  double overallScale = 1.0 / std::max(1.0, envDiffSum * 0.3);

  for (int i = 0; i < numSamples; ++i) {
    double x = input[i];
    double resSum = 0.0;
    for (int j = 0; j < 6; ++j)
      resSum += mFilters[j].Process(x) * mGains[j];
    double out = x * 0.5 + resSum * overallScale * 0.5;
    output[i] = (float)std::clamp(out, -1.0, 1.0);
  }
}
