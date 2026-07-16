#pragma once
#include "SDNA_Types.h"
#include "SDNA_FFT.h"
#include <vector>
#include <deque>

class DNAAnalyzer {
public:
  DNAAnalyzer();
  ~DNAAnalyzer() = default;

  void Reset();
  void SetSampleRate(double sr);
  void AnalyzeBlock(const float* inputL, const float* inputR,
                    int numSamples, bool isStereo);
  void ComputeFullAnalysis(const float* audioL, const float* audioR,
                           int numSamples, bool isStereo, DNAProfile& out);

private:
  double mSampleRate = 44100.0;
  FFTProcessor mFFT;

  void AnalyzeSpectral(const float* audio, int numSamples,
                       SpectralFeatures& out);
  void AnalyzeHarmonics(const std::vector<double>& mag,
                        double fundamentalBin,
                        std::vector<double>& harmonicProfile);
  void AnalyzeDynamics(const float* audio, int numSamples,
                       DynamicFeatures& out);
  void AnalyzeStereo(const float* audioL, const float* audioR,
                     int numSamples, StereoFeatures& out);
  void AnalyzeNoise(const float* audio, int numSamples,
                    NoiseFeatures& out);
  void AnalyzeTexture(const float* audio, int numSamples, double rms, double pitch,
                      TextureFeatures& out);
  void AnalyzeSpace(const float* audio, int numSamples,
                    SpaceFeatures& out);
  void AnalyzeMovement(const float* audio, int numSamples,
                       MovementFeatures& out);

  double ComputeSpectralCentroid(const std::vector<double>& mag);
  double ComputeSpectralSpread(const std::vector<double>& mag, double centroid);
  double ComputeSpectralRolloff(const std::vector<double>& mag,
                                double percent = 0.85);
  double ComputeSpectralFlatness(const std::vector<double>& mag);
  double ComputePitchAutocorrelation(const float* audio, int numSamples);

  std::deque<double> mRMSBuffer;
  std::deque<double> mPeakBuffer;
  static constexpr int kAnalysisWindow = 4410;
  static constexpr int kMinAnalysisSamples = 2048;
};
