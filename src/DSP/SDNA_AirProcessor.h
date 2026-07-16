#pragma once
#include "SDNA_Types.h"

class AirProcessor {
public:
  AirProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const SpectralFeatures& source);
  void SetTargetProfile(const SpectralFeatures& target);
  void Process(const float* input, float* output, int numSamples);

private:
  double mSampleRate = 44100.0;
  std::atomic<double> mTransferAmount{1.0};
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  SpectralFeatures mSource, mTarget;
  std::atomic<bool> mProfilesLoaded{false};
  double mHPZ1 = 0.0;
  double mHSZ1 = 0.0, mHSZ2 = 0.0;

  double HighShelf(double x, double gainDb, double freq);
  double PresenceBoost(double x, double amount);
};
