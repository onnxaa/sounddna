#include "Geno_SpaceProcessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

SpaceProcessor::SpaceProcessor() { Reset(); }

void SpaceProcessor::Reset() {
  for (int i = 0; i < 4; ++i) {
    mCombL[i] = CombFilter();
    mCombR[i] = CombFilter();
    mCombL[i].buf.resize(2048, 0.f);
    mCombR[i].buf.resize(2048, 0.f);
  }
  mAPDelayL = mAPDelayR = 0.0;
  mAPFeedback = 0.5;
  mWetDry = 0.3;
  mSmoothAmount = mTransferAmount;
}

void SpaceProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
  mRampCoef = std::exp(-1.0 / (0.005 * sr));
}
void SpaceProcessor::SetTransferAmount(double amount) {
  mTransferAmount = std::clamp(amount, 0.0, 1.0);
}
void SpaceProcessor::SetSourceProfile(const SpaceFeatures& source) {
  mSource = source; mProfilesLoaded = true;
  UpdateReverbParams();
}
void SpaceProcessor::SetTargetProfile(const SpaceFeatures& target) {
  mTarget = target; mProfilesLoaded = true;
  UpdateReverbParams();
}

void SpaceProcessor::UpdateReverbParams() {
  double decayDiff = mTarget.decayTime - mSource.decayTime;
  double roomDiff = mTarget.roomSize - mSource.roomSize;
  double dampDiff = mTarget.damping - mSource.damping;
  if (std::isnan(decayDiff)) decayDiff = 0.0;
  if (std::isnan(roomDiff)) roomDiff = 0.0;
  if (std::isnan(dampDiff)) dampDiff = 0.0;
  mWetDry = std::clamp(0.1 + decayDiff * 0.0005, 0.0, 0.8);
  double baseDelays[4] = {31, 37, 43, 53};
  for (int i = 0; i < 4; ++i) {
    double roomScale = std::max(0.5, 1.0 + roomDiff * 0.5);
    int delaySamples = (int)(baseDelays[i] * roomScale * mSampleRate / 1000.0);
    delaySamples = std::clamp(delaySamples, 8, 4096);
    mCombL[i].buf.assign(delaySamples, 0.f);
    mCombR[i].buf.assign(delaySamples, 0.f);
    mCombL[i].feedback = 0.5 + decayDiff * 0.001;
    mCombR[i].feedback = 0.5 + decayDiff * 0.001;
    mCombL[i].damp = 0.2 + dampDiff * 0.5;
    mCombR[i].damp = 0.2 + dampDiff * 0.5;
    mCombL[i].pos = 0;
    mCombR[i].pos = 0;
  }
  mAPFeedback = 0.3 + decayDiff * 0.001;
}

void SpaceProcessor::Process(const float* inputL, const float* inputR,
                              float* outputL, float* outputR, int numSamples) {
  if (!mProfilesLoaded) {
    std::copy(inputL, inputL + numSamples, outputL);
    std::copy(inputR, inputR + numSamples, outputR);
    return;
  }

  double blockRamp = std::pow(mRampCoef, numSamples);
  mSmoothAmount = mSmoothAmount * blockRamp + mTransferAmount * (1.0 - blockRamp);
  if (std::isnan(mSmoothAmount)) mSmoothAmount = 0.0;

  if (mSmoothAmount < 0.001) {
    if (inputL != outputL) {
      std::copy(inputL, inputL + numSamples, outputL);
      std::copy(inputR, inputR + numSamples, outputR);
    }
    return;
  }

  if (inputL != outputL)
    std::memset(outputL, 0, numSamples * sizeof(float));
  if (inputR != outputR)
    std::memset(outputR, 0, numSamples * sizeof(float));

  for (int i = 0; i < numSamples; ++i) {
    float reverbL = 0, reverbR = 0;
    auto processComb = [&](CombFilter& cf, float input) {
      if (cf.buf.empty()) return 0.0f;
      float read = cf.buf[cf.pos];
      cf.dampedState = (float)cf.damp * cf.dampedState + (1.0f - (float)cf.damp) * read;
      float write = input + cf.dampedState * (float)cf.feedback;
      if (std::isnan(write) || std::isinf(write)) write = 0.0f;
      cf.buf[cf.pos] = std::clamp(write, -1.0f, 1.0f);
      cf.pos = (cf.pos + 1) % (int)cf.buf.size();
      return read;
    };
    reverbL += processComb(mCombL[0], inputL[i]);
    reverbL += processComb(mCombL[1], inputL[i]);
    reverbL += processComb(mCombL[2], inputL[i]);
    reverbL += processComb(mCombL[3], inputL[i]);
    reverbR += processComb(mCombR[0], inputR[i]);
    reverbR += processComb(mCombR[1], inputR[i]);
    reverbR += processComb(mCombR[2], inputR[i]);
    reverbR += processComb(mCombR[3], inputR[i]);

    float apInL = reverbL + (float)mAPFeedback * mAPDelayL;
    float apInR = reverbR + (float)mAPFeedback * mAPDelayR;
    float apOutL = -apInL + mAPDelayL;
    float apOutR = -apInR + mAPDelayR;
    mAPDelayL = apInL * 0.5f;
    mAPDelayR = apInR * 0.5f;

    double effWetDry = mWetDry * mSmoothAmount;
    if (std::isnan(effWetDry) || std::abs(effWetDry) < 1e-15) effWetDry = 0.0;
    float wetL, wetR;
    if (effWetDry == 0.0) {
      wetL = inputL[i];
      wetR = inputR[i];
    } else {
      float fw = (float)effWetDry;
      float fd = 1.0f - fw;
      wetL = inputL[i] * fd + std::clamp(apOutL, -1.0f, 1.0f) * fw;
      wetR = inputR[i] * fd + std::clamp(apOutR, -1.0f, 1.0f) * fw;
    }
    outputL[i] = std::clamp(wetL, -1.0f, 1.0f);
    outputR[i] = std::clamp(wetR, -1.0f, 1.0f);
  }
}
