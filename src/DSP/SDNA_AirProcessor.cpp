#include "SDNA_AirProcessor.h"
#include <cmath>
#include <algorithm>

AirProcessor::AirProcessor() { Reset(); }
void AirProcessor::Reset() { mHPZ1 = 0.0; mHSZ1 = 0.0; mHSZ2 = 0.0; mSmoothAmount = mTransferAmount; }

void AirProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}
void AirProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}
void AirProcessor::SetSourceProfile(const SpectralFeatures& source) {
  mSource = source; mProfilesLoaded = true;
}
void AirProcessor::SetTargetProfile(const SpectralFeatures& target) {
  mTarget = target; mProfilesLoaded = true;
}

double AirProcessor::HighShelf(double x, double gainDb, double freq) {
  double w0 = 2.0 * M_PI * freq / mSampleRate;
  double A = std::pow(10.0, gainDb / 40.0);
  double alpha = std::sin(w0) / 2.0;
  double c = std::cos(w0);
  double b0 = A * ((A + 1.0) + (A - 1.0) * c + 2.0 * std::sqrt(A) * alpha);
  double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
  double b2 = A * ((A + 1.0) + (A - 1.0) * c - 2.0 * std::sqrt(A) * alpha);
  double a0 = (A + 1.0) - (A - 1.0) * c + 2.0 * std::sqrt(A) * alpha;
  if (std::abs(a0) < 1e-30) a0 = 1e-30;
  double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
  double a2 = (A + 1.0) - (A - 1.0) * c - 2.0 * std::sqrt(A) * alpha;
  double out = (b0 / a0) * x + (b1 / a0) * mHSZ1 + (b2 / a0) * mHSZ2
               - (a1 / a0) * mHSZ1 - (a2 / a0) * mHSZ2;
  if (std::isnan(out) || std::isinf(out)) out = 0.0;
  mHSZ2 = mHSZ1; mHSZ1 = out;
  return out;
}

double AirProcessor::PresenceBoost(double x, double amount) {
  double boosted = x + amount * (x - mHPZ1);
  if (std::isnan(boosted) || std::isinf(boosted)) boosted = x;
  mHPZ1 = x;
  return boosted;
}

void AirProcessor::Process(const float* input, float* output, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(input, input + numSamples, output);
    return;
  }
  double blockRamp = std::pow(mRampCoef, numSamples);
  mSmoothAmount = mSmoothAmount * blockRamp + mTransferAmount * (1.0 - blockRamp);
  double brightnessDiff = (mTarget.brightness - mSource.brightness) * mSmoothAmount;
  double shelfGain = brightnessDiff * 12.0;
  double airAmount = std::max(0.0, brightnessDiff);

  for (int i = 0; i < numSamples; ++i) {
    double x = input[i];
    if (std::isnan(x) || std::isinf(x)) {
      x = 0.0;
    }
    if (std::abs(shelfGain) > 0.5)
      x = HighShelf(x, shelfGain, 8000.0);
    x = PresenceBoost(x, airAmount * 0.3);
    if (std::isnan(x) || std::isinf(x)) x = 0.0;
    output[i] = (float)std::clamp(x, -1.0, 1.0);
  }
}
