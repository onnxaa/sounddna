#pragma once
#include "SDNA_Types.h"

class TextureProcessor {
public:
  TextureProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const TextureFeatures& source);
  void SetTargetProfile(const TextureFeatures& target);
  void Process(const float* input, float* output, int numSamples);

private:
  double mSampleRate = 44100.0;
  std::atomic<double> mTransferAmount{1.0};
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  TextureFeatures mSource, mTarget;
  std::atomic<bool> mProfilesLoaded{false};

  double mWarmthZ1 = 0.0;

  double SoftClip(double x, double drive);
  double TapeSaturate(double x, double bias);
  double AnalogWarmthFilter(double x, double warmth);
  double EvenHarmonics(double x, double amount);
  double OddHarmonics(double x, double amount);
};
