// Tell emacs that this is a C++ source
//  -*- C++ -*-.
#ifndef TPCCENTRALMEMBRANEMATCHING_H
#define TPCCENTRALMEMBRANEMATCHING_H

/**
 * \file TpcCentralMembraneMatching.h
 * \brief match reconstructed CM clusters to CM pads, calculate differences, store on the node tree and compute distortion reconstruction maps
 * \author Tony Frawley <frawley@fsunuc.physics.fsu.edu>, Hugo Pereira Da Costa <hugo.pereira-da-costa@cea.fr>
 */

#include <fun4all/SubsysReco.h>
#include <tpc/TpcDistortionCorrection.h>
#include <tpc/TpcDistortionCorrectionContainer.h>
#include <trackbase/TrkrClusterContainer.h>
#include <trackbase/TrkrDefs.h>

#include <memory>
#include <string>

class PHCompositeNode;
// class CMFlashClusterContainer;
class CMFlashDifferenceContainer;
class LaserClusterContainer;

class TF1;
class TFile;
class TH1F;
class TH1D;
class TH2F;
class TGraph;
class TNtuple;
class TTree;
class TVector3;

class TpcCentralMembraneMatching : public SubsysReco
{
 public:
  TpcCentralMembraneMatching(const std::string &name = "TpcCentralMembraneMatching");

  ~TpcCentralMembraneMatching() override = default;

  /// set to true to store evaluation histograms and ntuples
  void setSavehistograms(bool value)
  {
    m_savehistograms = value;
  }

  /// output file name for evaluation histograms
  void setHistogramOutputfile(const std::string &outputfile)
  {
    m_histogramfilename = outputfile;
  }

  /// output file name for storing the space charge reconstruction matrices
  void setOutputfile(const std::string &outputfile)
  {
    m_outputfile = outputfile;
  }

  void setDebugOutputFile(const std::string &debugfile)
  {
    m_debugfilename = debugfile;
  }

  void setNMatchIter(int val) { m_nMatchIter = val; }

  void set_useOnly_nClus2(bool val) { m_useOnly_nClus2 = val; }

  void set_grid_dimensions(int phibins, int rbins);

  //! run initialization
  int InitRun(PHCompositeNode *topNode) override;

  //! event processing
  int process_event(PHCompositeNode *topNode) override;

  //! event resetting
  int ResetEvent(PHCompositeNode *topNode) override;

  //! end of process
  int End(PHCompositeNode *topNode) override;

 private:
  int GetNodes(PHCompositeNode *topNode);

  /// tpc distortion correction utility class
  TpcDistortionCorrection m_distortionCorrection;

  // CMFlashClusterContainer *m_corrected_CMcluster_map{nullptr};
  LaserClusterContainer *m_corrected_CMcluster_map{nullptr};
  CMFlashDifferenceContainer *m_cm_flash_diffs{nullptr};

  /// static distortion container
  /** used in input to correct CM clusters before calculating residuals */
  TpcDistortionCorrectionContainer *m_dcc_in_static{nullptr};
  TpcDistortionCorrectionContainer *m_dcc_in_average{nullptr};

  /// fluctuation distortion container
  /** used in output to write fluctuation distortions */
  TpcDistortionCorrectionContainer *m_dcc_out{nullptr};

  /// local fluctuation distortion container
  /*
   * this one is used to aggregate multple CM events in a single map
   * it is not stored on the node tree but saved to a dedicated file at the end of the run
   */
  std::unique_ptr<TpcDistortionCorrectionContainer> m_dcc_out_aggregated;

  /// output file, to which aggregated central membrane distortion corrections are stored
  std::string m_outputfile = "CMDistortionCorrections.root";

  ///@name evaluation histograms
  //@{
  bool m_savehistograms = false;
  std::string m_histogramfilename = "TpcCentralMembraneMatching.root";

  TH2F *hxy_reco = nullptr;
  TH2F *hxy_truth = nullptr;
  TH2F *hdrdphi = nullptr;
  TH2F *hrdr = nullptr;
  TH2F *hrdphi = nullptr;
  TH1F *hdrphi = nullptr;
  TH1F *hdphi = nullptr;
  TH1F *hdr1_single = nullptr;
  TH1F *hdr2_single = nullptr;
  TH1F *hdr3_single = nullptr;
  TH1F *hdr1_double = nullptr;
  TH1F *hdr2_double = nullptr;
  TH1F *hdr3_double = nullptr;
  TH1F *hnclus = nullptr;

  std::unique_ptr<TFile> fout;

  std::unique_ptr<TFile> m_debugfile;
  std::string m_debugfilename = "CMMatcher.root";

  TH2F *hit_r_phi{};

  TH2F *clust_r_phi_pos{};
  TH2F *clust_r_phi_neg{};

  TNtuple *match_ntup = nullptr;
  TTree *phiTree = nullptr;

  int m_event_index = 0;

  //@}

  /// radius cut for matching clusters to pad, for size 2 clusters
  //  double m_rad_cut= 0.5;

  /// phi cut for matching clusters to pad
  /** TODO: this will need to be adjusted to match beam-induced time averaged distortions */
  double m_phi_cut = 0.02;

  ///@name distortion correction histograms
  //@{

  /// distortion correction grid size along phi
  int m_phibins = 24;

  static constexpr float m_phiMin = 0;
  static constexpr float m_phiMax = 2. * M_PI;

  /// distortion correction grid size along r
  int m_rbins = 12;

  static constexpr float m_rMin = 20;  // cm
  static constexpr float m_rMax = 80;  // cm

  //@}

  ///@name central membrane pads definitions
  //@{
  static constexpr double mm = 1.0;
  static constexpr double cm = 10.0;

  static constexpr int nRadii = 8;
  static constexpr int nStripes_R1 = 6;
  static constexpr int nStripes_R2 = 8;
  static constexpr int nStripes_R3 = 12;

  static constexpr int nPads_R1 = 6 * 16;
  static constexpr int nPads_R2 = 8 * 16;
  static constexpr int nPads_R3 = 12 * 16;

  /// stripe radii
  static constexpr std::array<double, nRadii> R1_e = {{227.0902789 * mm, 238.4100043 * mm, 249.7297296 * mm, 261.049455 * mm, 272.3691804 * mm, 283.6889058 * mm, 295.0086312 * mm, 306.3283566 * mm}};
  static constexpr std::array<double, nRadii> R1 = {{317.648082 * mm, 328.9678074 * mm, 340.2875328 * mm, 351.6072582 * mm, 362.9269836 * mm, 374.246709 * mm, 385.5664344 * mm, 396.8861597 * mm}};
  static constexpr std::array<double, nRadii> R2 = {{421.705532 * mm, 442.119258 * mm, 462.532984 * mm, 482.9467608 * mm, 503.36069 * mm, 523.774416 * mm, 544.188015 * mm, 564.601868 * mm}};
  static constexpr std::array<double, nRadii> R3 = {{594.6048725 * mm, 616.545823 * mm, 638.4867738 * mm, 660.4277246 * mm, 682.3686754 * mm, 704.3096262 * mm, 726.250577 * mm, 748.1915277 * mm}};

  double cx1_e[nStripes_R1][nRadii]{};
  double cx1[nStripes_R1][nRadii]{};
  double cx2[nStripes_R2][nRadii]{};
  double cx3[nStripes_R3][nRadii]{};

  double cy1_e[nStripes_R1][nRadii]{};
  double cy1[nStripes_R1][nRadii]{};
  double cy2[nStripes_R2][nRadii]{};
  double cy3[nStripes_R3][nRadii]{};

  // Check which stripes get removed
  std::array<int, nRadii> nGoodStripes_R1_e = {};
  std::array<int, nRadii> nGoodStripes_R1 = {};
  std::array<int, nRadii> nGoodStripes_R2 = {};
  std::array<int, nRadii> nGoodStripes_R3 = {};

  /// min stripe index
  static constexpr std::array<int, nRadii> keepThisAndAfter = {{1, 0, 1, 0, 1, 0, 1, 0}};

  /// max stripe index
  static constexpr std::array<int, nRadii> keepUntil_R1_e = {{4, 4, 5, 4, 5, 5, 5, 5}};
  static constexpr std::array<int, nRadii> keepUntil_R1 = {{5, 5, 6, 5, 6, 5, 6, 5}};
  static constexpr std::array<int, nRadii> keepUntil_R2 = {{7, 7, 8, 7, 8, 8, 8, 8}};
  static constexpr std::array<int, nRadii> keepUntil_R3 = {{11, 10, 11, 11, 11, 11, 12, 11}};

  std::array<int, nRadii> nStripesIn_R1_e = {};
  std::array<int, nRadii> nStripesIn_R1 = {};
  std::array<int, nRadii> nStripesIn_R2 = {};
  std::array<int, nRadii> nStripesIn_R3 = {};
  std::array<int, nRadii> nStripesBefore_R1_e = {};
  std::array<int, nRadii> nStripesBefore_R1 = {};
  std::array<int, nRadii> nStripesBefore_R2 = {};
  std::array<int, nRadii> nStripesBefore_R3 = {};

  static constexpr int nStripesPerPetal = 213;
  static constexpr int nPetals = 18;
  static constexpr int nTotStripes = nStripesPerPetal * nPetals;

  void CalculateCenters(
      int nPads,
      const std::array<double, nRadii> &R,
      std::array<int, nRadii> &nGoodStripes,
      const std::array<int, nRadii> &keepUntil,
      std::array<int, nRadii> &nStripesIn,
      std::array<int, nRadii> &nStripesBefore,
      double cx[][nRadii], double cy[][nRadii]);

  /// store centers of all central membrane pads
  std::vector<TVector3> m_truth_pos;

  std::vector<double> m_truth_RPeaks = {22.709, 23.841, 24.973, 26.1049, 27.2369, 28.3689, 29.5009, 30.6328, 31.7648, 32.8968, 34.0288, 35.1607, 36.2927, 37.4247, 38.5566, 39.6886, 42.1706, 44.2119, 46.2533, 48.2947, 50.3361, 52.3774, 54.4188, 56.4602, 59.4605, 61.6546, 63.8487, 66.0428, 68.2369, 70.431, 72.6251, 74.8192};

  std::vector<std::vector<double>> m_truth_phiPeaks = {{0.79563, 0.870629, 0.945628}, {0.756123, 0.829115, 0.902107, 0.975098}, {0.789881, 0.861048, 0.932214, 1.00338}, {0.752631, 0.822131, 0.89163, 0.96113}, {0.785089, 0.85306, 0.921031, 0.989002}, {0.749697, 0.816261, 0.882826, 0.949391, 1.01596}, {0.781031, 0.846298, 0.911564, 0.97683}, {0.747196, 0.811259, 0.875323, 0.939387, 1.00345}, {0.777552, 0.840499, 0.903446, 0.966393}, {0.745039, 0.806946, 0.868853, 0.93076, 0.992667}, {0.774536, 0.835473, 0.896409, 0.957345, 1.01828}, {0.74316, 0.803188, 0.863216, 0.923244, 0.983272}, {0.771896, 0.831073, 0.89025, 0.949426, 1.0086}, {0.741508, 0.799885, 0.858261, 0.916638, 0.975014}, {0.769567, 0.82719, 0.884813, 0.942437, 1.00006}, {0.740045, 0.796958, 0.853871, 0.910785, 0.967698}, {0.754094, 0.801403, 0.848711, 0.896019, 0.943328, 0.990636}, {0.729389, 0.775646, 0.821904, 0.868161, 0.914418, 0.960675, 1.00693}, {0.75108, 0.796379, 0.841678, 0.886977, 0.932276, 0.977575, 1.02287}, {0.727553, 0.771975, 0.816397, 0.860818, 0.90524, 0.949662, 0.994083}, {0.748555, 0.79217, 0.835786, 0.879401, 0.923017, 0.966632, 1.01025}, {0.726004, 0.768876, 0.811748, 0.85462, 0.897493, 0.940365, 0.983237, 1.02611}, {0.746409, 0.788593, 0.830778, 0.872963, 0.915147, 0.957332, 0.999517}, {0.724679, 0.766225, 0.807772, 0.849319, 0.890866, 0.932413, 0.973959, 1.01551}, {0.731893, 0.764401, 0.796908, 0.829416, 0.861924, 0.894431, 0.926939, 0.959447, 0.991954, 1.02446}, {0.715065, 0.746998, 0.778931, 0.810864, 0.842797, 0.87473, 0.906663, 0.938596, 0.970529, 1.00246}, {0.730229, 0.761627, 0.793025, 0.824423, 0.855821, 0.887219, 0.918617, 0.950015, 0.981413, 1.01281}, {0.71403, 0.744929, 0.775827, 0.806726, 0.837624, 0.868523, 0.899421, 0.93032, 0.961218, 0.992117, 1.02302}, {0.728778, 0.759209, 0.789641, 0.820072, 0.850503, 0.880934, 0.911365, 0.941796, 0.972227, 1.00266}, {0.713125, 0.743117, 0.77311, 0.803103, 0.833096, 0.863089, 0.893082, 0.923074, 0.953067, 0.98306, 1.01305}, {0.727503, 0.757084, 0.786665, 0.816246, 0.845827, 0.875408, 0.904989, 0.934571, 0.964152, 0.993733, 1.02331}, {0.712325, 0.741519, 0.770712, 0.799905, 0.829099, 0.858292, 0.887486, 0.916679, 0.945872, 0.975066, 1.00426}};

  //@}

  bool m_useOnly_nClus2 = false;

  int m_nMatchIter = 2;

  //  double m_clustRotation_pos[3]{};
  //  double m_clustRotation_neg[3]{};
  
  double m_clustRotation_pos{0.0};
  double m_clustRotation_neg{0.0};


  std::vector<double> m_clust_RPeaks_pos;
  std::vector<double> m_clust_RPeaks_neg;

  double m_global_RShift_pos{};
  double m_global_RShift_neg{};

  double m_nPhiBins[32] = {53, 59, 65, 71, 76, 82, 87, 93, 98, 103, 109, 114, 119, 124, 129, 134, 144, 152, 160, 167, 174, 181, 186, 192, 198, 201, 204, 206, 207, 206, 205, 202};
  TH1D *m_phi_row_pos[32];
  TH1D *m_phi_row_neg[32];

  double getPhiRotation_smoothed(TH1D *hitHist, TH1D *clustHist);

  std::vector<int> doGlobalRMatching(TH2F *r_phi, bool pos);

  double doGlobalPhiMatching(TH2F *r_phi, std::vector<int> hitMatches, bool pos);

  int getClusterRMatch(std::vector<int> hitMatches, std::vector<double> clusterPeaks, double clusterR);
};

#endif  // PHTPCCENTRALMEMBRANEMATCHER_H
