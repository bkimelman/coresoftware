#include "LaserClusterizer.h"

#include <trackbase/LaserCluster.h>
#include <trackbase/LaserClusterContainer.h>
#include <trackbase/LaserClusterContainerv1.h>
#include <trackbase/LaserClusterv1.h>
#include <trackbase/RawHit.h>
#include <trackbase/RawHitSet.h>
#include <trackbase/RawHitSetContainer.h>
#include <trackbase/RawHitSetv1.h>
#include <trackbase/TpcDefs.h>
#include <trackbase/TrkrDefs.h>  // for hitkey, getLayer
#include <trackbase/TrkrHit.h>
#include <trackbase/TrkrHitSet.h>
#include <trackbase/TrkrHitSetContainer.h>

#include <fun4all/Fun4AllReturnCodes.h>
#include <fun4all/SubsysReco.h>  // for SubsysReco

#include <g4detectors/PHG4TpcCylinderGeom.h>
#include <g4detectors/PHG4TpcCylinderGeomContainer.h>

#include <phool/PHCompositeNode.h>
#include <phool/PHIODataNode.h>  // for PHIODataNode
#include <phool/PHNode.h>        // for PHNode
#include <phool/PHNodeIterator.h>
#include <phool/PHObject.h>  // for PHObject
#include <phool/PHTimer.h>
#include <phool/getClass.h>
#include <phool/phool.h>  // for PHWHERE

#include <Acts/Definitions/Units.hpp>
#include <Acts/Surfaces/Surface.hpp>

#include <TF1.h>
#include <TFile.h>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <array>
#include <cmath>  // for sqrt, cos, sin
#include <iostream>
#include <limits>
#include <map>  // for _Rb_tree_cons...
#include <string>
#include <utility>  // for pair
#include <vector>

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using point = bg::model::point<float, 3, bg::cs::cartesian>;
using box = bg::model::box<point>;
using specHitKey = std::pair<TrkrDefs::hitkey, TrkrDefs::hitsetkey>;
using pointKeyLaser = std::pair<point, specHitKey>;

LaserClusterizer::LaserClusterizer(const std::string &name)
  : SubsysReco(name)
{
}

int LaserClusterizer::InitRun(PHCompositeNode *topNode)
{
  PHNodeIterator iter(topNode);

  // Looking for the DST node
  PHCompositeNode *dstNode = dynamic_cast<PHCompositeNode *>(iter.findFirst("PHCompositeNode", "DST"));
  if (!dstNode)
  {
    std::cout << PHWHERE << "DST Node missing, doing nothing." << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  // Create the Cluster node if required
  auto laserclusters = findNode::getClass<LaserClusterContainerv1>(dstNode, "LASER_CLUSTER");
  if (!laserclusters)
  {
    PHNodeIterator dstiter(dstNode);
    PHCompositeNode *DetNode =
        dynamic_cast<PHCompositeNode *>(dstiter.findFirst("PHCompositeNode", "TRKR"));
    if (!DetNode)
    {
      DetNode = new PHCompositeNode("TRKR");
      dstNode->addNode(DetNode);
    }

    laserclusters = new LaserClusterContainerv1;
    PHIODataNode<PHObject> *LaserClusterContainerNode =
        new PHIODataNode<PHObject>(laserclusters, "LASER_CLUSTER", "PHObject");
    DetNode->addNode(LaserClusterContainerNode);
  }

  if (m_debug)
  {
    m_debugFile = new TFile(m_debugFileName.c_str(), "RECREATE");
  }

  m_itHist_0 = new TH1I("m_itHist_0", "side 0;it", 360, -0.5, 359.5);
  m_itHist_1 = new TH1I("m_itHist_1", "side 1;it", 360, -0.5, 359.5);

  if (m_debug)
  {
    m_clusterTree = new TTree("clusterTree", "clusterTree");
    m_clusterTree->Branch("event", &m_event);
    m_clusterTree->Branch("clusters", &m_eventClusters);
    m_clusterTree->Branch("itHist_0", &m_itHist_0);
    m_clusterTree->Branch("itHist_1", &m_itHist_1);
    m_clusterTree->Branch("nClusters", &m_nClus);
    m_clusterTree->Branch("time_search", &time_search);
    m_clusterTree->Branch("time_clus", &time_clus);
    m_clusterTree->Branch("time_erase", &time_erase);
    m_clusterTree->Branch("time_all", &time_all);
  }

  m_tdriftmax = AdcClockPeriod * NZBinsSide;

  t_all = std::make_unique<PHTimer>("t_all");
  t_all->stop();
  t_search = std::make_unique<PHTimer>("t_search");
  t_search->stop();
  t_clus = std::make_unique<PHTimer>("t_clus");
  t_clus->stop();
  t_erase = std::make_unique<PHTimer>("t_erase");
  t_erase->stop();

  return Fun4AllReturnCodes::EVENT_OK;
}

int LaserClusterizer::process_event(PHCompositeNode *topNode)
{
  ++m_event;

  std::cout << "LaserClusterizer::process_event working on event " << m_event << std::endl;

  PHNodeIterator iter(topNode);
  PHCompositeNode *dstNode = static_cast<PHCompositeNode *>(iter.findFirst("PHCompositeNode", "DST"));
  if (!dstNode)
  {
    std::cout << PHWHERE << "DST Node missing, doing nothing." << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }
  if (!do_read_raw)
  {
    // get node containing the digitized hits
    m_hits = findNode::getClass<TrkrHitSetContainer>(topNode, "TRKR_HITSET");
    if (!m_hits)
    {
      std::cout << PHWHERE << "ERROR: Can't find node TRKR_HITSET" << std::endl;
      return Fun4AllReturnCodes::ABORTRUN;
    }
  }
  else
  {
    // get node containing the digitized hits
    m_rawhits = findNode::getClass<RawHitSetContainer>(topNode, "TRKR_RAWHITSET");
    if (!m_rawhits)
    {
      std::cout << PHWHERE << "ERROR: Can't find node TRKR_HITSET" << std::endl;
      return Fun4AllReturnCodes::ABORTRUN;
    }
  }

  // get node for clusters
  m_clusterlist = findNode::getClass<LaserClusterContainerv1>(topNode, "LASER_CLUSTER");
  if (!m_clusterlist)
  {
    std::cout << PHWHERE << " ERROR: Can't find LASER_CLUSTER." << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  m_geom_container =
      findNode::getClass<PHG4TpcCylinderGeomContainer>(topNode, "CYLINDERCELLGEOM_SVTX");
  if (!m_geom_container)
  {
    std::cout << PHWHERE << "ERROR: Can't find node CYLINDERCELLGEOM_SVTX" << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  m_tGeometry = findNode::getClass<ActsGeometry>(topNode,
                                                 "ActsGeometry");
  if (!m_tGeometry)
  {
    std::cout << PHWHERE
              << "ActsGeometry not found on node tree. Exiting"
              << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  TrkrHitSetContainer::ConstRange hitsetrange;
  RawHitSetContainer::ConstRange rawhitsetrange;
  if (!do_read_raw)
  {
    hitsetrange = m_hits->getHitSets(TrkrDefs::TrkrId::tpcId);
  }
  else
  {
    rawhitsetrange = m_rawhits->getHitSets(TrkrDefs::TrkrId::tpcId);
  }

  bgi::rtree<pointKeyLaser, bgi::quadratic<16>> rtree;
  std::multimap<unsigned int, std::pair<std::pair<TrkrDefs::hitkey, TrkrDefs::hitsetkey>, std::array<int, 3>>> adcMap;

  if (!do_read_raw)
  {
    for (TrkrHitSetContainer::ConstIterator hitsetitr = hitsetrange.first;
         hitsetitr != hitsetrange.second;
         ++hitsetitr)
    {
      TrkrHitSet *hitset = hitsetitr->second;
      int side = TpcDefs::getSide(hitsetitr->first);
      //PHG4TpcCylinderGeom *layergeom = m_geom_container->GetLayerCellGeom(TrkrDefs::getLayer(hitsetitr->first));

      TrkrHitSet::ConstRange hitrangei = hitset->getHits();
      for (TrkrHitSet::ConstIterator hitr = hitrangei.first;
           hitr != hitrangei.second;
           ++hitr)
      {

        int it = TpcDefs::getTBin(hitr->first);
        //double t = layergeom->get_zcenter(it);

        if (side == 0)
        {
          m_itHist_0->Fill(it);
          if (m_debug)
          {
            //m_tHist_0->Fill(t);
          }
        }
        else
        {
          m_itHist_1->Fill(it);
          if (m_debug)
          {
            //m_tHist_1->Fill(t);
	  }
        }
      }
    }

    double itMeanContent_0 = 0.0;
    double itMeanContent_1 = 0.0;

    for (int i = 1; i <= 360; i++)
    {
      itMeanContent_0 += m_itHist_0->GetBinContent(i);
      itMeanContent_1 += m_itHist_1->GetBinContent(i);
    }

    itMeanContent_0 = itMeanContent_0 / 360.0;
    itMeanContent_1 = itMeanContent_1 / 360.0;

    m_itHist_0->GetXaxis()->SetRange(150, 360);
    double itMax_0 = m_itHist_0->GetBinCenter(m_itHist_0->GetMaximumBin());
    double itMaxContent_0 = m_itHist_0->GetMaximum();
    m_itHist_0->GetXaxis()->SetRange(0, 0);

    m_itHist_1->GetXaxis()->SetRange(150, 360);
    double itMax_1 = m_itHist_1->GetBinCenter(m_itHist_1->GetMaximumBin());
    double itMaxContent_1 = m_itHist_1->GetMaximum();
    m_itHist_1->GetXaxis()->SetRange(0, 0);

    if (itMaxContent_0 / itMeanContent_0 < 1.5 || itMaxContent_1 / itMeanContent_1 < 1.5)
    {
      if(Verbosity())
	{
	  std::cout << PHWHERE << "no laser peak found in event" << std::endl;
	}
      return Fun4AllReturnCodes::ABORTEVENT;
    }

    for (TrkrHitSetContainer::ConstIterator hitsetitr = hitsetrange.first;
         hitsetitr != hitsetrange.second;
         ++hitsetitr)
    {
      TrkrHitSet *hitset = hitsetitr->second;
      unsigned int layer = TrkrDefs::getLayer(hitsetitr->first);
      int side = TpcDefs::getSide(hitsetitr->first);
      unsigned int sector = TpcDefs::getSectorId(hitsetitr->first);
      PHG4TpcCylinderGeom *layergeom = m_geom_container->GetLayerCellGeom(layer);
      double r = layergeom->get_radius();

      TrkrDefs::hitsetkey hitsetKey = TpcDefs::genHitSetKey(layer, sector, side);

      TrkrHitSet::ConstRange hitrangei = hitset->getHits();

      for (TrkrHitSet::ConstIterator hitr = hitrangei.first;
           hitr != hitrangei.second;
           ++hitr)
      {
        float_t fadc = (hitr->second->getAdc()) - m_pedestal;  // proper int rounding +0.5
        unsigned short adc = 0;
        if (fadc > 0)
        {
          adc = (unsigned short) fadc;
        }
        if (adc <= 0)
        {
          continue;
        }

        int iphi = TpcDefs::getPad(hitr->first);
        int it = TpcDefs::getTBin(hitr->first);

        if (side == 0 && fabs(it - itMax_0) > 50)
        {
          continue;
        }
        if (side == 1 && fabs(it - itMax_1) > 50)
        {
          continue;
        }

        double phi = layergeom->get_phi(iphi);
        double zdriftlength = layergeom->get_zcenter(it) * m_tGeometry->get_drift_velocity();

        float x = r * cos(phi);
        float y = r * sin(phi);
        float z = m_tdriftmax * m_tGeometry->get_drift_velocity() - zdriftlength;
        if (side == 1)
        {
          z = -z;
          it = -it;
        }

        m_currentHit.clear();
        m_currentHit.push_back(x);
        m_currentHit.push_back(y);
        m_currentHit.push_back(z);
        m_currentHit.push_back(1.0 * adc);

        m_currentHit_hardware.clear();
        m_currentHit_hardware.push_back(layer);
        m_currentHit_hardware.push_back(iphi);
        m_currentHit_hardware.push_back(it);
        m_currentHit_hardware.push_back(1.0 * adc);

        std::array<int, 3> coords = {(int) layer, iphi, it};

        std::vector<pointKeyLaser> testduplicate;
        rtree.query(bgi::intersects(box(point(layer - 0.001, iphi - 0.001, it - 0.001),
                                        point(layer + 0.001, iphi + 0.001, it + 0.001))),
                    std::back_inserter(testduplicate));
        if (!testduplicate.empty())
        {
          testduplicate.clear();
          continue;
        }

        TrkrDefs::hitkey hitKey = TpcDefs::genHitKey(iphi, it);

        auto spechitkey = std::make_pair(hitKey, hitsetKey);
        auto keyCoords = std::make_pair(spechitkey, coords);
        adcMap.insert(std::make_pair(adc, keyCoords));

        rtree.insert(std::make_pair(point(1.0 * layer, 1.0 * iphi, 1.0 * it), spechitkey));
      }
    }
  }

  if (Verbosity() > 1)
  {
    std::cout << "finished looping over hits" << std::endl;
    std::cout << "map size: " << adcMap.size() << std::endl;
    std::cout << "rtree size: " << rtree.size() << std::endl;
  }

  // done filling rTree

  t_all->restart();

  while (adcMap.size() > 0)
  {
    auto iterKey = adcMap.rbegin();
    if (iterKey == adcMap.rend())
    {
      break;
    }

    auto coords = iterKey->second.second;

    int layer = coords[0];
    int iphi = coords[1];
    int it = coords[2];

    int layerMax = layer + 1;
    if (layer == 22 || layer == 38 || layer == 54)
    {
      layerMax = layer;
    }
    int layerMin = layer - 1;
    if (layer == 7 || layer == 23 || layer == 39)
    {
      layerMin = layer;
    }

    std::vector<pointKeyLaser> clusHits;

    t_search->restart();
    rtree.query(bgi::intersects(box(point(layerMin, iphi - 2, it - 5), point(layerMax, iphi + 2, it + 5))), std::back_inserter(clusHits));
    t_search->stop();

    t_clus->restart();
    calc_cluster_parameter(clusHits, adcMap);
    t_clus->stop();

    t_erase->restart();
    remove_hits(clusHits, rtree, adcMap);
    t_erase->stop();

    clusHits.clear();
  }

  if (m_debug)
  {
    m_nClus = (int) m_eventClusters->size();
  }
  t_all->stop();

  if (m_debug)
  {
    time_search = t_search->get_accumulated_time() / 1000.;
    time_clus = t_clus->get_accumulated_time() / 1000.;
    time_erase = t_erase->get_accumulated_time() / 1000.;
    time_all = t_all->get_accumulated_time() / 1000.;

    m_eventClusters = m_clusterlist;

    m_clusterTree->Fill();
  }

  if (Verbosity())
  {
    std::cout << "rtree search time: " << t_search->get_accumulated_time() / 1000. << " sec" << std::endl;
    std::cout << "clustering time: " << t_clus->get_accumulated_time() / 1000. << " sec" << std::endl;
    std::cout << "erasing time: " << t_erase->get_accumulated_time() / 1000. << " sec" << std::endl;
    std::cout << "total time: " << t_all->get_accumulated_time() / 1000. << " sec" << std::endl;
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

int LaserClusterizer::ResetEvent(PHCompositeNode * /*topNode*/)
{
  m_itHist_0->Reset();
  m_itHist_1->Reset();

  if (m_debug)
  {
    //m_tHist_0->Reset();
    //m_tHist_1->Reset();
    
    //m_eventClusters.clear();
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

int LaserClusterizer::End(PHCompositeNode * /*topNode*/)
{
  if (m_debug)
  {
    m_debugFile->cd();
    m_clusterTree->Write();
    m_debugFile->Close();
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

void LaserClusterizer::calc_cluster_parameter(std::vector<pointKeyLaser> &clusHits, std::multimap<unsigned int, std::pair<std::pair<TrkrDefs::hitkey, TrkrDefs::hitsetkey>, std::array<int, 3>>> &adcMap)
{
  double rSum = 0.0;
  double phiSum = 0.0;
  double tSum = 0.0;

  double layerSum = 0.0;
  double iphiSum = 0.0;
  double itSum = 0.0;

  double adcSum = 0.0;

  double maxAdc = 0.0;
  TrkrDefs::hitsetkey maxKey = 0;

  unsigned int nHits = clusHits.size();

  auto *clus = new LaserClusterv1;

  for (auto &clusHit : clusHits)
  {
    float coords[3] = {clusHit.first.get<0>(), clusHit.first.get<1>(), clusHit.first.get<2>()};
    std::pair<TrkrDefs::hitkey, TrkrDefs::hitsetkey> spechitkey = clusHit.second;

    // int side = TpcDefs::getSide(spechitkey.second);
    // unsigned int sector= TpcDefs::getSectorId(spechitkey.second);

    PHG4TpcCylinderGeom *layergeom = m_geom_container->GetLayerCellGeom((int) coords[0]);

    double r = layergeom->get_radius();
    double phi = layergeom->get_phi(coords[1]);
    double t = layergeom->get_zcenter(fabs(coords[2]));

    double hitzdriftlength = t * m_tGeometry->get_drift_velocity();
    double hitZ = m_tdriftmax * m_tGeometry->get_drift_velocity() - hitzdriftlength;

    for (auto &iterKey : adcMap)
    {
      if (iterKey.second.first == spechitkey)
      {
        double adc = iterKey.first;

        clus->addHit();
        clus->setHitLayer(clus->getNhits() - 1, coords[0]);
        clus->setHitIPhi(clus->getNhits() - 1, coords[1]);
        clus->setHitIT(clus->getNhits() - 1, coords[2]);
        clus->setHitX(clus->getNhits() - 1, r * cos(phi));
        clus->setHitY(clus->getNhits() - 1, r * sin(phi));
        clus->setHitZ(clus->getNhits() - 1, hitZ);
        clus->setHitAdc(clus->getNhits() - 1, (float) adc);

        rSum += r * adc;
        phiSum += phi * adc;
        tSum += t * adc;

        layerSum += coords[0] * adc;
        iphiSum += coords[1] * adc;
        itSum += coords[2] * adc;

        adcSum += adc;

        if (adc > maxAdc)
        {
          maxAdc = adc;
          maxKey = spechitkey.second;
        }

        break;
      }
    }
  }

  if (nHits == 0)
  {
    return;
  }

  double clusR = rSum / adcSum;
  double clusPhi = phiSum / adcSum;
  double clusT = tSum / adcSum;
  double zdriftlength = clusT * m_tGeometry->get_drift_velocity();

  double clusX = clusR * cos(clusPhi);
  double clusY = clusR * sin(clusPhi);
  double clusZ = m_tdriftmax * m_tGeometry->get_drift_velocity() - zdriftlength;
  if (itSum < 0)
  {
    clusZ = -clusZ;
    for (int i = 0; i < (int) clus->getNhits(); i++)
    {
      clus->setHitZ(i, -1 * clus->getHitZ(i));
    }
  }

  clus->setAdc(adcSum);
  clus->setX(clusX);
  clus->setY(clusY);
  clus->setZ(clusZ);
  clus->setLayer(layerSum / adcSum);
  clus->setIPhi(iphiSum / adcSum);
  clus->setIT(itSum / adcSum);

  const auto ckey = TrkrDefs::genClusKey(maxKey, m_clusterlist->size());
  m_clusterlist->addClusterSpecifyKey(ckey, clus);
  if (m_debug)
  {
    m_currentCluster = (LaserClusterv1 *) clus->CloneMe();
    //m_eventClusters.push_back((LaserClusterv1 *) m_currentCluster->CloneMe());
  }
}

void LaserClusterizer::remove_hits(std::vector<pointKeyLaser> &clusHits, bgi::rtree<pointKeyLaser, bgi::quadratic<16>> &rtree, std::multimap<unsigned int, std::pair<std::pair<TrkrDefs::hitkey, TrkrDefs::hitsetkey>, std::array<int, 3>>> &adcMap)
{
  for (auto &clusHit : clusHits)
  {
    auto spechitkey = clusHit.second;

    rtree.remove(clusHit);

    for (auto iterAdc = adcMap.begin(); iterAdc != adcMap.end();)
    {
      if (iterAdc->second.first == spechitkey)
      {
        iterAdc = adcMap.erase(iterAdc);
        break;
      }
      else
      {
        ++iterAdc;
      }
    }
  }
}
