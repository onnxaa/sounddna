#pragma once
#include "SDNA_Types.h"
#include "SDNA_FFT.h"
#include <array>

class SpectralProcessor {
public:
  SpectralProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetTransientBlend(double blend);
  void SetSourceProfile(const SpectralFeatures& source);
  void SetTargetProfile(const SpectralFeatures& target);
  void Process(const float* input, float* output, int numSamples);
  void ComputeSpectralFilter(std::vector<double>& filterOut);

private:
  double mSampleRate = 44100.0;
  std::atomic<double> mTransferAmount{1.0};
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  double mTransientBlend = 0.5;
  FFTProcessor mFFT;
  SpectralFeatures mSource;
  SpectralFeatures mTarget;
  std::vector<double> mSpectralFilter;
  std::vector<double> mCurrentFilter;

  std::vector<double> mPrevPhase;
  std::vector<double> mPrevMag;
  std::vector<double> mPrevMag2;
  std::vector<double> mMagBuf;
  std::vector<double> mPhaseBuf;
  std::array<float, kMaxBlockSize> mDryBuf;
  std::array<float, kMaxBlockSize> mTempBlockBuf;
  std::atomic<bool> mProfilesLoaded{false};

  void RecomputeFilter();
  void MatchEnvelopes(const std::vector<double>& sourceEnv,
                       const std::vector<double>& targetEnv,
                       std::vector<double>& filter);
  void SmoothFilter(std::vector<double>& filter, double smoothing = 0.3);
  double DetectTransient(const std::vector<double>& mag);
};
