/**
 * @file mvtx/MvtxClusterizer.cc
 * @author D. McGlinchey
 * @date June 2018
 * @brief Implementation of MvtxClusterizer
 */
#include "MvtxClusterizer.h"
#include "CylinderGeom_Mvtx.h"

#include <g4detectors/PHG4CylinderGeom.h>
#include <g4detectors/PHG4CylinderGeomContainer.h>

#include <trackbase/ClusHitsVerbosev1.h>
#include <trackbase/MvtxDefs.h>
#include <trackbase/TrkrClusterContainerv4.h>
#include <trackbase/TrkrClusterHitAssocv3.h>
#include <trackbase/TrkrClusterv3.h>
#include <trackbase/TrkrClusterv4.h>
#include <trackbase/TrkrClusterv5.h>
#include <trackbase/TrkrDefs.h>  // for hitkey, getLayer
#include <trackbase/TrkrHitSet.h>
#include <trackbase/TrkrHitSetContainer.h>
#include <trackbase/TrkrHitv2.h>

#include <trackbase/RawHit.h>
#include <trackbase/RawHitSet.h>
#include <trackbase/RawHitSetContainer.h>

#include <fun4all/Fun4AllReturnCodes.h>
#include <fun4all/SubsysReco.h>  // for SubsysReco

#include <phool/PHCompositeNode.h>
#include <phool/PHIODataNode.h>
#include <phool/PHNode.h>  // for PHNode
#include <phool/PHNodeIterator.h>
#include <phool/PHObject.h>  // for PHObject
#include <phool/getClass.h>
#include <phool/phool.h>  // for PHWHERE

#include <TMatrixFfwd.h>    // for TMatrixF
#include <TMatrixT.h>       // for TMatrixT, operator*
#include <TMatrixTUtils.h>  // for TMatrixTRow
#include <TVector3.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <boost/graph/adjacency_list.hpp>
#pragma GCC diagnostic pop

#include <boost/graph/connected_components.hpp>

#include <array>
#include <cmath>
#include <cstdlib>  // for exit
#include <iostream>
#include <map>  // for multimap<>::iterator
#include <set>  // for set, set<>::iterator
#include <string>
#include <vector>  // for vector

namespace
{

  /// convenience square method
  template <class T>
  inline constexpr T square(const T &x)
  {
    return x * x;
  }
}  // namespace

bool MvtxClusterizer::are_adjacent(
    const std::pair<TrkrDefs::hitkey, TrkrHit *> &lhs,
    const std::pair<TrkrDefs::hitkey, TrkrHit *> &rhs)
{
  if (GetZClustering())
  {
    return

      // column adjacent
      ( (MvtxDefs::getCol(lhs.first) > MvtxDefs::getCol(rhs.first)) ?
      MvtxDefs::getCol(lhs.first)<=MvtxDefs::getCol(rhs.first)+1:
      MvtxDefs::getCol(rhs.first)<=MvtxDefs::getCol(lhs.first)+1) &&

      // row adjacent
      ( (MvtxDefs::getRow(lhs.first) > MvtxDefs::getRow(rhs.first)) ?
      MvtxDefs::getRow(lhs.first)<=MvtxDefs::getRow(rhs.first)+1:
      MvtxDefs::getRow(rhs.first)<=MvtxDefs::getRow(lhs.first)+1);

  } else {

    return
      // column identical
      MvtxDefs::getCol(rhs.first)==MvtxDefs::getCol(lhs.first) &&

      // row adjacent
      ( (MvtxDefs::getRow(lhs.first) > MvtxDefs::getRow(rhs.first)) ?
      MvtxDefs::getRow(lhs.first)<=MvtxDefs::getRow(rhs.first)+1:
      MvtxDefs::getRow(rhs.first)<=MvtxDefs::getRow(lhs.first)+1);

  }
}

bool MvtxClusterizer::are_adjacent(RawHit *lhs, RawHit *rhs)
{
  if (GetZClustering())
  {
    return

      // phi adjacent (== column)
      ((lhs->getPhiBin() > rhs->getPhiBin()) ?
      lhs->getPhiBin() <= rhs->getPhiBin()+1:
      rhs->getPhiBin() <= lhs->getPhiBin()+1) &&

      // time adjacent (== row)
      ((lhs->getTBin() > rhs->getTBin()) ?
      lhs->getTBin() <= rhs->getTBin()+1:
      rhs->getTBin() <= lhs->getTBin()+1);

  } else {

    return

      // phi identical (== column)
      lhs->getPhiBin() == rhs->getPhiBin() &&

      // time adjacent (== row)
      ((lhs->getTBin() >  rhs->getTBin()) ?
      lhs->getTBin() <= rhs->getTBin()+1:
      rhs->getTBin() <= lhs->getTBin()+1);

  }
}

MvtxClusterizer::MvtxClusterizer(const std::string &name)
  : SubsysReco(name)
{
}

int MvtxClusterizer::InitRun(PHCompositeNode *topNode)
{
  //-----------------
  // Add Cluster Node
  //-----------------

  PHNodeIterator iter(topNode);

  // Looking for the DST node
  PHCompositeNode *dstNode =
      dynamic_cast<PHCompositeNode *>(iter.findFirst("PHCompositeNode", "DST"));
  if (!dstNode)
  {
    std::cout << PHWHERE << "DST Node missing, doing nothing." << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }
  PHNodeIterator iter_dst(dstNode);

  // Create the SVX node if required
  PHCompositeNode *svxNode = dynamic_cast<PHCompositeNode *>(
      iter_dst.findFirst("PHCompositeNode", "TRKR"));
  if (!svxNode)
  {
    svxNode = new PHCompositeNode("TRKR");
    dstNode->addNode(svxNode);
  }

  // Create the Cluster node if required
  auto trkrclusters =
      findNode::getClass<TrkrClusterContainer>(dstNode, "TRKR_CLUSTER");
  if (!trkrclusters)
  {
    PHNodeIterator dstiter(dstNode);
    PHCompositeNode *DetNode = dynamic_cast<PHCompositeNode *>(dstiter.findFirst("PHCompositeNode", "TRKR"));
    if (!DetNode)
    {
      DetNode = new PHCompositeNode("TRKR");
      dstNode->addNode(DetNode);
    }

    trkrclusters = new TrkrClusterContainerv4;
    PHIODataNode<PHObject> *TrkrClusterContainerNode =
        new PHIODataNode<PHObject>(trkrclusters, "TRKR_CLUSTER", "PHObject");
    DetNode->addNode(TrkrClusterContainerNode);
  }

  auto clusterhitassoc =
      findNode::getClass<TrkrClusterHitAssoc>(topNode, "TRKR_CLUSTERHITASSOC");
  if (!clusterhitassoc)
  {
    PHNodeIterator dstiter(dstNode);
    PHCompositeNode *DetNode = dynamic_cast<PHCompositeNode *>(dstiter.findFirst("PHCompositeNode", "TRKR"));
    if (!DetNode)
    {
      DetNode = new PHCompositeNode("TRKR");
      dstNode->addNode(DetNode);
    }

    clusterhitassoc = new TrkrClusterHitAssocv3;
    PHIODataNode<PHObject> *newNode = new PHIODataNode<PHObject>(clusterhitassoc, "TRKR_CLUSTERHITASSOC", "PHObject");
    DetNode->addNode(newNode);
  }

  // Get the cluster hits verbose node, if required
  if (record_ClusHitsVerbose)
  {
    // get the node
    mClusHitsVerbose = findNode::getClass<ClusHitsVerbose>(topNode, "Trkr_SvtxClusHitsVerbose");
    if (!mClusHitsVerbose)
    {
      PHNodeIterator dstiter(dstNode);
      auto DetNode = dynamic_cast<PHCompositeNode *>(dstiter.findFirst("PHCompositeNode", "TRKR"));
      if (!DetNode)
      {
        DetNode = new PHCompositeNode("TRKR");
        dstNode->addNode(DetNode);
      }
      mClusHitsVerbose = new ClusHitsVerbosev1();
      auto newNode = new PHIODataNode<PHObject>(mClusHitsVerbose, "Trkr_SvtxClusHitsVerbose", "PHObject");
      DetNode->addNode(newNode);
    }
  }

  //----------------
  // Report Settings
  //----------------

  if (Verbosity() > 0)
  {
    std::cout << "====================== MvtxClusterizer::InitRun() "
            "====================="
         << std::endl;
    std::cout << " Z-dimension Clustering = " << std::boolalpha << m_makeZClustering
         << std::noboolalpha << std::endl;
    std::cout << "=================================================================="
            "========="
         << std::endl;
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

int MvtxClusterizer::process_event(PHCompositeNode *topNode)
{
  // get node containing the digitized hits
  if (!do_read_raw)
  {
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
    m_rawhits =
        findNode::getClass<RawHitSetContainer>(topNode, "TRKR_RAWHITSET");
    if (!m_rawhits)
    {
      std::cout << PHWHERE << "ERROR: Can't find node TRKR_HITSET" << std::endl;
      return Fun4AllReturnCodes::ABORTRUN;
    }
  }

  // get node for clusters
  m_clusterlist =
      findNode::getClass<TrkrClusterContainer>(topNode, "TRKR_CLUSTER");
  if (!m_clusterlist)
  {
    std::cout << PHWHERE << " ERROR: Can't find TRKR_CLUSTER." << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  // get node for cluster hit associations
  m_clusterhitassoc =
      findNode::getClass<TrkrClusterHitAssoc>(topNode, "TRKR_CLUSTERHITASSOC");
  if (!m_clusterhitassoc)
  {
    std::cout << PHWHERE << " ERROR: Can't find TRKR_CLUSTERHITASSOC" << std::endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  // reset MVTX clusters and cluster associations
  const auto hitsetkeys = m_clusterlist->getHitSetKeys(TrkrDefs::mvtxId);
  for( const auto& hitsetkey:hitsetkeys)
  {
    m_clusterlist->removeClusters(hitsetkey);
    m_clusterhitassoc->removeAssocs(hitsetkey);
  }

  // run clustering
  if (!do_read_raw)
  {
    ClusterMvtx(topNode);
  }
  else
  {
    ClusterMvtxRaw(topNode);
  }
  PrintClusters(topNode);

  // done
  return Fun4AllReturnCodes::EVENT_OK;
}

void MvtxClusterizer::ClusterMvtx(PHCompositeNode *topNode)
{
  if (Verbosity() > 0)
  {
    std::cout << "Entering MvtxClusterizer::ClusterMvtx " << std::endl;
  }

  PHG4CylinderGeomContainer *geom_container =
      findNode::getClass<PHG4CylinderGeomContainer>(topNode, "CYLINDERGEOM_MVTX");
  if (!geom_container)
  {
    return;
  }

  //-----------
  // Clustering
  //-----------

  // loop over each MvtxHitSet object (chip)
  TrkrHitSetContainer::ConstRange hitsetrange =
      m_hits->getHitSets(TrkrDefs::TrkrId::mvtxId);
  for (TrkrHitSetContainer::ConstIterator hitsetitr = hitsetrange.first;
       hitsetitr != hitsetrange.second; ++hitsetitr)
  {
    TrkrHitSet *hitset = hitsetitr->second;

    if (Verbosity() > 0)
    {
      unsigned int layer = TrkrDefs::getLayer(hitsetitr->first);
      unsigned int stave = MvtxDefs::getStaveId(hitsetitr->first);
      unsigned int chip = MvtxDefs::getChipId(hitsetitr->first);
      unsigned int strobe = MvtxDefs::getStrobeId(hitsetitr->first);
      std::cout << "MvtxClusterizer found hitsetkey " << hitsetitr->first
           << " layer " << layer << " stave " << stave << " chip " << chip
           << " strobe " << strobe << std::endl;
    }

    if (Verbosity() > 2)
    {
      hitset->identify();
    }

    // fill a vector of hits to make things easier
    std::vector<std::pair<TrkrDefs::hitkey, TrkrHit *> > hitvec;

    TrkrHitSet::ConstRange hitrangei = hitset->getHits();
    for (TrkrHitSet::ConstIterator hitr = hitrangei.first;
         hitr != hitrangei.second; ++hitr)
    {
      hitvec.emplace_back(hitr->first, hitr->second);
    }
    if (Verbosity() > 2)
    {
      std::cout << "hitvec.size(): " << hitvec.size() << std::endl;
    }

    if (Verbosity() > 0)
    {
      for (auto &hit : hitvec)
      {
        auto hitkey = hit.first;
        auto row = MvtxDefs::getRow(hitkey);
        auto col = MvtxDefs::getCol(hitkey);
        std::cout << "      hitkey " << hitkey << " row " << row << " col "
                  << col << std::endl;
      }
    }

    // do the clustering
    using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;
    Graph G;

    // loop over hits in this chip
    for (unsigned int i = 0; i < hitvec.size(); i++)
    {
      for (unsigned int j = 0; j < hitvec.size(); j++)
      {
        if (are_adjacent(hitvec[i], hitvec[j]))
        {
          add_edge(i, j, G);
        }
      }
    }

    // Find the connections between the vertices of the graph (vertices are the
    // rawhits,
    // connections are made when they are adjacent to one another)
    std::vector<int> component(num_vertices(G));

    // this is the actual clustering, performed by boost
    boost::connected_components(G, &component[0]);

    // Loop over the components(hits) compiling a list of the
    // unique connected groups (ie. clusters).
    std::set<int> cluster_ids;  // unique components
    std::multimap<int, std::pair<TrkrDefs::hitkey, TrkrHit *> > clusters;
    for (unsigned int i = 0; i < component.size(); i++)
    {
      cluster_ids.insert(component[i]);
      clusters.insert(make_pair(component[i], hitvec[i]));
    }
    for (const auto& clusid:cluster_ids)
    {
      auto clusrange = clusters.equal_range(clusid);
      auto ckey = TrkrDefs::genClusKey(hitset->getHitSetKey(), clusid);

      // determine the size of the cluster in phi and z
      std::set<int> phibins;
      std::set<int> zbins;
      std::map<int, unsigned int> m_phi, m_z;  // Note, there are no "cut" bins for Svtx Clusters

      // determine the cluster position...
      double locxsum = 0.;
      double loczsum = 0.;
      const unsigned int nhits =
          std::distance(clusrange.first, clusrange.second);

      double locclusx = std::numeric_limits<double>::quiet_NaN();
      double locclusz = std::numeric_limits<double>::quiet_NaN();

      // we need the geometry object for this layer to get the global positions
      int layer = TrkrDefs::getLayer(ckey);
      auto layergeom = dynamic_cast<CylinderGeom_Mvtx *>(geom_container->GetLayerGeom(layer));
      if (!layergeom)
      {
        exit(1);
      }

      for (auto mapiter = clusrange.first; mapiter != clusrange.second;
           ++mapiter)
      {
        // size
        const auto energy = (mapiter->second).second->getAdc();
        int col = MvtxDefs::getCol((mapiter->second).first);
        int row = MvtxDefs::getRow((mapiter->second).first);
        zbins.insert(col);
        phibins.insert(row);

        if (mClusHitsVerbose)
        {
          auto pnew = m_phi.try_emplace(row, energy);
          if (!pnew.second)
          {
            pnew.first->second += energy;
          }

          pnew = m_z.try_emplace(col, energy);
          if (!pnew.second)
          {
            pnew.first->second += energy;
          }
        }

        // get local coordinates, in stae reference frame, for hit
        auto local_coords = layergeom->get_local_coords_from_pixel(row, col);

        /*
          manually offset position along y (thickness of the sensor),
          to account for effective hit position in the sensor, resulting from
          diffusion.
          Effective position corresponds to 1um above the middle of the sensor
        */
        local_coords.SetY(1e-4);

        // update cluster position
        locxsum += local_coords.X();
        loczsum += local_coords.Z();
        // add the association between this cluster key and this hitkey to the
        // table
        m_clusterhitassoc->addAssoc(ckey, mapiter->second.first);

      }  // mapiter

      if (mClusHitsVerbose)
      {
        if (Verbosity() > 10)
        {
          for (auto &hit : m_phi)
          {
            std::cout << " m_phi(" << hit.first << " : " << hit.second << ") "
                      << std::endl;
          }
        }
        for (auto &hit : m_phi)
        {
          mClusHitsVerbose->addPhiHit(hit.first, (float) hit.second);
        }
        for (auto &hit : m_z)
        {
          mClusHitsVerbose->addZHit(hit.first, (float) hit.second);
        }
        mClusHitsVerbose->push_hits(ckey);
      }

      // This is the local position
      locclusx = locxsum / nhits;
      locclusz = loczsum / nhits;

      const double pitch = layergeom->get_pixel_x();
      const double length = layergeom->get_pixel_z();
      const double phisize = phibins.size() * pitch;
      const double zsize = zbins.size() * length;

      static const double invsqrt12 = 1. / std::sqrt(12);

      // scale factors (phi direction)
      /*
        they corresponds to clusters of size (2,2), (2,3), (3,2) and (3,3) in
        phi and z
        other clusters, which are very few and pathological, get a scale factor
        of 1
        These scale factors are applied to produce cluster pulls with width
        unity
      */

      double phierror = pitch * invsqrt12;

      static constexpr std::array<double, 7> scalefactors_phi = {
          {0.36, 0.6, 0.37, 0.49, 0.4, 0.37, 0.33}};

      if ((phibins.size() == 1 && zbins.size() == 1) ||
          (phibins.size() == 2 && zbins.size() == 2))
      {
        phierror *= scalefactors_phi[0];
      }
      else if ((phibins.size() == 2 && zbins.size() == 1) ||
               (phibins.size() == 2 && zbins.size() == 3))
      {
        phierror *= scalefactors_phi[1];
      }
      else if ((phibins.size() == 1 && zbins.size() == 2) ||
               (phibins.size() == 3 && zbins.size() == 2))
      {
        phierror *= scalefactors_phi[2];
      }
      else if (phibins.size() == 3 && zbins.size() == 3)
      {
        phierror *= scalefactors_phi[3];
      }

      // scale factors (z direction)
      /*
        they corresponds to clusters of size (2,2), (2,3), (3,2) and (3,3) in z
        and phi
        other clusters, which are very few and pathological, get a scale factor
        of 1
      */
      static constexpr std::array<double, 4> scalefactors_z = {
          {0.47, 0.48, 0.71, 0.55}};
      double zerror = length * invsqrt12;
      if (zbins.size() == 2 && phibins.size() == 2)
      {
        zerror *= scalefactors_z[0];
      }
      else if (zbins.size() == 2 && phibins.size() == 3)
      {
        zerror *= scalefactors_z[1];
      }
      else if (zbins.size() == 3 && phibins.size() == 2)
      {
        zerror *= scalefactors_z[2];
      }
      else if (zbins.size() == 3 && phibins.size() == 3)
      {
        zerror *= scalefactors_z[3];
      }

      if (Verbosity() > 0)
      {
        std::cout << " MvtxClusterizer: cluskey " << ckey << " layer " << layer
             << " rad " << layergeom->get_radius() << " phibins "
             << phibins.size() << " pitch " << pitch << " phisize " << phisize
             << " zbins " << zbins.size() << " length " << length << " zsize "
             << zsize << " local x " << locclusx << " local y " << locclusz
             << std::endl;
      }

      auto clus = std::make_unique<TrkrClusterv5>();
      clus->setAdc(nhits);
      clus->setMaxAdc(1);
      clus->setLocalX(locclusx);
      clus->setLocalY(locclusz);
      clus->setPhiError(phierror);
      clus->setZError(zerror);
      clus->setPhiSize(phibins.size());
      clus->setZSize(zbins.size());
      // All silicon surfaces have a 1-1 map to hitsetkey.
      // So set subsurface key to 0
      clus->setSubSurfKey(0);

      if (Verbosity() > 2)
      {
        clus->identify();
      }

      if (zbins.size() <= 127)
      {
        m_clusterlist->addClusterSpecifyKey(ckey, clus.release());
      }

    }  // clusitr loop
  }    // loop over hitsets

  if (Verbosity() > 1)
  {
    // check that the associations were written correctly
    m_clusterhitassoc->identify();
  }

  return;
}

void MvtxClusterizer::ClusterMvtxRaw(PHCompositeNode *topNode)
{
  if (Verbosity() > 0)
  {
    std::cout << "Entering MvtxClusterizer::ClusterMvtx " << std::endl;
  }

  PHG4CylinderGeomContainer *geom_container =
      findNode::getClass<PHG4CylinderGeomContainer>(topNode,
                                                    "CYLINDERGEOM_MVTX");
  if (!geom_container)
  {
    return;
  }

  //-----------
  // Clustering
  //-----------

  // loop over each MvtxHitSet object (chip)
  RawHitSetContainer::ConstRange hitsetrange =
      m_rawhits->getHitSets(TrkrDefs::TrkrId::mvtxId);
  for (RawHitSetContainer::ConstIterator hitsetitr = hitsetrange.first;
       hitsetitr != hitsetrange.second; ++hitsetitr)
  {
    RawHitSet *hitset = hitsetitr->second;

    if (Verbosity() > 0)
    {
      unsigned int layer = TrkrDefs::getLayer(hitsetitr->first);
      unsigned int stave = MvtxDefs::getStaveId(hitsetitr->first);
      unsigned int chip = MvtxDefs::getChipId(hitsetitr->first);
      unsigned int strobe = MvtxDefs::getStrobeId(hitsetitr->first);
      std::cout << "MvtxClusterizer found hitsetkey " << hitsetitr->first
           << " layer " << layer << " stave " << stave << " chip " << chip
           << " strobe " << strobe << std::endl;
    }

    if (Verbosity() > 2)
    {
      hitset->identify();
    }

    // fill a vector of hits to make things easier
    std::vector<RawHit *> hitvec;

    RawHitSet::ConstRange hitrangei = hitset->getHits();
    for (RawHitSet::ConstIterator hitr = hitrangei.first;
         hitr != hitrangei.second; ++hitr)
    {
      hitvec.push_back((*hitr));
    }
    if (Verbosity() > 2)
    {
      std::cout << "hitvec.size(): " << hitvec.size() << std::endl;
    }

    // do the clustering
    using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;
    Graph G;

    // loop over hits in this chip
    for (unsigned int i = 0; i < hitvec.size(); i++)
    {
      for (unsigned int j = 0; j < hitvec.size(); j++)
      {
        if (are_adjacent(hitvec[i], hitvec[j]))
        {
          add_edge(i, j, G);
        }
      }
    }

    // Find the connections between the vertices of the graph (vertices are the
    // rawhits,
    // connections are made when they are adjacent to one another)
    std::vector<int> component(num_vertices(G));

    // this is the actual clustering, performed by boost
    boost::connected_components(G, &component[0]);

    // Loop over the components(hits) compiling a list of the
    // unique connected groups (ie. clusters).
    std::set<int> cluster_ids;  // unique components

    std::multimap<int, RawHit *> clusters;
    for (unsigned int i = 0; i < component.size(); i++)
    {
      cluster_ids.insert(component[i]);
      clusters.emplace(component[i], hitvec[i]);
    }
    //    std::cout << "found cluster #: "<< clusters.size()<< std::endl;
    // loop over the componenets and make clusters
    for( const auto& clusid:cluster_ids)
    {
      auto clusrange = clusters.equal_range(clusid);

      // make the cluster directly in the node tree
      auto ckey = TrkrDefs::genClusKey(hitset->getHitSetKey(), clusid);

      // determine the size of the cluster in phi and z
      std::set<int> phibins;
      std::set<int> zbins;

      // determine the cluster position...
      double locxsum = 0.;
      double loczsum = 0.;
      const unsigned int nhits =
          std::distance(clusrange.first, clusrange.second);

      double locclusx = NAN;
      double locclusz = NAN;

      // we need the geometry object for this layer to get the global positions
      int layer = TrkrDefs::getLayer(ckey);
      auto layergeom = dynamic_cast<CylinderGeom_Mvtx *>(
          geom_container->GetLayerGeom(layer));
      if (!layergeom)
      {
        exit(1);
      }

      for (auto mapiter = clusrange.first; mapiter != clusrange.second;
           ++mapiter)
      {
        // size
        int col = (mapiter->second)->getPhiBin();
        int row = (mapiter->second)->getTBin();
        zbins.insert(col);
        phibins.insert(row);

        // get local coordinates, in stae reference frame, for hit
        auto local_coords = layergeom->get_local_coords_from_pixel(row, col);

        /*
          manually offset position along y (thickness of the sensor),
          to account for effective hit position in the sensor, resulting from
          diffusion.
          Effective position corresponds to 1um above the middle of the sensor
        */
        local_coords.SetY(1e-4);

        // update cluster position
        locxsum += local_coords.X();
        loczsum += local_coords.Z();
        // add the association between this cluster key and this hitkey to the
        // table
        //	      m_clusterhitassoc->addAssoc(ckey, mapiter->second.first);

      }  // mapiter

      // This is the local position
      locclusx = locxsum / nhits;
      locclusz = loczsum / nhits;

      const double pitch = layergeom->get_pixel_x();
      //	std::cout << " pitch: " <<  pitch << std::endl;
      const double length = layergeom->get_pixel_z();
      //	std::cout << " length: " << length << std::endl;
      const double phisize = phibins.size() * pitch;
      const double zsize = zbins.size() * length;

      static const double invsqrt12 = 1. / std::sqrt(12);

      // scale factors (phi direction)
      /*
        they corresponds to clusters of size (2,2), (2,3), (3,2) and (3,3) in
        phi and z
        other clusters, which are very few and pathological, get a scale factor
        of 1
        These scale factors are applied to produce cluster pulls with width
        unity
      */

      double phierror = pitch * invsqrt12;

      static constexpr std::array<double, 7> scalefactors_phi = {
          {0.36, 0.6, 0.37, 0.49, 0.4, 0.37, 0.33}};
      if ((phibins.size() == 1 && zbins.size() == 1) ||
          (phibins.size() == 2 && zbins.size() == 2))
      {
        phierror *= scalefactors_phi[0];
      }
      else if ((phibins.size() == 2 && zbins.size() == 1) ||
               (phibins.size() == 2 && zbins.size() == 3))
      {
        phierror *= scalefactors_phi[1];
      }
      else if ((phibins.size() == 1 && zbins.size() == 2) ||
               (phibins.size() == 3 && zbins.size() == 2))
      {
        phierror *= scalefactors_phi[2];
      }
      else if (phibins.size() == 3 && zbins.size() == 3)
      {
        phierror *= scalefactors_phi[3];
      }

      // scale factors (z direction)
      /*
        they corresponds to clusters of size (2,2), (2,3), (3,2) and (3,3) in z
        and phi
        other clusters, which are very few and pathological, get a scale factor
        of 1
      */
      static constexpr std::array<double, 4> scalefactors_z = {
          {0.47, 0.48, 0.71, 0.55}};
      double zerror = length * invsqrt12;

      if (zbins.size() == 2 && phibins.size() == 2)
      {
        zerror *= scalefactors_z[0];
      }
      else if (zbins.size() == 2 && phibins.size() == 3)
      {
        zerror *= scalefactors_z[1];
      }
      else if (zbins.size() == 3 && phibins.size() == 2)
      {
        zerror *= scalefactors_z[2];
      }
      else if (zbins.size() == 3 && phibins.size() == 3)
      {
        zerror *= scalefactors_z[3];
      }

      if (Verbosity() > 0)
      {
        std::cout << " MvtxClusterizer: cluskey " << ckey << " layer " << layer
             << " rad " << layergeom->get_radius() << " phibins "
             << phibins.size() << " pitch " << pitch << " phisize " << phisize
             << " zbins " << zbins.size() << " length " << length << " zsize "
             << zsize << " local x " << locclusx << " local y " << locclusz
             << std::endl;
      }

      auto clus = std::make_unique<TrkrClusterv5>();
      clus->setAdc(nhits);
      clus->setMaxAdc(1);
      clus->setLocalX(locclusx);
      clus->setLocalY(locclusz);
      clus->setPhiError(phierror);
      clus->setZError(zerror);
      clus->setPhiSize(phibins.size());
      clus->setZSize(zbins.size());
      // All silicon surfaces have a 1-1 map to hitsetkey.
      // So set subsurface key to 0
      clus->setSubSurfKey(0);

      if (Verbosity() > 2)
      {
        clus->identify();
      }

      if (zbins.size() <= 127)
      {
        m_clusterlist->addClusterSpecifyKey(ckey, clus.release());
      }
    }  // clusitr loop
  }    // loop over hitsets

  if (Verbosity() > 1)
  {
    // check that the associations were written correctly
    m_clusterhitassoc->identify();
  }

  return;
}

void MvtxClusterizer::PrintClusters(PHCompositeNode *topNode)
{
  if (Verbosity() > 0)
  {
    TrkrClusterContainer *clusterlist =
        findNode::getClass<TrkrClusterContainer>(topNode, "TRKR_CLUSTER");
    if (!clusterlist)
    {
      return;
    }

    std::cout << "================= After MvtxClusterizer::process_event() "
            "===================="
         << std::endl;

    std::cout << " There are " << clusterlist->size()
         << " clusters recorded: " << std::endl;

    if (Verbosity() > 3)
    {
      clusterlist->identify();
    }

    std::cout << "=================================================================="
            "========="
         << std::endl;
  }

  return;
}
