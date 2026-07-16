#include "Geno_Analyzer.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>

GenoAnalyzer::GenoAnalyzer()
  : mFFT(kFFTSize, kFFTHop)
{
  mRMSBuffer.resize(kAnalysisWindow, 0.0);
  mPeakBuffer.resize(kAnalysisWindow, 0.0);
}

void GenoAnalyzer::Reset() {
  mFFT.Reset();
  std::fill(mRMSBuffer.begin(), mRMSBuffer.end(), 0.0);
  std::fill(mPeakBuffer.begin(), mPeakBuffer.end(), 0.0);
}

void GenoAnalyzer::SetSampleRate(double sr) {
  mSampleRate = sr;
  mFFT.SetSampleRate(sr);
}

void GenoAnalyzer::AnalyzeBlock(const float* inputL, const float* /*inputR*/,
                                int numSamples, bool /*isStereo*/) {
  mFFT.ProcessBlock(inputL, numSamples);
}

void GenoAnalyzer::ComputeFullAnalysis(const float* audioL, const float* audioR,
                                       int numSamples, bool isStereo,
                                       GenoProfile& out) {
  if (numSamples < kMinAnalysisSamples) return;
  double rms = 0.0;
  for (int i = 0; i < numSamples; ++i) rms += (double)audioL[i] * audioL[i];
  rms = std::sqrt(rms / numSamples);

  AnalyzeSpectral(audioL, numSamples, out.spectral);
  AnalyzeDynamics(audioL, numSamples, out.dynamics);
  if (isStereo) AnalyzeStereo(audioL, audioR, numSamples, out.stereo);
  AnalyzeNoise(audioL, numSamples, out.noise);
  AnalyzeTexture(audioL, numSamples, rms, out.spectral.pitch, out.texture);
  AnalyzeSpace(audioL, numSamples, out.space);
  AnalyzeMovement(audioL, numSamples, out.movement);
}

void GenoAnalyzer::AnalyzeSpectral(const float* audio, int numSamples,
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

  out.pitch = ComputePitchAutocorrelation(audio, numSamples);

  double fundamentalBin = 0.0;
  if (out.pitch > 0.0) {
    fundamentalBin = out.pitch * mFFT.GetFFTSize() / mSampleRate;
  } else {
    int maxIdx = (int)(std::max_element(accumMag.begin() + 1, accumMag.end()) - accumMag.begin());
    if (maxIdx < (int)accumMag.size()) fundamentalBin = (double)maxIdx;
  }

  mFFT.MagnitudeToEnvelope(accumMag, out.spectralEnvelope, kNumSpectralBands);
  AnalyzeHarmonics(accumMag, fundamentalBin, out.harmonicProfile);
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

  out.pitchConfidence = (out.pitch > 0.0) ?
    std::min(1.0, (1.0 - out.spectralFlatness) * 1.5) : 0.0;
}

double GenoAnalyzer::ComputePitchAutocorrelation(const float* audio, int numSamples) {
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

void GenoAnalyzer::AnalyzeHarmonics(const std::vector<double>& mag,
                                    double fundamentalBin,
                                    std::vector<double>& harmonicProfile) {
  harmonicProfile.resize(kNumHarmonics, 0.0);
  if (mag.empty() || fundamentalBin < 1.0) return;
  int numBins = (int)mag.size();

  for (int h = 0; h < kNumHarmonics; ++h) {
    int binIdx = (int)std::round(fundamentalBin * (h + 1));
    if (binIdx < numBins) harmonicProfile[h] = mag[binIdx];
  }

  double maxH = *std::max_element(harmonicProfile.begin(), harmonicProfile.end());
  if (maxH > 0.0)
    for (auto& v : harmonicProfile) v /= maxH;
}

void GenoAnalyzer::AnalyzeDynamics(const float* audio, int numSamples,
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
  double envMean = 0.0;
  for (int i = 0; i < numSamples; ++i) envMean += envelope[i];
  out.envelopeMean = envMean / numSamples;

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

  // Estimate compression ratio from envelope dB range compression
  // Ratio = (peak_dB - noiseFloor_dB) / (peak_dB - avgEnvelope_dB)
  // Higher ratio = more compressed (envelope stays near peak)
  double envDb = (out.envelopeMean > 1e-10) ? 20.0 * std::log10(out.envelopeMean) : -60.0;
  double peakDb = (peakVal > 1e-10) ? 20.0 * std::log10(peakVal) : -60.0;
  double noiseDb = -60.0;
  if (peakDb - noiseDb > 6.0) {
    double inputRange = peakDb - noiseDb;
    double outputRange = peakDb - envDb;
    out.compressionRatio = (outputRange > 1.0) ?
      std::clamp(inputRange / outputRange, 1.0, 20.0) : 20.0;
  } else {
    out.compressionRatio = 1.0;
  }
}

void GenoAnalyzer::AnalyzeStereo(const float* audioL, const float* audioR,
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

  // Phase drift: average inter-channel phase difference from FFT cross-spectrum
  out.phaseDrift = 0.0;
  int numFrames = 0;
  for (int f = 0; f * kFFTHop + kFFTSize <= numSamples; ++f) {
    int offset = f * kFFTHop;
    mFFT.ProcessBlock(audioL + offset, kFFTHop);
    std::vector<double> magL, phaseL;
    mFFT.GetMagnitudeSpectrum(magL);
    mFFT.GetPhaseSpectrum(phaseL);

    mFFT.ProcessBlock(audioR + offset, kFFTHop);
    std::vector<double> magR, phaseR;
    mFFT.GetMagnitudeSpectrum(magR);
    mFFT.GetPhaseSpectrum(phaseR);

    double weightedSum = 0.0, weightSum = 0.0;
    for (int i = 1; i < std::min({(int)magL.size(), (int)magR.size(), (int)phaseL.size(), (int)phaseR.size()}); ++i) {
      double w = magL[i] + magR[i];
      double phaseDiff = std::abs(phaseL[i] - phaseR[i]);
      if (phaseDiff > M_PI) phaseDiff = 2.0 * M_PI - phaseDiff;
      weightedSum += w * phaseDiff;
      weightSum += w;
    }
    if (weightSum > 0.0)
      out.phaseDrift += weightedSum / weightSum;
    numFrames++;
  }
  if (numFrames > 0) out.phaseDrift /= numFrames;
}

void GenoAnalyzer::AnalyzeNoise(const float* audio, int numSamples,
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

void GenoAnalyzer::AnalyzeTexture(const float* audio, int numSamples,
                                  double rms, double pitch,
                                  TextureFeatures& out) {
  double oddHarmonics = 0.0, evenHarmonics = 0.0;
  std::vector<double> mag;
  int numFrames = std::max(1, numSamples / kFFTHop);
  double fundFreq = (pitch > 0.0) ? pitch : 200.0;
  for (int f = 0; f < numFrames; ++f) {
    int offset = f * kFFTHop;
    if (numSamples - offset < kFFTSize) break;
    mFFT.ProcessBlock(audio + offset, kFFTHop);
    mFFT.GetMagnitudeSpectrum(mag);
    for (int h = 1; h <= 16; ++h) {
      int bin = (int)(h * fundFreq * mFFT.GetFFTSize() / mSampleRate);
      if (bin < (int)mag.size() && bin > 0) {
        if (h % 2 == 0) evenHarmonics += mag[bin];
        else oddHarmonics += mag[bin];
      }
    }
  }
  double total = oddHarmonics + evenHarmonics;
  out.harmonicDistortion = (total > 0.0) ? evenHarmonics / total : 0.0;
  out.saturationAmount = (total > 0.0) ? oddHarmonics / total : 0.0;

  // Adaptive transient threshold relative to RMS
  double transientThreshold = std::max(0.01, rms * 2.0);
  double transientEnergy = 0.0, steadyEnergy = 0.0;
  for (int i = 1; i < numSamples; ++i) {
    double diff = std::abs((double)audio[i] - audio[i - 1]);
    if (diff > transientThreshold) transientEnergy += diff * diff;
    else steadyEnergy += (double)audio[i] * audio[i];
  }
  out.transientShape = (transientEnergy + steadyEnergy > 0.0) ?
    transientEnergy / (transientEnergy + steadyEnergy) : 0.0;
}

void GenoAnalyzer::AnalyzeSpace(const float* audio, int numSamples,
                                SpaceFeatures& out) {
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

  // RT60 from linear regression on log envelope after peak
  if (peakVal > 0.001 && peakIdx < numSamples - 10) {
    std::vector<double> logEnv;
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
    int n = 0;
    double noiseFloor = peakVal * 0.01;
    for (int i = peakIdx; i < numSamples && envelope[i] > noiseFloor; ++i, ++n) {
      double x = (double)(i - peakIdx) / mSampleRate;
      double y = std::log(envelope[i] / peakVal + 1e-30);
      sumX += x; sumY += y; sumXY += x * y; sumX2 += x * x;
    }
    if (n > 5) {
      double denom = n * sumX2 - sumX * sumX;
      if (std::abs(denom) < 1e-30) denom = 1e-30;
      double slope = (n * sumXY - sumX * sumY) / denom;
      if (slope < 0.0) {
        out.decayTime = std::clamp(-6.9078 / slope * 1000.0, 0.0, 10000.0);
      } else {
        out.decayTime = 0.0;
      }
    } else {
      out.decayTime = 0.0;
    }
  } else {
    out.decayTime = 0.0;
  }

  out.earlyReflections = (peakVal > 0.001 && numSamples > 100) ?
    std::min(1.0, (double)peakIdx / (numSamples * 0.2)) : 0.0;
}

void GenoAnalyzer::AnalyzeMovement(const float* audio, int numSamples,
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

  double envMean = 0.0;
  for (int i = 0; i < numSamples; ++i) envMean += envelope[i];
  envMean /= numSamples;

  // Zero-crossings of mean-subtracted envelope → actual modulation rate
  double zeroCrossings = 0;
  for (int i = 1; i < numSamples; ++i) {
    double e0 = envelope[i] - envMean;
    double e1 = envelope[i - 1] - envMean;
    if (e0 * e1 <= 0.0 && (e0 != 0.0 || e1 != 0.0))
      zeroCrossings++;
  }
  double envFreq = zeroCrossings * mSampleRate / (2.0 * numSamples);
  out.modulationRate = envFreq;

  double envVar = 0.0;
  for (int i = 0; i < numSamples; ++i)
    envVar += (envelope[i] - envMean) * (envelope[i] - envMean);
  envVar = std::sqrt(envVar / numSamples);
  out.modulationDepth = (envMean > 0.0) ? std::min(2.0, envVar / envMean) : 0.0;

  out.tremoloAmount = (envFreq < 10.0) ? out.modulationDepth : 0.0;
  out.vibratoAmount = (envFreq >= 10.0) ? out.modulationDepth : 0.0;
  out.wobbleRate = envFreq * 0.3;
}

double GenoAnalyzer::ComputeSpectralCentroid(const std::vector<double>& mag) {
  double num = 0.0, den = 0.0;
  int nyquist = (int)mag.size();
  for (int i = 0; i < nyquist; ++i) {
    double freq = i * mSampleRate / mFFT.GetFFTSize();
    num += freq * mag[i];
    den += mag[i];
  }
  return (den > 0.0) ? num / den : 0.0;
}

double GenoAnalyzer::ComputeSpectralSpread(const std::vector<double>& mag,
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

double GenoAnalyzer::ComputeSpectralRolloff(const std::vector<double>& mag,
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

double GenoAnalyzer::ComputeSpectralFlatness(const std::vector<double>& mag) {
  double geomMean = 0.0, arithMean = 0.0;
  int count = 0;
  for (auto& m : mag) {
    double p = m * m;
    if (p > 1e-20) {
      geomMean += std::log(p);
      arithMean += p;
      count++;
    }
  }
  if (count == 0) return 1.0;
  geomMean = std::exp(geomMean / count);
  arithMean /= count;
  return (arithMean > 0.0) ? geomMean / arithMean : 1.0;
}
