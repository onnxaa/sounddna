#pragma once
#include "SDNA_Types.h"

class DynamicsProcessor {
public:
  DynamicsProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const DynamicFeatures& source);
  void SetTargetProfile(const DynamicFeatures& target);
  void Process(const float* input, float* output, int numSamples);

private:
  double mSampleRate = 44100.0;
  double mTransferAmount = 1.0;
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  DynamicFeatures mSource, mTarget;
  bool mProfilesLoaded = false;

  double mEnvelope = 0.0;
  double mGainSmooth = 1.0;
  double mAttackCoef = 0.0;
  double mReleaseCoef = 0.0;

  static constexpr int kMaxLookahead = 128;
  static constexpr int kDelayLineSize = 256;
  std::array<float, kDelayLineSize> mDelayLine;
  int mDelayPos = 0;

  void UpdateCoefficients();
  double ComputeEnvelope(double sample);
  double ComputeGain(double envelope, double ratio, double kneeDb);
};
