#include "Geno_DynamicsProcessor.h"
#include <cmath>
#include <algorithm>

DynamicsProcessor::DynamicsProcessor() {
  Reset();
}

void DynamicsProcessor::Reset() {
  mEnvelope = 0.0;
  mGainSmooth = 1.0;
  mDelayLine.fill(0.f);
  mDelayPos = 0;
  mSmoothAmount = mTransferAmount;
  UpdateCoefficients();
}

void DynamicsProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
  UpdateCoefficients();
}

void DynamicsProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}

void DynamicsProcessor::SetSourceProfile(const DynamicFeatures& source) {
  mSource = source;
  mProfilesLoaded = true;
}

void DynamicsProcessor::SetTargetProfile(const DynamicFeatures& target) {
  mTarget = target;
  mProfilesLoaded = true;
}

void DynamicsProcessor::UpdateCoefficients() {
  double attackMs = 3.0;
  double releaseMs = 40.0;
  mAttackCoef = std::exp(-1.0 / (attackMs * 0.001 * mSampleRate));
  mReleaseCoef = std::exp(-1.0 / (releaseMs * 0.001 * mSampleRate));
}

double DynamicsProcessor::ComputeEnvelope(double sample) {
  double absSample = std::abs(sample);
  double coef = (absSample > mEnvelope) ? mAttackCoef : mReleaseCoef;
  mEnvelope = coef * mEnvelope + (1.0 - coef) * absSample;
  return mEnvelope;
}

double DynamicsProcessor::ComputeGain(double envelope, double ratio, double kneeDb) {
  if (envelope < 1e-10) return 1.0;
  double envDb = 20.0 * std::log10(envelope);
  double thresholdDb = -24.0;
  double kneeHalf = kneeDb * 0.5;

  double reductionDb;
  if (envDb < thresholdDb - kneeHalf) {
    reductionDb = 0.0;
  } else if (envDb > thresholdDb + kneeHalf) {
    reductionDb = (envDb - thresholdDb) * (1.0 - 1.0 / ratio);
  } else {
    double x = envDb - thresholdDb + kneeHalf;
    reductionDb = x * x / (2.0 * kneeDb) * (1.0 - 1.0 / ratio);
  }

  return std::pow(10.0, -reductionDb / 20.0);
}

void DynamicsProcessor::Process(const float* input, float* output, int numSamples) {
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

  double sourceDR = std::max(mSource.dynamicRange, 1.0);
  double targetDR = std::max(mTarget.dynamicRange, 1.0);

  {
    static int dc = 0;
    if (++dc % 500 == 0) {
      float inPeak = 0;
      for (int i = 0; i < numSamples; ++i)
        if (std::fabs(input[i]) > inPeak) inPeak = std::fabs(input[i]);
      fprintf(stderr, "[DBUG] DynProc: sDR=%.1f tDR=%.1f amt=%.3f inPk=%.4f gSmooth=%.3f\n",
              sourceDR, targetDR, mSmoothAmount, inPeak, mGainSmooth);
    }
  }
  double rangeRatio = targetDR / sourceDR;
  rangeRatio = 1.0 + (rangeRatio - 1.0) * mSmoothAmount;

  double compressionRatio = std::max(1.0, rangeRatio);
  double kneeWidth = 6.0 + (1.0 - mSmoothAmount) * 6.0;

  int lookahead = std::min(kMaxLookahead, numSamples);

  for (int i = 0; i < numSamples; ++i) {
    mDelayLine[mDelayPos] = input[i];
    mDelayPos = (mDelayPos + 1) % kDelayLineSize;
    int lookaheadIdx = (mDelayPos + lookahead / 2) % kDelayLineSize;

    double envelope = ComputeEnvelope(mDelayLine[lookaheadIdx]);
    double targetGain = ComputeGain(envelope, compressionRatio, kneeWidth);
    double smoothGain = mGainSmooth * 0.7 + targetGain * 0.3;
    mGainSmooth = smoothGain;

    int readPos = (mDelayPos - lookahead + kDelayLineSize) % kDelayLineSize;
    output[i] = mDelayLine[readPos] * (float)smoothGain;
  }
}
