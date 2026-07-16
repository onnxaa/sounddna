#pragma once
#include "Geno_Types.h"
#include <vector>

struct MorphPoint {
  GenoProfile profile;
  double position; // 0.0 to 1.0 along the morph path
  double blendAmount; // relative weight
};

class MorphEngine {
public:
  MorphEngine();
  ~MorphEngine() = default;

  void Reset();
  void AddPoint(const GenoProfile& profile, double position, double blend = 1.0);
  void Clear();
  void SetMorphPosition(double position); // 0.0 to 1.0

  GenoProfile GetCurrentMorph() const;
  std::vector<MorphPoint> GetPoints() const { return mMorphPoints; }

private:
  std::vector<MorphPoint> mMorphPoints;
  double mMorphPosition = 0.0;

  GenoProfile InterpolateProfiles(const GenoProfile& a, const GenoProfile& b,
                                  double t) const;
};
