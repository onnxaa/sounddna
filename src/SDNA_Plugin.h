#pragma once
#include "IPlug_include_in_plug_hdr.h"

BEGIN_IPLUG_NAMESPACE
extern const char* kSoundDNAUIHTML;
END_IPLUG_NAMESPACE
#include "DSP/SDNA_Types.h"
#include "SDNA_Params.h"
#include "DSP/SDNA_TransferEngine.h"
#include "DSP/SDNA_MorphEngine.h"
#include "DSP/SDNA_Analyzer.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

using namespace iplug;

class SoundDNA final : public Plugin {
public:
  SoundDNA(const InstanceInfo& info);

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
  DNAAnalyzer mAnalyzer;

  DNAProfile mCurrentTargetProfile;
  DNAProfile mCurrentSourceProfile;
  DNAProfile mCurrentInputProfile;
  DNATransferParams mTransferParams;

  std::vector<DNAProfile> mDNALibrary;
  std::vector<DNAProfile> mHistory;
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
  DNAProfile mPendingProfile;
  int mPendingTag = 0; // 0=source, 1=target

  void RunAnalysis(int numSamples, bool isStereo, DNAProfile& out);
  bool ValidateBufferEnergy(const float* audio, int numSamples);
  void ProcessMorph();
  void UpdateProcessors();
  void PushHistory(const DNAProfile& profile);
  void SendAnalyzerReportToUI(const DNAProfile& profile);
  void SendDNAProfileToUI(const DNAProfile& profile, const char* type);
  void SendParametersToUI();
  void SendCaptureStatus();
};
