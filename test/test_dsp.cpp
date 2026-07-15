#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include <sndfile.h>

#include "SDNA_Types.h"
#include "SDNA_FFT.h"
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
#include "SDNA_TransferEngine.h"
#include "SDNA_MorphEngine.h"

static constexpr double kSR = 44100.0;
static constexpr int kLen = (int)(kSR * 3);

struct TestTone {
  std::vector<float> L, R;
  std::string name;
};

TestTone genSine(double freq, double amp = 0.5) {
  TestTone t;
  t.L.resize(kLen); t.R.resize(kLen);
  t.name = "sine_" + std::to_string((int)freq) + "Hz";
  for (int i = 0; i < kLen; i++) {
    double x = amp * std::sin(2.0 * M_PI * freq * i / kSR);
    t.L[i] = (float)x; t.R[i] = (float)x;
  }
  return t;
}

TestTone genComplex(double amp = 0.4) {
  TestTone t;
  t.L.resize(kLen); t.R.resize(kLen);
  t.name = "complex";
  for (int i = 0; i < kLen; i++) {
    double e = std::exp(-i / kSR * 0.5);
    double x = e * amp * (
      std::sin(2.0 * M_PI * 220.0 * i / kSR) * 0.6 +
      std::sin(2.0 * M_PI * 440.0 * i / kSR) * 0.3 +
      std::sin(2.0 * M_PI * 660.0 * i / kSR) * 0.15 +
      std::sin(2.0 * M_PI * 880.0 * i / kSR) * 0.07 +
      std::sin(2.0 * M_PI * 1320.0 * i / kSR) * 0.03);
    t.L[i] = (float)x; t.R[i] = (float)(x * 0.8);
  }
  return t;
}

TestTone genPercussive() {
  TestTone t;
  t.L.resize(kLen); t.R.resize(kLen);
  t.name = "percussive";
  for (int i = 0; i < kLen; i++) {
    int beat = (i / 11025) * 11025;
    double hit = (i - beat < 200) ? std::exp(-(i - beat) / 2000.0) : 0.0;
    double x = hit * 0.8 * std::sin(2.0 * M_PI * 1200.0 * i / kSR);
    t.L[i] = (float)x; t.R[i] = (float)(x * 0.7);
  }
  return t;
}

TestTone genNoiseFloor() {
  TestTone t;
  t.L.resize(kLen); t.R.resize(kLen);
  t.name = "noise_floor";
  uint32_t rng = 0xDEADBEEF;
  for (int i = 0; i < kLen; i++) {
    rng = rng * 1103515245 + 12345;
    double n = (double)(int32_t)(rng & 0x7FFFFFFF) / 0x40000000 - 1.0;
    double s = 0.3 * std::sin(2.0 * M_PI * 440.0 * i / kSR);
    t.L[i] = (float)(s + n * 0.05); t.R[i] = (float)(s + n * 0.05);
  }
  return t;
}

TestTone genWideStereo() {
  TestTone t;
  t.L.resize(kLen); t.R.resize(kLen);
  t.name = "wide_stereo";
  for (int i = 0; i < kLen; i++) {
    t.L[i] = (float)(0.4 * std::sin(2.0 * M_PI * 440.0 * i / kSR));
    t.R[i] = (float)(0.4 * std::sin(2.0 * M_PI * 440.0 * i / kSR + 0.8));
  }
  return t;
}

bool saveWav(const char* path, const std::vector<float>& L,
             const std::vector<float>& R) {
  SF_INFO info;
  memset(&info, 0, sizeof(info));
  info.samplerate = (int)kSR;
  info.channels = 2;
  info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  SNDFILE* f = sf_open(path, SFM_WRITE, &info);
  if (!f) { fprintf(stderr, "Failed to open %s\n", path); return false; }
  std::vector<short> buf(L.size() * 2);
  for (size_t i = 0; i < L.size(); i++) {
    buf[i*2]   = (short)std::clamp(L[i] * 32767.0, -32768.0, 32767.0);
    buf[i*2+1] = (short)std::clamp(R[i] * 32767.0, -32768.0, 32767.0);
  }
  sf_writef_short(f, buf.data(), L.size());
  sf_close(f);
  printf("  Saved %s\n", path);
  return true;
}

void printProfile(const DNAProfile& p, const char* label) {
  printf("\n=== %s ===\n", label);
  printf("  Pitch: %.1f Hz (conf: %.2f)\n", p.spectral.pitch, p.spectral.pitchConfidence);
  printf("  Centroid: %.1f Hz | Brightness: %.3f\n", p.spectral.centroid, p.spectral.brightness);
  printf("  Flatness: %.4f | Rolloff: %.1f\n", p.spectral.spectralFlatness, p.spectral.rolloff);
  printf("  RMS: %.5f | Peak: %.5f | Crest: %.2f\n", p.dynamics.rms, p.dynamics.peak, p.dynamics.crestFactor);
  printf("  DynamicRange: %.1f dB | Attack: %.2f ms | Release: %.2f ms\n",
         p.dynamics.dynamicRange, p.dynamics.attackMs, p.dynamics.releaseMs);
  printf("  Stereo Width: %.3f | Correlation: %.3f\n", p.stereo.width, p.stereo.phaseCorrelation);
  printf("  Noise Floor: %.1f dB | SNR: %.1f dB\n", p.noise.noiseFloorDb, p.noise.signalToNoise);
  printf("  Saturation: %.3f | Distortion: %.3f | Warmth: %.3f\n",
         p.texture.saturationAmount, p.texture.harmonicDistortion, p.texture.analogWarmth);
  printf("  Space decay: %.1f ms | Room: %.2f\n", p.space.decayTime, p.space.roomSize);
  printf("  Mod rate: %.2f Hz | Depth: %.2f\n", p.movement.modulationRate, p.movement.modulationDepth);
}

int testFFT() {
  printf("=== FFT ===\n");
  FFTProcessor fft(2048, 512);
  fft.SetSampleRate(kSR);
  std::vector<float> buf(512, 0.0f);
  for (int i = 0; i < 512; i++)
    buf[i] = (float)std::sin(2.0 * M_PI * 440.0 * i / kSR);
  fft.ProcessBlock(buf.data(), 512);
  std::vector<double> mag, phase;
  fft.GetMagnitudeSpectrum(mag);
  fft.GetPhaseSpectrum(phase);
  int peak = 0;
  double maxM = 0;
  for (size_t i = 0; i < mag.size(); i++)
    if (mag[i] > maxM) { maxM = mag[i]; peak = i; }
  double freq = peak * kSR / fft.GetFFTSize();
  printf("  Peak bin=%d freq=%.1fHz (expect ~440) %s\n",
         peak, freq, std::abs(freq - 440) < 20 ? "OK" : "FAIL");

  std::vector<double> filt(mag.size(), 1.0);
  for (size_t i = mag.size()/2; i < mag.size(); i++) filt[i] = 0.1;
  fft.ApplySpectralFilter(filt, mag);
  fft.GetMagnitudeSpectrum(mag);
  peak = 0; maxM = 0;
  for (size_t i = 0; i < mag.size(); i++)
    if (mag[i] > maxM) { maxM = mag[i]; peak = i; }
  printf("  Filter test: peak bin=%d %s\n", peak,
         peak <= (int)mag.size()/2+2 ? "OK" : "FAIL");

  std::vector<double> env;
  fft.MagnitudeToEnvelope(mag, env, 16);
  printf("  Envelope bands: %zu %s\n", env.size(), env.size()==16?"OK":"FAIL");
  return 0;
}

int testAnalyzer() {
  printf("=== Analyzer ===\n");
  TestTone tones[] = {genSine(220), genSine(440), genComplex(),
                      genPercussive(), genNoiseFloor(), genWideStereo()};
  for (auto& t : tones) {
    printf("--- %s ---\n", t.name.c_str());
    DNAAnalyzer an;
    an.SetSampleRate(kSR);
    DNAProfile p;
    an.ComputeFullAnalysis(t.L.data(), t.R.data(), kLen, true, p);
    printProfile(p, t.name.c_str());
  }
  return 0;
}

int testProcessors() {
  printf("=== Individual Processors ===\n");
  auto tone = genSine(440);
  int pass = 0, fail = 0;

  auto run = [&](const char* name, auto& proc) {
    std::vector<float> out(kLen);
    proc.Process(tone.L.data(), out.data(), kLen);
    float maxV = 0;
    for (auto v : out) maxV = std::max(maxV, std::abs(v));
    bool ok = maxV > 0.001f && maxV < 2.0f;
    printf("  %s: max=%.4f %s\n", name, maxV, ok ? "OK" : "FAIL");
    (ok ? pass : fail)++;
  };

  {
    SpectralFeatures s, t;
    s.spectralEnvelope.resize(kNumSpectralBands, 1.0);
    t.spectralEnvelope.resize(kNumSpectralBands, 2.0);
    s.brightness = 0.3; t.brightness = 0.7;
    SpectralProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    p.SetTransferAmount(1.0);
    run("SpectralProcessor", p);
  }
  {
    DynamicFeatures s, t;
    s.dynamicRange = 10; t.dynamicRange = 20;
    DynamicsProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    run("DynamicsProcessor", p);
  }
  {
    NoiseFeatures s, t;
    s.noiseFloorDb = -80; t.noiseFloorDb = -40;
    NoiseProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    run("NoiseProcessor", p);
  }
  {
    TextureFeatures s, t;
    s.saturationAmount = 0.1; t.saturationAmount = 0.8;
    t.analogWarmth = 0.5; t.tapeSaturation = 0.5; t.harmonicDistortion = 0.3;
    TextureProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    run("TextureProcessor", p);
  }
  {
    SpectralFeatures s, t;
    s.brightness = 0.2; t.brightness = 0.8;
    AirProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    run("AirProcessor", p);
  }
  {
    SpectralFeatures s, t;
    t.spectralEnvelope = {1.0, 1.5, 2.0, 1.5, 1.0, 0.5};
    s.spectralEnvelope = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    ResonanceProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    p.SetTransferAmount(1.0);
    run("ResonanceProcessor", p);
  }
  // Stereo-only processors
  {
    MovementFeatures s, t;
    s.modulationRate = 0; t.modulationRate = 4;
    t.modulationDepth = 0.8; t.tremoloAmount = 0.5;
    MovementProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    std::vector<float> oL(kLen), oR(kLen);
    p.Process(tone.L.data(), tone.R.data(), oL.data(), oR.data(), kLen);
    float maxV = 0;
    for (int i = 0; i < kLen; i++) maxV = std::max({maxV, std::abs(oL[i]), std::abs(oR[i])});
    printf("  MovementProcessor: max=%.4f %s\n", maxV, (maxV > 0.001f && maxV < 2.0f) ? "OK" : "FAIL");
  }
  {
    SpaceFeatures s, t;
    t.decayTime = 500; t.roomSize = 0.6;
    SpaceProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    std::vector<float> oL(kLen), oR(kLen);
    p.Process(tone.L.data(), tone.R.data(), oL.data(), oR.data(), kLen);
    float maxV = 0;
    for (int i = 0; i < kLen; i++) maxV = std::max({maxV, std::abs(oL[i]), std::abs(oR[i])});
    printf("  SpaceProcessor: max=%.4f %s\n", maxV, (maxV > 0.001f && maxV < 2.0f) ? "OK" : "FAIL");
  }
  {
    DynamicFeatures s, t;
    s.rms = 0.01; t.rms = 0.05;
    s.dynamicRange = 15; t.dynamicRange = 5;
    GlueProcessor p;
    p.SetSampleRate(kSR); p.SetSourceProfile(s); p.SetTargetProfile(t);
    std::vector<float> oL(kLen), oR(kLen);
    p.Process(tone.L.data(), tone.R.data(), oL.data(), oR.data(), kLen);
    float maxV = 0;
    for (int i = 0; i < kLen; i++) maxV = std::max({maxV, std::abs(oL[i]), std::abs(oR[i])});
    printf("  GlueProcessor: max=%.4f %s\n", maxV, (maxV > 0.001f && maxV < 2.0f) ? "OK" : "FAIL");
  }
  printf("  -> %d pass, %d fail\n", pass, fail);
  return fail;
}

int testTransfer() {
  printf("=== Transfer ===\n");
  auto src = genComplex();
  auto tgt = genPercussive();
  DNAAnalyzer an;
  an.SetSampleRate(kSR);
  DNAProfile sP, tP;
  an.ComputeFullAnalysis(src.L.data(), src.R.data(), kLen, true, sP);
  an.ComputeFullAnalysis(tgt.L.data(), tgt.R.data(), kLen, true, tP);
  printProfile(sP, "SOURCE");
  printProfile(tP, "TARGET");

  TransferEngine eng;
  eng.SetSampleRate(kSR);
  eng.SetSourceProfile(sP);
  eng.SetTargetProfile(tP);
  DNATransferParams params;
  params.amounts.fill(0.8);
  eng.SetTransferParams(params);
  std::vector<float> oL(kLen), oR(kLen);
  eng.Process(src.L.data(), src.R.data(), oL.data(), oR.data(), kLen, true);
  saveWav("/tmp/sounddna_src.wav", src.L, src.R);
  saveWav("/tmp/sounddna_tgt.wav", tgt.L, tgt.R);
  saveWav("/tmp/sounddna_out.wav", oL, oR);
  DNAProfile oP;
  an.ComputeFullAnalysis(oL.data(), oR.data(), kLen, true, oP);
  printProfile(oP, "OUTPUT");
  return 0;
}

int testMorph() {
  printf("=== Morph ===\n");
  auto a = genSine(220);
  auto b = genSine(880);
  DNAAnalyzer an;
  an.SetSampleRate(kSR);
  DNAProfile pA, pB;
  an.ComputeFullAnalysis(a.L.data(), a.R.data(), kLen, true, pA);
  an.ComputeFullAnalysis(b.L.data(), b.R.data(), kLen, true, pB);
  MorphEngine morph;
  morph.AddPoint(pA, 0.0);
  morph.AddPoint(pB, 1.0);
  for (double pos : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    morph.SetMorphPosition(pos);
    auto m = morph.GetCurrentMorph();
    bool ok = (m.spectral.pitch > 0 || m.spectral.centroid > 0);
    printf("  pos=%.2f pitch=%.1f centroid=%.1f %s\n",
           pos, m.spectral.pitch, m.spectral.centroid, ok ? "OK" : "FAIL");
  }
  return 0;
}

int main() {
  int f = 0;
  f += testFFT();
  f += testAnalyzer();
  f += testProcessors();
  f += testTransfer();
  f += testMorph();
  printf("\nTotal failures: %d\n", f);
  return f;
}
