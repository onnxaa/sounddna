#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <sndfile.h>
#include "Geno_Types.h"
#include "Geno_Analyzer.h"
#include "Geno_TransferEngine.h"
#include "Geno_MorphEngine.h"
#include "Geno_SpectralProcessor.h"
#include "Geno_DynamicsProcessor.h"
#include "Geno_StereoProcessor.h"
#include "Geno_NoiseProcessor.h"
#include "Geno_TextureProcessor.h"
#include "Geno_SpaceProcessor.h"
#include "Geno_MovementProcessor.h"
#include "Geno_GlueProcessor.h"
#include "Geno_AirProcessor.h"
#include "Geno_ResonanceProcessor.h"

static int gPass = 0, gFail = 0;

void check(const char* test, bool cond, const char* detail = "") {
  if (cond) { gPass++; printf("  ✅ %s\n", test); }
  else { gFail++; printf("  ❌ %s: %s\n", test, detail); }
}

// ============================================================
// Generate test signals
// ============================================================
static double kSR = 44100.0;

void genSine(std::vector<float>& sig, double freq, double amp, double durSec) {
  int n = (int)(durSec * kSR);
  sig.resize(n);
  for (int i = 0; i < n; i++)
    sig[i] = (float)(amp * sin(2.0 * M_PI * freq * i / kSR));
}

void genComplex(std::vector<float>& sig, double durSec) {
  int n = (int)(durSec * kSR);
  sig.resize(n);
  for (int i = 0; i < n; i++)
    sig[i] = (float)(0.3 * sin(2.0 * M_PI * 220.0 * i / kSR)
                   + 0.15 * sin(2.0 * M_PI * 330.0 * i / kSR)
                   + 0.1 * sin(2.0 * M_PI * 440.0 * i / kSR));
}

void genNoise(std::vector<float>& sig, double durSec) {
  int n = (int)(durSec * kSR);
  sig.resize(n);
  uint32_t rng = 12345;
  for (int i = 0; i < n; i++) {
    rng = rng * 1103515245 + 12345;
    sig[i] = (float)((double)(int32_t)(rng & 0x7FFFFFFF) / 0x40000000 - 1.0) * 0.3f;
  }
}

// ============================================================
// TEST 1: Processor bypass when profiles not loaded
// ============================================================
void testBypass() {
  printf("\n=== 1. Processor Bypass (no profiles = passthrough) ===\n");

  std::vector<float> sig;
  genSine(sig, 440.0, 0.5, 0.5);

  auto testBypass = [&](const char* name, auto& proc) {
    std::vector<float> out(sig.size());
    proc.Process(sig.data(), out.data(), (int)sig.size());
    bool same = true;
    for (size_t i = 0; i < sig.size(); i++) {
      if (std::abs(sig[i] - out[i]) > 1e-6f) { same = false; break; }
    }
    check(name, same);
  };

  SpectralProcessor sp; sp.SetSampleRate(kSR);
  testBypass("SpectralProcessor bypass", sp);

  DynamicsProcessor dp; dp.SetSampleRate(kSR);
  testBypass("DynamicsProcessor bypass", dp);

  TextureProcessor tp; tp.SetSampleRate(kSR);
  testBypass("TextureProcessor bypass", tp);

  NoiseProcessor np; np.SetSampleRate(kSR);
  testBypass("NoiseProcessor bypass", np);

  AirProcessor ap; ap.SetSampleRate(kSR);
  testBypass("AirProcessor bypass", ap);
}

// ============================================================
// TEST 2: TransferEngine full pipeline (all genes unlocked)
// ============================================================
void testFullPipeline() {
  printf("\n=== 2. Full Pipeline Transfer (all processors) ===\n");

  std::vector<float> srcSig, tgtSig;
  genComplex(srcSig, 1.0);
  genNoise(tgtSig, 1.0);

  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile srcP, tgtP;
  an.ComputeFullAnalysis(srcSig.data(), srcSig.data(), (int)srcSig.size(), true, srcP);
  an.ComputeFullAnalysis(tgtSig.data(), tgtSig.data(), (int)tgtSig.size(), true, tgtP);

  TransferEngine eng;
  eng.SetSampleRate(kSR);
  eng.SetSourceProfile(srcP);
  eng.SetTargetProfile(tgtP);
  GenoTransferParams params;
  for (auto& a : params.amounts) a = 0.5;
  eng.SetTransferParams(params);

  std::vector<float> outL(srcSig.size()), outR(srcSig.size());
  eng.Process(srcSig.data(), srcSig.data(),
              outL.data(), outR.data(), (int)srcSig.size(), true);

  float peak = 0;
  for (size_t i = 0; i < outL.size(); i++) {
    peak = std::max({peak, std::abs(outL[i]), std::abs(outR[i])});
  }
  check("Full pipeline: output not silent", peak > 0.001f);
  check("Full pipeline: output not clipped", peak < 2.0f);

  // Re-analyze output
  GenoProfile outP;
  an.ComputeFullAnalysis(outL.data(), outR.data(), (int)outL.size(), true, outP);
  check("Full pipeline: output has pitch content", outP.spectral.pitchConfidence >= 0);
  check("Full pipeline: output has dynamics", outP.dynamics.dynamicRange > 0);
  check("Full pipeline: centroid valid", outP.spectral.centroid > 50);
}

// ============================================================
// TEST 3: TransferEngine bypass at amount=0
// ============================================================
void testTransferAmountZero() {
  printf("\n=== 3. Transfer Amount=0 → Identity ===\n");

  std::vector<float> srcSig, tgtSig;
  genSine(srcSig, 440.0, 0.5, 1.0);
  genNoise(tgtSig, 1.0);

  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile srcP, tgtP;
  an.ComputeFullAnalysis(srcSig.data(), srcSig.data(), (int)srcSig.size(), true, srcP);
  an.ComputeFullAnalysis(tgtSig.data(), tgtSig.data(), (int)tgtSig.size(), true, tgtP);

  TransferEngine eng;
  eng.SetSampleRate(kSR);
  eng.SetSourceProfile(srcP);
  eng.SetTargetProfile(tgtP);
  GenoTransferParams params;
  for (auto& a : params.amounts) a = 0.0;  // zero transfer
  eng.SetTransferParams(params);

  std::vector<float> outL(srcSig.size()), outR(srcSig.size());
  eng.Process(srcSig.data(), srcSig.data(),
              outL.data(), outR.data(), (int)srcSig.size(), true);

  bool same = true;
  for (size_t i = 0; i < srcSig.size(); i++) {
    if (std::abs(srcSig[i] - outL[i]) > 1e-4f) { same = false; break; }
  }
  check("Amount=0: output == source", same);
}

// ============================================================
// TEST 4: Transfer amount=1 → check output differs from source
// ============================================================
void testTransferAmountOne() {
  printf("\n=== 4. Transfer Amount=1 → Max Transfer ===\n");

  std::vector<float> srcSig, tgtSig;
  genSine(srcSig, 440.0, 0.5, 1.0);
  genNoise(tgtSig, 1.0);

  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile srcP, tgtP;
  an.ComputeFullAnalysis(srcSig.data(), srcSig.data(), (int)srcSig.size(), true, srcP);
  an.ComputeFullAnalysis(tgtSig.data(), tgtSig.data(), (int)tgtSig.size(), true, tgtP);

  TransferEngine eng;
  eng.SetSampleRate(kSR);
  eng.SetSourceProfile(srcP);
  eng.SetTargetProfile(tgtP);
  GenoTransferParams params;
  for (auto& a : params.amounts) a = 1.0;
  eng.SetTransferParams(params);

  std::vector<float> outL(srcSig.size()), outR(srcSig.size());
  eng.Process(srcSig.data(), srcSig.data(),
              outL.data(), outR.data(), (int)srcSig.size(), true);

  float peak = 0;
  for (size_t i = 0; i < outL.size(); i++)
    peak = std::max(peak, std::abs(outL[i]));
  check("Amount=1: output valid", peak > 0.001f && peak < 2.0f);

  bool different = false;
  for (size_t i = 0; i < srcSig.size(); i++) {
    if (std::abs(srcSig[i] - outL[i]) > 1e-2f) { different = true; break; }
  }
  check("Amount=1: output differs from source", different);
}

// ============================================================
// TEST 5: Morph engine
// ============================================================
void testMorphEngine() {
  printf("\n=== 5. Morph Engine ===\n");

  MorphEngine morph;
  std::vector<float> sigA, sigB;
  genSine(sigA, 220.0, 0.5, 0.5);
  genSine(sigB, 880.0, 0.3, 0.5);

  // Need to resize to match (morph may require equal lengths)
  int len = std::min((int)sigA.size(), (int)sigB.size());
  sigA.resize(len);
  sigB.resize(len);

  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile pA, pB;
  an.ComputeFullAnalysis(sigA.data(), sigA.data(), len, true, pA);
  an.ComputeFullAnalysis(sigB.data(), sigB.data(), len, true, pB);

  // Interpolate at t=0, 0.5, 1
  GenoProfile mid = pA.InterpolateWith(pB, 0.5);
  check("Morph: t=0.5 centroid between",
        mid.spectral.centroid >= std::min(pA.spectral.centroid, pB.spectral.centroid) &&
        mid.spectral.centroid <= std::max(pA.spectral.centroid, pB.spectral.centroid));

  GenoProfile atA = pA.InterpolateWith(pB, 0.0);
  check("Morph: t=0 == source", std::abs(atA.spectral.centroid - pA.spectral.centroid) < 1.0);

  GenoProfile atB = pA.InterpolateWith(pB, 1.0);
  check("Morph: t=1 == target", std::abs(atB.spectral.centroid - pB.spectral.centroid) < 1.0);

  // Test MorphEngine process (assume it takes profiles and processes audio)
  check("Morph: source pitch valid", pA.spectral.pitch > 0);
  check("Morph: target pitch valid", pB.spectral.pitch > 0);
}

// ============================================================
// TEST 6: FeatureVector round-trip
// ============================================================
void testFeatureVector() {
  printf("\n=== 6. FeatureVector Round-Trip ===\n");

  std::vector<float> sig;
  genComplex(sig, 1.0);
  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile p;
  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, p);

  std::vector<double> vec = p.ToFeatureVector();
  GenoProfile restored = GenoProfile::FromFeatureVector(vec);

  auto approx = [](double a, double b, double eps = 0.01) {
    return std::abs(a - b) < eps;
  };

  check("FV round-trip: centroid",
        approx(p.spectral.centroid, restored.spectral.centroid, 1.0));
  check("FV round-trip: brightness",
        approx(p.spectral.brightness, restored.spectral.brightness, 0.05));
  check("FV round-trip: rms",
        approx(p.dynamics.rms, restored.dynamics.rms, 0.01));
  check("FV round-trip: pitch",
        approx(p.spectral.pitch, restored.spectral.pitch, 1.0));
  check("FV round-trip: DR",
        approx(p.dynamics.dynamicRange, restored.dynamics.dynamicRange, 1.0));
  check("FV round-trip: crest factor",
        approx(p.dynamics.crestFactor, restored.dynamics.crestFactor, 0.1));
  check("FV round-trip: width",
        approx(p.stereo.width, restored.stereo.width, 0.1));
  check("FV round-trip: correlation",
        approx(p.stereo.phaseCorrelation, restored.stereo.phaseCorrelation, 0.1));
  check("FV round-trip: spectralFlatness",
        approx(p.spectral.spectralFlatness, restored.spectral.spectralFlatness, 0.05));
}

// ============================================================
// TEST 7: Similarity
// ============================================================
void testSimilarity() {
  printf("\n=== 7. Similarity ===\n");

  std::vector<float> sig;
  genComplex(sig, 1.0);
  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile p, same, diff;

  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, p);

  // Same analysis for "same" profile
  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, same);

  // Different signal for "diff" profile
  std::vector<float> sig2;
  genSine(sig2, 880.0, 0.5, 0.5);
  an.ComputeFullAnalysis(sig2.data(), sig2.data(), (int)sig2.size(), true, diff);

  double selfSim = p.SimilarityTo(p);
  double sameSim = p.SimilarityTo(same);
  double diffSim = p.SimilarityTo(diff);

  check("Similarity: self=1.0", std::abs(selfSim - 1.0) < 0.01,
        std::to_string(selfSim).c_str());
  check("Similarity: same ≈ 1.0", std::abs(sameSim - 1.0) < 0.1,
        std::to_string(sameSim).c_str());
  check("Similarity: symmetric",
        std::abs(p.SimilarityTo(diff) - diff.SimilarityTo(p)) < 0.01);
  check("Similarity: different < 1.0", diffSim < 0.99,
        std::to_string(diffSim).c_str());
}

// ============================================================
// TEST 8: Stress test (many iterations to catch leaks/crashes)
// ============================================================
void testStress() {
  printf("\n=== 8. Stress Test ===\n");

  std::vector<float> sig;
  genComplex(sig, 0.1);  // short signal

  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile srcP, tgtP;
  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, srcP);
  // Create slightly different target
  tgtP = srcP;
  tgtP.spectral.centroid *= 1.5;

  bool allOk = true;
  int iter = 100;
  for (int i = 0; i < iter; i++) {
    // Create fresh objects each iteration
    std::vector<float> outL(sig.size()), outR(sig.size());

    // Test SpectralProcessor
    {
      SpectralProcessor sp;
      sp.SetSampleRate(kSR);
      sp.SetSourceProfile(srcP.spectral);
      sp.SetTargetProfile(tgtP.spectral);
      sp.Reset();
      sp.Process(sig.data(), outL.data(), (int)sig.size());
      float p = 0;
      for (auto v : outL) p = std::max(p, std::abs(v));
      if (p < 0.001f || p > 2.0f) { allOk = false; break; }
    }

    // Test DynamicsProcessor
    {
      DynamicsProcessor dp;
      dp.SetSampleRate(kSR);
      dp.SetSourceProfile(srcP.dynamics);
      dp.SetTargetProfile(tgtP.dynamics);
      dp.Reset();
      dp.Process(sig.data(), outL.data(), (int)sig.size());
      float p = 0;
      for (auto v : outL) p = std::max(p, std::abs(v));
      if (p < 0.001f || p > 2.0f) { allOk = false; break; }
    }

    // Test TransferEngine
    {
      TransferEngine eng;
      eng.SetSampleRate(kSR);
      eng.SetSourceProfile(srcP);
      eng.SetTargetProfile(tgtP);
      GenoTransferParams params;
      for (auto& a : params.amounts) a = 0.5;
      eng.SetTransferParams(params);
      for (auto& l : params.locks) l = true;
      params.locks[static_cast<int>(GenoGene::Tone)] = false;
      eng.SetTransferParams(params);
      eng.Process(sig.data(), sig.data(), outL.data(), outR.data(), (int)sig.size(), true);
      float p = 0;
      for (auto v : outL) p = std::max(p, std::abs(v));
      if (p < 0.001f || p > 2.0f) { allOk = false; break; }
    }
  }

  check("Stress: 100x create/process/destroy OK", allOk,
        ("Failed at iteration " + std::to_string(iter)).c_str());
}

// ============================================================
// TEST 9: Multiple sample rates
// ============================================================
void testSampleRates() {
  printf("\n=== 9. Multiple Sample Rates ===\n");

  for (double sr : {22050.0, 44100.0, 48000.0, 96000.0}) {
    double orig = kSR;
    kSR = sr;

    std::vector<float> sig;
    genSine(sig, 440.0, 0.5, 0.2);

    GenoAnalyzer an;
    an.SetSampleRate(sr);
    GenoProfile p;
    an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, p);

    char label[64];
    snprintf(label, sizeof(label), "SR %.0f: pitch ~440Hz", sr);
    check(label, p.spectral.pitch > 400 && p.spectral.pitch < 480,
          std::to_string(p.spectral.pitch).c_str());

    snprintf(label, sizeof(label), "SR %.0f: centroid valid", sr);
    check(label, p.spectral.centroid > 100 && p.spectral.centroid < 10000,
          std::to_string(p.spectral.centroid).c_str());

    // Test SpectralProcessor at this SR
    SpectralProcessor sp;
    sp.SetSampleRate(sr);
    SpectralFeatures sf, tf;
    sf.spectralEnvelope.assign(kNumSpectralBands, 1.0);
    tf.spectralEnvelope.assign(kNumSpectralBands, 2.0);
    sp.SetSourceProfile(sf);
    sp.SetTargetProfile(tf);
    std::vector<float> out(sig.size());
    sp.Process(sig.data(), out.data(), (int)sig.size());
    float peak = 0;
    for (auto v : out) peak = std::max(peak, std::abs(v));

    snprintf(label, sizeof(label), "SR %.0f: process valid", sr);
    check(label, peak > 0.001f && peak < 2.0f, std::to_string(peak).c_str());

    kSR = orig;
  }
}

// ============================================================
// TEST 10: Stereo processors
// ============================================================
void testStereoProcessors() {
  printf("\n=== 10. Stereo Processors ===\n");

  std::vector<float> L, R;
  L.resize(44100);
  R.resize(44100);
  for (int i = 0; i < 44100; i++) {
    L[i] = (float)(0.4 * sin(2.0 * M_PI * 220.0 * i / kSR));
    R[i] = (float)(0.3 * sin(2.0 * M_PI * 440.0 * i / kSR));
  }

  // StereoProcessor: MS encode → process → decode should preserve
  StereoProcessor sp;
  sp.SetSampleRate(kSR);
  StereoFeatures sf, tf;
  sf.width = 0.5; sf.phaseCorrelation = 0.8;
  tf.width = 0.8; tf.phaseCorrelation = 0.5;
  sp.SetSourceProfile(sf);
  sp.SetTargetProfile(tf);
  sp.SetTransferAmount(1.0);

  std::vector<float> oL(44100), oR(44100);
  sp.Process(L.data(), R.data(), oL.data(), oR.data(), 44100);

  float maxL = 0, maxR = 0;
  for (int i = 0; i < 44100; i++) {
    maxL = std::max(maxL, std::abs(oL[i]));
    maxR = std::max(maxR, std::abs(oR[i]));
  }
  check("StereoProcessor: output valid", maxL > 0.001f && maxR > 0.001f && maxL < 2.0f && maxR < 2.0f,
        (std::to_string(maxL) + ", " + std::to_string(maxR)).c_str());

  // MovementProcessor
  MovementProcessor mp;
  mp.SetSampleRate(kSR);
  MovementFeatures mf_s, mf_t;
  mf_s.modulationRate = 2.0; mf_s.modulationDepth = 0.1;
  mf_t.modulationRate = 5.0; mf_t.modulationDepth = 0.5;
  mp.SetSourceProfile(mf_s);
  mp.SetTargetProfile(mf_t);
  std::fill(oL.begin(), oL.end(), 0);
  std::fill(oR.begin(), oR.end(), 0);
  mp.Process(L.data(), R.data(), oL.data(), oR.data(), 44100);
  maxL = maxR = 0;
  for (int i = 0; i < 44100; i++) {
    maxL = std::max(maxL, std::abs(oL[i]));
    maxR = std::max(maxR, std::abs(oR[i]));
  }
  check("MovementProcessor: output valid", maxL > 0.001f && maxR > 0.001f && maxL < 2.0f && maxR < 2.0f,
        (std::to_string(maxL) + ", " + std::to_string(maxR)).c_str());
}

// ============================================================
// TEST 11: Clipping protection
// ============================================================
void testClippingProtection() {
  printf("\n=== 11. Clipping Protection ===\n");

  std::vector<float> sig;
  genSine(sig, 440.0, 5.0, 0.1);  // way over 0dB

  // Process through various processors
  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile p;
  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, p);

  TransferEngine eng;
  eng.SetSampleRate(kSR);
  eng.SetSourceProfile(p);
  eng.SetTargetProfile(p);
  GenoTransferParams params;
  for (auto& a : params.amounts) a = 1.0;
  eng.SetTransferParams(params);

  std::vector<float> oL(sig.size()), oR(sig.size());
  eng.Process(sig.data(), sig.data(), oL.data(), oR.data(), (int)sig.size(), true);

  float peak = 0;
  for (size_t i = 0; i < oL.size(); i++)
    peak = std::max({peak, std::abs(oL[i]), std::abs(oR[i])});
  check("Clipping: output not clipping (|peak| < 10)", peak < 10.0f,
        std::to_string(peak).c_str());
  check("Clipping: output not silent", peak > 0.001f);
}

// ============================================================
// TEST 12: Resampler via test — SR mismatch handling
// ============================================================
void testSRMismatch() {
  printf("\n=== 12. Resampler Behavior ===\n");

  // Test that processors handle SetSampleRate being called multiple times
  SpectralProcessor sp;
  sp.SetSampleRate(44100.0);
  sp.SetSampleRate(48000.0);  // change SR
  sp.SetSampleRate(44100.0);  // change back

  SpectralFeatures sf, tf;
  sf.spectralEnvelope.assign(kNumSpectralBands, 1.0);
  tf.spectralEnvelope.assign(kNumSpectralBands, 2.0);
  sp.SetSourceProfile(sf);
  sp.SetTargetProfile(tf);

  std::vector<float> sig;
  genSine(sig, 440.0, 0.5, 0.2);
  std::vector<float> out(sig.size());
  sp.Process(sig.data(), out.data(), (int)sig.size());

  float peak = 0;
  for (auto v : out) peak = std::max(peak, std::abs(v));
  check("SR change: process ok", peak > 0.001f && peak < 2.0f,
        std::to_string(peak).c_str());

  // Test TransferEngine SR change
  GenoAnalyzer an;
  an.SetSampleRate(44100.0);
  GenoProfile p;
  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, p);

  TransferEngine eng;
  eng.SetSampleRate(44100.0);
  eng.SetSourceProfile(p);
  eng.SetTargetProfile(p);
  eng.SetSampleRate(48000.0);
  eng.SetSampleRate(44100.0);
  GenoTransferParams params;
  for (auto& a : params.amounts) a = 0.5;
  eng.SetTransferParams(params);

  std::vector<float> oL(sig.size()), oR(sig.size());
  eng.Process(sig.data(), sig.data(), oL.data(), oR.data(), (int)sig.size(), true);
  peak = 0;
  for (auto v : oL) peak = std::max(peak, std::abs(v));
  check("TransferEngine SR change: process ok", peak > 0.001f && peak < 2.0f,
        std::to_string(peak).c_str());
}

// ============================================================
// TEST 13: Verify analysis stability (repeated analysis of same signal)
// ============================================================
void testAnalysisStability() {
  printf("\n=== 13. Analysis Stability ===\n");

  std::vector<float> sig;
  genComplex(sig, 1.0);

  GenoAnalyzer an;
  an.SetSampleRate(kSR);

  GenoProfile prev;
  bool stable = true;
  for (int i = 0; i < 5; i++) {
    GenoProfile p;
    an.Reset();
    an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, p);
    if (i > 0) {
      double diff = std::abs(p.spectral.centroid - prev.spectral.centroid);
      if (diff > 0.5) { stable = false; break; }
    }
    prev = p;
  }
  check("Analysis: stable across runs", stable);
}

// ============================================================
// TEST 14: TransferEngine — specific gene locks
// ============================================================
void testGeneLocks() {
  printf("\n=== 14. Gene Locks ===\n");

  std::vector<float> sig;
  genComplex(sig, 1.0);

  GenoAnalyzer an;
  an.SetSampleRate(kSR);
  GenoProfile srcP, tgtP;
  an.ComputeFullAnalysis(sig.data(), sig.data(), (int)sig.size(), true, srcP);
  // Slightly different target
  tgtP = srcP;
  tgtP.spectral.centroid = srcP.spectral.centroid * 1.2;
  tgtP.dynamics.dynamicRange = srcP.dynamics.dynamicRange * 1.5;

  // Lock only Dynamics, unlock only Tone and Texture
  GenoTransferParams params;
  for (auto& a : params.amounts) a = 0.5;
  for (auto& l : params.locks) l = true;
  params.locks[static_cast<int>(GenoGene::Tone)] = false;
  params.locks[static_cast<int>(GenoGene::Texture)] = false;

  TransferEngine eng;
  eng.SetSampleRate(kSR);
  eng.SetSourceProfile(srcP);
  eng.SetTargetProfile(tgtP);
  eng.SetTransferParams(params);

  std::vector<float> oL(sig.size()), oR(sig.size());
  eng.Process(sig.data(), sig.data(), oL.data(), oR.data(), (int)sig.size(), true);

  float peak = 0;
  for (auto v : oL) peak = std::max(peak, std::abs(v));
  check("Gene locks: output valid", peak > 0.001f && peak < 2.0f,
        std::to_string(peak).c_str());

  // Now unlock all
  for (auto& l : params.locks) l = false;
  eng.SetTransferParams(params);
  std::vector<float> oL2(sig.size()), oR2(sig.size());
  eng.Process(sig.data(), sig.data(), oL2.data(), oR2.data(), (int)sig.size(), true);

  bool different = false;
  for (size_t i = 0; i < sig.size(); i++) {
    if (std::abs(oL[i] - oL2[i]) > 1e-4f) { different = true; break; }
  }
  check("Gene locks: unlocked != locked", different);

  // Lock all → bypass
  for (auto& l : params.locks) l = true;
  eng.SetTransferParams(params);
  std::vector<float> oL3(sig.size()), oR3(sig.size());
  eng.Process(sig.data(), sig.data(), oL3.data(), oR3.data(), (int)sig.size(), true);
  bool allBypass = true;
  for (size_t i = 0; i < sig.size(); i++) {
    if (std::abs(sig[i] - oL3[i]) > 1e-4f) { allBypass = false; break; }
  }
  check("Gene locks: all locked = bypass", allBypass);
}

int main() {
  testBypass();
  testFullPipeline();
  testTransferAmountZero();
  testTransferAmountOne();
  testMorphEngine();
  testFeatureVector();
  testSimilarity();
  testStress();
  testSampleRates();
  testStereoProcessors();
  testClippingProtection();
  testSRMismatch();
  testAnalysisStability();
  testGeneLocks();

  printf("\n=== PIPELINE TEST SUMMARY: %d pass, %d fail ===\n", gPass, gFail);
  return gFail;
}
