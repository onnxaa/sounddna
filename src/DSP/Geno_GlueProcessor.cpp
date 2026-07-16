#include "Geno_GlueProcessor.h"
#include <cmath>
#include <algorithm>

GlueProcessor::GlueProcessor() { Reset(); }
void GlueProcessor::Reset() { mEnvL = mEnvR = 0.0; mSmoothAmount = mTransferAmount; }

void GlueProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}
void GlueProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}
void GlueProcessor::SetSourceProfile(const DynamicFeatures& source) {
  mSource = source; mProfilesLoaded = true;
}
void GlueProcessor::SetTargetProfile(const DynamicFeatures& target) {
  mTarget = target; mProfilesLoaded = true;
}

void GlueProcessor::Process(const float* inputL, const float* inputR,
                             float* outputL, float* outputR, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(inputL, inputL + numSamples, outputL);
    std::copy(inputR, inputR + numSamples, outputR);
    return;
  }

  double sourceRMS = std::max(mSource.rms, 0.001);
  double targetRMS = std::max(mTarget.rms, 0.001);
  double blockRamp = std::pow(mRampCoef, numSamples);
  mSmoothAmount = mSmoothAmount * blockRamp + mTransferAmount * (1.0 - blockRamp);
  double rmsRatio = targetRMS / sourceRMS;
  rmsRatio = 1.0 + (rmsRatio - 1.0) * mSmoothAmount;
  double makeupGain = std::clamp(1.0 / std::sqrt(rmsRatio), 0.25, 4.0);

  double sourceDR = std::max(mSource.dynamicRange, 1.0);
  double targetDR = std::max(mTarget.dynamicRange, 1.0);
  double drRatio = targetDR / sourceDR;
  double compression = std::max(0.0, 1.0 - drRatio);
  double glue = compression * mSmoothAmount;

  double atkCoef = std::exp(-1.0 / (0.001 * mSampleRate));
  double relCoef = std::exp(-1.0 / (0.050 * mSampleRate));

  for (int i = 0; i < numSamples; ++i) {
    double avgL = (double)inputL[i] * (double)inputL[i];
    double avgR = (double)inputR[i] * (double)inputR[i];
    double coef = (avgL > mEnvL) ? atkCoef : relCoef;
    mEnvL = mEnvL * coef + avgL * (1.0 - coef);
    coef = (avgR > mEnvR) ? atkCoef : relCoef;
    mEnvR = mEnvR * coef + avgR * (1.0 - coef);
    double sumEnv = mEnvL + mEnvR;

    double thresholdDb = -24.0;
    double envDb = 10.0 * std::log10(sumEnv * 0.5 + 1e-30);
    double reduction = std::max(0.0, envDb - thresholdDb) * glue * 0.1;
    reduction = std::min(reduction, 12.0);
    double gainDb = -reduction + 20.0 * std::log10(makeupGain);
    double gain = std::pow(10.0, gainDb / 20.0);

    outputL[i] = std::clamp((float)(inputL[i] * gain), -1.0f, 1.0f);
    outputR[i] = std::clamp((float)(inputR[i] * gain), -1.0f, 1.0f);
  }
}
