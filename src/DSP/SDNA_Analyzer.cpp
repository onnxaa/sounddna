#include "SDNA_Analyzer.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

DNAAnalyzer::DNAAnalyzer()
  : mFFT(kFFTSize, kFFTHop)
{
  mRMSBuffer.resize(kAnalysisWindow, 0.0);
  mPeakBuffer.resize(kAnalysisWindow, 0.0);
}

void DNAAnalyzer::Reset() {
  mFFT.Reset();
  std::fill(mRMSBuffer.begin(), mRMSBuffer.end(), 0.0);
  std::fill(mPeakBuffer.begin(), mPeakBuffer.end(), 0.0);
}

void DNAAnalyzer::SetSampleRate(double sr) {
  mSampleRate = sr;
  mFFT.SetSampleRate(sr);
}

void DNAAnalyzer::AnalyzeBlock(const float* inputL, const float* /*inputR*/,
                                int numSamples, bool /*isStereo*/) {
  mFFT.ProcessBlock(inputL, numSamples);
}

void DNAAnalyzer::ComputeFullAnalysis(const float* audioL, const float* audioR,
                                       int numSamples, bool isStereo,
                                       DNAProfile& out) {
  if (numSamples < kMinAnalysisSamples) return;
  AnalyzeSpectral(audioL, numSamples, out.spectral);
  AnalyzeDynamics(audioL, numSamples, out.dynamics);
  if (isStereo) AnalyzeStereo(audioL, audioR, numSamples, out.stereo);
  AnalyzeNoise(audioL, numSamples, out.noise);
  AnalyzeTexture(audioL, numSamples, out.texture);
  AnalyzeSpace(audioL, numSamples, out.space);
  AnalyzeMovement(audioL, numSamples, out.movement);
}

void DNAAnalyzer::AnalyzeSpectral(const float* audio, int numSamples,
                                   SpectralFeatures& out) {
  int numFrames = numSamples / kFFTHop;
  if (numFrames < 1) numFrames = 1;

  std::vector<double> accumMag(mFFT.GetNumBins(), 0.0);
  std::vector<double> mag;

  for (int f = 0; f < numFrames; ++f) {
    int offset = f * kFFTHop;
    if (numSamples - offset < kFFTSize) break;
    mFFT.ProcessBlock(audio + offset, kFFTHop);
    mFFT.GetMagnitudeSpectrum(mag);
    for (size_t i = 0; i < mag.size(); ++i)
      accumMag[i] += mag[i];
  }

  double invFrames = 1.0 / numFrames;
  for (auto& v : accumMag) v *= invFrames;

  mFFT.MagnitudeToEnvelope(accumMag, out.spectralEnvelope, kNumSpectralBands);
  AnalyzeHarmonics(accumMag, out.harmonicProfile);
  out.centroid = ComputeSpectralCentroid(accumMag);
  out.spread = ComputeSpectralSpread(accumMag, out.centroid);
  out.rolloff = ComputeSpectralRolloff(accumMag);
  out.spectralFlatness = ComputeSpectralFlatness(accumMag);

  int nyquist = mFFT.GetNumBins();
  int crossoverBin = (int)(2000.0 * nyquist / (mSampleRate * 0.5));
  crossoverBin = std::min(crossoverBin, nyquist - 1);
  double brightSum = 0.0, totalSum = 0.0;
  for (int i = 0; i < nyquist; ++i) {
    totalSum += accumMag[i];
    if (i >= crossoverBin) brightSum += accumMag[i];
  }
  out.brightness = (totalSum > 0.0) ? brightSum / totalSum : 0.0;

  out.pitch = ComputePitchAutocorrelation(audio, numSamples);
  out.pitchConfidence = (out.pitch > 0.0) ?
    std::min(1.0, (1.0 - out.spectralFlatness) * 1.5) : 0.0;
}

double DNAAnalyzer::ComputePitchAutocorrelation(const float* audio, int numSamples) {
  double bestCorr = 0.0;
  double bestLag = 0.0;
  int minLag = (int)(mSampleRate / 2000.0);
  int maxLag = (int)(mSampleRate / 50.0);
  if (maxLag >= numSamples / 2) maxLag = numSamples / 2 - 1;
  if (minLag >= maxLag) return 0.0;

  double mean = 0.0;
  for (int i = 0; i < numSamples; ++i) mean += (double)audio[i];
  mean /= numSamples;
  double energy = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    double centered = (double)audio[i] - mean;
    energy += centered * centered;
  }
  if (energy < 1e-10) return 0.0;

  for (int lag = minLag; lag <= maxLag; ++lag) {
    double corr = 0.0;
    for (int i = 0; i < numSamples - lag; ++i) {
      double c0 = (double)audio[i] - mean;
      double c1 = (double)audio[i + lag] - mean;
      corr += c0 * c1;
    }
    corr /= energy;
    if (corr > bestCorr) {
      bestCorr = corr;
      bestLag = (double)lag;
    }
  }

  if (bestCorr > 0.3)
    return mSampleRate / bestLag;
  return 0.0;
}

void DNAAnalyzer::AnalyzeHarmonics(const std::vector<double>& mag,
                                    std::vector<double>& harmonicProfile) {
  harmonicProfile.resize(kNumHarmonics, 0.0);
  if (mag.empty()) return;
  int numBins = (int)mag.size();
  double maxMag = 0.0;
  int maxIdx = 0;
  for (int i = 1; i < numBins; ++i) {
    if (mag[i] > maxMag) { maxMag = mag[i]; maxIdx = i; }
  }
  if (maxIdx < 1) return;
  double fundamentalBin = (double)maxIdx;

  for (int h = 0; h < kNumHarmonics; ++h) {
    int binIdx = (int)std::round(fundamentalBin * (h + 1));
    if (binIdx < numBins) harmonicProfile[h] = mag[binIdx];
  }

  double maxH = *std::max_element(harmonicProfile.begin(), harmonicProfile.end());
  if (maxH > 0.0)
    for (auto& v : harmonicProfile) v /= maxH;
}

void DNAAnalyzer::AnalyzeDynamics(const float* audio, int numSamples,
                                   DynamicFeatures& out) {
  double sumSq = 0.0, peakVal = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    double s = std::abs((double)audio[i]);
    sumSq += s * s;
    if (s > peakVal) peakVal = s;
  }

  double rms = std::sqrt(sumSq / numSamples);
  out.rms = rms;
  out.peak = peakVal;
  out.crestFactor = (rms > 0.0) ? peakVal / rms : 1.0;
  out.dynamicRange = (rms > 0.0) ? 20.0 * std::log10(peakVal / rms) : 0.0;

  double env = 0.0;
  double attackCoef = std::exp(-1.0 / (0.003 * mSampleRate));
  double releaseCoef = std::exp(-1.0 / (0.050 * mSampleRate));
  std::vector<double> envelope(numSamples);
  for (int i = 0; i < numSamples; ++i) {
    double absIn = std::abs((double)audio[i]);
    double coef = (absIn > env) ? attackCoef : releaseCoef;
    env = coef * env + (1.0 - coef) * absIn;
    envelope[i] = env;
  }
  out.envelopeMean = env;

  double attackMs = 0.0, releaseMs = 0.0;
  double peakEnv = *std::max_element(envelope.begin(), envelope.end());
  double threshold = peakEnv * 0.1;
  int onsetIdx = -1, peakIdx = -1, releaseEndIdx = -1;
  for (int i = 1; i < numSamples; ++i) {
    if (onsetIdx < 0 && envelope[i] > threshold && envelope[i] > envelope[i-1])
      onsetIdx = i;
    if (onsetIdx >= 0 && peakIdx < 0 && envelope[i] >= peakEnv * 0.98)
      peakIdx = i;
    if (peakIdx >= 0 && releaseEndIdx < 0 &&
        envelope[i] < threshold && envelope[i] < envelope[i-1])
      releaseEndIdx = i;
  }
  if (onsetIdx >= 0 && peakIdx > onsetIdx)
    attackMs = (peakIdx - onsetIdx) / mSampleRate * 1000.0;
  if (peakIdx >= 0 && releaseEndIdx > peakIdx)
    releaseMs = (releaseEndIdx - peakIdx) / mSampleRate * 1000.0;
  out.attackMs = attackMs;
  out.releaseMs = releaseMs;

  double compSum = 0.0;
  int compCount = 0;
  double compThreshold = rms * 0.5;
  for (int i = 0; i < numSamples; ++i) {
    if (envelope[i] > compThreshold) {
      compSum += envelope[i] / compThreshold;
      compCount++;
    }
  }
  out.compressionRatio = (compCount > 0) ? compSum / compCount : 1.0;
}

void DNAAnalyzer::AnalyzeStereo(const float* audioL, const float* audioR,
                                 int numSamples, StereoFeatures& out) {
  double sumL2 = 0.0, sumR2 = 0.0, sumLR = 0.0;
  double sumL = 0.0, sumR = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    sumL += audioL[i]; sumR += audioR[i];
    sumL2 += audioL[i] * audioL[i];
    sumR2 += audioR[i] * audioR[i];
    sumLR += audioL[i] * audioR[i];
  }
  double varL = sumL2 - sumL * sumL / numSamples;
  double varR = sumR2 - sumR * sumR / numSamples;
  double cov = sumLR - sumL * sumR / numSamples;
  out.isMono = (varL < 1e-10 && varR < 1e-10);
  out.phaseCorrelation = (!out.isMono && varL * varR > 0.0) ?
    cov / std::sqrt(varL * varR) : 1.0;

  double sumMid = 0.0, sumSide = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    double mid = (audioL[i] + audioR[i]) * 0.5;
    double side = (audioL[i] - audioR[i]) * 0.5;
    sumMid += mid * mid; sumSide += side * side;
  }
  out.width = (sumMid > 0.0) ? std::sqrt(sumSide / sumMid) : 0.0;
  out.balance = (sumR2 + sumL2 > 0.0) ?
    (sumR2 - sumL2) / (sumR2 + sumL2) : 0.0;

  double phaseSum = 0.0;
  int phaseCount = 0;
  for (int i = 1; i < numSamples; ++i) {
    double phL = std::atan2(audioL[i], audioL[i - 1]);
    double phR = std::atan2(audioR[i], audioR[i - 1]);
    phaseSum += std::abs(phL - phR);
    phaseCount++;
  }
  out.phaseDrift = (phaseCount > 0) ? phaseSum / phaseCount : 0.0;
}

void DNAAnalyzer::AnalyzeNoise(const float* audio, int numSamples,
                                NoiseFeatures& out) {
  std::vector<double> magAccum;
  int numFrames = 0;

  for (int f = 0; f * kFFTHop + kFFTSize <= numSamples; ++f) {
    int offset = f * kFFTHop;
    mFFT.ProcessBlock(audio + offset, kFFTHop);
    std::vector<double> mag;
    mFFT.GetMagnitudeSpectrum(mag);
    if (magAccum.empty()) magAccum.resize(mag.size(), 0.0);
    for (size_t i = 0; i < mag.size(); ++i)
      magAccum[i] += mag[i];
    numFrames++;
  }

  if (numFrames == 0) {
    out.noiseFloorDb = -90.0;
    out.signalToNoise = 90.0;
    out.spectralTilt = 0.0;
    out.humContent = 0.0;
    return;
  }

  for (auto& v : magAccum) v /= numFrames;

  // Noise floor = spectral minimum (bottom 10% percentile)
  std::vector<double> sorted = magAccum;
  std::sort(sorted.begin(), sorted.end());
  int noiseBinCount = std::max(1, (int)sorted.size() / 10);
  double noiseSum = 0.0;
  for (int i = 0; i < noiseBinCount; ++i) noiseSum += sorted[i];
  double noiseLevel = noiseSum / noiseBinCount;

  // Signal level = total spectral energy
  double signalLevel = 0.0;
  for (auto& v : magAccum) signalLevel += v;
  signalLevel /= magAccum.size();

  // Convert to dB (normalize by FFT size)
  double noiseAbs = noiseLevel / (kFFTSize / 2);
  double sigAbs = signalLevel / (kFFTSize / 2);
  out.noiseFloorDb = (noiseAbs > 1e-15) ? std::max(-120.0, 20.0 * std::log10(noiseAbs)) : -120.0;
  out.signalToNoise = (noiseAbs > 1e-15 && sigAbs > noiseAbs) ?
    20.0 * std::log10(sigAbs / noiseAbs) : 90.0;

  // Hum detection: look for peak at 50/60Hz
  int humBin = (int)(55.0 * kFFTSize / mSampleRate);
  if (humBin > 0 && humBin < (int)magAccum.size())
    out.humContent = magAccum[humBin] / (noiseLevel + 1e-15);
  else
    out.humContent = 0.0;

  // Spectral tilt: avg slope of spectrum in dB/oct
  double lowSum = 0.0, highSum = 0.0;
  int lowCount = 0, highCount = 0;
  int midBin = (int)(0.25 * magAccum.size());
  for (int i = 2; i < midBin && i < (int)magAccum.size(); ++i) {
    lowSum += magAccum[i]; lowCount++;
  }
  for (int i = midBin; i < (int)magAccum.size() && i < (int)(0.9 * magAccum.size()); ++i) {
    highSum += magAccum[i]; highCount++;
  }
  double lowAvg = (lowCount > 0) ? lowSum / lowCount : 1e-15;
  double highAvg = (highCount > 0) ? highSum / highCount : 1e-15;
  out.spectralTilt = (lowAvg > 0.0 && highAvg > 0.0) ?
    10.0 * std::log10(highAvg / lowAvg) : 0.0;

  // Noise shape coefficients
  int binStep = (int)magAccum.size() / kNumNoiseCoefs;
  for (int i = 0; i < kNumNoiseCoefs; ++i) {
    int start = i * binStep;
    int end = std::min(start + binStep, (int)magAccum.size());
    double sum = 0.0;
    for (int b = start; b < end; ++b) sum += magAccum[b];
    out.noiseShape[i] = (end > start) ? sum / (end - start) / (noiseLevel + 1e-15) : 0.0;
  }
}

void DNAAnalyzer::AnalyzeTexture(const float* audio, int numSamples,
                                  TextureFeatures& out) {
  double oddHarmonics = 0.0, evenHarmonics = 0.0;
  std::vector<double> mag;
  int numFrames = std::max(1, numSamples / kFFTHop);
  for (int f = 0; f < numFrames; ++f) {
    int offset = f * kFFTHop;
    if (numSamples - offset < kFFTSize) break;
    mFFT.ProcessBlock(audio + offset, kFFTHop);
    mFFT.GetMagnitudeSpectrum(mag);
    for (int h = 1; h <= 16; ++h) {
      int bin = (int)(h * 100.0 * mFFT.GetFFTSize() / mSampleRate);
      if (bin < (int)mag.size() && bin > 0) {
        if (h % 2 == 0) evenHarmonics += mag[bin];
        else oddHarmonics += mag[bin];
      }
    }
  }
  double total = oddHarmonics + evenHarmonics;
  out.harmonicDistortion = (total > 0.0) ? evenHarmonics / total : 0.0;
  out.saturationAmount = (total > 0.0) ? oddHarmonics / total : 0.0;

  double transientEnergy = 0.0, steadyEnergy = 0.0;
  for (int i = 1; i < numSamples; ++i) {
    double diff = std::abs((double)audio[i] - audio[i - 1]);
    if (diff > 0.1) transientEnergy += diff * diff;
    else steadyEnergy += (double)audio[i] * audio[i];
  }
  out.transientShape = (transientEnergy + steadyEnergy > 0.0) ?
    transientEnergy / (transientEnergy + steadyEnergy) : 0.0;
}

void DNAAnalyzer::AnalyzeSpace(const float* audio, int numSamples,
                                SpaceFeatures& out) {
  double decayEstimate = 0.0;
  double env = 0.0;
  double attackCoef = std::exp(-1.0 / (0.001 * mSampleRate));
  double releaseCoef = std::exp(-1.0 / (0.200 * mSampleRate));
  std::vector<double> envelope(numSamples);
  for (int i = 0; i < numSamples; ++i) {
    double absIn = std::abs((double)audio[i]);
    double coef = (absIn > env) ? attackCoef : releaseCoef;
    env = coef * env + (1.0 - coef) * absIn;
    envelope[i] = env;
  }
  int peakIdx = (int)(std::max_element(envelope.begin(), envelope.end()) - envelope.begin());
  double peakVal = envelope[peakIdx];
  if (peakVal > 0.001) {
    double halfVal = peakVal * 0.5;
    int halfIdx = peakIdx;
    for (int i = peakIdx; i < numSamples; ++i) {
      if (envelope[i] <= halfVal) { halfIdx = i; break; }
    }
    decayEstimate = (halfIdx - peakIdx) / mSampleRate * 1000.0;
  }
  out.decayTime = std::clamp(decayEstimate * 4.0, 0.0, 10000.0);
  out.earlyReflections = (decayEstimate > 0.0) ?
    std::min(1.0, decayEstimate / 100.0) : 0.0;
}

void DNAAnalyzer::AnalyzeMovement(const float* audio, int numSamples,
                                   MovementFeatures& out) {
  double env = 0.0;
  double attackCoef = std::exp(-1.0 / (0.01 * mSampleRate));
  double releaseCoef = std::exp(-1.0 / (0.1 * mSampleRate));
  std::vector<double> envelope(numSamples);
  for (int i = 0; i < numSamples; ++i) {
    double absIn = std::abs((double)audio[i]);
    double coef = (absIn > env) ? attackCoef : releaseCoef;
    env = coef * env + (1.0 - coef) * absIn;
    envelope[i] = env;
  }

  double zeroCrossings = 0;
  for (int i = 1; i < numSamples; ++i) {
    if (envelope[i] * envelope[i - 1] <= 0.0)
      zeroCrossings++;
  }
  double envFreq = zeroCrossings * mSampleRate / (2.0 * numSamples);
  out.modulationRate = envFreq;

  double envVar = 0.0, envMean = 0.0;
  for (int i = 0; i < numSamples; ++i) envMean += envelope[i];
  envMean /= numSamples;
  for (int i = 0; i < numSamples; ++i)
    envVar += (envelope[i] - envMean) * (envelope[i] - envMean);
  envVar = std::sqrt(envVar / numSamples);
  out.modulationDepth = (envMean > 0.0) ? std::min(2.0, envVar / envMean) : 0.0;

  out.tremoloAmount = out.modulationDepth * 0.5;
  out.vibratoAmount = out.modulationDepth * 0.2;
  out.wobbleRate = envFreq * 0.3;
}

double DNAAnalyzer::ComputeSpectralCentroid(const std::vector<double>& mag) {
  double num = 0.0, den = 0.0;
  int nyquist = (int)mag.size();
  for (int i = 0; i < nyquist; ++i) {
    double freq = i * mSampleRate / mFFT.GetFFTSize();
    num += freq * mag[i];
    den += mag[i];
  }
  return (den > 0.0) ? num / den : 0.0;
}

double DNAAnalyzer::ComputeSpectralSpread(const std::vector<double>& mag,
                                           double centroid) {
  double num = 0.0, den = 0.0;
  int nyquist = (int)mag.size();
  for (int i = 0; i < nyquist; ++i) {
    double freq = i * mSampleRate / mFFT.GetFFTSize();
    double diff = freq - centroid;
    num += diff * diff * mag[i];
    den += mag[i];
  }
  return (den > 0.0) ? std::sqrt(num / den) : 0.0;
}

double DNAAnalyzer::ComputeSpectralRolloff(const std::vector<double>& mag,
                                            double percent) {
  double totalEnergy = 0.0;
  for (auto& m : mag) totalEnergy += m;
  double target = totalEnergy * percent;
  double cumSum = 0.0;
  int nyquist = (int)mag.size();
  for (int i = 0; i < nyquist; ++i) {
    cumSum += mag[i];
    if (cumSum >= target)
      return i * mSampleRate / mFFT.GetFFTSize();
  }
  return mSampleRate * 0.5;
}

double DNAAnalyzer::ComputeSpectralFlatness(const std::vector<double>& mag) {
  double geomMean = 0.0, arithMean = 0.0;
  int count = 0;
  for (auto& m : mag) {
    if (m > 1e-10) {
      geomMean += std::log(m);
      arithMean += m;
      count++;
    }
  }
  if (count == 0) return 1.0;
  geomMean = std::exp(geomMean / count);
  arithMean /= count;
  return (arithMean > 0.0) ? geomMean / arithMean : 1.0;
}
