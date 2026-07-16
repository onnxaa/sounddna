#pragma once
#include "Geno_Types.h"
#include "Geno_Analyzer.h"
#include "Geno_SpectralProcessor.h"
#include "Geno_DynamicsProcessor.h"
#include "Geno_StereoProcessor.h"
#include "Geno_NoiseProcessor.h"
#include "Geno_TextureProcessor.h"
#include "Geno_AirProcessor.h"
#include "Geno_MovementProcessor.h"
#include "Geno_SpaceProcessor.h"
#include "Geno_GlueProcessor.h"
#include "Geno_ResonanceProcessor.h"
#include <memory>
#include <array>

class TransferEngine {
public:
  TransferEngine();
  ~TransferEngine() = default;

  void Reset();
  void SetSampleRate(double sr);

  void SetSourceProfile(const GenoProfile& profile);
  void SetTargetProfile(const GenoProfile& profile);
  void SetTransferParams(const GenoTransferParams& params);

  void Process(const float* inputL, const float* inputR,
               float* outputL, float* outputR, int numSamples,
               bool isStereo);

  GenoProfile GetCurrentSource() const { return mSourceProfile; }
  GenoProfile GetCurrentTarget() const { return mTargetProfile; }

private:
  double mSampleRate = 44100.0;
  GenoTransferParams mParams;
  GenoProfile mSourceProfile;
  GenoProfile mTargetProfile;
  bool mProfilesLoaded = false;

  SpectralProcessor mSpectralProc;
  DynamicsProcessor mDynamicsProc;
  StereoProcessor mStereoProc;
  NoiseProcessor mNoiseProc;
  TextureProcessor mTextureProc;
  AirProcessor mAirProc;
  MovementProcessor mMovementProc;
  SpaceProcessor mSpaceProc;
  GlueProcessor mGlueProc;
  ResonanceProcessor mResonanceProc;

  static constexpr int kMaxBlock = 8192;
  std::array<float, kMaxBlock> mScratchL;
  std::array<float, kMaxBlock> mScratchR;

  void UpdateProcessors();
  template<typename Proc>
  void ApplyProcessor(Proc& proc, const GenoTransferParams& params,
                      GenoGene gene, std::array<float, kMaxBlock>& bufL,
                      std::array<float, kMaxBlock>& bufR,
                      int numSamples, bool isStereo);
};
