/**
 *  @file   SpacePointHit3DBuilder_tool.cc
 * 
 *  @brief  This tool provides "standard" 3D hits built (by this tool) from 2D hits
 * 
 */

// Framework Includes
#include "art/Utilities/ToolMacros.h"
#include "cetlib/search_path.h"
#include "cetlib/cpu_timer.h"
#include "canvas/Utilities/InputTag.h"

#include "larreco/RecoAlg/Cluster3DAlgs/IHit3DBuilder.h"

// LArSoft includes
#include "larcore/Geometry/Geometry.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/SpacePoint.h"
#include "lardata/RecoObjects/Cluster3D.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusService.h"
#include "larevt/CalibrationDBI/Interface/ChannelStatusProvider.h"

// std includes
#include <string>
#include <functional>
#include <iostream>
#include <memory>

//------------------------------------------------------------------------------------------------------------------------------------------
// implementation follows

namespace lar_cluster3d {

/**
 *  @brief  SpacePointHit3DBuilder class definiton
 */
class SpacePointHit3DBuilder : virtual public IHit3DBuilder
{
public:
    /**
     *  @brief  Constructor
     *
     *  @param  pset
     */
    explicit SpacePointHit3DBuilder(fhicl::ParameterSet const &pset);
    
    /**
     *  @brief  Destructor
     */
    ~SpacePointHit3DBuilder();
    
    void configure(const fhicl::ParameterSet&) override;
    
    /**
     *  @brief Given a set of recob hits, run DBscan to form 3D clusters
     *
     *  @param hitPairList           The input list of 3D hits to run clustering on
     *  @param clusterParametersList A list of cluster objects (parameters from associated hits)
     */
    void Hit3DBuilder(const art::Event &evt, reco::HitPairList& hitPairList, RecobHitToPtrMap&) const override;
    
    /**
     *  @brief If monitoring, recover the time to execute a particular function
     */
    float getTimeToExecute(IHit3DBuilder::TimeValues index) const override {return m_timeVector.at(index);}
    
private:

    using Hit2DVector                 = std::vector<reco::ClusterHit2D>;

    /**
     *  @brief Data members to follow
     */
    art::InputTag                        m_spacePointTag;
    
    bool                                 m_enableMonitoring;      ///<
    mutable std::vector<float>           m_timeVector;            ///<
    
    // Get instances of the primary data structures needed
    mutable Hit2DVector                  m_clusterHit2DMasterVec;
    
    const detinfo::DetectorProperties*   m_detector;              //< Pointer to the detector properties
};

SpacePointHit3DBuilder::SpacePointHit3DBuilder(fhicl::ParameterSet const &pset)
{
    this->configure(pset);
}

//------------------------------------------------------------------------------------------------------------------------------------------

SpacePointHit3DBuilder::~SpacePointHit3DBuilder()
{
}

//------------------------------------------------------------------------------------------------------------------------------------------
    
void SpacePointHit3DBuilder::configure(fhicl::ParameterSet const &pset)
{
    m_spacePointTag    = pset.get<art::InputTag>("SpacePointTag");
    m_enableMonitoring = pset.get<bool>         ("EnableMonitoring",    true);

    m_detector = lar::providerFrom<detinfo::DetectorPropertiesService>();
}

void SpacePointHit3DBuilder::Hit3DBuilder(const art::Event& evt, reco::HitPairList& hitPairList, RecobHitToPtrMap& recobHitToArtPtrMap) const
{
    m_timeVector.resize(NUMTIMEVALUES, 0.);
    
    cet::cpu_timer theClockMakeHits;
    
    if (m_enableMonitoring) theClockMakeHits.start();

    /**
     *  @brief Recover the 2D hits from art and fill out the local data structures for the 3D clustering
     */

    // Start by recovering the associations between space points and hits
    art::Handle< art::Assns<recob::Hit, recob::SpacePoint> > hitSpacePointAssnsHandle;
    evt.getByLabel(m_spacePointTag, hitSpacePointAssnsHandle);
    
    if (!hitSpacePointAssnsHandle.isValid()) return;
    
    // First step is to loop through and get a mapping between space points and associated hits
    // and, importantly, a list of unique hits (and mapping between art ptr and hit)
    using SpacePointHitVecMap = std::map<const recob::SpacePoint*, std::vector<const recob::Hit*>>;
    using RecobHitSet         = std::set<const recob::Hit*>;
    
    SpacePointHitVecMap spacePointHitVecMap;
    RecobHitSet         recobHitSet;

    for(auto& assnPair : *hitSpacePointAssnsHandle)
    {
        const art::Ptr<recob::SpacePoint> spacePoint = assnPair.second;
        const art::Ptr<recob::Hit>&       recobHit   = assnPair.first;

        spacePointHitVecMap[spacePoint.get()].push_back(recobHit.get());
        recobHitSet.insert(recobHit.get());
        recobHitToArtPtrMap[recobHit.get()] = recobHit;
    }
    
    // We'll want to correct the hit times for the plane offsets
    // (note this is already taken care of when converting to position)
    std::map<size_t, double> planeOffsetMap;
    
    planeOffsetMap[0] = m_detector->GetXTicksOffset(0, 0, 0)-m_detector->TriggerOffset();
    planeOffsetMap[1] = m_detector->GetXTicksOffset(1, 0, 0)-m_detector->TriggerOffset();
    planeOffsetMap[2] = m_detector->GetXTicksOffset(2, 0, 0)-m_detector->TriggerOffset();
    
    // We need temporary mapping from recob::Hit's to our 2D hits
    using RecobHitTo2DHitMap = std::map<const recob::Hit*,const reco::ClusterHit2D*>;
    
    RecobHitTo2DHitMap recobHitTo2DHitMap;
    
    // Set the size of the container for our hits
    m_clusterHit2DMasterVec.clear();
    m_clusterHit2DMasterVec.reserve(recobHitSet.size());

    // Now go throught the list of unique hits and create the 2D hits we'll use
    for(auto& recobHit : recobHitSet)
    {
        const geo::WireID& hitWireID(recobHit->WireID());
        
        double hitPeakTime(recobHit->PeakTime() - planeOffsetMap[hitWireID.Plane]);
        double xPosition(m_detector->ConvertTicksToX(recobHit->PeakTime(), hitWireID.Plane, hitWireID.TPC, hitWireID.Cryostat));
        
        m_clusterHit2DMasterVec.emplace_back(0, 0., 0., xPosition, hitPeakTime, *recobHit);
        
        recobHitTo2DHitMap[recobHit]  = &m_clusterHit2DMasterVec.back();
    }
    
    // Now we can go through the space points and build our 3D hits
    for(auto& pointPair : spacePointHitVecMap)
    {
        const recob::SpacePoint*              spacePoint  = pointPair.first;
        const std::vector<const recob::Hit*>& recobHitVec = pointPair.second;
        
        if (recobHitVec.size() != 3)
        {
            std::cout << "************>>>>>> do not have 3 hits associated to space point! " << recobHitVec.size() << " ***************" << std::endl;
            continue;
        }
        
        std::vector<const reco::ClusterHit2D*> hit2DVec(recobHitVec.size());
        
        for(const auto& recobHit : recobHitVec)
        {
            const reco::ClusterHit2D* hit2D = recobHitTo2DHitMap.at(recobHit);
            
            hit2DVec[hit2D->getHit().WireID().Plane] = hit2D;
        }
        
        // Weighted average, delta and sigmas
        float hit0Sigma     = hit2DVec[0]->getHit().RMS();
        float hit1Sigma     = hit2DVec[1]->getHit().RMS();
        float hit2Sigma     = hit2DVec[2]->getHit().RMS();
        float hit0WidWeight = 1. / (hit0Sigma * hit0Sigma);
        float hit1WidWeight = 1. / (hit1Sigma * hit1Sigma);
        float hit2WidWeight = 1. / (hit2Sigma * hit2Sigma);
        float denominator   = 1. / (hit0WidWeight + hit1WidWeight + hit2WidWeight);
        float avePeakTime   = (hit2DVec[0]->getTimeTicks() * hit0WidWeight + hit2DVec[1]->getTimeTicks() * hit1WidWeight + hit2DVec[2]->getTimeTicks() * hit2WidWeight) * denominator;
            
            // The x position is a weighted sum but the y-z position is simply the average
        float position[]  = { float(spacePoint->XYZ()[0]), float(spacePoint->XYZ()[1]), float(spacePoint->XYZ()[2])};
        float totalCharge = hit2DVec[0]->getHit().Integral() + hit2DVec[1]->getHit().Integral() + hit2DVec[2]->getHit().Integral();
            
        std::vector<const reco::ClusterHit2D*> hitVector(3);
            
        // Make sure we have the hits
        hitVector[hit2DVec[0]->getHit().WireID().Plane] = hit2DVec[0];
        hitVector[hit2DVec[1]->getHit().WireID().Plane] = hit2DVec[1];
        hitVector[hit2DVec[2]->getHit().WireID().Plane] = hit2DVec[2];
            
        // And get the wire IDs
        std::vector<geo::WireID> wireIDVec = {geo::WireID(0,0,geo::kU,0), geo::WireID(0,0,geo::kV,0), geo::WireID(0,0,geo::kW,0)};
            
        for(const auto& hit : hitVector)
        {
            wireIDVec[hit->getHit().WireID().Plane] = hit->getHit().WireID();
                
            if (hit->getStatusBits() & reco::ClusterHit2D::USEDINTRIPLET) hit->setStatusBit(reco::ClusterHit2D::SHAREDINTRIPLET);
                
            hit->setStatusBit(reco::ClusterHit2D::USEDINTRIPLET);
        }
            
        unsigned int statusBits(0x7);
            
        // For compiling at the moment
        std::vector<float> hitDelTSigVec = {0.,0.,0.};

        hitDelTSigVec[0] = std::fabs(hitVector[0]->getTimeTicks() - 0.5 * (hitVector[1]->getTimeTicks() + hitVector[2]->getTimeTicks()));
        hitDelTSigVec[1] = std::fabs(hitVector[1]->getTimeTicks() - 0.5 * (hitVector[2]->getTimeTicks() + hitVector[0]->getTimeTicks()));
        hitDelTSigVec[2] = std::fabs(hitVector[2]->getTimeTicks() - 0.5 * (hitVector[0]->getTimeTicks() + hitVector[1]->getTimeTicks()));

        // Want deltaPeakTime and sigmaPeakTime to be the worst of the lot...
        float deltaPeakTime = *std::min_element(hitDelTSigVec.begin(),hitDelTSigVec.end());
        float sigmaPeakTime = std::sqrt(hit0Sigma * hit0Sigma + hit1Sigma * hit1Sigma + hit2Sigma * hit2Sigma);
        
        // Create the 3D cluster hit
        hitPairList.emplace_back(new reco::ClusterHit3D(0,
                                                        statusBits,
                                                        position,
                                                        totalCharge,
                                                        avePeakTime,
                                                        deltaPeakTime,
                                                        sigmaPeakTime,
                                                        0.,
                                                        0.,
                                                        hitDelTSigVec,
                                                        wireIDVec,
                                                        hitVector));
    }
    
    if (m_enableMonitoring)
    {
        theClockMakeHits.stop();
        
        m_timeVector[BUILDTHREEDHITS] = theClockMakeHits.accumulated_real_time();
    }
    
    mf::LogDebug("Cluster3D") << ">>>>> 3D hit building done, found " << hitPairList.size() << " 3D Hits" << std::endl;

    return;
}
    
//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------
    
DEFINE_ART_CLASS_TOOL(SpacePointHit3DBuilder)
} // namespace lar_cluster3d