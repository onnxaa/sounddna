#include "Geno_SpectralProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

SpectralProcessor::SpectralProcessor() : mFFT(kFFTSize, kFFTHop) {
  int bins = mFFT.GetNumBins();
  mSpectralFilter.resize(bins, 1.0);
  mCurrentFilter.resize(bins, 1.0);
  mPrevPhase.resize(bins, 0.0);
  mPrevMag.resize(bins, 0.0);
  mPrevMag2.resize(bins, 0.0);
  mMagBuf.resize(bins, 0.0);
  mPhaseBuf.resize(bins, 0.0);
  mDryBuf.fill(0.f);
  mTempBlockBuf.fill(0.f);
}

void SpectralProcessor::Reset() {
  mFFT.Reset();
  std::fill(mSpectralFilter.begin(), mSpectralFilter.end(), 1.0);
  std::fill(mPrevPhase.begin(), mPrevPhase.end(), 0.0);
  std::fill(mPrevMag.begin(), mPrevMag.end(), 0.0);
  std::fill(mPrevMag2.begin(), mPrevMag2.end(), 0.0);
  mSmoothAmount = mTransferAmount;
}

void SpectralProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
  mFFT.SetSampleRate(sr);
}

void SpectralProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}

void SpectralProcessor::SetTransientBlend(double blend) {
  mTransientBlend = std::clamp(blend, 0.0, 1.0);
}

void SpectralProcessor::SetSourceProfile(const SpectralFeatures& source) {
  mSource = source;
  mProfilesLoaded = true;
  RecomputeFilter();
}

void SpectralProcessor::SetTargetProfile(const SpectralFeatures& target) {
  mTarget = target;
  mProfilesLoaded = true;
  RecomputeFilter();
}

void SpectralProcessor::RecomputeFilter() {
  if (!mProfilesLoaded) return;
  std::vector<double> sourceEnv, targetEnv;
  if (!mSource.spectralEnvelope.empty())
    sourceEnv = mSource.spectralEnvelope;
  else
    sourceEnv.resize(kNumSpectralBands, 1.0);
  if (!mTarget.spectralEnvelope.empty())
    targetEnv = mTarget.spectralEnvelope;
  else
    targetEnv.resize(kNumSpectralBands, 1.0);
  std::vector<double> fullFilter;
  MatchEnvelopes(sourceEnv, targetEnv, fullFilter);
  mSpectralFilter = fullFilter;
  mCurrentFilter = fullFilter;
}

void SpectralProcessor::MatchEnvelopes(const std::vector<double>& sourceEnv,
                                        const std::vector<double>& targetEnv,
                                        std::vector<double>& filter) {
  int numBins = mFFT.GetNumBins();
  filter.resize(numBins, 1.0);
  int numBands = (int)std::min(sourceEnv.size(), targetEnv.size());
  int binsPerBand = std::max(1, numBins / numBands);

  for (int b = 0; b < numBands && b < numBins; ++b) {
    double srcVal = sourceEnv[b];
    double tgtVal = targetEnv[b];
    double ratio = (srcVal > 1e-10) ? tgtVal / srcVal : 1.0;
    ratio = std::clamp(ratio, 0.01, 100.0);
    int startBin = b * binsPerBand;
    int endBin = std::min((b + 1) * binsPerBand, numBins);
    for (int i = startBin; i < endBin; ++i)
      filter[i] = ratio;
  }

  SmoothFilter(filter, 0.3);
}

void SpectralProcessor::SmoothFilter(std::vector<double>& filter, double smoothing) {
  if (filter.empty()) return;
  std::vector<double> smoothed = filter;
  for (size_t i = 1; i < filter.size(); ++i)
    smoothed[i] = smoothing * smoothed[i - 1] + (1.0 - smoothing) * filter[i];
  for (size_t i = filter.size() - 1; i > 0; --i)
    smoothed[i - 1] = smoothing * smoothed[i] + (1.0 - smoothing) * smoothed[i - 1];
  filter = smoothed;
}

double SpectralProcessor::DetectTransient(const std::vector<double>& mag) {
  double onsetSum = 0.0;
  double magSum = 0.0;
  for (size_t i = 0; i < mag.size(); ++i) {
    double diff = mag[i] - mPrevMag[i];
    double diff2 = mPrevMag[i] - mPrevMag2[i];
    double onset = std::max(0.0, diff - diff2 * 1.5);
    onsetSum += onset;
    magSum += mag[i];
  }
  double threshold = magSum * 0.05 / mag.size();
  double transient = (onsetSum > threshold) ? onsetSum / (onsetSum + threshold) : 0.0;
  return std::clamp(transient, 0.0, 1.0);
}

void SpectralProcessor::Process(const float* input, float* output, int numSamples) {
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

  for (size_t j = 0; j < mCurrentFilter.size(); ++j)
    mCurrentFilter[j] = 1.0 + (mSpectralFilter[j] - 1.0) * mSmoothAmount;

  int pos = 0;
  while (pos < numSamples) {
    int chunk = std::min(numSamples - pos, kMaxBlockSize);
    std::copy(input + pos, input + pos + chunk, mDryBuf.data());

    for (int i = 0; i < chunk; i += kFFTHop) {
      int blockSize = std::min(kFFTHop, chunk - i);
      if (blockSize <= 0) break;

      mFFT.ProcessBlock(input + pos + i, blockSize);

      mFFT.GetMagnitudeSpectrum(mMagBuf);
      mFFT.GetPhaseSpectrum(mPhaseBuf);

      double transient = DetectTransient(mMagBuf);

      mFFT.ApplySpectralFilter(mCurrentFilter, mMagBuf);

      for (size_t j = 0; j < mPhaseBuf.size() && j < mPrevPhase.size(); ++j) {
        double measured = mPhaseBuf[j];
        double phaseBlend = 0.5 + transient * 0.4;
        mPhaseBuf[j] = mPrevPhase[j] + (measured - mPrevPhase[j]) * phaseBlend;
        mPrevPhase[j] = measured;
      }

    std::copy(mPrevMag.begin(), mPrevMag.end(), mPrevMag2.begin());
    std::copy(mMagBuf.begin(), mMagBuf.end(), mPrevMag.begin());

      mFFT.SynthesizeFromSpectrum(mMagBuf, mPhaseBuf, mTempBlockBuf.data(), blockSize);

      for (int j = 0; j < blockSize; ++j) {
        float dry = mDryBuf[i + j];
        float wet = mTempBlockBuf[j];
        float blend = (transient * mTransientBlend);
        output[pos + i + j] = dry * blend + wet * (1.0f - blend);
      }
    }
    pos += chunk;
  }
}
