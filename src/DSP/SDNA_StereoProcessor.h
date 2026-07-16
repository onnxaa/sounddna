#pragma once
#include "SDNA_Types.h"
#include <array>

class StereoProcessor {
public:
  StereoProcessor();
  ~StereoProcessor() = default;

  void Reset();
  void SetSampleRate(double sr);
  void SetTransferAmount(double amount);
  void SetSourceProfile(const StereoFeatures& source);
  void SetTargetProfile(const StereoFeatures& target);

  void Process(const float* inputL, const float* inputR,
               float* outputL, float* outputR, int numSamples);

private:
  double mSampleRate = 44100.0;
  std::atomic<double> mTransferAmount{1.0};
  double mSmoothAmount = 1.0;
  double mRampCoef = 0.0;
  StereoFeatures mSource, mTarget;
  std::atomic<bool> mProfilesLoaded{false};

  std::array<float, kMaxBlockSize> mMidBuf;
  std::array<float, kMaxBlockSize> mSideBuf;

  void ProcessMidSide(float* mid, float* side, int numSamples);
  void EncodeMS(const float* L, const float* R, float* M, float* S, int n);
  void DecodeMS(const float* M, const float* S, float* L, float* R, int n);
};
