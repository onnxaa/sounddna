#include "SDNA_TransferEngine.h"
#include <cstring>
#include <algorithm>

TransferEngine::TransferEngine() {
  Reset();
}

void TransferEngine::Reset() {
  mSpectralProc.Reset();
  mDynamicsProc.Reset();
  mStereoProc.Reset();
  mNoiseProc.Reset();
  mTextureProc.Reset();
  mAirProc.Reset();
  mMovementProc.Reset();
  mSpaceProc.Reset();
  mGlueProc.Reset();
  mResonanceProc.Reset();
  mProfilesLoaded = false;
  mScratchL.fill(0.f);
  mScratchR.fill(0.f);
}

void TransferEngine::SetSampleRate(double sr) {
  mSampleRate = sr;
  mSpectralProc.SetSampleRate(sr);
  mDynamicsProc.SetSampleRate(sr);
  mStereoProc.SetSampleRate(sr);
  mNoiseProc.SetSampleRate(sr);
  mTextureProc.SetSampleRate(sr);
  mAirProc.SetSampleRate(sr);
  mMovementProc.SetSampleRate(sr);
  mSpaceProc.SetSampleRate(sr);
  mGlueProc.SetSampleRate(sr);
  mResonanceProc.SetSampleRate(sr);
}

void TransferEngine::SetSourceProfile(const DNAProfile& profile) {
  mSourceProfile = profile;
  mProfilesLoaded = true;
  UpdateProcessors();
}

void TransferEngine::SetTargetProfile(const DNAProfile& profile) {
  mTargetProfile = profile;
  mProfilesLoaded = true;
  UpdateProcessors();
}

void TransferEngine::SetTransferParams(const DNATransferParams& params) {
  mParams = params;
  UpdateProcessors();
}

void TransferEngine::UpdateProcessors() {
  if (!mProfilesLoaded) return;

  auto amt = [&](DNAGene g) { return mParams.amounts[static_cast<int>(g)]; };

  mSpectralProc.SetSourceProfile(mSourceProfile.spectral);
  mSpectralProc.SetTargetProfile(mTargetProfile.spectral);
  mSpectralProc.SetTransferAmount(amt(DNAGene::Tone));

  mDynamicsProc.SetSourceProfile(mSourceProfile.dynamics);
  mDynamicsProc.SetTargetProfile(mTargetProfile.dynamics);
  mDynamicsProc.SetTransferAmount(amt(DNAGene::Dynamics));

  mStereoProc.SetSourceProfile(mSourceProfile.stereo);
  mStereoProc.SetTargetProfile(mTargetProfile.stereo);
  mStereoProc.SetTransferAmount(amt(DNAGene::Stereo));

  mNoiseProc.SetSourceProfile(mSourceProfile.noise);
  mNoiseProc.SetTargetProfile(mTargetProfile.noise);
  mNoiseProc.SetTransferAmount(amt(DNAGene::Noise));

  mTextureProc.SetSourceProfile(mSourceProfile.texture);
  mTextureProc.SetTargetProfile(mTargetProfile.texture);
  mTextureProc.SetTransferAmount(amt(DNAGene::Texture));

  mAirProc.SetSourceProfile(mSourceProfile.spectral);
  mAirProc.SetTargetProfile(mTargetProfile.spectral);
  mAirProc.SetTransferAmount(amt(DNAGene::Air));

  mMovementProc.SetSourceProfile(mSourceProfile.movement);
  mMovementProc.SetTargetProfile(mTargetProfile.movement);
  mMovementProc.SetTransferAmount(amt(DNAGene::Movement));

  mSpaceProc.SetSourceProfile(mSourceProfile.space);
  mSpaceProc.SetTargetProfile(mTargetProfile.space);
  mSpaceProc.SetTransferAmount(amt(DNAGene::Space));

  mGlueProc.SetSourceProfile(mSourceProfile.dynamics);
  mGlueProc.SetTargetProfile(mTargetProfile.dynamics);
  mGlueProc.SetTransferAmount(amt(DNAGene::Glue));

  mResonanceProc.SetSourceProfile(mSourceProfile.spectral);
  mResonanceProc.SetTargetProfile(mTargetProfile.spectral);
  mResonanceProc.SetTransferAmount(amt(DNAGene::Resonance));
}

template<typename Proc>
void TransferEngine::ApplyProcessor(Proc& proc, const DNATransferParams& params,
                                     DNAGene gene,
                                     std::array<float, kMaxBlock>& bufL,
                                     std::array<float, kMaxBlock>& bufR,
                                     int numSamples, bool isStereo) {
  if (params.locks[static_cast<int>(gene)]) return;
  proc.Process(bufL.data(), mScratchL.data(), numSamples);
  std::copy(mScratchL.begin(), mScratchL.begin() + numSamples, bufL.begin());
  if (isStereo) {
    proc.Process(bufR.data(), mScratchR.data(), numSamples);
    std::copy(mScratchR.begin(), mScratchR.begin() + numSamples, bufR.begin());
  }
}

template void TransferEngine::ApplyProcessor<>(SpectralProcessor&, const DNATransferParams&, DNAGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(DynamicsProcessor&, const DNATransferParams&, DNAGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(NoiseProcessor&, const DNATransferParams&, DNAGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(TextureProcessor&, const DNATransferParams&, DNAGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(AirProcessor&, const DNATransferParams&, DNAGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(ResonanceProcessor&, const DNATransferParams&, DNAGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);

void TransferEngine::Process(const float* inputL, const float* inputR,
                              float* outputL, float* outputR, int numSamples,
                              bool isStereo) {
  if (!mProfilesLoaded) {
    std::copy(inputL, inputL + numSamples, outputL);
    if (isStereo)
      std::copy(inputR, inputR + numSamples, outputR);
    else
      std::copy(inputL, inputL + numSamples, outputR);
    return;
  }

  auto& p = mParams;
  int pos = 0;
  while (pos < numSamples) {
    int n = std::min(numSamples - pos, kMaxBlock);

    std::copy(inputL + pos, inputL + pos + n, mScratchL.begin());
    const float* rSrc = isStereo ? inputR : inputL;
    std::copy(rSrc + pos, rSrc + pos + n, mScratchR.begin());

    if (!p.locks[static_cast<int>(DNAGene::Tone)])
      ApplyProcessor(mSpectralProc, p, DNAGene::Tone, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(DNAGene::Dynamics)])
      ApplyProcessor(mDynamicsProc, p, DNAGene::Dynamics, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(DNAGene::Noise)])
      ApplyProcessor(mNoiseProc, p, DNAGene::Noise, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(DNAGene::Texture)])
      ApplyProcessor(mTextureProc, p, DNAGene::Texture, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(DNAGene::Air)])
      ApplyProcessor(mAirProc, p, DNAGene::Air, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(DNAGene::Resonance)])
      ApplyProcessor(mResonanceProc, p, DNAGene::Resonance, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(DNAGene::Stereo)] && isStereo)
      mStereoProc.Process(mScratchL.data(), mScratchR.data(),
                           mScratchL.data(), mScratchR.data(), n);

    if (!p.locks[static_cast<int>(DNAGene::Space)] && isStereo) {
      mSpaceProc.Process(mScratchL.data(), mScratchR.data(),
                          mScratchL.data(), mScratchR.data(), n);
    }

    if (!p.locks[static_cast<int>(DNAGene::Movement)] && isStereo) {
      mMovementProc.Process(mScratchL.data(), mScratchR.data(),
                             mScratchL.data(), mScratchR.data(), n);
    }

    if (!p.locks[static_cast<int>(DNAGene::Glue)] && isStereo) {
      mGlueProc.Process(mScratchL.data(), mScratchR.data(),
                         mScratchL.data(), mScratchR.data(), n);
    }

    for (int i = 0; i < n; ++i) {
      outputL[pos + i] = mScratchL[i];
      outputR[pos + i] = isStereo ? mScratchR[i] : mScratchL[i];
    }
    pos += n;
  }
}
