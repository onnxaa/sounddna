#include "Geno_MovementProcessor.h"
#include <cmath>
#include <algorithm>

MovementProcessor::MovementProcessor() { Reset(); }
void MovementProcessor::Reset() { mPhase = 0.0; mDelayL = 0.0; mDelayR = 0.0; mSmoothAmount = mTransferAmount; }

void MovementProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}
void MovementProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}
void MovementProcessor::SetSourceProfile(const MovementFeatures& source) {
  mSource = source; mProfilesLoaded = true;
}
void MovementProcessor::SetTargetProfile(const MovementFeatures& target) {
  mTarget = target; mProfilesLoaded = true;
}

void MovementProcessor::Process(const float* inputL, const float* inputR,
                                 float* outputL, float* outputR, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(inputL, inputL + numSamples, outputL);
    std::copy(inputR, inputR + numSamples, outputR);
    return;
  }

  double blockRamp = std::pow(mRampCoef, numSamples);
  mSmoothAmount = mSmoothAmount * blockRamp + mTransferAmount * (1.0 - blockRamp);
  double rate = 1.0 + (mTarget.modulationRate - mSource.modulationRate) * mSmoothAmount;
  rate = std::max(0.1, rate);
  double depth = 1.0 + (mTarget.modulationDepth - mSource.modulationDepth) * mSmoothAmount;
  depth = std::clamp(depth, 0.0, 2.0);
  double tremolo = mTarget.tremoloAmount * mSmoothAmount;

  bool bypass = mSmoothAmount < 0.001;
  if (bypass) {
    std::copy(inputL, inputL + numSamples, outputL);
    std::copy(inputR, inputR + numSamples, outputR);
    return;
  }

  double phaseInc = rate / mSampleRate;
  for (int i = 0; i < numSamples; ++i) {
    mPhase += phaseInc;
    if (mPhase > 1.0) mPhase -= 1.0;
    double lfo = std::sin(mPhase * 2.0 * M_PI);
    double mod = 1.0 + lfo * depth * 0.2;
    double trem = 1.0 - (lfo * 0.5 + 0.5) * tremolo * 0.5;
    outputL[i] = std::clamp(inputL[i] * (float)(mod * trem), -1.0f, 1.0f);
    outputR[i] = std::clamp(inputR[i] * (float)(mod * trem), -1.0f, 1.0f);

    double chorus = lfo * depth * 0.05;
    double delayedL = mDelayL;
    double delayedR = mDelayR;
    mDelayL = outputL[i];
    mDelayR = outputR[i];
    outputL[i] = std::clamp((float)(outputL[i] * 0.7 + delayedL * 0.3 * (1.0 + chorus)), -1.0f, 1.0f);
    outputR[i] = std::clamp((float)(outputR[i] * 0.7 + delayedR * 0.3 * (1.0 - chorus)), -1.0f, 1.0f);
  }
}
