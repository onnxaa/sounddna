#pragma once
#include "SDNA_Types.h"

class GlueProcessor {
public:
  GlueProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const DynamicFeatures& source);
  void SetTargetProfile(const DynamicFeatures& target);
  void Process(const float* inputL, const float* inputR,
               float* outputL, float* outputR, int numSamples);

private:
  double mSampleRate = 44100.0;
  double mTransferAmount = 1.0;
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  DynamicFeatures mSource, mTarget;
  bool mProfilesLoaded = false;
  double mEnvL = 0.0, mEnvR = 0.0;
  double mRMSAvg = 0.0;
  int mRMSWindow = 0;
};
