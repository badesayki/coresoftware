/*!
 *  \file		PHGenFitTrkFitter.C
 *  \brief		Refit SvtxTracks with PHGenFit.
 *  \details	Refit SvtxTracks with PHGenFit.
 *  \author		Haiwang Yu <yuhw@nmsu.edu>
 */

#include "PHGenFitTrkFitter.h"

#include <fun4all/Fun4AllReturnCodes.h>
#include <fun4all/PHTFileServer.h>
#include <fun4all/SubsysReco.h>  // for SubsysReco

#include <g4detectors/PHG4CylinderGeom.h>  // for PHG4CylinderGeom
#include <g4detectors/PHG4CylinderGeomContainer.h>

#include <intt/CylinderGeomIntt.h>
#include <intt/CylinderGeomInttHelper.h>

#include <micromegas/CylinderGeomMicromegas.h>
#include <micromegas/MicromegasDefs.h>

#include <mvtx/CylinderGeom_Mvtx.h>
#include <mvtx/CylinderGeom_MvtxHelper.h>

#include <phfield/PHFieldUtility.h>

#include <phgenfit/Fitter.h>
#include <phgenfit/Measurement.h>  // for Measurement
#include <phgenfit/PlanarMeasurement.h>
#include <phgenfit/SpacepointMeasurement.h>
#include <phgenfit/Track.h>

#include <phgeom/PHGeomUtility.h>

#include <phool/PHCompositeNode.h>
#include <phool/PHIODataNode.h>
#include <phool/PHNode.h>  // for PHNode
#include <phool/PHNodeIterator.h>
#include <phool/PHObject.h>  // for PHObject
#include <phool/getClass.h>
#include <phool/phool.h>

#include <trackbase/ActsGeometry.h>
#include <trackbase/InttDefs.h>
#include <trackbase/MvtxDefs.h>
#include <trackbase/TpcDefs.h>
#include <trackbase/TrkrCluster.h>  // for TrkrCluster
#include <trackbase/TrkrClusterContainer.h>
#include <trackbase/TrkrDefs.h>

#include <trackbase_historic/SvtxTrack.h>
#include <trackbase_historic/SvtxTrackMap.h>
#include <trackbase_historic/SvtxTrackMap_v2.h>
#include <trackbase_historic/SvtxTrackState.h>  // for SvtxTrackState
#include <trackbase_historic/SvtxTrackState_v2.h>
#include <trackbase_historic/SvtxTrack_v4.h>
#include <trackbase_historic/TrackSeed.h>
#include <trackbase_historic/TrackSeedContainer.h>
#include <trackbase_historic/TrackSeedHelper.h>

#include <GenFit/AbsMeasurement.h>  // for AbsMeasurement
#include <GenFit/Exception.h>       // for Exception
#include <GenFit/KalmanFitterInfo.h>
#include <GenFit/MeasuredStateOnPlane.h>
#include <GenFit/RKTrackRep.h>
#include <GenFit/Track.h>
#include <GenFit/TrackPoint.h>  // for TrackPoint

#include <TMatrixDSymfwd.h>  // for TMatrixDSym
#include <TMatrixFfwd.h>     // for TMatrixF
#include <TMatrixT.h>        // for TMatrixT, operator*
#include <TMatrixTSym.h>     // for TMatrixTSym
#include <TMatrixTUtils.h>   // for TMatrixTRow
#include <TRotation.h>
#include <TTree.h>
#include <TVector3.h>
#include <TVectorDfwd.h>  // for TVectorD
#include <TVectorT.h>     // for TVectorT

#include <cmath>  // for sqrt, NAN
#include <iostream>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class PHField;
class TGeoManager;
namespace genfit
{
  class AbsTrackRep;
}

#define LogDebug(exp) std::cout << "DEBUG: " << __FILE__ << ": " << __LINE__ << ": " << (exp) << std::endl
#define LogError(exp) std::cout << "ERROR: " << __FILE__ << ": " << __LINE__ << ": " << (exp) << std::endl
#define LogWarning(exp) std::cout << "WARNING: " << __FILE__ << ": " << __LINE__ << ": " << (exp) << std::endl

using namespace std;

//______________________________________________________
namespace
{

  // square
  template <class T>
  inline static constexpr T square(const T& x)
  {
    return x * x;
  }

  // square
  template <class T>
  inline static T get_r(const T& x, const T& y)
  {
    return std::sqrt( square(x)+square(y));
  }

  // convert gf state to SvtxTrackState_v2
  SvtxTrackState_v2 create_track_state(float pathlength, const genfit::MeasuredStateOnPlane* gf_state)
  {
    SvtxTrackState_v2 out(pathlength);
    out.set_x(gf_state->getPos().x());
    out.set_y(gf_state->getPos().y());
    out.set_z(gf_state->getPos().z());

    out.set_px(gf_state->getMom().x());
    out.set_py(gf_state->getMom().y());
    out.set_pz(gf_state->getMom().z());

    for (int i = 0; i < 6; i++)
    {
      for (int j = i; j < 6; j++)
      {
        out.set_error(i, j, gf_state->get6DCov()[i][j]);
      }
    }

    return out;
  }

  // get cluster keys from a given track
  std::vector<TrkrDefs::cluskey> get_cluster_keys(const SvtxTrack* track)
  {
    std::vector<TrkrDefs::cluskey> out;
    for (const auto& seed : {track->get_silicon_seed(), track->get_tpc_seed()})
    {
      if (seed)
      {
        std::copy(seed->begin_cluster_keys(), seed->end_cluster_keys(), std::back_inserter(out));
      }
    }
    return out;
  }

  [[maybe_unused]] std::ostream& operator<<(std::ostream& out, const Acts::Vector3& vector)
  {
    out << "(" << vector.x() << ", " << vector.y() << ", " << vector.z() << ")";
    return out;
  }

  TVector3 get_world_from_local_vect( ActsGeometry* geometry, Surface surface, const TVector3& local_vect )
  {

    // get global vector from local, using ACTS surface
    Acts::Vector3 local(
      local_vect.x()*Acts::UnitConstants::cm,
      local_vect.y()*Acts::UnitConstants::cm,
      local_vect.z()*Acts::UnitConstants::cm );

    // TODO: check signification of the last two parameters to referenceFrame.
    const Acts::Vector3 global = surface->referenceFrame(geometry->geometry().getGeoContext(), {0,0,0}, {0,0,0})*local;
    return TVector3(
      global.x()/Acts::UnitConstants::cm,
      global.y()/Acts::UnitConstants::cm,
      global.z()/Acts::UnitConstants::cm );
  }

}  // namespace

/*
 * Constructor
 */
PHGenFitTrkFitter::PHGenFitTrkFitter(const string& name)
  : SubsysReco(name)
{
}

/*
 * Init
 */
int PHGenFitTrkFitter::Init(PHCompositeNode* /*topNode*/)
{
  return Fun4AllReturnCodes::EVENT_OK;
}

/*
 * Init run
 */
int PHGenFitTrkFitter::InitRun(PHCompositeNode* topNode)
{
  CreateNodes(topNode);

  auto tgeo_manager = PHGeomUtility::GetTGeoManager(topNode);
  auto field = PHFieldUtility::GetFieldMapNode(nullptr, topNode);

  _fitter.reset(PHGenFit::Fitter::getInstance(tgeo_manager, field, _track_fitting_alg_name, "RKTrackRep", false));
  _fitter->set_verbosity(Verbosity());

  std::cout << "PHGenFitTrkFitter::InitRun - m_fit_silicon_mms: " << m_fit_silicon_mms << std::endl;
  std::cout << "PHGenFitTrkFitter::InitRun - m_use_micromegas: " << m_use_micromegas << std::endl;

  // print disabled layers
  // if( Verbosity() )
  {
    for (const auto& layer : _disabled_layers)
    {
      std::cout << PHWHERE << " Layer " << layer << " is disabled." << std::endl;
    }
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

/*
 * process_event():
 *  Call user instructions for every event.
 *  This function contains the analysis structure.
 *
 */
int PHGenFitTrkFitter::process_event(PHCompositeNode* topNode)
{
  ++_event;

  if (Verbosity() > 1)
  {
    std::cout << PHWHERE << "Events processed: " << _event << std::endl;
  }

  // clear global position map
  GetNodes(topNode);

  // clear default track map, fill with seeds
  m_trackMap->Reset();

  unsigned int trackid = 0;
  for (const auto& track : *m_seedMap)
  {
    if (!track) continue;

    // get silicon seed and check
    const auto siid = track->get_silicon_seed_index();
    if (siid == std::numeric_limits<unsigned int>::max()) continue;
    const auto siseed = m_siliconSeeds->get(siid);
    if (!siseed) continue;

    // get crossing number and check
    const auto crossing = siseed->get_crossing();
    if (crossing == SHRT_MAX) continue;

    // get tpc seed and check
    const auto tpcid = track->get_tpc_seed_index();
    const auto tpcseed = m_tpcSeeds->get(tpcid);
    if (!tpcseed) continue;

    // build track
    auto svtxtrack = std::make_unique<SvtxTrack_v4>();
    svtxtrack->set_id(trackid++);
    svtxtrack->set_silicon_seed(siseed);
    svtxtrack->set_tpc_seed(tpcseed);
    svtxtrack->set_crossing(crossing);

    // track position comes from silicon seed
    const auto position = TrackSeedHelper::get_xyz(siseed);
    svtxtrack->set_x(position.x());
    svtxtrack->set_y(position.y());
    svtxtrack->set_z(position.z());

    // track momentum comes from tpc seed
    svtxtrack->set_charge(tpcseed->get_qOverR() > 0 ? 1 : -1);
    svtxtrack->set_px(tpcseed->get_px());
    svtxtrack->set_py(tpcseed->get_py());
    svtxtrack->set_pz(tpcseed->get_pz());

    // insert in map
    m_trackMap->insert(svtxtrack.get());
  }

  // stands for Refit_GenFit_Tracks
  vector<genfit::Track*> rf_gf_tracks;
  vector<std::shared_ptr<PHGenFit::Track> > rf_phgf_tracks;

  map<unsigned int, unsigned int> svtxtrack_genfittrack_map;

  for (const auto& [key, svtx_track] : *m_trackMap)
  {
    if (!svtx_track) continue;

    if (Verbosity() > 10)
    {
      cout << "   process SVTXTrack " << key << endl;
      svtx_track->identify();
    }

    if (!(svtx_track->get_pt() > _fit_min_pT)) continue;

    // This is the final track (re)fit. It does not include the collision vertex. If fit_primary_track is set, a refit including the vertex is done below.
    // rf_phgf_track stands for Refit_PHGenFit_Track
    const auto rf_phgf_track = ReFitTrack(topNode, svtx_track);
    if (rf_phgf_track)
    {
      svtxtrack_genfittrack_map[svtx_track->get_id()] = rf_phgf_tracks.size();
      rf_phgf_tracks.push_back(rf_phgf_track);
      if (rf_phgf_track->get_ndf() > _vertex_min_ndf)
      {
        rf_gf_tracks.push_back(rf_phgf_track->getGenFitTrack());
      }

      if (Verbosity() > 10) cout << "Done refitting input track" << svtx_track->get_id() << " or rf_phgf_track " << rf_phgf_tracks.size() << endl;
    }
    else if (Verbosity() >= 1)
    {
      cout << "failed refitting input track# " << key << endl;
    }
  }

  // Finds the refitted rf_phgf_track corresponding to each SvtxTrackMap entry
  // Converts it to an SvtxTrack in MakeSvtxTrack
  // MakeSvtxTrack takes a vertex that it gets from the map made in FillSvtxVertex
  // If the refit was succesful, the track on the node tree is replaced with the new one
  // If not, the track is erased from the node tree
  for (SvtxTrackMap::Iter iter = m_trackMap->begin(); iter != m_trackMap->end();)
  {
    std::shared_ptr<PHGenFit::Track> rf_phgf_track;

    // find the genfit track that corresponds to this one on the node tree
    unsigned int itrack = 0;
    if (svtxtrack_genfittrack_map.find(iter->second->get_id()) != svtxtrack_genfittrack_map.end())
    {
      itrack = svtxtrack_genfittrack_map[iter->second->get_id()];
      rf_phgf_track = rf_phgf_tracks[itrack];
    }

    if (rf_phgf_track)
    {
      const auto rf_track = MakeSvtxTrack(iter->second, rf_phgf_track);
      if (rf_track)
      {
        // replace track in map
        iter->second->CopyFrom(rf_track.get());
      }
      else
      {
        // converting track failed. erase track from map
        auto key = iter->first;
        ++iter;
        m_trackMap->erase(key);
        continue;
      }
    }
    else
    {
      // genfit track is invalid. erase track from map
      auto key = iter->first;
      ++iter;
      m_trackMap->erase(key);
      continue;
    }

    ++iter;
  }

  // clear genfit tracks
  rf_phgf_tracks.clear();

  return Fun4AllReturnCodes::EVENT_OK;
}

/*
 * End
 */
int PHGenFitTrkFitter::End(PHCompositeNode* /*topNode*/)
{
  return Fun4AllReturnCodes::EVENT_OK;
}

int PHGenFitTrkFitter::CreateNodes(PHCompositeNode* topNode)
{
  // create nodes...
  PHNodeIterator iter(topNode);

  auto dstNode = static_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "DST"));
  if (!dstNode)
  {
    cerr << PHWHERE << "DST Node missing, doing nothing." << endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }
  PHNodeIterator iter_dst(dstNode);

  // Create the SVTX node
  auto svtx_node = dynamic_cast<PHCompositeNode*>(iter_dst.findFirst("PHCompositeNode", "SVTX"));
  if (!svtx_node)
  {
    svtx_node = new PHCompositeNode("SVTX");
    dstNode->addNode(svtx_node);
    if (Verbosity())
    {
      cout << "SVTX node added" << endl;
    }
  }

  // default track map
  m_trackMap = findNode::getClass<SvtxTrackMap>(topNode, _trackMap_name);
  if (!m_trackMap)
  {
    m_trackMap = new SvtxTrackMap_v2;
    auto node = new PHIODataNode<PHObject>(m_trackMap, _trackMap_name, "PHObject");
    svtx_node->addNode(node);
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

//______________________________________________________
void PHGenFitTrkFitter::disable_layer(int layer, bool disabled)
{
  if (disabled)
    _disabled_layers.insert(layer);
  else
    _disabled_layers.erase(layer);
}

//______________________________________________________
void PHGenFitTrkFitter::set_disabled_layers(const std::set<int>& layers)
{
  _disabled_layers = layers;
}

//______________________________________________________
void PHGenFitTrkFitter::clear_disabled_layers()
{
  _disabled_layers.clear();
}

//______________________________________________________
const std::set<int>& PHGenFitTrkFitter::get_disabled_layers() const
{
  return _disabled_layers;
}

//______________________________________________________
void PHGenFitTrkFitter::set_fit_silicon_mms(bool value)
{
  // store flags
  m_fit_silicon_mms = value;

  // disable/enable layers accordingly
  for (int layer = 7; layer < 23; ++layer)
  {
    disable_layer(layer, value);
  }
  for (int layer = 23; layer < 39; ++layer)
  {
    disable_layer(layer, value);
  }
  for (int layer = 39; layer < 55; ++layer)
  {
    disable_layer(layer, value);
  }
}

/*
 * GetNodes():
 *  Get all the all the required nodes off the node tree
 */
int PHGenFitTrkFitter::GetNodes(PHCompositeNode* topNode)
{
  // acts geometry
  m_tgeometry = findNode::getClass<ActsGeometry>(topNode, "ActsGeometry");
  if (!m_tgeometry)
  {
    std::cout << "PHGenFitTrkFitter::GetNodes - No acts tracking geometry, can't proceed" << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  // DST objects
  // clusters
  m_clustermap = findNode::getClass<TrkrClusterContainer>(topNode, "CORRECTED_TRKR_CLUSTER");
  if (m_clustermap)
  {
    if (_event < 2)
    {
      std::cout << "PHGenFitTrkFitter::GetNodes - Using CORRECTED_TRKR_CLUSTER node " << std::endl;
    }
  }
  else
  {
    if (_event < 2)
    {
      std::cout << "PHGenFitTrkFitter::GetNodes - CORRECTED_TRKR_CLUSTER node not found, using TRKR_CLUSTER" << std::endl;
    }
    m_clustermap = findNode::getClass<TrkrClusterContainer>(topNode, "TRKR_CLUSTER");
  }

  if (!m_clustermap)
  {
    cout << PHWHERE << "PHGenFitTrkFitter::GetNodes - TRKR_CLUSTER node not found on node tree" << endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  // seeds
  m_seedMap = findNode::getClass<TrackSeedContainer>(topNode, _seedMap_name);
  if (!m_seedMap)
  {
    std::cout << "PHGenFitTrkFitter::GetNodes - No Svtx seed map on node tree. Exiting." << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  m_tpcSeeds = findNode::getClass<TrackSeedContainer>(topNode, "TpcTrackSeedContainer");
  if (!m_tpcSeeds)
  {
    std::cout << "PHGenFitTrkFitter::GetNodes - TpcTrackSeedContainer not on node tree. Bailing"
              << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  m_siliconSeeds = findNode::getClass<TrackSeedContainer>(topNode, "SiliconTrackSeedContainer");
  if (!m_siliconSeeds)
  {
    std::cout << "PHGenFitTrkFitter::GetNodes - SiliconTrackSeedContainer not on node tree. Bailing"
              << std::endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  // Svtx Tracks
  m_trackMap = findNode::getClass<SvtxTrackMap>(topNode, _trackMap_name);
  if (!m_trackMap && _event < 2)
  {
    cout << "PHGenFitTrkFitter::GetNodes - SvtxTrackMap node not found on node tree" << endl;
    return Fun4AllReturnCodes::ABORTEVENT;
  }

  // global position wrapper
  m_globalPositionWrapper.loadNodes(topNode);
  if (m_disable_module_edge_corr) { m_globalPositionWrapper.set_enable_module_edge_corr(false); }
  if (m_disable_static_corr) { m_globalPositionWrapper.set_enable_static_corr(false); }
  if (m_disable_average_corr) { m_globalPositionWrapper.set_enable_average_corr(false); }
  if (m_disable_fluctuation_corr) { m_globalPositionWrapper.set_enable_fluctuation_corr(false); }

  return Fun4AllReturnCodes::EVENT_OK;
}

/*
 * fit track with SvtxTrack as input seed.
 * \param intrack Input SvtxTrack
 */
std::shared_ptr<PHGenFit::Track> PHGenFitTrkFitter::ReFitTrack(PHCompositeNode* /*topNode*/, const SvtxTrack* intrack)
{
  // std::shared_ptr<PHGenFit::Track> empty_track(nullptr);
  if (!intrack)
  {
    cerr << PHWHERE << " Input SvtxTrack is nullptr!" << endl;
    return nullptr;
  }

  // get crossing from track
  const auto crossing = intrack->get_crossing();
  assert(crossing != SHRT_MAX);

  // prepare seed
  TVector3 seed_mom(100, 0, 0);
  TVector3 seed_pos(0, 0, 0);
  TMatrixDSym seed_cov(6);
  for (int i = 0; i < 6; i++)
  {
    for (int j = 0; j < 6; j++)
    {
      seed_cov[i][j] = 100.;
    }
  }

  // Create measurements
  std::vector<PHGenFit::Measurement*> measurements;

  // sort clusters with radius before fitting
  if (Verbosity() > 10)
  {
    intrack->identify();
  }
  std::map<float, TrkrDefs::cluskey> m_r_cluster_id;

  unsigned int n_silicon_clusters = 0;
  unsigned int n_micromegas_clusters = 0;

  for (const auto& cluster_key : get_cluster_keys(intrack))
  {
    // count clusters
    switch (TrkrDefs::getTrkrId(cluster_key))
    {
    case TrkrDefs::mvtxId:
    case TrkrDefs::inttId:
      ++n_silicon_clusters;
      break;

    case TrkrDefs::micromegasId:
      ++n_micromegas_clusters;
      break;

    default:
      break;
    }

    const auto cluster = m_clustermap->findCluster(cluster_key);
    const auto globalPosition = m_globalPositionWrapper.getGlobalPositionDistortionCorrected(cluster_key, cluster, crossing);
    const float r = get_r(globalPosition.x(), globalPosition.y());
    m_r_cluster_id.emplace(r, cluster_key);
    if (Verbosity() > 10)
    {
      const int layer_out = TrkrDefs::getLayer(cluster_key);
      cout << "    Layer " << layer_out << " cluster " << cluster_key << " radius " << r << endl;
    }
  }

  // discard track if not enough clusters when fitting with silicon + mm only
  if (m_fit_silicon_mms)
  {
    if (n_silicon_clusters == 0)
    {
      return nullptr;
    }
    if (m_use_micromegas && n_micromegas_clusters == 0)
    {
      return nullptr;
    }
  }

  for (const auto& [r, cluster_key] : m_r_cluster_id)
  {
    const int layer = TrkrDefs::getLayer(cluster_key);

    // skip disabled layers
    if (_disabled_layers.find(layer) != _disabled_layers.end())
    {
      continue;
    }

    auto cluster = m_clustermap->findCluster(cluster_key);
    if (!cluster)
    {
      LogError("No cluster Found!");
      continue;
    }

    const auto globalPosition_acts = m_globalPositionWrapper.getGlobalPositionDistortionCorrected(cluster_key, cluster, crossing);
    const TVector3 pos(globalPosition_acts.x(), globalPosition_acts.y(), globalPosition_acts.z());

    const double cluster_rphi_error = cluster->getRPhiError();
    const double cluster_z_error = cluster->getZError();

    seed_mom.SetPhi(pos.Phi());
    seed_mom.SetTheta(pos.Theta());

    std::unique_ptr<PHGenFit::PlanarMeasurement> meas;
    switch (TrkrDefs::getTrkrId(cluster_key))
	    {
	    case TrkrDefs::mvtxId:
	    {
	      auto hitsetkey = TrkrDefs::getHitSetKeyFromClusKey(cluster_key);
	      auto surface = m_tgeometry->maps().getSiliconSurface(hitsetkey);
        const auto u = get_world_from_local_vect(m_tgeometry, surface, {1, 0, 0});
        const auto v = get_world_from_local_vect(m_tgeometry, surface, {0, 1, 0});
	      meas.reset( new PHGenFit::PlanarMeasurement(pos, u, v, cluster_rphi_error, cluster_z_error) );

	      break;
	    }

	    case TrkrDefs::inttId:
	    {
	      auto hitsetkey = TrkrDefs::getHitSetKeyFromClusKey(cluster_key);
	      auto surface = m_tgeometry->maps().getSiliconSurface(hitsetkey);
        const auto u = get_world_from_local_vect(m_tgeometry, surface, {1, 0, 0});
        const auto v = get_world_from_local_vect(m_tgeometry, surface, {0, 1, 0});
	      meas.reset( new PHGenFit::PlanarMeasurement(pos, u, v, cluster_rphi_error, cluster_z_error) );
	      break;
	    }

	    case TrkrDefs::micromegasId:
	    {

	      // get geometry
	      /* a situation where micromegas clusters are found, but not the geometry, should not happen */
	      auto hitsetkey = TrkrDefs::getHitSetKeyFromClusKey(cluster_key);
	      auto surface = m_tgeometry->maps().getMMSurface(hitsetkey);
        const auto u = get_world_from_local_vect(m_tgeometry, surface, {1, 0, 0});
        const auto v = get_world_from_local_vect(m_tgeometry, surface, {0, 1, 0});
	      meas.reset( new PHGenFit::PlanarMeasurement(pos, u, v, cluster_rphi_error, cluster_z_error) );
	      break;
	    }

	    case TrkrDefs::tpcId:
	    {
	      // create measurement
	      const TVector3 n(globalPosition_acts.x(), globalPosition_acts.y(), 0);
	      meas.reset( new PHGenFit::PlanarMeasurement(pos, n, cluster_rphi_error, cluster_z_error) );
	      break;
	    }

    }

    // assign cluster key to measurement
    meas->set_cluster_key( cluster_key );

    // add to list
    measurements.push_back(meas.release());
  }

  /*!
   * mu+:	-13
   * mu-:	13
   * pi+:	211
   * pi-:	-211
   * e-:	11
   * e+:	-11
   */
  // TODO Add multiple TrackRep choices.
  // int pid = 211;
  auto rep = new genfit::RKTrackRep(_primary_pid_guess);
  std::shared_ptr<PHGenFit::Track> track(new PHGenFit::Track(rep, seed_pos, seed_mom, seed_cov));

  // TODO unsorted measurements, should use sorted ones?
  track->addMeasurements(measurements);

  /*!
   *  Fit the track
   *  ret code 0 means 0 error or good status
   */

  if (_fitter->processTrack(track.get(), false) != 0)
  {
    // if (Verbosity() >= 1)
    {
      LogWarning("Track fitting failed");
    }
    // delete track;
    return nullptr;
  }

  if (Verbosity() > 10)
    cout << " track->getChisq() " << track->get_chi2() << " get_ndf " << track->get_ndf()
         << " mom.X " << track->get_mom().X()
         << " mom.Y " << track->get_mom().Y()
         << " mom.Z " << track->get_mom().Z()
         << endl;

  return track;
}

/*
 * Make SvtxTrack from PHGenFit::Track and SvtxTrack
 */
// SvtxTrack* PHGenFitTrkFitter::MakeSvtxTrack(const SvtxTrack* svtx_track,
std::shared_ptr<SvtxTrack> PHGenFitTrkFitter::MakeSvtxTrack(const SvtxTrack* svtx_track, const std::shared_ptr<PHGenFit::Track>& phgf_track)
{
  double chi2 = phgf_track->get_chi2();
  double ndf = phgf_track->get_ndf();

  TVector3 vertex_position(0, 0, 0);
  TMatrixF vertex_cov(3, 3);
  double dvr2 = 0;
  double dvz2 = 0;

  std::unique_ptr<genfit::MeasuredStateOnPlane> gf_state_beam_line_ca;
  try
  {
    gf_state_beam_line_ca.reset(phgf_track->extrapolateToLine(vertex_position, TVector3(0., 0., 1.)));
  }
  catch (...)
  {
    if (Verbosity() >= 2)
    {
      LogWarning("extrapolateToLine failed!");
    }
  }
  if (!gf_state_beam_line_ca)
  {
    return nullptr;
  }

  /*!
   *  1/p, u'/z', v'/z', u, v
   *  u is defined as momentum X beam line at POCA of the beam line
   *  v is alone the beam line
   *  so u is the dca2d direction
   */

  double u = gf_state_beam_line_ca->getState()[3];
  double v = gf_state_beam_line_ca->getState()[4];

  double du2 = gf_state_beam_line_ca->getCov()[3][3];
  double dv2 = gf_state_beam_line_ca->getCov()[4][4];
  // cout << PHWHERE << "        u " << u << " v " << v << " du2 " << du2 << " dv2 " << dv2 << " dvr2 " << dvr2 << endl;
  // delete gf_state_beam_line_ca;

  // create new track
  auto out_track = std::make_shared<SvtxTrack_v4>(*svtx_track);

  // clear states and insert empty one for vertex position
  out_track->clear_states();
  {
    /*
    insert first, dummy state, as done in constructor,
    so that the track state list is never empty. Note that insert_state, despite taking a pointer as argument,
    does not take ownership of the state
    */
    SvtxTrackState_v2 first(0.0);
    out_track->insert_state(&first);
  }

  out_track->set_dca2d(u);
  out_track->set_dca2d_error(sqrt(du2 + dvr2));

  std::unique_ptr<genfit::MeasuredStateOnPlane> gf_state_vertex_ca;
  try
  {
    gf_state_vertex_ca.reset(phgf_track->extrapolateToPoint(vertex_position));
  }
  catch (...)
  {
    if (Verbosity() >= 2)
    {
      LogWarning("extrapolateToPoint failed!");
    }
  }
  if (!gf_state_vertex_ca)
  {
    // delete out_track;
    return nullptr;
  }

  const auto mom = gf_state_vertex_ca->getMom();
  const auto pos = gf_state_vertex_ca->getPos();
  const auto cov = gf_state_vertex_ca->get6DCov();

  //	genfit::MeasuredStateOnPlane* gf_state_vertex_ca =
  //			phgf_track->extrapolateToLine(vertex_position,
  //					TVector3(0., 0., 1.));

  u = gf_state_vertex_ca->getState()[3];
  v = gf_state_vertex_ca->getState()[4];

  du2 = gf_state_vertex_ca->getCov()[3][3];
  dv2 = gf_state_vertex_ca->getCov()[4][4];

  double dca3d = sqrt(square(u) + square(v));
  double dca3d_error = sqrt(du2 + dv2 + dvr2 + dvz2);

  out_track->set_dca(dca3d);
  out_track->set_dca_error(dca3d_error);

  //
  // in: X, Y, Z; out; r: n X Z, Z X r, Z

  float dca3d_xy = NAN;
  float dca3d_z = NAN;
  float dca3d_xy_error = NAN;
  float dca3d_z_error = NAN;

  try
  {
    TMatrixF pos_in(3, 1);
    TMatrixF cov_in(3, 3);
    TMatrixF pos_out(3, 1);
    TMatrixF cov_out(3, 3);

    TVectorD state6(6);      // pos(3), mom(3)
    TMatrixDSym cov6(6, 6);  //

    gf_state_vertex_ca->get6DStateCov(state6, cov6);

    TVector3 vn(state6[3], state6[4], state6[5]);

    // mean of two multivariate gaussians Pos - Vertex
    pos_in[0][0] = state6[0] - vertex_position.X();
    pos_in[1][0] = state6[1] - vertex_position.Y();
    pos_in[2][0] = state6[2] - vertex_position.Z();

    for (int i = 0; i < 3; ++i)
    {
      for (int j = 0; j < 3; ++j)
      {
        cov_in[i][j] = cov6[i][j] + vertex_cov[i][j];
      }
    }

    // vn is momentum vector, pos_in is position vector (of what?)
    pos_cov_XYZ_to_RZ(vn, pos_in, cov_in, pos_out, cov_out);

    if (Verbosity() > 30)
    {
      cout << " vn.X " << vn.X() << " vn.Y " << vn.Y() << " vn.Z " << vn.Z() << endl;
      cout << " pos_in.X " << pos_in[0][0] << " pos_in.Y " << pos_in[1][0] << " pos_in.Z " << pos_in[2][0] << endl;
      cout << " pos_out.X " << pos_out[0][0] << " pos_out.Y " << pos_out[1][0] << " pos_out.Z " << pos_out[2][0] << endl;
    }

    dca3d_xy = pos_out[0][0];
    dca3d_z = pos_out[2][0];
    dca3d_xy_error = sqrt(cov_out[0][0]);
    dca3d_z_error = sqrt(cov_out[2][2]);

#ifdef _DEBUG_
    cout << __LINE__ << ": Vertex: ----------------" << endl;
    vertex_position.Print();
    vertex_cov.Print();

    cout << __LINE__ << ": State: ----------------" << endl;
    state6.Print();
    cov6.Print();

    cout << __LINE__ << ": Mean: ----------------" << endl;
    pos_in.Print();
    cout << "===>" << endl;
    pos_out.Print();

    cout << __LINE__ << ": Cov: ----------------" << endl;
    cov_in.Print();
    cout << "===>" << endl;
    cov_out.Print();

    cout << endl;
#endif
  }
  catch (...)
  {
    if (Verbosity())
    {
      LogWarning("DCA calculationfailed!");
    }
  }

  out_track->set_dca3d_xy(dca3d_xy);
  out_track->set_dca3d_z(dca3d_z);
  out_track->set_dca3d_xy_error(dca3d_xy_error);
  out_track->set_dca3d_z_error(dca3d_z_error);

  // if(gf_state_vertex_ca) delete gf_state_vertex_ca;

  out_track->set_chisq(chi2);
  out_track->set_ndf(ndf);
  out_track->set_charge(phgf_track->get_charge());

  out_track->set_px(mom.Px());
  out_track->set_py(mom.Py());
  out_track->set_pz(mom.Pz());

  out_track->set_x(pos.X());
  out_track->set_y(pos.Y());
  out_track->set_z(pos.Z());

  for (int i = 0; i < 6; i++)
  {
    for (int j = i; j < 6; j++)
    {
      out_track->set_error(i, j, cov[i][j]);
    }
  }

#ifdef _DEBUG_
  cout << __LINE__ << endl;
#endif

  const auto gftrack = phgf_track->getGenFitTrack();
  const auto rep = gftrack->getCardinalRep();
  for (unsigned int id = 0; id < gftrack->getNumPointsWithMeasurement(); ++id)
  {
    genfit::TrackPoint* trpoint = gftrack->getPointWithMeasurementAndFitterInfo(id, gftrack->getCardinalRep());

    if (!trpoint)
    {
      if (Verbosity() > 1) LogWarning("!trpoint");
      continue;
    }

    auto kfi = static_cast<genfit::KalmanFitterInfo*>(trpoint->getFitterInfo(rep));
    if (!kfi)
    {
      if (Verbosity() > 1) LogWarning("!kfi");
      continue;
    }

    const genfit::MeasuredStateOnPlane* gf_state = nullptr;
    try
    {
      // this works because KalmanFitterInfo returns a const reference to internal object and not a temporary object
      gf_state = &kfi->getFittedState(true);
    }
    catch (...)
    {
      if (Verbosity() >= 1)
        LogWarning("Exrapolation failed!");
    }
    if (!gf_state)
    {
      if (Verbosity() >= 1)
        LogWarning("Exrapolation failed!");
      continue;
    }
    genfit::MeasuredStateOnPlane temp;
    float pathlength = -phgf_track->extrapolateToPoint(temp, vertex_position, id);

    // create new svtx state and add to track
    auto state = create_track_state(pathlength, gf_state);

    // get matching cluster key from phgf_track and assign to state
    state.set_cluskey(phgf_track->get_cluster_keys()[id]);

    out_track->insert_state(&state);

#ifdef _DEBUG_
    cout
        << __LINE__
        << ": " << id
        << ": " << pathlength << " => "
        << sqrt(square(state->get_x()) + square(state->get_y()))
        << endl;
#endif
  }

  // loop over clusters, check if layer is disabled, include extrapolated SvtxTrackState
  if (!_disabled_layers.empty())
  {
    // get crossing
    const auto crossing = svtx_track->get_crossing();
    assert(crossing != SHRT_MAX);

    unsigned int id_min = 0;
    for (const auto& cluster_key : get_cluster_keys(svtx_track))
    {
      const auto cluster = m_clustermap->findCluster(cluster_key);
      const auto layer = TrkrDefs::getLayer(cluster_key);

      // skip enabled layers
      if (_disabled_layers.find(layer) == _disabled_layers.end())
      {
        continue;
      }

      // get position
      const auto globalPosition = m_globalPositionWrapper.getGlobalPositionDistortionCorrected( cluster_key, cluster, crossing );
      const TVector3 pos_A(globalPosition.x(), globalPosition.y(), globalPosition.z() );
      const float r_cluster = std::sqrt( square(globalPosition.x()) + square(globalPosition.y()) );

      // loop over states
      /* find first state whose radius is larger than that of cluster if any */
      unsigned int id = id_min;
      for (; id < gftrack->getNumPointsWithMeasurement(); ++id)
      {
        auto trpoint = gftrack->getPointWithMeasurementAndFitterInfo(id, rep);
        if (!trpoint) continue;

        auto kfi = static_cast<genfit::KalmanFitterInfo*>(trpoint->getFitterInfo(rep));
        if (!kfi) continue;

        const genfit::MeasuredStateOnPlane* gf_state = nullptr;
        try
        {
          gf_state = &kfi->getFittedState(true);
        }
        catch (...)
        {
          if (Verbosity())
          {
            LogWarning("Failed to get kf fitted state");
          }
        }

        if (!gf_state) continue;

        float r_track = std::sqrt(square(gf_state->getPos().x()) + square(gf_state->getPos().y()));
        if (r_track > r_cluster) break;
      }

      // forward extrapolation
      genfit::MeasuredStateOnPlane gf_state;
      float pathlength = 0;

      // first point is previous, if valid
      if (id > 0) id_min = id - 1;

      // extrapolate forward
      try
      {
        auto trpoint = gftrack->getPointWithMeasurementAndFitterInfo(id_min, rep);
        if (!trpoint) continue;

        auto kfi = static_cast<genfit::KalmanFitterInfo*>(trpoint->getFitterInfo(rep));
        gf_state = *kfi->getForwardUpdate();
        pathlength = gf_state.extrapolateToPoint( pos_A );
        auto tmp = *kfi->getBackwardUpdate();
        pathlength -= tmp.extrapolateToPoint(vertex_position);
      }
      catch (...)
      {
        if (Verbosity())
        {
          std::cerr << PHWHERE << "Failed to forward extrapolate from id " << id_min << " to disabled layer " << layer << std::endl;
        }
        continue;
      }

      // also extrapolate backward from next state if any
      // and take the weighted average between both points
      if (id > 0 && id < gftrack->getNumPointsWithMeasurement())
        try
        {
          auto trpoint = gftrack->getPointWithMeasurementAndFitterInfo(id, rep);
          if (!trpoint) continue;

          auto kfi = static_cast<genfit::KalmanFitterInfo*>(trpoint->getFitterInfo(rep));
          genfit::KalmanFittedStateOnPlane gf_state_backward = *kfi->getBackwardUpdate();
          gf_state_backward.extrapolateToPlane(gf_state.getPlane());
          gf_state = genfit::calcAverageState(gf_state, gf_state_backward);
        }
        catch (...)
        {
          if (Verbosity())
          {
            std::cerr << PHWHERE << "Failed to backward extrapolate from id " << id << " to disabled layer " << layer << std::endl;
          }
          continue;
        }

      // create new svtx state and add to track
      auto state = create_track_state(pathlength, &gf_state);
      state.set_cluskey(cluster_key);
      out_track->insert_state(&state);
    }
  }

  // printout all track state
  if (Verbosity())
  {
    for (auto&& iter = out_track->begin_states(); iter != out_track->end_states(); ++iter)
    {
      const auto& [pathlength, state] = *iter;
      const auto r = std::sqrt(square(state->get_x()) + square(state->get_y()));
      const auto phi = std::atan2(state->get_y(), state->get_x());
      std::cout << "PHGenFitTrkFitter::MakeSvtxTrack -"
                << " pathlength: " << pathlength
                << " radius: " << r
                << " phi: " << phi
                << " z: " << state->get_z()
                << std::endl;
    }

    std::cout << std::endl;
  }
  return out_track;
}

//______________________________________________________________________
bool PHGenFitTrkFitter::pos_cov_XYZ_to_RZ(
    const TVector3& n, const TMatrixF& pos_in, const TMatrixF& cov_in,
    TMatrixF& pos_out, TMatrixF& cov_out) const
{
  if (pos_in.GetNcols() != 1 || pos_in.GetNrows() != 3)
  {
    if (Verbosity())
    {
      LogWarning("pos_in.GetNcols() != 1 || pos_in.GetNrows() != 3");
    }
    return false;
  }

  if (cov_in.GetNcols() != 3 || cov_in.GetNrows() != 3)
  {
    if (Verbosity())
    {
      LogWarning("cov_in.GetNcols() != 3 || cov_in.GetNrows() != 3");
    }
    return false;
  }

  // produces a vector perpendicular to both the momentum vector and beam line - i.e. in the direction of the dca_xy
  // only the angle of r will be used, not the magnitude
  TVector3 r = n.Cross(TVector3(0., 0., 1.));
  if (r.Mag() < 0.00001)
  {
    if (Verbosity())
    {
      LogWarning("n is parallel to z");
    }
    return false;
  }

  // R: rotation from u,v,n to n X Z, nX(nXZ), n
  TMatrixF R(3, 3);
  TMatrixF R_T(3, 3);

  try
  {
    // rotate u along z to up
    float phi = -TMath::ATan2(r.Y(), r.X());
    R[0][0] = cos(phi);
    R[0][1] = -sin(phi);
    R[0][2] = 0;
    R[1][0] = sin(phi);
    R[1][1] = cos(phi);
    R[1][2] = 0;
    R[2][0] = 0;
    R[2][1] = 0;
    R[2][2] = 1;

    R_T.Transpose(R);
  }
  catch (...)
  {
    if (Verbosity())
    {
      LogWarning("Can't get rotation matrix");
    }

    return false;
  }

  pos_out.ResizeTo(3, 1);
  cov_out.ResizeTo(3, 3);

  pos_out = R * pos_in;
  cov_out = R * cov_in * R_T;

  return true;
}
