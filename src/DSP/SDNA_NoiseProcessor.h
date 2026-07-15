#pragma once
#include "SDNA_Types.h"
#include "SDNA_FFT.h"

class NoiseProcessor {
public:
  NoiseProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const NoiseFeatures& source);
  void SetTargetProfile(const NoiseFeatures& target);
  void Process(const float* input, float* output, int numSamples);

private:
  double mSampleRate = 44100.0;
  double mTransferAmount = 1.0;
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  NoiseFeatures mSource, mTarget;
  bool mProfilesLoaded = false;

  FFTProcessor mFFT;
  double mNoiseGateState = 0.0;
  uint32_t mLFSRState = 0xDEADBEEF;
  static constexpr uint32_t kLFSRSeed = 0xDEADBEEF;
  std::vector<double> mNoiseShapingFilter;

  double GenerateNoiseSample();
  void UpdateNoiseFilter();
};
