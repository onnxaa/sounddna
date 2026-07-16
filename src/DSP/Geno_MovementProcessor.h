#pragma once
#include "Geno_Types.h"

class MovementProcessor {
public:
  MovementProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const MovementFeatures& source);
  void SetTargetProfile(const MovementFeatures& target);
  void Process(const float* inputL, const float* inputR,
               float* outputL, float* outputR, int numSamples);

private:
  double mSampleRate = 44100.0;
  std::atomic<double> mTransferAmount{1.0};
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  MovementFeatures mSource, mTarget;
  std::atomic<bool> mProfilesLoaded{false};
  double mPhase = 0.0;
  double mDelayL = 0.0, mDelayR = 0.0;
};
