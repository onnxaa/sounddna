#pragma once
#include "SDNA_Types.h"
#include <vector>

struct MorphPoint {
  DNAProfile profile;
  double position; // 0.0 to 1.0 along the morph path
  double blendAmount; // relative weight
};

class MorphEngine {
public:
  MorphEngine();
  ~MorphEngine() = default;

  void Reset();
  void AddPoint(const DNAProfile& profile, double position, double blend = 1.0);
  void Clear();
  void SetMorphPosition(double position); // 0.0 to 1.0

  DNAProfile GetCurrentMorph() const;
  std::vector<MorphPoint> GetPoints() const { return mMorphPoints; }

private:
  std::vector<MorphPoint> mMorphPoints;
  double mMorphPosition = 0.0;

  DNAProfile InterpolateProfiles(const DNAProfile& a, const DNAProfile& b,
                                  double t) const;
};
