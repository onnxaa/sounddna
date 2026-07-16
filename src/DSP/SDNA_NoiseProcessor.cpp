#include "SDNA_NoiseProcessor.h"
#include <cmath>
#include <algorithm>

NoiseProcessor::NoiseProcessor() : mFFT(128, 32) {
  Reset();
}

void NoiseProcessor::Reset() {
  mNoiseGateState = 0.0;
  mLFSRState = 0xDEADBEEF;
  mNoiseShapingFilter.assign(kNumNoiseCoefs, 0.0);
  mFFT.Reset();
  mSmoothAmount = mTransferAmount;
}

void NoiseProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
  mFFT.SetSampleRate(sr);
}

void NoiseProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}

void NoiseProcessor::SetSourceProfile(const NoiseFeatures& source) {
  mSource = source;
  mProfilesLoaded = true;
  UpdateNoiseFilter();
}

void NoiseProcessor::SetTargetProfile(const NoiseFeatures& target) {
  mTarget = target;
  mProfilesLoaded = true;
  UpdateNoiseFilter();
}

double NoiseProcessor::GenerateNoiseSample() {
  mLFSRState = mLFSRState * 1103515245 + 12345;
  return (double)(int32_t)(mLFSRState & 0x7FFFFFFF) / 0x40000000 - 1.0;
}

void NoiseProcessor::UpdateNoiseFilter() {
  for (int i = 0; i < kNumNoiseCoefs; ++i) {
    double srcCoeff = mSource.noiseShape[i];
    double tgtCoeff = mTarget.noiseShape[i];
    mNoiseShapingFilter[i] = tgtCoeff - srcCoeff;
  }
}

void NoiseProcessor::Process(const float* input, float* output, int numSamples) {
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

  double noiseDiffDb = mTarget.noiseFloorDb - mSource.noiseFloorDb;
  double noiseGain = std::pow(10.0, (noiseDiffDb * mSmoothAmount) / 20.0);
  double tiltDiff = mTarget.spectralTilt - mSource.spectralTilt;
  double tiltAmount = tiltDiff * mSmoothAmount * 0.1;

  double noiseBuf[4] = {0, 0, 0, 0};
  double z1 = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    double absIn = std::abs((double)input[i]);
    double gateCoef = (absIn > 0.0005) ? 0.9995 : 0.9;
    mNoiseGateState = mNoiseGateState * gateCoef +
                      (1.0 - gateCoef) * (absIn > 0.0005 ? 1.0 : 0.0);
    double noise = GenerateNoiseSample() * noiseGain * (1.0 - mNoiseGateState);
    noiseBuf[0] = noise;

    double filtered = noiseBuf[0];
    for (int j = 1; j < kNumNoiseCoefs; ++j) {
      filtered += (mSource.noiseShape[j] + mNoiseShapingFilter[j] * mSmoothAmount) * noiseBuf[j];
    }
    for (int j = kNumNoiseCoefs - 1; j > 0; --j)
      noiseBuf[j] = noiseBuf[j - 1];

    double tilted = filtered + tiltAmount * (filtered - z1);
    z1 = filtered;

    double out = (double)input[i] + tilted;
    output[i] = (float)std::clamp(out, -1.0, 1.0);
  }
}
