#include "Geno_TransferEngine.h"
#include <cstring>
#include <algorithm>
#include <cmath>

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
  mSourceLoaded = false;
  mTargetLoaded = false;
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

void TransferEngine::SetSourceProfile(const GenoProfile& profile) {
  mSourceProfile = profile;
  mSourceLoaded = true;
  if (mSourceLoaded && mTargetLoaded) UpdateProcessors();
}

void TransferEngine::SetTargetProfile(const GenoProfile& profile) {
  mTargetProfile = profile;
  mTargetLoaded = true;
  if (mSourceLoaded && mTargetLoaded) UpdateProcessors();
}

void TransferEngine::SetTransferParams(const GenoTransferParams& params) {
  mParams = params;
  UpdateProcessors();
}

void TransferEngine::UpdateProcessors() {
  if (!mSourceLoaded || !mTargetLoaded) {
    fprintf(stderr, "[DBUG] TE::UpdateProcessors: SKIP (sL=%d tL=%d)\n", mSourceLoaded, mTargetLoaded);
    return;
  }

  auto amt = [&](GenoGene g) { return mParams.amounts[static_cast<int>(g)]; };

  fprintf(stderr, "[DBUG] TE::UpdateProcessors: amts Tone=%.3f Dyn=%.3f Noise=%.3f Tex=%.3f Air=%.3f Res=%.3f\n",
          amt(GenoGene::Tone), amt(GenoGene::Dynamics), amt(GenoGene::Noise),
          amt(GenoGene::Texture), amt(GenoGene::Air), amt(GenoGene::Resonance));

  mSpectralProc.SetSourceProfile(mSourceProfile.spectral);
  mSpectralProc.SetTargetProfile(mTargetProfile.spectral);
  mSpectralProc.SetTransferAmount(amt(GenoGene::Tone));

  mDynamicsProc.SetSourceProfile(mSourceProfile.dynamics);
  mDynamicsProc.SetTargetProfile(mTargetProfile.dynamics);
  mDynamicsProc.SetTransferAmount(amt(GenoGene::Dynamics));

  mStereoProc.SetSourceProfile(mSourceProfile.stereo);
  mStereoProc.SetTargetProfile(mTargetProfile.stereo);
  mStereoProc.SetTransferAmount(amt(GenoGene::Stereo));

  mNoiseProc.SetSourceProfile(mSourceProfile.noise);
  mNoiseProc.SetTargetProfile(mTargetProfile.noise);
  mNoiseProc.SetTransferAmount(amt(GenoGene::Noise));

  mTextureProc.SetSourceProfile(mSourceProfile.texture);
  mTextureProc.SetTargetProfile(mTargetProfile.texture);
  mTextureProc.SetTransferAmount(amt(GenoGene::Texture));

  mAirProc.SetSourceProfile(mSourceProfile.spectral);
  mAirProc.SetTargetProfile(mTargetProfile.spectral);
  mAirProc.SetTransferAmount(amt(GenoGene::Air));

  mMovementProc.SetSourceProfile(mSourceProfile.movement);
  mMovementProc.SetTargetProfile(mTargetProfile.movement);
  mMovementProc.SetTransferAmount(amt(GenoGene::Movement));

  mSpaceProc.SetSourceProfile(mSourceProfile.space);
  mSpaceProc.SetTargetProfile(mTargetProfile.space);
  mSpaceProc.SetTransferAmount(amt(GenoGene::Space));

  mGlueProc.SetSourceProfile(mSourceProfile.dynamics);
  mGlueProc.SetTargetProfile(mTargetProfile.dynamics);
  mGlueProc.SetTransferAmount(amt(GenoGene::Glue));

  mResonanceProc.SetSourceProfile(mSourceProfile.spectral);
  mResonanceProc.SetTargetProfile(mTargetProfile.spectral);
  mResonanceProc.SetTransferAmount(amt(GenoGene::Resonance));
}

template<typename Proc>
void TransferEngine::ApplyProcessor(Proc& proc, const GenoTransferParams& params,
                                     GenoGene gene,
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

template void TransferEngine::ApplyProcessor<>(SpectralProcessor&, const GenoTransferParams&, GenoGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(DynamicsProcessor&, const GenoTransferParams&, GenoGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(NoiseProcessor&, const GenoTransferParams&, GenoGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(TextureProcessor&, const GenoTransferParams&, GenoGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(AirProcessor&, const GenoTransferParams&, GenoGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);
template void TransferEngine::ApplyProcessor<>(ResonanceProcessor&, const GenoTransferParams&, GenoGene, std::array<float, kMaxBlock>&, std::array<float, kMaxBlock>&, int, bool);

void TransferEngine::Process(const float* inputL, const float* inputR,
                              float* outputL, float* outputR, int numSamples,
                              bool isStereo) {
  {
    static int pfc = 0;
    if (++pfc % 500 == 0) {
      float inPeakL = 0, inPeakR = 0;
      for (int i = 0; i < numSamples; ++i) {
        if (std::fabs(inputL[i]) > inPeakL) inPeakL = std::fabs(inputL[i]);
        if (std::fabs(inputR[i]) > inPeakR) inPeakR = std::fabs(inputR[i]);
      }
      fprintf(stderr, "[DBUG] TE::Process: sL=%d tL=%d inPk=%.4f/%.4f %s\n",
              mSourceLoaded, mTargetLoaded, inPeakL, inPeakR,
              (!mSourceLoaded || !mTargetLoaded) ? "PASS" : "PROC");
    }
  }

  if (!mSourceLoaded || !mTargetLoaded) {
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

    if (!p.locks[static_cast<int>(GenoGene::Tone)])
      ApplyProcessor(mSpectralProc, p, GenoGene::Tone, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(GenoGene::Dynamics)])
      ApplyProcessor(mDynamicsProc, p, GenoGene::Dynamics, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(GenoGene::Noise)])
      ApplyProcessor(mNoiseProc, p, GenoGene::Noise, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(GenoGene::Texture)])
      ApplyProcessor(mTextureProc, p, GenoGene::Texture, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(GenoGene::Air)])
      ApplyProcessor(mAirProc, p, GenoGene::Air, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(GenoGene::Resonance)])
      ApplyProcessor(mResonanceProc, p, GenoGene::Resonance, mScratchL, mScratchR, n, isStereo);

    if (!p.locks[static_cast<int>(GenoGene::Stereo)] && isStereo)
      mStereoProc.Process(mScratchL.data(), mScratchR.data(),
                           mScratchL.data(), mScratchR.data(), n);

    if (!p.locks[static_cast<int>(GenoGene::Space)] && isStereo) {
      mSpaceProc.Process(mScratchL.data(), mScratchR.data(),
                          mScratchL.data(), mScratchR.data(), n);
    }

    if (!p.locks[static_cast<int>(GenoGene::Movement)] && isStereo) {
      mMovementProc.Process(mScratchL.data(), mScratchR.data(),
                             mScratchL.data(), mScratchR.data(), n);
    }

    if (!p.locks[static_cast<int>(GenoGene::Glue)] && isStereo) {
      mGlueProc.Process(mScratchL.data(), mScratchR.data(),
                         mScratchL.data(), mScratchR.data(), n);
    }

    for (int i = 0; i < n; ++i) {
      outputL[pos + i] = mScratchL[i];
      outputR[pos + i] = isStereo ? mScratchR[i] : mScratchL[i];
    }
    pos += n;
  }

  {
    static int ofc = 0;
    if (++ofc % 500 == 0) {
      float outPeakL = 0, outPeakR = 0;
      for (int i = 0; i < numSamples; ++i) {
        if (std::fabs(outputL[i]) > outPeakL) outPeakL = std::fabs(outputL[i]);
        if (std::fabs(outputR[i]) > outPeakR) outPeakR = std::fabs(outputR[i]);
        if (outputL[i] != outputL[i]) { fprintf(stderr, "[DBUG] TE::Process: NaN in outputL at %d\n", i); break; }
        if (outputR[i] != outputR[i]) { fprintf(stderr, "[DBUG] TE::Process: NaN in outputR at %d\n", i); break; }
      }
      fprintf(stderr, "[DBUG] TE::Process: outPk=%.4f/%.4f amts=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f] locks=[%d,%d,%d,%d,%d,%d,%d,%d,%d]\n",
              outPeakL, outPeakR,
              mParams.amounts[0], mParams.amounts[1], mParams.amounts[2],
              mParams.amounts[3], mParams.amounts[4], mParams.amounts[5],
              mParams.amounts[6], mParams.amounts[7], mParams.amounts[8],
              (int)mParams.locks[0], (int)mParams.locks[1], (int)mParams.locks[2],
              (int)mParams.locks[3], (int)mParams.locks[4], (int)mParams.locks[5],
              (int)mParams.locks[6], (int)mParams.locks[7], (int)mParams.locks[8]);
    }
  }
}
