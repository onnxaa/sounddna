#include "SDNA_StereoProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

StereoProcessor::StereoProcessor() {
  Reset();
}

void StereoProcessor::Reset() {
  mSmoothAmount = mTransferAmount;
}

void StereoProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}

void StereoProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}

void StereoProcessor::SetSourceProfile(const StereoFeatures& source) {
  mSource = source;
  mProfilesLoaded = true;
}

void StereoProcessor::SetTargetProfile(const StereoFeatures& target) {
  mTarget = target;
  mProfilesLoaded = true;
}

void StereoProcessor::EncodeMS(const float* L, const float* R,
                                float* M, float* S, int n) {
  for (int i = 0; i < n; ++i) {
    M[i] = (L[i] + R[i]) * 0.5f;
    S[i] = (L[i] - R[i]) * 0.5f;
  }
}

void StereoProcessor::DecodeMS(const float* M, const float* S,
                                float* L, float* R, int n) {
  for (int i = 0; i < n; ++i) {
    L[i] = M[i] + S[i];
    R[i] = M[i] - S[i];
  }
}

void StereoProcessor::ProcessMidSide([[maybe_unused]] float* mid, float* side, int numSamples) {
  if (!mProfilesLoaded) return;

  double sourceWidth = mSource.width;
  double targetWidth = mTarget.width;
  if (sourceWidth < 0.01) sourceWidth = 0.5;
  if (targetWidth < 0.01) targetWidth = 0.5;

  double blockRamp = std::pow(mRampCoef, numSamples);
  mSmoothAmount = mSmoothAmount * blockRamp + mTransferAmount * (1.0 - blockRamp);
  double widthRatio = targetWidth / sourceWidth;
  widthRatio = 1.0 + (widthRatio - 1.0) * mSmoothAmount;

  for (int i = 0; i < numSamples; ++i) {
    side[i] *= (float)widthRatio;
  }
}

void StereoProcessor::Process(const float* inputL, const float* inputR,
                               float* outputL, float* outputR, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(inputL, inputL + numSamples, outputL);
    std::copy(inputR, inputR + numSamples, outputR);
    return;
  }

  int done = 0;
  while (done < numSamples) {
    int n = std::min(numSamples - done, kMaxBlockSize);
    EncodeMS(inputL + done, inputR + done, mMidBuf.data(), mSideBuf.data(), n);
    ProcessMidSide(mMidBuf.data(), mSideBuf.data(), n);
    DecodeMS(mMidBuf.data(), mSideBuf.data(), outputL + done, outputR + done, n);
    done += n;
  }
}
