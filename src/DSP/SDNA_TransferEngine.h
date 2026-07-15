#pragma once
#include "SDNA_Types.h"
#include "SDNA_Analyzer.h"
#include "SDNA_SpectralProcessor.h"
#include "SDNA_DynamicsProcessor.h"
#include "SDNA_StereoProcessor.h"
#include "SDNA_NoiseProcessor.h"
#include "SDNA_TextureProcessor.h"
#include "SDNA_AirProcessor.h"
#include "SDNA_MovementProcessor.h"
#include "SDNA_SpaceProcessor.h"
#include "SDNA_GlueProcessor.h"
#include "SDNA_ResonanceProcessor.h"
#include <memory>
#include <array>

class TransferEngine {
public:
  TransferEngine();
  ~TransferEngine() = default;

  void Reset();
  void SetSampleRate(double sr);

  void SetSourceProfile(const DNAProfile& profile);
  void SetTargetProfile(const DNAProfile& profile);
  void SetTransferParams(const DNATransferParams& params);

  void Process(const float* inputL, const float* inputR,
               float* outputL, float* outputR, int numSamples,
               bool isStereo);

  DNAProfile GetCurrentSource() const { return mSourceProfile; }
  DNAProfile GetCurrentTarget() const { return mTargetProfile; }
  void AnalyzeInput(const float* inputL, const float* inputR,
                    int numSamples, bool isStereo, DNAProfile& out);

private:
  double mSampleRate = 44100.0;
  DNATransferParams mParams;
  DNAProfile mSourceProfile;
  DNAProfile mTargetProfile;
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
  void ApplyProcessor(Proc& proc, const DNATransferParams& params,
                      DNAGene gene, std::array<float, kMaxBlock>& bufL,
                      std::array<float, kMaxBlock>& bufR,
                      int numSamples, bool isStereo);
};
