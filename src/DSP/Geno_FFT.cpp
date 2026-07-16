#include "Geno_FFT.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <utility>

FFTProcessor::FFTProcessor(int fftSize, int hopSize)
  : mFFTSize(std::max(32, fftSize))
  , mHopSize(std::clamp(hopSize, 1, mFFTSize))
  , mNumBins(mFFTSize / 2 + 1)
{
  mInputBuffer.resize(mFFTSize, 0.f);
  mOverlapBuffer.resize(mFFTSize, 0.0);
  mFFTIn = (double*)fftw_malloc(sizeof(double) * mFFTSize);
  mFFTOut = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * mFFTSize);
  std::memset(mFFTIn, 0, sizeof(double) * mFFTSize);
  std::memset(mFFTOut, 0, sizeof(fftw_complex) * mFFTSize);
  mForwardPlan = fftw_plan_dft_r2c_1d(mFFTSize, mFFTIn, mFFTOut, FFTW_MEASURE);
  mInversePlan = fftw_plan_dft_c2r_1d(mFFTSize, mFFTOut, mFFTIn, FFTW_MEASURE);
  BuildWindow();
}

FFTProcessor::~FFTProcessor() {
  DestroyResources();
}

void FFTProcessor::DestroyResources() {
  if (mForwardPlan) { fftw_destroy_plan(mForwardPlan); mForwardPlan = nullptr; }
  if (mInversePlan) { fftw_destroy_plan(mInversePlan); mInversePlan = nullptr; }
  if (mFFTIn) { fftw_free(mFFTIn); mFFTIn = nullptr; }
  if (mFFTOut) { fftw_free(mFFTOut); mFFTOut = nullptr; }
}

FFTProcessor::FFTProcessor(FFTProcessor&& other) noexcept
  : mFFTSize(other.mFFTSize)
  , mHopSize(other.mHopSize)
  , mNumBins(other.mNumBins)
  , mSampleRate(other.mSampleRate)
  , mInputBuffer(std::move(other.mInputBuffer))
  , mWindow(std::move(other.mWindow))
  , mFFTIn(other.mFFTIn)
  , mFFTOut(other.mFFTOut)
  , mForwardPlan(other.mForwardPlan)
  , mInversePlan(other.mInversePlan)
  , mOverlapBuffer(std::move(other.mOverlapBuffer))
  , mWindowedBuffer(std::move(other.mWindowedBuffer))
{
  other.mFFTIn = nullptr;
  other.mFFTOut = nullptr;
  other.mForwardPlan = nullptr;
  other.mInversePlan = nullptr;
}

FFTProcessor& FFTProcessor::operator=(FFTProcessor&& other) noexcept {
  if (this != &other) {
    DestroyResources();
    mFFTSize = other.mFFTSize;
    mHopSize = other.mHopSize;
    mNumBins = other.mNumBins;
    mSampleRate = other.mSampleRate;
    mInputBuffer = std::move(other.mInputBuffer);
    mWindow = std::move(other.mWindow);
    mFFTIn = other.mFFTIn;
    mFFTOut = other.mFFTOut;
    mForwardPlan = other.mForwardPlan;
    mInversePlan = other.mInversePlan;
    mOverlapBuffer = std::move(other.mOverlapBuffer);
    mWindowedBuffer = std::move(other.mWindowedBuffer);
    other.mFFTIn = nullptr;
    other.mFFTOut = nullptr;
    other.mForwardPlan = nullptr;
    other.mInversePlan = nullptr;
  }
  return *this;
}

void FFTProcessor::Reset() {
  std::fill(mInputBuffer.begin(), mInputBuffer.end(), 0.f);
  std::fill(mOverlapBuffer.begin(), mOverlapBuffer.end(), 0.0);
}

void FFTProcessor::SetSampleRate(double sr) {
  mSampleRate = sr;
}

void FFTProcessor::BuildWindow() {
  mWindow.resize(mFFTSize);
  for (int i = 0; i < mFFTSize; ++i) {
    mWindow[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (mFFTSize - 1)));
  }
}

void FFTProcessor::ApplyWindowToBuffer(double* buf) {
  for (int i = 0; i < mFFTSize; ++i) {
    buf[i] *= mWindow[i];
  }
}

void FFTProcessor::ProcessBlock(const float* input, int numSamples) {
  int writePos = mFFTSize - mHopSize;
  std::copy(mInputBuffer.begin() + mHopSize, mInputBuffer.end(),
            mInputBuffer.begin());
  int copyLen = std::min(numSamples, mHopSize);
  std::copy(input, input + copyLen,
            mInputBuffer.begin() + writePos);

  std::copy(mInputBuffer.begin(), mInputBuffer.end(), mFFTIn);
  ApplyWindowToBuffer(mFFTIn);
  fftw_execute(mForwardPlan);
}

void FFTProcessor::GetMagnitudeSpectrum(std::vector<double>& mag, bool /*applyWindow*/) {
  if ((int)mag.size() != mNumBins) mag.assign(mNumBins, 0.0);
  double norm = 1.0 / mFFTSize;
  for (int i = 0; i < mNumBins; ++i) {
    double re = mFFTOut[i][0] * norm;
    double im = mFFTOut[i][1] * norm;
    mag[i] = std::sqrt(re * re + im * im);
  }
}

void FFTProcessor::GetPhaseSpectrum(std::vector<double>& phase) {
  if ((int)phase.size() != mNumBins) phase.assign(mNumBins, 0.0);
  for (int i = 0; i < mNumBins; ++i) {
    phase[i] = std::atan2(mFFTOut[i][1], mFFTOut[i][0]);
  }
}

void FFTProcessor::MagnitudeToEnvelope(const std::vector<double>& mag,
                                        std::vector<double>& envelope,
                                        int numBands) {
  envelope.resize(numBands, 0.0);
  if (mag.empty()) return;
  int binsPerBand = std::max(1, mNumBins / numBands);
  for (int b = 0; b < numBands; ++b) {
    int startBin = b * binsPerBand;
    int endBin = std::min((b + 1) * binsPerBand, mNumBins);
    double sum = 0.0;
    for (int i = startBin; i < endBin; ++i) {
      sum += mag[i];
    }
    envelope[b] = sum / (endBin - startBin);
  }
}

void FFTProcessor::EnsureWindowedBuffer() {
  if ((int)mWindowedBuffer.size() != mFFTSize)
    mWindowedBuffer.resize(mFFTSize, 0.0);
}

void FFTProcessor::ApplySpectralFilter(const std::vector<double>& filter,
                                        std::vector<double>& magOut) {
  int numBins = std::min((int)filter.size(), mNumBins);
  for (int i = 0; i < numBins; ++i) {
    magOut[i] *= filter[i];
  }
}

void FFTProcessor::SynthesizeFromSpectrum(const std::vector<double>& mag,
                                           const std::vector<double>& phase,
                                           float* output, int numSamples) {
  for (int i = 0; i < mNumBins; ++i) {
    mFFTOut[i][0] = mag[i] * std::cos(phase[i]) * mFFTSize;
    mFFTOut[i][1] = mag[i] * std::sin(phase[i]) * mFFTSize;
  }
  fftw_execute(mInversePlan);

  EnsureWindowedBuffer();
  for (int i = 0; i < mFFTSize; ++i) {
    mWindowedBuffer[i] = mFFTIn[i] * mWindow[i];
  }

  int outSamples = std::min(numSamples, mHopSize);
  for (int i = 0; i < outSamples; ++i) {
    double val = (mWindowedBuffer[i] + mOverlapBuffer[i]) / (mFFTSize * 0.5);
    output[i] = (float)std::clamp(val, -1.0, 1.0);
  }

  for (int i = 0; i < mFFTSize - mHopSize; ++i) {
    mOverlapBuffer[i] = mWindowedBuffer[i + mHopSize];
  }
  for (int i = mFFTSize - mHopSize; i < mFFTSize; ++i) {
    mOverlapBuffer[i] = 0.0;
  }
}
