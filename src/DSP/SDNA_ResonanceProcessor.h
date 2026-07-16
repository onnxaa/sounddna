#pragma once
#include "SDNA_Types.h"

class ResonanceProcessor {
public:
  ResonanceProcessor();
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

  struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    void Reset() { z1 = z2 = 0; }
    double Process(double x) {
      double out = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
      z2 = z1; z1 = out;
      return out;
    }
    void Bandpass(double freq, double q, double sr);
  };

  Biquad mFilters[6];
  double mGains[6] = {};
};
