#pragma once
#include "IPlug_include_in_plug_hdr.h"

BEGIN_IPLUG_NAMESPACE
extern const char* kGenoUIHTML;
END_IPLUG_NAMESPACE
#include "DSP/Geno_Types.h"
#include "Geno_Params.h"
#include "DSP/Geno_TransferEngine.h"
#include "DSP/Geno_MorphEngine.h"
#include "DSP/Geno_Analyzer.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

using namespace iplug;

class GenoPlugin final : public Plugin {
public:
  GenoPlugin(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;
  void OnIdle() override;

#ifdef WEBVIEW_EDITOR_DELEGATE
  bool CanNavigateToURL(const char* url);
  bool OnCanDownloadMIMEType(const char* mimeType) override;
#endif

private:
  TransferEngine mTransferEngine;
  MorphEngine mMorphEngine;
  GenoAnalyzer mAnalyzer;

  GenoProfile mCurrentTargetProfile;
  GenoProfile mCurrentSourceProfile;
  GenoProfile mCurrentInputProfile;
  GenoTransferParams mTransferParams;

  std::vector<GenoProfile> mProfileLibrary;
  std::vector<GenoProfile> mHistory;
  int mHistoryPosition = -1;

  bool mBypass = false;
  double mSampleRate = 44100.0;

  // Circular buffer for audio capture
  static constexpr int kMaxBufferSamples = 44100 * 5; // 5 seconds
  std::vector<float> mCircBufL{kMaxBufferSamples, 0.f};
  std::vector<float> mCircBufR{kMaxBufferSamples, 0.f};
  std::atomic<int> mCircWritePos{0};
  std::atomic<int> mCircAvail{0};
  std::atomic<bool> mCapturing{false};

  // Async analysis
  std::mutex mAnalysisMutex;
  std::atomic<bool> mAnalysisPending{false};
  GenoProfile mPendingProfile;
  int mPendingTag = 0; // 0=source, 1=target

  void RunAnalysis(int numSamples, bool isStereo, GenoProfile& out);
  bool ValidateBufferEnergy(const float* audio, int numSamples);
  void ProcessMorph();
  void UpdateProcessors();
  void PushHistory(const GenoProfile& profile);
  void SendAnalyzerReportToUI(const GenoProfile& profile);
  void SendGenoProfileToUI(const GenoProfile& profile, const char* type);
  void SendParametersToUI();
  void SendCaptureStatus();
};
