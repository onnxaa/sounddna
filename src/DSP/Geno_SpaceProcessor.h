#pragma once
#include "Geno_Types.h"

class SpaceProcessor {
public:
  SpaceProcessor();
  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const SpaceFeatures& source);
  void SetTargetProfile(const SpaceFeatures& target);
  void Process(const float* inputL, const float* inputR,
               float* outputL, float* outputR, int numSamples);

private:
  double mSampleRate = 44100.0;
  std::atomic<double> mTransferAmount{1.0};
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  SpaceFeatures mSource, mTarget;
  std::atomic<bool> mProfilesLoaded{false};

  struct CombFilter {
    double feedback = 0.0, damp = 0.0;
    std::vector<float> buf;
    float dampedState = 0.0f;
    int pos = 0;
  };
  CombFilter mCombL[4], mCombR[4];
  double mAPDelayL = 0.0, mAPDelayR = 0.0;
  double mAPFeedback = 0.0;
  double mWetDry = 0.3;

  void UpdateReverbParams();
};
