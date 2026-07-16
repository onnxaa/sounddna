#include "SDNA_Plugin.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include <cstring>
#include <sstream>
#include <algorithm>

SoundDNA::SoundDNA(const InstanceInfo& info)
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

  // Handle audio file drops from helper process
  SetAudioDropHandler([this](const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { DBGMSG("AUDIO_DROP: cannot open %s\n", path); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 44) { fclose(f); return; } // too small
    rewind(f);
    auto* buf = (unsigned char*)malloc(sz);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, sz, f);
    fclose(f);
    if (got < 44) { free(buf); return; }
    // Pass raw WAV data to OnMessage for analysis
    OnMessage(kMsgTagAnalyzeTarget, -1, (int)got, buf);
    free(buf);
    unlink(path);
  });

  mEditorInitFunc = [&]() {
    LoadHTML(kSoundDNAUIHTML);
    EnableScroll(false);
    SendParametersToUI();
  };
#endif
}

#if IPLUG_DSP
void SoundDNA::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
  const double inputGain = GetParam(kInputGain)->DBToAmp();
  const double outputGain = GetParam(kOutputGain)->DBToAmp();
  const double mix = GetParam(kMix)->Value() / 100.0;

  sample* inL = inputs[0];
  sample* inR = inputs[1];
  sample* outL = outputs[0];
  sample* outR = outputs[1];

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

  for (int s = 0; s < nFrames; s++) {
    mAnalysisBuf.bufferL.push_back(dryL[s]);
    mAnalysisBuf.bufferR.push_back(dryR[s]);
  }
  while ((int)mAnalysisBuf.bufferL.size() > AnalysisBuffer::kMaxBufferSize) {
    mAnalysisBuf.bufferL.pop_front();
    mAnalysisBuf.bufferR.pop_front();
  }

  std::vector<float> wetL(nFrames), wetR(nFrames);
  mTransferEngine.Process(dryL.data(), dryR.data(),
                           wetL.data(), wetR.data(), nFrames, true);

  for (int s = 0; s < nFrames; s++) {
    float blendedL = dryL[s] * (1.f - (float)mix) + wetL[s] * (float)mix;
    float blendedR = dryR[s] * (1.f - (float)mix) + wetR[s] * (float)mix;
    outL[s] = blendedL * (float)outputGain;
    outR[s] = blendedR * (float)outputGain;
  }
}

void SoundDNA::OnReset() {
  mSampleRate = GetSampleRate();
  mTransferEngine.SetSampleRate(mSampleRate);
  mAnalyzer.SetSampleRate(mSampleRate);
}
#endif

bool SoundDNA::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) {
  switch (msgTag) {
    case kMsgTagRequestParameters: {
      SendParametersToUI();
      return true;
    }

    case kMsgTagAnalyzeSource: {
      if (!mAnalysisBuf.bufferL.empty()) {
        int numSamples = (int)mAnalysisBuf.bufferL.size();
        std::vector<float> audioL(numSamples), audioR(numSamples);
        for (int i = 0; i < numSamples; ++i) {
          audioL[i] = mAnalysisBuf.bufferL[i];
          audioR[i] = mAnalysisBuf.bufferR[i];
        }

        DNAProfile profile;
        mTransferEngine.AnalyzeInput(audioL.data(), audioR.data(),
                                      numSamples, true, profile);
        mCurrentSourceProfile = profile;
        mTransferEngine.SetSourceProfile(profile);
        SendAnalyzerReportToUI(profile);
        SendDNAProfileToUI(profile, "source");
        PushHistory(profile);
      }
      return true;
    }

    case kMsgTagAnalyzeTarget: {
      if (!mAnalysisBuf.bufferL.empty()) {
        int numSamples = (int)mAnalysisBuf.bufferL.size();
        std::vector<float> audioL(numSamples), audioR(numSamples);
        for (int i = 0; i < numSamples; ++i) {
          audioL[i] = mAnalysisBuf.bufferL[i];
          audioR[i] = mAnalysisBuf.bufferR[i];
        }

        DNAProfile profile;
        mTransferEngine.AnalyzeInput(audioL.data(), audioR.data(),
                                      numSamples, true, profile);
        mCurrentTargetProfile = profile;
        mTransferEngine.SetTargetProfile(profile);
        SendAnalyzerReportToUI(profile);
        SendDNAProfileToUI(profile, "target");
        PushHistory(profile);
      }
      return true;
    }

    case kMsgTagBrowserSearch: {
      const char* json = static_cast<const char*>(pData);
      DBGMSG("Browser search: %s\n", json);
      return true;
    }

    case kMsgTagBrowserSelect: {
      const char* json = static_cast<const char*>(pData);
      DBGMSG("Browser select: %s\n", json);
      return true;
    }

    case kMsgTagLoadDNA: {
      const char* json = static_cast<const char*>(pData);
      DBGMSG("Load DNA: %s\n", json);
      return true;
    }

    case kMsgTagSaveDNA: {
      const char* json = static_cast<const char*>(pData);
      DBGMSG("Save DNA: %s\n", json);
      return true;
    }

    case kMsgTagCompareProfiles: {
      const char* json = static_cast<const char*>(pData);
      DBGMSG("Compare: %s\n", json);
      return true;
    }

    case kMsgTagMorphAdd: {
      const char* json = static_cast<const char*>(pData);
      DBGMSG("Morph add: %s\n", json);
      return true;
    }

    case kMsgTagMorphClear: {
      mMorphEngine.Clear();
      return true;
    }

    default:
      return false;
  }
}

void SoundDNA::OnParamChange(int paramIdx) {
  auto setAmount = [&](int paramIdx, DNAGene gene) {
    mTransferParams.amounts[static_cast<int>(gene)] =
      GetParam(paramIdx)->Value() / 100.0;
  };

  auto setLock = [&](int paramIdx, DNAGene gene) {
    mTransferParams.locks[static_cast<int>(gene)] =
      GetParam(paramIdx)->Int() != 0;
  };

  switch (paramIdx) {
    case kToneAmount: setAmount(paramIdx, DNAGene::Tone); break;
    case kDynamicsAmount: setAmount(paramIdx, DNAGene::Dynamics); break;
    case kNoiseAmount: setAmount(paramIdx, DNAGene::Noise); break;
    case kStereoSpatialAmount: setAmount(paramIdx, DNAGene::Stereo); break;
    case kTextureAmount: setAmount(paramIdx, DNAGene::Texture); break;
    case kAirAmount: setAmount(paramIdx, DNAGene::Air); break;
    case kMovementAmount: setAmount(paramIdx, DNAGene::Movement); break;
    case kSpaceAmount: setAmount(paramIdx, DNAGene::Space); break;
    case kGlueAmount: setAmount(paramIdx, DNAGene::Glue); break;
    case kPunchAmount: setAmount(paramIdx, DNAGene::Punch); break;
    case kBodyAmount: setAmount(paramIdx, DNAGene::Body); break;
    case kResonanceAmount: setAmount(paramIdx, DNAGene::Resonance); break;
    case kWarmthAmount: setAmount(paramIdx, DNAGene::Warmth); break;
    case kSparkleAmount: setAmount(paramIdx, DNAGene::Sparkle); break;

    case kToneLock: setLock(paramIdx, DNAGene::Tone); break;
    case kDynamicsLock: setLock(paramIdx, DNAGene::Dynamics); break;
    case kNoiseLock: setLock(paramIdx, DNAGene::Noise); break;
    case kStereoSpatialLock: setLock(paramIdx, DNAGene::Stereo); break;
    case kTextureLock: setLock(paramIdx, DNAGene::Texture); break;
    case kAirLock: setLock(paramIdx, DNAGene::Air); break;
    case kMovementLock: setLock(paramIdx, DNAGene::Movement); break;
    case kSpaceLock: setLock(paramIdx, DNAGene::Space); break;
    case kGlueLock: setLock(paramIdx, DNAGene::Glue); break;
    case kPunchLock: setLock(paramIdx, DNAGene::Punch); break;
    case kBodyLock: setLock(paramIdx, DNAGene::Body); break;
    case kResonanceLock: setLock(paramIdx, DNAGene::Resonance); break;
    case kWarmthLock: setLock(paramIdx, DNAGene::Warmth); break;
    case kSparkleLock: setLock(paramIdx, DNAGene::Sparkle); break;

    case kMorphPosition: {
      mMorphEngine.SetMorphPosition(GetParam(kMorphPosition)->Value() / 100.0);
      auto morphed = mMorphEngine.GetCurrentMorph();
      mTransferEngine.SetSourceProfile(morphed);
      break;
    }
  }

  mTransferEngine.SetTransferParams(mTransferParams);
}

void SoundDNA::PushHistory(const DNAProfile& profile) {
  mHistoryPosition++;
  if (mHistoryPosition < (int)mHistory.size()) {
    mHistory.resize(mHistoryPosition);
  }
  mHistory.push_back(profile);
}

void SoundDNA::UpdateProcessors() {
  mTransferEngine.SetTransferParams(mTransferParams);
}

void SoundDNA::SendParametersToUI() {
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

void SoundDNA::SendAnalyzerReportToUI(const DNAProfile& profile) {
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

void SoundDNA::SendDNAProfileToUI(const DNAProfile& profile, const char* type) {
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
  json << "\"distortion\":" << profile.texture.harmonicDistortion;
  json << "}";
  json << "}";

  WDL_String str(json.str().c_str());
  SendArbitraryMsgFromDelegate(kMsgTagSendDNAProfile, str.GetLength(), str.Get());
}

#ifdef WEBVIEW_EDITOR_DELEGATE
bool SoundDNA::CanNavigateToURL(const char* url) {
  return true;
}

bool SoundDNA::OnCanDownloadMIMEType(const char* mimeType) {
  return std::string_view(mimeType) != "text/html";
}
#endif
