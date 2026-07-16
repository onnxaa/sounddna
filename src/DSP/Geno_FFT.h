#pragma once
#include <fftw3.h>
#include <vector>
#include <memory>
#include "Geno_Types.h"

class FFTProcessor {
public:
  FFTProcessor(int fftSize = kFFTSize, int hopSize = kFFTHop);
  ~FFTProcessor();
  FFTProcessor(const FFTProcessor&) = delete;
  FFTProcessor& operator=(const FFTProcessor&) = delete;
  FFTProcessor(FFTProcessor&& other) noexcept;
  FFTProcessor& operator=(FFTProcessor&& other) noexcept;

  void Reset();
  void SetSampleRate(double sr);
  void ProcessBlock(const float* input, int numSamples);
  void GetMagnitudeSpectrum(std::vector<double>& mag, bool applyWindow = true);
  void GetPhaseSpectrum(std::vector<double>& phase);
  void SynthesizeFromSpectrum(const std::vector<double>& mag,
                              const std::vector<double>& phase,
                              float* output, int numSamples);
  void MagnitudeToEnvelope(const std::vector<double>& mag,
                           std::vector<double>& envelope, int numBands);
  void ApplySpectralFilter(const std::vector<double>& filter,
                           std::vector<double>& magOut);

  int GetFFTSize() const { return mFFTSize; }
  int GetHopSize() const { return mHopSize; }
  int GetNumBins() const { return mNumBins; }

private:
  int mFFTSize;
  int mHopSize;
  int mNumBins;
  double mSampleRate = 44100.0;

  std::vector<float> mInputBuffer;
  std::vector<double> mWindow;
  double* mFFTIn = nullptr;
  fftw_complex* mFFTOut = nullptr;
  fftw_plan mForwardPlan = nullptr;
  fftw_plan mInversePlan = nullptr;

  std::vector<double> mOverlapBuffer;
  std::vector<double> mWindowedBuffer;

  void BuildWindow();
  void EnsureWindowedBuffer();
  void ApplyWindowToBuffer(double* buf);
  void DestroyResources();
};
