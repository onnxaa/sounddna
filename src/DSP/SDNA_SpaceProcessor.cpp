#include "SDNA_SpaceProcessor.h"
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
  mSmoothAmount += (mTransferAmount - mSmoothAmount) * (1.0 - mRampCoef);
  double decayDiff = (mTarget.decayTime - mSource.decayTime) * mSmoothAmount;
  double roomDiff = (mTarget.roomSize - mSource.roomSize) * mSmoothAmount;
  double dampDiff = (mTarget.damping - mSource.damping) * mSmoothAmount;

  mWetDry = std::clamp(0.1 + decayDiff * 0.0005, 0.0, 0.8);

  double baseDelays[4] = {31, 37, 43, 53};
  double roomScale = std::max(0.5, 1.0 + roomDiff);
  for (int i = 0; i < 4; ++i) {
    int delaySamples = (int)(baseDelays[i] * roomScale * mSampleRate / 1000.0);
    delaySamples = std::clamp(delaySamples, 8, 4096);
    mCombL[i].buf.resize(delaySamples, 0.f);
    mCombR[i].buf.resize(delaySamples, 0.f);
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

  std::memset(outputL, 0, numSamples * sizeof(float));
  std::memset(outputR, 0, numSamples * sizeof(float));

  for (int i = 0; i < numSamples; ++i) {
    float reverbL = 0, reverbR = 0;
    auto processComb = [&](CombFilter& cf, float input) {
      float read = cf.buf[cf.pos];
      float out = read;
      float damped = cf.damp * read + (1.0f - (float)cf.damp) * out;
      cf.buf[cf.pos] = input + damped * (float)cf.feedback;
      cf.pos = (cf.pos + 1) % (int)cf.buf.size();
      return out;
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

    float wetL = inputL[i] * (1.0f - (float)mWetDry) + apOutL * (float)mWetDry;
    float wetR = inputR[i] * (1.0f - (float)mWetDry) + apOutR * (float)mWetDry;
    outputL[i] = std::clamp(wetL, -1.0f, 1.0f);
    outputR[i] = std::clamp(wetR, -1.0f, 1.0f);
  }
}
