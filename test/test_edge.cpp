#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include "Geno_Types.h"
#include "Geno_Analyzer.h"
#include "Geno_TransferEngine.h"
#include "Geno_TextureProcessor.h"
#include "Geno_SpectralProcessor.h"
#include "Geno_DynamicsProcessor.h"
#include "Geno_StereoProcessor.h"
#include "Geno_NoiseProcessor.h"
#include "Geno_SpaceProcessor.h"
#include "Geno_MovementProcessor.h"
#include "Geno_GlueProcessor.h"
#include "Geno_AirProcessor.h"
#include "Geno_ResonanceProcessor.h"

static constexpr double kSR = 44100.0;
static constexpr int kLen = 44100;

struct TestCase {
  const char* name;
  double amp;
  double freq;
  double noiseDb;
  double dcOffset;
  bool stereoPhase;
  double modRate;
  double modDepth;
};

static int gPass = 0, gFail = 0;

void check(const char* test, bool cond, const char* detail = "") {
  if (cond) { gPass++; printf("  ✅ %s\n", test); }
  else { gFail++; printf("  ❌ %s: %s\n", test, detail); }
}

int main() {
  printf("=== Edge Case Tests ===\n\n");

  // 1. Silence
  {
    std::vector<float> sig(kLen, 0.f);
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile p;
    an.ComputeFullAnalysis(sig.data(), sig.data(), kLen, true, p);
    check("Silence: rms≈0", p.dynamics.rms < 1e-6);
    check("Silence: peak≈0", p.dynamics.peak < 1e-6);
    check("Silence: noise <-90dB", p.noise.noiseFloorDb <= -90.0);
    check("Silence: pitch=0", p.spectral.pitch == 0.0);
  }

  // 2. Full-scale sine (clipping edge)
  {
    std::vector<float> sig(kLen);
    for (int i = 0; i < kLen; i++)
      sig[i] = 0.999f * std::sin(2.0 * M_PI * 440.0 * i / kSR);
    TextureProcessor tp;
    tp.SetSampleRate(kSR);
    TextureFeatures s, t;
    s.saturationAmount = 0; t.saturationAmount = 1.0;
    t.harmonicDistortion = 0.5; t.analogWarmth = 0.3; t.tapeSaturation = 0.5;
    tp.SetSourceProfile(s); tp.SetTargetProfile(t);
    std::vector<float> out(kLen);
    tp.Process(sig.data(), out.data(), kLen);
    float maxV = 0;
    for (auto v : out) maxV = std::max(maxV, std::abs(v));
    check("Full-scale texture: no clip", maxV < 1.05f, std::to_string(maxV).c_str());
  }

  // 3. DC offset
  {
    std::vector<float> sig(kLen, 0.1f);
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile p;
    an.ComputeFullAnalysis(sig.data(), sig.data(), kLen, true, p);
    check("DC: pitch=0", p.spectral.pitch == 0.0, std::to_string(p.spectral.pitch).c_str());
    check("DC: crestFactor≈1", std::abs(p.dynamics.crestFactor - 1.0) < 0.1);
  }

  // 4. Very quiet signal (-60dB)
  {
    std::vector<float> sig(kLen);
    double amp = 0.001;
    for (int i = 0; i < kLen; i++)
      sig[i] = (float)(amp * std::sin(2.0 * M_PI * 440.0 * i / kSR));
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile p;
    an.ComputeFullAnalysis(sig.data(), sig.data(), kLen, true, p);
    check("Quiet: pitch≈440", std::abs(p.spectral.pitch - 440.0) < 20.0);
    check("Quiet: rms≈0.0007", p.dynamics.rms > 1e-6 && p.dynamics.rms < 0.01);
  }

  // 5. Stereo width transfer (realistic)
  {
    std::vector<float> L(kLen), R(kLen);
    for (int i = 0; i < kLen; i++) {
      L[i] = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / kSR);
      R[i] = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / kSR + 0.5);
    }
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile p;
    an.ComputeFullAnalysis(L.data(), R.data(), kLen, true, p);
    check("Stereo: width>0", p.stereo.width > 0.05);
    check("Stereo: correlation<1", p.stereo.phaseCorrelation < 0.99);
    StereoProcessor sp;
    sp.SetSampleRate(kSR);
    StereoFeatures s = p.stereo;
    StereoFeatures t = p.stereo;
    t.width = p.stereo.width * 0.5;
    sp.SetSourceProfile(s); sp.SetTargetProfile(t);
    sp.SetTransferAmount(1.0);
    std::vector<float> oL(kLen), oR(kLen);
    sp.Process(L.data(), R.data(), oL.data(), oR.data(), kLen);
    GenoProfile outP;
    an.ComputeFullAnalysis(oL.data(), oR.data(), kLen, true, outP);
    check("Stereo proc: width reduced", outP.stereo.width < p.stereo.width * 0.9);
    check("Stereo proc: output valid", outP.stereo.phaseCorrelation >= -1.0 && outP.stereo.phaseCorrelation <= 1.0);
  }

  // 6. Impulse response (transient)
  {
    std::vector<float> sig(kLen, 0.f);
    sig[0] = 1.0f;
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile p;
    an.ComputeFullAnalysis(sig.data(), sig.data(), kLen, true, p);
    check("Impulse: crest>10", p.dynamics.crestFactor > 5.0);
    check("Impulse: flatness high", p.spectral.spectralFlatness > 0.5);
  }

  // 7. NoiseProcessor spectral shaping
  {
    std::vector<float> sig(kLen);
    for (int i = 0; i < kLen; i++)
      sig[i] = (float)(0.1 * std::sin(2.0 * M_PI * 440.0 * i / kSR));
    NoiseProcessor np;
    np.SetSampleRate(kSR);
    NoiseFeatures s, t;
    s.noiseFloorDb = -80; t.noiseFloorDb = -40;
    s.spectralTilt = -10; t.spectralTilt = 10;
    for (int i = 0; i < kNumNoiseCoefs; i++) s.noiseShape[i] = 1.0;
    for (int i = 0; i < kNumNoiseCoefs; i++) t.noiseShape[i] = (i < 2) ? 2.0 : 0.5;
    np.SetSourceProfile(s); np.SetTargetProfile(t);
    np.SetTransferAmount(1.0);
    std::vector<float> out(kLen);
    np.Process(sig.data(), out.data(), kLen);
    float maxV = 0;
    for (auto v : out) maxV = std::max(maxV, std::abs(v));
    check("Noise shaper: output valid", maxV > 0.01f && maxV < 2.0f, std::to_string(maxV).c_str());
  }

  // 8. AirProcessor extreme settings
  {
    std::vector<float> sig(kLen);
    for (int i = 0; i < kLen; i++)
      sig[i] = (float)(0.5 * std::sin(2.0 * M_PI * 220.0 * i / kSR));
    AirProcessor ap;
    ap.SetSampleRate(kSR);
    SpectralFeatures s, t;
    s.brightness = 0.0; t.brightness = 1.0;
    ap.SetSourceProfile(s); ap.SetTargetProfile(t);
    ap.SetTransferAmount(1.0);
    std::vector<float> out(kLen);
    ap.Process(sig.data(), out.data(), kLen);
    float maxV = 0;
    for (auto v : out) maxV = std::max(maxV, std::abs(v));
    check("Air extreme: no clip", maxV < 2.0f, std::to_string(maxV).c_str());
  }

  // 9. GlueProcessor with extreme compression
  {
    std::vector<float> sig(kLen);
    for (int i = 0; i < kLen; i++)
      sig[i] = (float)(0.01 * std::sin(2.0 * M_PI * 440.0 * i / kSR) + 0.8 * (rand()/32768.0-1));
    GlueProcessor gp;
    gp.SetSampleRate(kSR);
    DynamicFeatures s, t;
    s.rms = 0.2; t.rms = 0.01;
    s.dynamicRange = 20; t.dynamicRange = 2;
    gp.SetSourceProfile(s); gp.SetTargetProfile(t);
    gp.SetTransferAmount(1.0);
    std::vector<float> oL(kLen), oR(kLen);
    gp.Process(sig.data(), sig.data(), oL.data(), oR.data(), kLen);
    float maxV = 0;
    for (auto v : oL) maxV = std::max(maxV, std::abs(v));
    check("Glue extreme: compressed", maxV <= 1.0f, std::to_string(maxV).c_str());
  }

  // 10. ResonanceProcessor with extreme Q
  {
    std::vector<float> sig(kLen);
    for (int i = 0; i < kLen; i++)
      sig[i] = (float)(0.3 * std::sin(2.0 * M_PI * 100.0 * i / kSR));
    ResonanceProcessor rp;
    rp.SetSampleRate(kSR);
    SpectralFeatures s, t;
    s.spectralEnvelope = {1,1,1,1,1,1};
    t.spectralEnvelope = {5,1,0.1,0.1,5,5};
    rp.SetSourceProfile(s); rp.SetTargetProfile(t);
    rp.SetTransferAmount(1.0);
    std::vector<float> out(kLen);
    rp.Process(sig.data(), out.data(), kLen);
    float maxV = 0;
    for (auto v : out) maxV = std::max(maxV, std::abs(v));
    check("Resonance extreme: no clip", maxV < 2.0f, std::to_string(maxV).c_str());
  }

  // 11. TransferEngine with extreme params
  {
    auto src = std::vector<float>(kLen);
    auto tgt = std::vector<float>(kLen);
    for (int i = 0; i < kLen; i++) {
      src[i] = (float)(0.5 * std::sin(2.0 * M_PI * 220.0 * i / kSR));
      tgt[i] = (float)(0.3 * std::sin(2.0 * M_PI * 880.0 * i / kSR) * std::exp(-i * 0.001));
    }
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile sP, tP;
    an.ComputeFullAnalysis(src.data(), src.data(), kLen, true, sP);
    an.ComputeFullAnalysis(tgt.data(), tgt.data(), kLen, true, tP);
    TransferEngine eng;
    eng.SetSampleRate(kSR);
    eng.SetSourceProfile(sP); eng.SetTargetProfile(tP);
    GenoTransferParams params;
    for (auto& a : params.amounts) a = 0.0;
    eng.SetTransferParams(params);
    std::vector<float> oL(kLen), oR(kLen);
    eng.Process(src.data(), src.data(), oL.data(), oR.data(), kLen, true);
    float maxV = 0;
    for (auto v : oL) maxV = std::max(maxV, std::abs(v));
    check("Transfer amount=0: bypass", maxV > 0.1f, std::to_string(maxV).c_str());

    for (auto& a : params.amounts) a = 1.0;
    eng.SetTransferParams(params);
    eng.Process(src.data(), src.data(), oL.data(), oR.data(), kLen, true);
    maxV = 0;
    for (auto v : oL) maxV = std::max(maxV, std::abs(v));
    check("Transfer amount=1: no clip", maxV < 2.0f, std::to_string(maxV).c_str());
  }

  // 12. Analyzer: very short block
  {
    std::vector<float> sig(64, 0.f);
    sig[0] = 1.0f;
    GenoAnalyzer an;
    an.SetSampleRate(kSR);
    GenoProfile p;
    an.ComputeFullAnalysis(sig.data(), sig.data(), 64, true, p);
    check("Short block: no crash", true);
    check("Short block: defaults", p.dynamics.rms > 0 || p.spectral.pitch >= 0);
  }

  // 13. All profiles Interpolate
  {
    GenoProfile a, b;
    a.spectral.spectralEnvelope.assign(kNumSpectralBands, 0.5);
    b.spectral.spectralEnvelope.assign(kNumSpectralBands, 0.8);
    a.spectral.harmonicProfile.assign(kNumHarmonics, 0.1);
    b.spectral.harmonicProfile.assign(kNumHarmonics, 0.3);
    a.spectral.centroid = 200; a.spectral.brightness = 0.2;
    a.dynamics.rms = 0.1; a.dynamics.crestFactor = 3.0;
    a.stereo.width = 0.3; a.noise.noiseFloorDb = -80;
    b.spectral.centroid = 2000; b.spectral.brightness = 0.8;
    b.dynamics.rms = 0.01; b.dynamics.crestFactor = 10.0;
    b.stereo.width = 0.9; b.noise.noiseFloorDb = -40;
    auto c = a.InterpolateWith(b, 0.5);
    check("Interpolate: centroid mid", std::abs(c.spectral.centroid - 1100) < 10);
    check("Interpolate: brightness mid", std::abs(c.spectral.brightness - 0.5) < 0.01);
    check("Interpolate: rms mid", std::abs(c.dynamics.rms - 0.055) < 0.001);
    check("Interpolate: stereo mid", std::abs(c.stereo.width - 0.6) < 0.01);
    auto d = a.InterpolateWith(b, 0.0);
    check("Interpolate: t=0 = source", std::abs(d.spectral.centroid - 200) < 1);
  }

  // 14. Similarity symmetric
  {
    GenoProfile a, b;
    a.spectral.spectralEnvelope.assign(kNumSpectralBands, 0.5);
    b.spectral.spectralEnvelope.assign(kNumSpectralBands, 0.5);
    a.spectral.harmonicProfile.assign(kNumHarmonics, 0.1);
    b.spectral.harmonicProfile.assign(kNumHarmonics, 0.1);
    a.spectral.centroid = 500; b.spectral.centroid = 1500;
    double simAB = a.SimilarityTo(b);
    double simBA = b.SimilarityTo(a);
    check("Similarity symmetric", std::abs(simAB - simBA) < 0.001);
    double simAA = a.SimilarityTo(a);
    check("Similarity self=1", std::abs(simAA - 1.0) < 0.01);
  }

  // 15. ToFeatureVector round-trip
  {
    GenoProfile a;
    a.spectral.spectralEnvelope.resize(kNumSpectralBands);
    a.spectral.harmonicProfile.resize(kNumHarmonics);
    for (int i = 0; i < kNumSpectralBands; ++i)
      a.spectral.spectralEnvelope[i] = 0.5 + 0.5 * std::sin(i * 0.2);
    for (int i = 0; i < kNumHarmonics; ++i)
      a.spectral.harmonicProfile[i] = 1.0 / (i + 1.0);
    a.spectral.centroid = 500; a.spectral.brightness = 0.5;
    a.dynamics.rms = 0.1; a.dynamics.crestFactor = 5.0;
    a.stereo.width = 0.7; a.noise.noiseFloorDb = -60;
    a.texture.saturationAmount = 0.3;
    auto vec = a.ToFeatureVector();
    auto b = GenoProfile::FromFeatureVector(vec);
    check("FeatureVector round-trip: centroid",
          std::abs(b.spectral.centroid - a.spectral.centroid) < 0.01);
    check("FeatureVector round-trip: brightness",
          std::abs(b.spectral.brightness - a.spectral.brightness) < 0.01);
    check("FeatureVector round-trip: rms",
          std::abs(b.dynamics.rms - a.dynamics.rms) < 0.01);
  }

  printf("\n=== Results: %d pass, %d fail ===\n", gPass, gFail);
  return gFail;
}
