// Tell emacs that this is a C++ source
//  -*- C++ -*-.
#ifndef G4MAIN_PHG4PARTICLEV2_H
#define G4MAIN_PHG4PARTICLEV2_H

#include "PHG4Particlev1.h"

#include <iostream>
#include <limits>
#include <string>

class PHG4Particle;

class PHG4Particlev2 : public PHG4Particlev1
{
 public:
  PHG4Particlev2() = default;
  PHG4Particlev2(const std::string &name, const int pid, const double px, const double py, const double pz);
  PHG4Particlev2(const PHG4Particle *in);

  ~PHG4Particlev2() override = default;

  PHObject *CloneMe() const override { return new PHG4Particlev2(*this); }
  void identify(std::ostream &os = std::cout) const override;

  int get_track_id() const override { return trkid; }
  int get_vtx_id() const override { return vtxid; }
  int get_parent_id() const override { return parentid; }
  int get_primary_id() const override { return primaryid; }
  double get_e() const override { return fe; }

  void set_track_id(const int i) override { trkid = i; }
  void set_vtx_id(const int i) override { vtxid = i; }
  void set_parent_id(const int i) override { parentid = i; }
  void set_primary_id(const int i) override { primaryid = i; }
  void set_e(const double e) override { fe = e; }

 protected:
  int trkid{0};
  int vtxid{0};
  int parentid{0};
  int primaryid{std::numeric_limits<int>::min()};
  double fe{0};

  ClassDefOverride(PHG4Particlev2, 2)
};

#endif
