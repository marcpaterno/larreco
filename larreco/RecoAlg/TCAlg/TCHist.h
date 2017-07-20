////////////////////////////////////////////////////////////////////////
//
//
// TCAlg debug struct
//
// Bruce Baller
//
///////////////////////////////////////////////////////////////////////
#ifndef TRAJCLUSTERALGHISTSTRUCT_H
#define TRAJCLUSTERALGHISTSTRUCT_H

#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"

#include "TH1F.h"
#include "TH2F.h"
#include "TProfile.h"

namespace tca {
  
  struct HistStuff {
    void CreateHists(art::TFileService& tfs);
    TH2F *fMCSMom_TruMom_e;
    TH2F *fMCSMom_TruMom_mu;
    TH2F *fMCSMom_TruMom_pi;
    TH2F *fMCSMom_TruMom_p;
    
    TH2F *fMCSMomEP_TruMom_e;
    
    // Reco-MC vertex position difference
    TH1F* fNuVtx_dx;
    TH1F* fNuVtx_dy;
    TH1F* fNuVtx_dz;
    TH1F* fNuVtx_Score;
    TProfile* fNuVtx_Enu_Score_p;
    
    // Vertex score for 2D and 3D vertices
    TH1F* fVx2_Score;
    TH1F* fVx3_Score;
    
    // Reco-MC stopping wire difference for different MC Particles
    TH1F* fdWire[5];
    // EP vs KE for different MC Particles
    TProfile* fEP_T[5];
  };
} // namespace tca

#endif // ifndef TRAJCLUSTERALGHISTSTRUCT_H