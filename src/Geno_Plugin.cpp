#include "Geno_Plugin.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include <cstring>
#include <cmath>

#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <fstream>
#include <sndfile.h>

GenoPlugin::GenoPlugin(const InstanceInfo& info)
  : PLUGIN_API_BASE(info, MakeConfig(kNumParams, 0))
{
  GetParam(kInputGain)->InitGain("InputGain", 0., -18., 18.);
  GetParam(kOutputGain)->InitGain("OutputGain", 0., -18., 18.);
  GetParam(kMix)->InitDouble("Mix", 50., 0., 100., 0.1, "%");

  GetParam(kToneAmount)->InitDouble("Tone", 50., 0., 100., 0.1, "%");
  GetParam(kDynamicsAmount)->InitDouble("Dynamics", 50., 0., 100., 0.1, "%");
  GetParam(kNoiseAmount)->InitDouble("Noise", 50., 0., 100., 0.1, "%");
  GetParam(kStereoSpatialAmount)->InitDouble("Stereo", 50., 0., 100., 0.1, "%");
  GetParam(kAirAmount)->InitDouble("Air", 50., 0., 100., 0.1, "%");
  GetParam(kMovementAmount)->InitDouble("Movement", 50., 0., 100., 0.1, "%");
  GetParam(kSpaceAmount)->InitDouble("Space", 50., 0., 100., 0.1, "%");
  GetParam(kTextureAmount)->InitDouble("Texture", 50., 0., 100., 0.1, "%");
  GetParam(kPunchAmount)->InitDouble("Punch", 50., 0., 100., 0.1, "%");
  GetParam(kBodyAmount)->InitDouble("Body", 50., 0., 100., 0.1, "%");
  GetParam(kResonanceAmount)->InitDouble("Resonance", 50., 0., 100., 0.1, "%");
  GetParam(kWarmthAmount)->InitDouble("Warmth", 50., 0., 100., 0.1, "%");
  GetParam(kSparkleAmount)->InitDouble("Sparkle", 50., 0., 100., 0.1, "%");
  GetParam(kGlueAmount)->InitDouble("Glue", 50., 0., 100., 0.1, "%");

  GetParam(kToneLock)->InitInt("ToneLock", 0, 0, 1);
  GetParam(kDynamicsLock)->InitInt("DynamicsLock", 0, 0, 1);
  GetParam(kNoiseLock)->InitInt("NoiseLock", 0, 0, 1);
  GetParam(kStereoSpatialLock)->InitInt("StereoSpatialLock", 0, 0, 1);
  GetParam(kAirLock)->InitInt("AirLock", 0, 0, 1);
  GetParam(kMovementLock)->InitInt("MovementLock", 0, 0, 1);
  GetParam(kSpaceLock)->InitInt("SpaceLock", 0, 0, 1);
  GetParam(kTextureLock)->InitInt("TextureLock", 0, 0, 1);
  GetParam(kPunchLock)->InitInt("PunchLock", 0, 0, 1);
  GetParam(kBodyLock)->InitInt("BodyLock", 0, 0, 1);
  GetParam(kResonanceLock)->InitInt("ResonanceLock", 0, 0, 1);
  GetParam(kWarmthLock)->InitInt("WarmthLock", 0, 0, 1);
  GetParam(kSparkleLock)->InitInt("SparkleLock", 0, 0, 1);
  GetParam(kGlueLock)->InitInt("GlueLock", 0, 0, 1);

  GetParam(kMorphPosition)->InitDouble("Morph", 0., 0., 100., 0.1, "%");

#ifdef WEBVIEW_EDITOR_DELEGATE
#ifdef DEBUG
  SetEnableDevTools(true);
#endif

  mEditorInitFunc = [&]() {
    LoadHTML(kGenoUIHTML);
    EnableScroll(false);
    SendParametersToUI();
  };
#endif
}

#if IPLUG_DSP
void GenoPlugin::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
  const double inputGain = GetParam(kInputGain)->DBToAmp();
  const double outputGain = GetParam(kOutputGain)->DBToAmp();
  const double mix = GetParam(kMix)->Value() / 100.0;

  sample* inL = inputs[0];
  sample* inR = inputs[1];
  sample* outL = outputs[0];
  sample* outR = outputs[1];

  {
    static int rc = 0; ++rc;
    float rPkL = 0, rPkF = 0, rOutPkL = 0, rOutPkR = 0;
    int firstNZ = -1;
    for (int s = 0; s < nFrames; ++s) {
      float fL = (float)inL[s], fR = (float)inR[s];
      if (fabsf(fL) > rPkL) rPkL = fabsf(fL);
      if (fabsf(fR) > rPkF) rPkF = fabsf(fR);
      if (firstNZ < 0 && (fL != 0.0f || fR != 0.0f)) firstNZ = s;
    }
    for (int s = 0; s < nFrames; ++s) {
      float fL = (float)outL[s], fR = (float)outR[s];
      if (fabsf(fL) > rOutPkL) rOutPkL = fabsf(fL);
      if (fabsf(fR) > rOutPkR) rOutPkR = fabsf(fR);
    }
    if (rc % 500 == 0 || firstNZ >= 0) {
      fprintf(stderr, "[DBUG] RAW: rc=%d nz=%d i(%.6f/%.6f) o(%.6f/%.6f) iG=%.6f oG=%.6f mix=%.3f in0=%p out0=%p sz=%zu\n",
              rc, firstNZ, rPkL, rPkF, rOutPkL, rOutPkR, inputGain, outputGain, mix,
              (void*)inL, (void*)outL, sizeof(sample));
    }
  }

  if (mBypass) {
    for (int s = 0; s < nFrames; s++) {
      outL[s] = inL[s] * inputGain * outputGain;
      outR[s] = inR[s] * inputGain * outputGain;
    }
    return;
  }

  std::vector<float> dryL(nFrames), dryR(nFrames);
  for (int s = 0; s < nFrames; s++) {
    dryL[s] = inL[s] * (float)inputGain;
    dryR[s] = inR[s] * (float)inputGain;
  }

  // Capture audio into circular buffer only when recording
  if (mCapturing.load(std::memory_order_relaxed)) {
    int wp = mCircWritePos.load(std::memory_order_relaxed);
    for (int s = 0; s < nFrames; s++) {
      mCircBufL[wp] = dryL[s];
      mCircBufR[wp] = dryR[s];
      if (++wp >= kMaxBufferSamples) wp = 0;
    }
    mCircWritePos.store(wp, std::memory_order_release);
    mCircAvail.store(nFrames > 0 ? 1 : 0, std::memory_order_release);
  }

  std::vector<float> wetL(nFrames), wetR(nFrames);
  mTransferEngine.Process(dryL.data(), dryR.data(),
                           wetL.data(), wetR.data(), nFrames, true);

  {
    static int cnt = 0;
    if (++cnt % 500 == 0) {
      float wPkL = 0, wPkR = 0, dPkL = 0, dPkR = 0;
      for (int s = 0; s < nFrames; ++s) {
        if (std::fabs(wetL[s]) > wPkL) wPkL = std::fabs(wetL[s]);
        if (std::fabs(wetR[s]) > wPkR) wPkR = std::fabs(wetR[s]);
        if (std::fabs(dryL[s]) > dPkL) dPkL = std::fabs(dryL[s]);
        if (std::fabs(dryR[s]) > dPkR) dPkR = std::fabs(dryR[s]);
      }
      fprintf(stderr, "[DBUG] PB: nF=%d mix=%.3f bp=%d sL=%d tL=%d inPk=%.4f/%.4f wetPk=%.4f/%.4f\n",
              nFrames, mix, mBypass, mTransferEngine.GetSourceLoaded(), mTransferEngine.GetTargetLoaded(),
              dPkL, dPkR, wPkL, wPkR);
    }
  }

  for (int s = 0; s < nFrames; s++) {
    float blendedL = dryL[s] * (1.f - (float)mix) + wetL[s] * (float)mix;
    float blendedR = dryR[s] * (1.f - (float)mix) + wetR[s] * (float)mix;
    outL[s] = blendedL * (float)outputGain;
    outR[s] = blendedR * (float)outputGain;
  }
}

void GenoPlugin::OnReset() {
  mSampleRate = GetSampleRate();
  mTransferEngine.SetSampleRate(mSampleRate);
  mAnalyzer.SetSampleRate(mSampleRate);
  mCircWritePos.store(0, std::memory_order_relaxed);
  mCircAvail.store(0, std::memory_order_relaxed);
}

void GenoPlugin::OnIdle() {
  DrainHelperEvents();
  if (mAnalysisPending.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(mAnalysisMutex);
    GenoProfile profile = mPendingProfile;
    int tag = mPendingTag;
    mAnalysisPending.store(false, std::memory_order_release);

    if (tag == 0) {
      mCurrentSourceProfile = profile;
      mTransferEngine.SetSourceProfile(profile);
    } else {
      mCurrentTargetProfile = profile;
      mTransferEngine.SetTargetProfile(profile);
    }
    SendAnalyzerReportToUI(profile);
    SendGenoProfileToUI(profile, tag == 0 ? "source" : "target");
    PushHistory(profile);
  }
}
#endif

// Lightweight JSON helper: extract a double value for a given key from a simple JSON string.
// Handles "key": value and "key": "string" formats. Not a general-purpose parser.
static double ExtractJSONDouble(const char* json, const char* key, double def = 0.0) {
  const char* p = strstr(json, key);
  if (!p) return def;
  p = strchr(p, ':');
  if (!p) return def;
  ++p;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  char* end = nullptr;
  double v = strtod(p, &end);
  return (end != p) ? v : def;
}

static std::string ExtractJSONString(const char* json, const char* key, const char* def = "") {
  const char* p = strstr(json, key);
  if (!p) return def;
  p = strchr(p, ':');
  if (!p) return def;
  ++p;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  if (*p != '"') return def;
  ++p;
  const char* end = strchr(p, '"');
  if (!end) return def;
  return std::string(p, end - p);
}

static std::string ExtractJSONObject(const char* json, const char* key) {
  const char* p = strstr(json, key);
  if (!p) return "";
  p = strchr(p, ':');
  if (!p) return "";
  ++p;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  if (*p != '{') return "";
  int depth = 0;
  const char* start = p;
  while (*p) {
    if (*p == '{') ++depth;
    else if (*p == '}') { --depth; if (depth == 0) { ++p; break; } }
    else if (*p == '"') { ++p; while (*p && *p != '"') { if (*p == '\\') ++p; ++p; } }
    ++p;
  }
  return std::string(start, p - start);
}

// Lightweight JSON array extractor: parses [v1,v2,...,vN]
static std::vector<double> ExtractJSONArray(const char* json, const char* key) {
  std::vector<double> result;
  const char* p = strstr(json, key);
  if (!p) return result;
  p = strchr(p, ':');
  if (!p) return result;
  ++p;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  if (*p != '[') return result;
  ++p;
  while (*p && *p != ']') {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) ++p;
    if (*p == ']') break;
    char* end = nullptr;
    double v = strtod(p, &end);
    if (end == p) break;
    result.push_back(v);
    p = end;
  }
  return result;
}

static void ParseGenoProfileJSON(const char* json, GenoProfile& profile) {
  profile.spectral.centroid = ExtractJSONDouble(json, "centroid");
  profile.spectral.brightness = ExtractJSONDouble(json, "brightness");
  profile.spectral.spectralFlatness = ExtractJSONDouble(json, "flatness");
  profile.dynamics.dynamicRange = ExtractJSONDouble(json, "dynamicRange");
  profile.dynamics.crestFactor = ExtractJSONDouble(json, "crestFactor");
  profile.stereo.width = ExtractJSONDouble(json, "stereoWidth");
  profile.stereo.phaseCorrelation = ExtractJSONDouble(json, "phaseCorrelation");
  profile.noise.noiseFloorDb = ExtractJSONDouble(json, "noiseFloor");
  profile.texture.saturationAmount = ExtractJSONDouble(json, "saturation");
  profile.texture.harmonicDistortion = ExtractJSONDouble(json, "distortion");
  profile.texture.analogWarmth = ExtractJSONDouble(json, "analogWarmth");
  profile.texture.tapeSaturation = ExtractJSONDouble(json, "tapeSaturation");
  profile.noise.spectralTilt = ExtractJSONDouble(json, "spectralTilt");
  profile.spectral.pitch = ExtractJSONDouble(json, "pitch");
  profile.spectral.rolloff = ExtractJSONDouble(json, "rolloff");
  profile.spectral.spread = ExtractJSONDouble(json, "spread");
  profile.confidence = ExtractJSONDouble(json, "confidence", 1.0);
  profile.sourceName = ExtractJSONString(json, "name");
  profile.instrument = ExtractJSONString(json, "instrument");
  profile.category = ExtractJSONString(json, "category");
  profile.spectral.spectralEnvelope = ExtractJSONArray(json, "spectralEnvelope");
  profile.spectral.harmonicProfile = ExtractJSONArray(json, "harmonicProfile");
  auto ns = ExtractJSONArray(json, "noiseShape");
  for (size_t i = 0; i < ns.size() && i < kNumNoiseCoefs; ++i)
    profile.noise.noiseShape[i] = ns[i];
}

bool GenoPlugin::ValidateBufferEnergy(const float* audio, int numSamples) {
  if (numSamples < 2048) return false;
  double sumSq = 0.0;
  for (int i = 0; i < numSamples; ++i) sumSq += (double)audio[i] * audio[i];
  double rms = std::sqrt(sumSq / numSamples);
  return rms > 0.001; // -60 dB threshold
}

void GenoPlugin::RunAnalysis(int numSamples, bool isStereo, GenoProfile& out) {
  int wp = mCircWritePos.load(std::memory_order_acquire);
  int avail = std::min(numSamples, kMaxBufferSamples);

  std::vector<float> audioL(avail), audioR(avail);
  int start = (wp - avail + kMaxBufferSamples) % kMaxBufferSamples;
  for (int i = 0; i < avail; ++i) {
    int idx = (start + i) % kMaxBufferSamples;
    audioL[i] = mCircBufL[idx];
    audioR[i] = mCircBufR[idx];
  }

  if (!ValidateBufferEnergy(audioL.data(), avail)) {
    out.confidence = 0.0;
    return;
  }

  mAnalyzer.ComputeFullAnalysis(audioL.data(), audioR.data(), avail, isStereo, out);
}

bool GenoPlugin::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) {
  auto parseAndLoad = [&](const char* json) {
    GenoProfile profile;
    ParseGenoProfileJSON(json, profile);
    mCurrentSourceProfile = profile;
    mTransferEngine.SetSourceProfile(profile);
    SendAnalyzerReportToUI(profile);
    SendGenoProfileToUI(profile, "source");
    PushHistory(profile);
  };

  auto startCapture = [&]() {
    mCircWritePos.store(0, std::memory_order_relaxed);
    mCircAvail.store(0, std::memory_order_relaxed);
    mCapturing.store(true, std::memory_order_release);
    SendCaptureStatus();
  };

  auto stopCapture = [&]() {
    mCapturing.store(false, std::memory_order_release);
    SendCaptureStatus();
  };

  switch (msgTag) {
    case kMsgTagRequestParameters: {
      SendParametersToUI();
      return true;
    }

    case kMsgTagAnalyzeCapture: {
      if (mCapturing.load(std::memory_order_acquire)) {
        stopCapture();
      } else {
        startCapture();
      }
      return true;
    }

    case kMsgTagAnalyzeSource: {
      if (mCircAvail.load(std::memory_order_acquire)) {
        int wp = mCircWritePos.load(std::memory_order_acquire);
        GenoProfile profile;
        RunAnalysis(wp, true, profile);
        if (profile.confidence > 0.0) {
          std::lock_guard<std::mutex> lock(mAnalysisMutex);
          mPendingProfile = profile;
          mPendingTag = 0;
          mAnalysisPending.store(true, std::memory_order_release);
        }
      }
      return true;
    }

    case kMsgTagAnalyzeTarget: {
      if (mCircAvail.load(std::memory_order_acquire)) {
        int wp = mCircWritePos.load(std::memory_order_acquire);
        GenoProfile profile;
        RunAnalysis(wp, true, profile);
        if (profile.confidence > 0.0) {
          std::lock_guard<std::mutex> lock(mAnalysisMutex);
          mPendingProfile = profile;
          mPendingTag = 1;
          mAnalysisPending.store(true, std::memory_order_release);
        }
      }
      return true;
    }

    case kMsgTagBrowserSearch: {
      const char* json = static_cast<const char*>(pData);
      std::string query = ExtractJSONString(json, "query");
      std::ostringstream result;
      result << "{\"results\":[";
      bool first = true;
      for (const auto& prof : mProfileLibrary) {
        if (!query.empty() &&
            prof.sourceName.find(query) == std::string::npos &&
            prof.instrument.find(query) == std::string::npos) {
          continue;
        }
        if (!first) result << ",";
        first = false;
        result << "{\"name\":\"" << prof.sourceName << "\",";
        result << "\"instrument\":\"" << prof.instrument << "\",";
        result << "\"category\":\"" << prof.category << "\",";
        result << "\"confidence\":" << prof.confidence << "}";
      }
      result << "]}";
      WDL_String str(result.str().c_str());
      SendArbitraryMsgFromDelegate(kMsgTagBrowserSearch, str.GetLength(), str.Get());
      return true;
    }

    case kMsgTagBrowserSelect: {
      const char* json = static_cast<const char*>(pData);
      std::string name = ExtractJSONString(json, "name");
      for (const auto& prof : mProfileLibrary) {
        if (prof.sourceName == name) {
          mCurrentSourceProfile = prof;
          mTransferEngine.SetSourceProfile(prof);
          SendGenoProfileToUI(prof, "source");
          PushHistory(prof);
          break;
        }
      }
      return true;
    }

    case kMsgTagLoadDNA: {
      const char* json = static_cast<const char*>(pData);
      GenoProfile profile;
      ParseGenoProfileJSON(json, profile);

      // Check if year or isAnalog are present
      double yearVal = ExtractJSONDouble(json, "year", -1);
      if (yearVal >= 0) profile.year = (int)yearVal;
      profile.isAnalog = ExtractJSONDouble(json, "isAnalog", 1.0) > 0.5;

      mCurrentSourceProfile = profile;
      mTransferEngine.SetSourceProfile(profile);
      SendGenoProfileToUI(profile, "source");
      PushHistory(profile);

      // Also add to library
      mProfileLibrary.push_back(profile);
      return true;
    }

    case kMsgTagSaveDNA: {
      const char* json = static_cast<const char*>(pData);
      GenoProfile profile;
      ParseGenoProfileJSON(json, profile);
      mProfileLibrary.push_back(profile);
      return true;
    }

    case kMsgTagCompareProfiles: {
      const char* json = static_cast<const char*>(pData);
      std::string aObj = ExtractJSONObject(json, "a");
      std::string bObj = ExtractJSONObject(json, "b");
      GenoProfile a, b;
      if (!aObj.empty()) ParseGenoProfileJSON(aObj.c_str(), a);
      else a = mCurrentSourceProfile;
      if (!bObj.empty()) ParseGenoProfileJSON(bObj.c_str(), b);
      else b = mCurrentTargetProfile;
      double similarity = a.SimilarityTo(b);

      std::ostringstream result;
      result << "{";
      result << "\"similarity\":" << similarity;
      result << ",\"a\":{\"brightness\":" << a.spectral.brightness
             << ",\"dynamicRange\":" << a.dynamics.dynamicRange
             << ",\"stereoWidth\":" << a.stereo.width
             << ",\"noiseFloor\":" << a.noise.noiseFloorDb
             << ",\"saturation\":" << a.texture.saturationAmount
             << ",\"distortion\":" << a.texture.harmonicDistortion << "}";
      result << ",\"b\":{\"brightness\":" << b.spectral.brightness
             << ",\"dynamicRange\":" << b.dynamics.dynamicRange
             << ",\"stereoWidth\":" << b.stereo.width
             << ",\"noiseFloor\":" << b.noise.noiseFloorDb
             << ",\"saturation\":" << b.texture.saturationAmount
             << ",\"distortion\":" << b.texture.harmonicDistortion << "}";
      result << "}";
      WDL_String str(result.str().c_str());
      SendArbitraryMsgFromDelegate(kMsgTagCompareProfiles, str.GetLength(), str.Get());
      return true;
    }

    case kMsgTagMorphAdd: {
      const char* json = static_cast<const char*>(pData);
      GenoProfile profile;
      ParseGenoProfileJSON(json, profile);
      double position = ExtractJSONDouble(json, "position", 0.5);
      double blend = ExtractJSONDouble(json, "blendAmount", 1.0);
      mMorphEngine.AddPoint(profile, position, blend);
      return true;
    }

    case kMsgTagMorphClear: {
      mMorphEngine.Clear();
      return true;
    }

    case kMsgTagUndo: {
      if (mHistoryPosition > 0) {
        mHistoryPosition--;
        mCurrentSourceProfile = mHistory[mHistoryPosition];
        mTransferEngine.SetSourceProfile(mCurrentSourceProfile);
        SendAnalyzerReportToUI(mCurrentSourceProfile);
        SendGenoProfileToUI(mCurrentSourceProfile, "source");
      }
      return true;
    }

    case kMsgTagRedo: {
      if (mHistoryPosition < (int)mHistory.size() - 1) {
        mHistoryPosition++;
        mCurrentSourceProfile = mHistory[mHistoryPosition];
        mTransferEngine.SetSourceProfile(mCurrentSourceProfile);
        SendAnalyzerReportToUI(mCurrentSourceProfile);
        SendGenoProfileToUI(mCurrentSourceProfile, "source");
      }
      return true;
    }

    case kMsgTagLoadAudioFile: {
      if (!pData || dataSize <= 0) return true;
      const char* audioPath = static_cast<const char*>(pData);

      SF_INFO info;
      memset(&info, 0, sizeof(info));
      SNDFILE* sndFile = sf_open(audioPath, SFM_READ, &info);
      if (!sndFile) {
        DBGMSG("LoadAudioFile: cannot open %s\n", audioPath);
        return true;
      }

      std::vector<double> interleaved(info.frames * info.channels);
      sf_readf_double(sndFile, interleaved.data(), info.frames);
      sf_close(sndFile);

      // Place into circular buffer
      int numSamples = (int)std::min((sf_count_t)kMaxBufferSamples, info.frames);
      for (int i = 0; i < numSamples; ++i) {
        mCircBufL[i] = (float)interleaved[i * info.channels];
        mCircBufR[i] = info.channels > 1 ? (float)interleaved[i * info.channels + 1] : (float)interleaved[i * info.channels];
      }
      mCircWritePos.store(numSamples, std::memory_order_release);
      mCircAvail.store(1, std::memory_order_release);

      // Trigger analysis
      GenoProfile profile;
      RunAnalysis(numSamples, info.channels > 1, profile);
      if (profile.confidence > 0.0) {
        std::lock_guard<std::mutex> lock(mAnalysisMutex);
        mPendingProfile = profile;
        mPendingTag = 1;
        mAnalysisPending.store(true, std::memory_order_release);
      }
      return true;
    }

    case kMsgTagLoadDNAFile: {
      if (!pData || dataSize <= 0) return true;
      const char* dnaPath = static_cast<const char*>(pData);

      std::ifstream file(dnaPath);
      if (!file.is_open()) {
        DBGMSG("LoadDNAFile: cannot open %s\n", dnaPath);
        return true;
      }
      std::stringstream ss;
      ss << file.rdbuf();
      file.close();
      std::string content = ss.str();

      GenoProfile profile;
      ParseGenoProfileJSON(content.c_str(), profile);

      mCurrentSourceProfile = profile;
      mTransferEngine.SetSourceProfile(profile);
      SendGenoProfileToUI(profile, "source");
      PushHistory(profile);
      mProfileLibrary.push_back(profile);
      return true;
    }

    default:
      return false;
  }
}

void GenoPlugin::OnParamChange(int paramIdx) {
  auto setAmount = [&](int paramIdx, GenoGene gene) {
    mTransferParams.amounts[static_cast<int>(gene)] =
      GetParam(paramIdx)->Value() / 100.0;
  };

  auto setLock = [&](int paramIdx, GenoGene gene) {
    mTransferParams.locks[static_cast<int>(gene)] =
      GetParam(paramIdx)->Int() != 0;
  };

  switch (paramIdx) {
    case kToneAmount: setAmount(paramIdx, GenoGene::Tone); break;
    case kDynamicsAmount: setAmount(paramIdx, GenoGene::Dynamics); break;
    case kNoiseAmount: setAmount(paramIdx, GenoGene::Noise); break;
    case kStereoSpatialAmount: setAmount(paramIdx, GenoGene::Stereo); break;
    case kTextureAmount: setAmount(paramIdx, GenoGene::Texture); break;
    case kAirAmount: setAmount(paramIdx, GenoGene::Air); break;
    case kMovementAmount: setAmount(paramIdx, GenoGene::Movement); break;
    case kSpaceAmount: setAmount(paramIdx, GenoGene::Space); break;
    case kGlueAmount: setAmount(paramIdx, GenoGene::Glue); break;
    case kPunchAmount: setAmount(paramIdx, GenoGene::Punch); break;
    case kBodyAmount: setAmount(paramIdx, GenoGene::Body); break;
    case kResonanceAmount: setAmount(paramIdx, GenoGene::Resonance); break;
    case kWarmthAmount: setAmount(paramIdx, GenoGene::Warmth); break;
    case kSparkleAmount: setAmount(paramIdx, GenoGene::Sparkle); break;

    case kToneLock: setLock(paramIdx, GenoGene::Tone); break;
    case kDynamicsLock: setLock(paramIdx, GenoGene::Dynamics); break;
    case kNoiseLock: setLock(paramIdx, GenoGene::Noise); break;
    case kStereoSpatialLock: setLock(paramIdx, GenoGene::Stereo); break;
    case kTextureLock: setLock(paramIdx, GenoGene::Texture); break;
    case kAirLock: setLock(paramIdx, GenoGene::Air); break;
    case kMovementLock: setLock(paramIdx, GenoGene::Movement); break;
    case kSpaceLock: setLock(paramIdx, GenoGene::Space); break;
    case kGlueLock: setLock(paramIdx, GenoGene::Glue); break;
    case kPunchLock: setLock(paramIdx, GenoGene::Punch); break;
    case kBodyLock: setLock(paramIdx, GenoGene::Body); break;
    case kResonanceLock: setLock(paramIdx, GenoGene::Resonance); break;
    case kWarmthLock: setLock(paramIdx, GenoGene::Warmth); break;
    case kSparkleLock: setLock(paramIdx, GenoGene::Sparkle); break;

    case kMorphPosition: {
      mMorphEngine.SetMorphPosition(GetParam(kMorphPosition)->Value() / 100.0);
      if (mMorphEngine.HasPoints()) {
        auto morphed = mMorphEngine.GetCurrentMorph();
        mTransferEngine.SetSourceProfile(morphed);
      }
      break;
    }
  }

  mTransferEngine.SetTransferParams(mTransferParams);
}

void GenoPlugin::PushHistory(const GenoProfile& profile) {
  mHistoryPosition++;
  if (mHistoryPosition < (int)mHistory.size()) {
    mHistory.resize(mHistoryPosition);
  }
  mHistory.push_back(profile);
}

void GenoPlugin::UpdateProcessors() {
  mTransferEngine.SetTransferParams(mTransferParams);
}

void GenoPlugin::SendCaptureStatus() {
  std::ostringstream json;
  json << "{\"capturing\":" << (mCapturing.load(std::memory_order_relaxed) ? "true" : "false") << "}";
  WDL_String str(json.str().c_str());
  SendArbitraryMsgFromDelegate(kMsgTagCaptureStatus, str.GetLength(), str.Get());
}

void GenoPlugin::SendParametersToUI() {
  std::ostringstream json;
  json << "{";
  for (int i = 0; i < kNumParams; ++i) {
    if (i > 0) json << ",";
    json << "\"" << i << "\":{";
    json << "\"name\":\"" << GetParam(i)->GetName() << "\",";
    json << "\"value\":" << GetParam(i)->Value() << ",";
    json << "\"min\":" << 0.0 << ",";
    json << "\"max\":" << 100.0 << ",";
    json << "\"default\":" << GetParam(i)->GetDefault();
    json << "}";
  }
  json << "}";

  WDL_String str(json.str().c_str());
  SendArbitraryMsgFromDelegate(kMsgTagRequestParameters, str.GetLength(), str.Get());
}

void GenoPlugin::SendAnalyzerReportToUI(const GenoProfile& profile) {
  std::ostringstream json;
  json << "{";
  json << "\"type\":\"analyzer\",";
  json << "\"instrument\":\"" << profile.instrument << "\",";
  json << "\"confidence\":" << profile.confidence << ",";
  json << "\"pitch\":" << profile.spectral.pitch << ",";
  json << "\"brightness\":" << profile.spectral.brightness << ",";
  json << "\"dynamicRange\":" << profile.dynamics.dynamicRange << ",";
  json << "\"stereoWidth\":" << profile.stereo.width << ",";
  json << "\"noiseFloor\":" << profile.noise.noiseFloorDb;

  json << ",\"spectralEnvelope\":[";
  for (size_t i = 0; i < profile.spectral.spectralEnvelope.size(); ++i) {
    if (i > 0) json << ",";
    json << profile.spectral.spectralEnvelope[i];
  }
  json << "]";

  json << ",\"harmonicProfile\":[";
  for (size_t i = 0; i < profile.spectral.harmonicProfile.size(); ++i) {
    if (i > 0) json << ",";
    json << profile.spectral.harmonicProfile[i];
  }
  json << "]";

  json << "}";

  WDL_String str(json.str().c_str());
  SendArbitraryMsgFromDelegate(kMsgTagSendAnalyzerReport, str.GetLength(), str.Get());
}

void GenoPlugin::SendGenoProfileToUI(const GenoProfile& profile, const char* type) {
  std::ostringstream json;
  json << "{";
  json << "\"type\":\"" << type << "\",";
  json << "\"name\":\"" << profile.sourceName << "\",";
  json << "\"instrument\":\"" << profile.instrument << "\",";
  json << "\"category\":\"" << profile.category << "\",";
  json << "\"confidence\":" << profile.confidence;
  json << ",\"features\":{";
  json << "\"centroid\":" << profile.spectral.centroid << ",";
  json << "\"brightness\":" << profile.spectral.brightness << ",";
  json << "\"flatness\":" << profile.spectral.spectralFlatness << ",";
  json << "\"dynamicRange\":" << profile.dynamics.dynamicRange << ",";
  json << "\"crestFactor\":" << profile.dynamics.crestFactor << ",";
  json << "\"stereoWidth\":" << profile.stereo.width << ",";
  json << "\"phaseCorrelation\":" << profile.stereo.phaseCorrelation << ",";
  json << "\"noiseFloor\":" << profile.noise.noiseFloorDb << ",";
  json << "\"saturation\":" << profile.texture.saturationAmount << ",";
  json << "\"distortion\":" << profile.texture.harmonicDistortion << ",";
  json << "\"analogWarmth\":" << profile.texture.analogWarmth << ",";
  json << "\"tapeSaturation\":" << profile.texture.tapeSaturation << ",";
  json << "\"spectralTilt\":" << profile.noise.spectralTilt << ",";
  json << "\"pitch\":" << profile.spectral.pitch << ",";
  json << "\"rolloff\":" << profile.spectral.rolloff << ",";
  json << "\"spread\":" << profile.spectral.spread;
  json << "}";

  json << ",\"spectralEnvelope\":[";
  for (size_t i = 0; i < profile.spectral.spectralEnvelope.size(); ++i) {
    if (i > 0) json << ",";
    json << profile.spectral.spectralEnvelope[i];
  }
  json << "]";

  json << ",\"harmonicProfile\":[";
  for (size_t i = 0; i < profile.spectral.harmonicProfile.size(); ++i) {
    if (i > 0) json << ",";
    json << profile.spectral.harmonicProfile[i];
  }
  json << "]";

  json << ",\"noiseShape\":[";
  for (int i = 0; i < kNumNoiseCoefs; ++i) {
    if (i > 0) json << ",";
    json << profile.noise.noiseShape[i];
  }
  json << "]";

  json << "}";

  WDL_String str(json.str().c_str());
  SendArbitraryMsgFromDelegate(kMsgTagSendGenoProfile, str.GetLength(), str.Get());
}

#ifdef WEBVIEW_EDITOR_DELEGATE
bool GenoPlugin::CanNavigateToURL(const char* url) {
  return true;
}

bool GenoPlugin::OnCanDownloadMIMEType(const char* mimeType) {
  return std::string_view(mimeType) != "text/html";
}
#endif
