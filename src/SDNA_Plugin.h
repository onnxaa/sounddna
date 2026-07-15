#pragma once
#include "IPlug_include_in_plug_hdr.h"
#include "DSP/SDNA_Types.h"
#include "SDNA_Params.h"
#include "DSP/SDNA_TransferEngine.h"
#include "DSP/SDNA_MorphEngine.h"
#include "DSP/SDNA_Analyzer.h"
#include <vector>
#include <deque>
#include <string>

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

  struct AnalysisBuffer {
    std::deque<float> bufferL;
    std::deque<float> bufferR;
    static constexpr int kMaxBufferSize = 44100 * 5; // 5 seconds
  } mAnalysisBuf;

  void ProcessMorph();
  void UpdateProcessors();
  void PushHistory(const DNAProfile& profile);
  void SendAnalyzerReportToUI(const DNAProfile& profile);
  void SendDNAProfileToUI(const DNAProfile& profile, const char* type);
  void SendParametersToUI();
};
