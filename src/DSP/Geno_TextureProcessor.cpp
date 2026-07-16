#include "Geno_TextureProcessor.h"
#include <cmath>
#include <algorithm>

TextureProcessor::TextureProcessor() {
  Reset();
}

void TextureProcessor::Reset() {
  mWarmthZ1 = 0.0;
  mSmoothAmount = mTransferAmount;
}

void TextureProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}

void TextureProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}

void TextureProcessor::SetSourceProfile(const TextureFeatures& source) {
  mSource = source;
  mProfilesLoaded = true;
}

void TextureProcessor::SetTargetProfile(const TextureFeatures& target) {
  mTarget = target;
  mProfilesLoaded = true;
}

double TextureProcessor::SoftClip(double x, double drive) {
  return std::tanh(x * drive);
}

double TextureProcessor::TapeSaturate(double x, double bias) {
  double absX = std::abs(x);
  return (x >= 0.0 ? 1.0 : -1.0) * absX / (1.0 + absX * bias * 2.0);
}

double TextureProcessor::AnalogWarmthFilter(double x, double warmth) {
  double b1 = 1.0 - warmth * 0.5;
  double out = x * warmth + mWarmthZ1 * b1;
  mWarmthZ1 = out;
  return out;
}

double TextureProcessor::EvenHarmonics(double x, double amount) {
  return x + amount * x * std::abs(x);
}

double TextureProcessor::OddHarmonics(double x, double amount) {
  double cubed = x * x * x;
  return x + amount * (cubed - x) * 0.33;
}

void TextureProcessor::Process(const float* input, float* output, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(input, input + numSamples, output);
    return;
  }

  double blockRamp = std::pow(mRampCoef, numSamples);
  mSmoothAmount = mSmoothAmount * blockRamp + mTransferAmount * (1.0 - blockRamp);

  if (mSmoothAmount < 0.001) {
    std::copy(input, input + numSamples, output);
    return;
  }

  double satDiff = (mTarget.saturationAmount - mSource.saturationAmount) * mSmoothAmount;
  double drive = 1.0 + std::max(0.0, satDiff) * 4.0;

  double warmthAmt = 1.0 + (mTarget.analogWarmth - mSource.analogWarmth) * mSmoothAmount;
  warmthAmt = std::clamp(warmthAmt, 0.0, 0.95);

  double tapeBias = mTarget.tapeSaturation * mSmoothAmount;
  double evenAmt = mTarget.harmonicDistortion * mSmoothAmount * 2.0;
  double oddAmt = mTarget.saturationAmount * mSmoothAmount * 1.5;

  for (int i = 0; i < numSamples; ++i) {
    double x = input[i];
    x = AnalogWarmthFilter(x, warmthAmt);
    x = TapeSaturate(x, tapeBias);
    x = SoftClip(x, drive);
    x = EvenHarmonics(x, evenAmt * 0.5);
    x = OddHarmonics(x, oddAmt * 0.3);
    output[i] = (float)std::clamp(x, -1.0, 1.0);
  }
}
