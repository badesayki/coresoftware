#include "FastJetAlgo.h"

#include "Jet.h"
#include "Jetv1.h"
#include "Jetv2.h"
#include "JetContainer.h"

#include <TClonesArray.h>
#include <assert.h>
#include <phool/phool.h>

// fastjet includes
#include "fastjet/AreaDefinition.hh"
#include "fastjet/ClusterSequenceArea.hh"
#include "fastjet/tools/BackgroundEstimatorBase.hh"
#include "fastjet/tools/JetMedianBackgroundEstimator.hh"
#include <fastjet/ClusterSequence.hh>
#include <fastjet/FunctionOfPseudoJet.hh>  // for FunctionOfPse...
#include <fastjet/JetDefinition.hh>
#include <fastjet/PseudoJet.hh>

// SoftDrop includes
#include <fastjet/contrib/RecursiveSymmetryCutBase.hh>  // for RecursiveSymm...
#include <fastjet/contrib/SoftDrop.hh>

#include <TSystem.h>

// standard includes
#include <cmath>  // for isfinite
#include <iostream>
#include <map>      // for _Rb_tree_iterator
#include <memory>   // for allocator_traits<>::value_type
#include <string>   // for operator<<
#include <utility>  // for pair
#include <vector>

FastJetAlgo::FastJetAlgo(Jet::ALGO algo, float par, int verbosity, Jet::SORT sort)
  : m_Verbosity(verbosity)
  , m_AlgoFlag(algo)
  , m_Par(par)
  , m_SDFlag(false)
  , m_SDBeta(0)
  , m_SDZCut(0.1)
  , m_whichsort(sort)
{
  fastjet::ClusterSequence clusseq;
  if (m_Verbosity > 0)
  {
    clusseq.print_banner();
  }
  else
  {
    std::ostringstream nullstream;
    clusseq.set_fastjet_banner_stream(&nullstream);
    clusseq.print_banner();
    clusseq.set_fastjet_banner_stream(&std::cout);
  }
}

void FastJetAlgo::identify(std::ostream& os)
{
  os << "   FastJetAlgo: ";
  if (m_AlgoFlag == Jet::ANTIKT)
  {
    os << "ANTIKT r=" << m_Par;
  }
  else if (m_AlgoFlag == Jet::KT)
  {
    os << "KT r=" << m_Par;
  }
  else if (m_AlgoFlag == Jet::CAMBRIDGE)
  {
    os << "CAMBRIDGE r=" << m_Par;
  }
  os << std::endl;
}

//can make these fastjet properties, which are inserted into the jet container
/* void FastJetAlgo::init_prop() { */
/*   int cnt_prop { 0 }; */
/*   if (m_SDFlag) { */
/*     m_properties[Jet::PROPERTY::prop_zg] = cnt_prop++; */
/*     m_properties[Jet::PROPERTY::prop_Rg] = cnt_prop++; */
/*     m_properties[Jet::PROPERTY::prop_mu] = cnt_prop++; */
/*   } */
/*   if (m_JetAreaFlag) { */
/*     m_properties[Jet::PROPERTY::prop_area] = cnt_prop++; */
/*   } */

/*   // use local indices (only for efficiency and convenience -- could just ready from map, too) */
/*   index_zg = m_properties[Jet::PROPERTY::prop_zg]; */
/*   index_Rg = m_properties[Jet::PROPERTY::prop_Rg]; */
/*   index_mu = m_properties[Jet::PROPERTY::prop_mu]; */
/*   index_area = m_properties[Jet::PROPERTY::prop_area]; */

/*   n_properties = m_properties.size(); */

/*   if (m_JetAreaFlag && m_GhostMaxRap == 0) { */
/*     m_GhostMaxRap = 1.1 - m_Par; */
/*   } */

/*   if (m_RhoMedianFlag && m_RapCutHardest==0) { */
/*     m_RapCutHardest = 1.1 - m_Par; */
/*   } */
/* } */

void FastJetAlgo::cluster_and_fill(std::vector<Jet*>& particles, JetContainer* jetcont)
{
  if (m_first_cluster_call) {
    m_first_cluster_call = false;
    // initalize the properties in JetContainer
    if (m_SDFlag) {
      jetcont->add_property( {Jet::PROPERTY::prop_zg, Jet::PROPERTY::prop_Rg, Jet::PROPERTY::prop_mu} );
      m_zg_index = jetcont->find_prop_index(Jet::PROPERTY::prop_zg);
      m_Rg_index = jetcont->find_prop_index(Jet::PROPERTY::prop_Rg);
      m_mu_index = jetcont->find_prop_index(Jet::PROPERTY::prop_mu);
    }
    if (m_JetAreaFlag) {
      jetcont->add_property(Jet::PROPERTY::prop_area);
      m_area_index = jetcont->find_prop_index(Jet::PROPERTY::prop_area);
    }

    // set values if calculating jet areas and rapidities
    if (m_JetAreaFlag && m_GhostMaxRap == 0) {
      m_GhostMaxRap = 1.1 - m_Par;
    }

    if (m_RhoMedianFlag && m_RapCutHardest==0) {
      m_RapCutHardest = 1.1 - m_Par;
    }


    // set if fastjet is doing any sorting
    if (m_whichsort != Jet::SORT::NO_SORT) {
      if (m_whichsort == Jet::SORT::PT 
      ||  m_whichsort == Jet::SORT::ETA
      ||  m_whichsort == Jet::SORT::E) {
        jetcont->set_sorted_by(m_whichsort, true);
      } else {
          std::cout << PHWHERE << std::endl;
          std::cout << " Unknown sort option (only Jet::SORT::PT,E,ETA,NO_SORT supported." << std::endl;
          std::cout << " -> setting sort to Jet::SORT::NO_SORT. " << std::endl;
          m_whichsort = Jet::SORT::NO_SORT;
      }
    }
  }

  if (m_Verbosity > 1) std::cout << "   Verbosity>1 FastJetAlgo::process_event -- entered" << std::endl;

  if (m_Verbosity > 8) std::cout << "   Verbosity>8 #input particles: " << particles.size() << std::endl;
  // translate to fastjet
  std::vector<fastjet::PseudoJet> pseudojets;
  for (unsigned int ipart = 0; ipart < particles.size(); ++ipart)
  {
    // fastjet performs strangely with exactly (px,py,pz,E) =
    // (0,0,0,0) inputs, such as placeholder towers or those with
    // zero'd out energy after CS. this catch also in FastJetAlgoSub
    if (particles[ipart]->get_e() == 0.) continue;
    if (!std::isfinite(particles[ipart]->get_px()) ||
        !std::isfinite(particles[ipart]->get_py()) ||
        !std::isfinite(particles[ipart]->get_pz()) ||
        !std::isfinite(particles[ipart]->get_e()))
    {
      std::cout << PHWHERE << " invalid particle kinematics:"
                << " px: " << particles[ipart]->get_px()
                << " py: " << particles[ipart]->get_py()
                << " pz: " << particles[ipart]->get_pz()
                << " e: " << particles[ipart]->get_e() << std::endl;
      gSystem->Exit(1);
    }
    fastjet::PseudoJet pseudojet(particles[ipart]->get_px(),
                                 particles[ipart]->get_py(),
                                 particles[ipart]->get_pz(),
                                 particles[ipart]->get_e());
    pseudojet.set_user_index(ipart);
    pseudojets.push_back(pseudojet);
  }
  // run fast jet
  fastjet::JetDefinition* jetdef = nullptr;

  if (m_AlgoFlag == Jet::ANTIKT)
  {
    jetdef = new fastjet::JetDefinition(fastjet::antikt_algorithm, m_Par, fastjet::E_scheme, fastjet::Best);
  }
  else if (m_AlgoFlag == Jet::KT)
  {
    jetdef = new fastjet::JetDefinition(fastjet::kt_algorithm, m_Par, fastjet::E_scheme, fastjet::Best);
  }
  else if (m_AlgoFlag == Jet::CAMBRIDGE)
  {
    jetdef = new fastjet::JetDefinition(fastjet::cambridge_algorithm, m_Par, fastjet::E_scheme, fastjet::Best);
  }
  else
  {
    return;
  }


  std::vector<fastjet::PseudoJet> fastjets;
  if (m_JetAreaFlag) {
    fastjet::AreaDefinition area_def( 
        fastjet::active_area_explicit_ghosts, fastjet::GhostedAreaSpec(m_GhostMaxRap, 1, m_GhostArea));
    fastjet::ClusterSequenceArea jetFinderArea { pseudojets, *jetdef, area_def };

    // -- make the inclusive jets --
    if (m_whichsort == Jet::SORT::NO_SORT) {
      fastjets = jetFinderArea.inclusive_jets();
    } else if (m_whichsort == Jet::SORT::PT) {
      fastjets = fastjet::sorted_by_pt(jetFinderArea.inclusive_jets());
    } else if (m_whichsort == Jet::SORT::E) {
      fastjets = fastjet::sorted_by_E(jetFinderArea.inclusive_jets());
    } else if (m_whichsort == Jet::SORT::ETA) {
      fastjets = sorted_by_rapidity(jetFinderArea.inclusive_jets());
    } else {
        // fatal error 
        std::cout << PHWHERE << std::endl
            << "ERROR: do not use set_default_sort after calling cluster_and_fill!" << std::endl;
        assert(false);
    }
    // -- end inclusive jets

    if (m_RhoMedianFlag) {
      fastjet::Selector rho_select = fastjet::SelectorAbsEtaMax(m_RapCutHardest) * 
        (!fastjet::SelectorNHardest(m_CutNHardest)); // <--
      fastjet::JetDefinition jet_def_bkgd(fastjet::kt_algorithm, m_Par); // <--
      fastjet::JetMedianBackgroundEstimator bge {rho_select, jet_def_bkgd, area_def};
      bge.set_particles(pseudojets);
      jetcont->set_rho_median(bge.rho());
    }

    std::vector<fastjet::PseudoJet> comps = fastjets[0].constituents();

    fillJetContainer(&fastjets, jetcont, particles);
  } else { // aren't clustering with areas
    fastjet::ClusterSequence jetFinder { pseudojets, *jetdef };

    // check about fastjet's sorting
    // -- make the inclusive jets --
    if (m_whichsort == Jet::SORT::NO_SORT) {
      fastjets = jetFinder.inclusive_jets();
    } else if (m_whichsort == Jet::SORT::PT) {
      fastjets = fastjet::sorted_by_pt(jetFinder.inclusive_jets());
    } else if (m_whichsort == Jet::SORT::E) {
      fastjets = fastjet::sorted_by_E(jetFinder.inclusive_jets());
    } else if (m_whichsort == Jet::SORT::ETA) {
      fastjets = sorted_by_rapidity(jetFinder.inclusive_jets());
    } else {
        // fatal error 
        std::cout << PHWHERE << std::endl
            << "ERROR: do not use set_default_sort after calling cluster_and_fill!" << std::endl;
        assert(false);
    }
    // -- end inclusive jets
    fillJetContainer(&fastjets, jetcont, particles);
  }

  delete jetdef;
}

void FastJetAlgo::fillJetContainer(std::vector<fastjet::PseudoJet>* pfastjets, 
    JetContainer* jetcont, std::vector<Jet*>& particles) {

  auto& fastjets = *pfastjets;
  if (m_Verbosity > 8) std::cout << "   Verbosity>8 fastjets: " << fastjets.size() << std::endl;
  for (unsigned int ijet = 0; ijet < fastjets.size(); ++ijet)
  {
    if (fastjets[ijet].is_pure_ghost()) continue;
    auto* jet = jetcont->add_jet();
    jet->set_px(fastjets[ijet].px());
    jet->set_py(fastjets[ijet].py());
    jet->set_pz(fastjets[ijet].pz());
    jet->set_e(fastjets[ijet].e());
    jet->set_id(ijet);

    if (m_JetAreaFlag) {
      jetcont->set_prop_by_index(m_area_index, fastjets[ijet].area());
    }

    // if SoftDrop enabled, and jets have > 5 GeV (do not waste time
    // on very low-pT jets), run SD and pack output into jet properties
    if (m_SDFlag && fastjets[ijet].perp() > 5)
    {
      fastjet::contrib::SoftDrop sd(m_SDBeta, m_SDZCut);
      if (m_Verbosity > 5)
      {
        std::cout << "FastJetAlgo::get_jets : created SoftDrop groomer configuration : " 
          << sd.description() << std::endl;
      }

      fastjet::PseudoJet sd_jet = sd(fastjets[ijet]);

      if (m_Verbosity > 5)
      {
        std::cout << "original    jet: pt / eta / phi / m = " << fastjets[ijet].perp() 
            << " / " << fastjets[ijet].eta() << " / " << fastjets[ijet].phi() << " / " 
            << fastjets[ijet].m() << std::endl;
        std::cout << "SoftDropped jet: pt / eta / phi / m = " << sd_jet.perp() << " / " 
            << sd_jet.eta() << " / " << sd_jet.phi() << " / " << sd_jet.m() << std::endl;

        std::cout << "  delta_R between subjets: " << sd_jet.structure_of<fastjet::contrib::SoftDrop>().delta_R() << std::endl;
        std::cout << "  symmetry measure(z):     " << sd_jet.structure_of<fastjet::contrib::SoftDrop>().symmetry() << std::endl;
        std::cout << "  mass drop(mu):           " << sd_jet.structure_of<fastjet::contrib::SoftDrop>().mu() << std::endl;
      }
 
      // attach SoftDrop quantities as jet properties
      jetcont->set_prop_by_index(m_zg_index, sd_jet.structure_of<fastjet::contrib::SoftDrop>().symmetry());
      jetcont->set_prop_by_index(m_Rg_index, sd_jet.structure_of<fastjet::contrib::SoftDrop>().delta_R() );
      jetcont->set_prop_by_index(m_mu_index, sd_jet.structure_of<fastjet::contrib::SoftDrop>().mu()      );
    }

    // copy components into output jet
    std::vector<fastjet::PseudoJet> comps = fastjets[ijet].constituents();
    for (auto & comp : comps)
    {
      if (comp.is_pure_ghost()) continue;
      Jet* particle = particles[comp.user_index()];

      for (Jet::Iter iter = particle->begin_comp();
           iter != particle->end_comp();
           ++iter)
      {
        jetcont->add_component(iter->first, iter->second);
      }
    }
  }
  if (m_Verbosity > 1) std::cout << "FastJetAlgo::process_event -- exited" << std::endl;
}

std::vector<Jet*> FastJetAlgo::get_jets(std::vector<Jet*> particles)
{
  if (m_Verbosity > 1) std::cout << "FastJetAlgo::process_event -- entered" << std::endl;

  // translate to fastjet
  std::vector<fastjet::PseudoJet> pseudojets;
  for (unsigned int ipart = 0; ipart < particles.size(); ++ipart)
  {
    // fastjet performs strangely with exactly (px,py,pz,E) =
    // (0,0,0,0) inputs, such as placeholder towers or those with
    // zero'd out energy after CS. this catch also in FastJetAlgoSub
    if (particles[ipart]->get_e() == 0.) continue;
    if (!std::isfinite(particles[ipart]->get_px()) ||
        !std::isfinite(particles[ipart]->get_py()) ||
        !std::isfinite(particles[ipart]->get_pz()) ||
        !std::isfinite(particles[ipart]->get_e()))
    {
      std::cout << PHWHERE << " invalid particle kinematics:"
                << " px: " << particles[ipart]->get_px()
                << " py: " << particles[ipart]->get_py()
                << " pz: " << particles[ipart]->get_pz()
                << " e: " << particles[ipart]->get_e() << std::endl;
      gSystem->Exit(1);
    }
    fastjet::PseudoJet pseudojet(particles[ipart]->get_px(),
                                 particles[ipart]->get_py(),
                                 particles[ipart]->get_pz(),
                                 particles[ipart]->get_e());
    pseudojet.set_user_index(ipart);
    pseudojets.push_back(pseudojet);
  }
  // run fast jet
  fastjet::JetDefinition* jetdef = nullptr;
  if (m_AlgoFlag == Jet::ANTIKT)
  {
    jetdef = new fastjet::JetDefinition(fastjet::antikt_algorithm, m_Par, fastjet::E_scheme, fastjet::Best);
  }
  else if (m_AlgoFlag == Jet::KT)
  {
    jetdef = new fastjet::JetDefinition(fastjet::kt_algorithm, m_Par, fastjet::E_scheme, fastjet::Best);
  }
  else if (m_AlgoFlag == Jet::CAMBRIDGE)
  {
    jetdef = new fastjet::JetDefinition(fastjet::cambridge_algorithm, m_Par, fastjet::E_scheme, fastjet::Best);
  }
  else
  {
    return std::vector<Jet*>();
  }
  fastjet::ClusterSequence jetFinder(pseudojets, *jetdef);
  std::vector<fastjet::PseudoJet> fastjets = jetFinder.inclusive_jets();
  delete jetdef;

  fastjet::contrib::SoftDrop sd(m_SDBeta, m_SDZCut);
  if (m_Verbosity > 5)
  {
    std::cout << "FastJetAlgo::get_jets : created SoftDrop groomer configuration : " << sd.description() << std::endl;
  }

  // translate into jet output...
  std::vector<Jet*> jets;
  for (unsigned int ijet = 0; ijet < fastjets.size(); ++ijet)
  {
    Jet* jet = new Jetv1();
    jet->set_px(fastjets[ijet].px());
    jet->set_py(fastjets[ijet].py());
    jet->set_pz(fastjets[ijet].pz());
    jet->set_e(fastjets[ijet].e());
    jet->set_id(ijet);

    // if SoftDrop enabled, and jets have > 5 GeV (do not waste time
    // on very low-pT jets), run SD and pack output into jet properties
    if (m_SDFlag && fastjets[ijet].perp() > 5)
    {
      fastjet::PseudoJet sd_jet = sd(fastjets[ijet]);

      if (m_Verbosity > 5)
      {
        std::cout << "original    jet: pt / eta / phi / m = " << fastjets[ijet].perp() << " / " << fastjets[ijet].eta() << " / " << fastjets[ijet].phi() << " / " << fastjets[ijet].m() << std::endl;
        std::cout << "SoftDropped jet: pt / eta / phi / m = " << sd_jet.perp() << " / " << sd_jet.eta() << " / " << sd_jet.phi() << " / " << sd_jet.m() << std::endl;

        std::cout << "  delta_R between subjets: " << sd_jet.structure_of<fastjet::contrib::SoftDrop>().delta_R() << std::endl;
        std::cout << "  symmetry measure(z):     " << sd_jet.structure_of<fastjet::contrib::SoftDrop>().symmetry() << std::endl;
        std::cout << "  mass drop(mu):           " << sd_jet.structure_of<fastjet::contrib::SoftDrop>().mu() << std::endl;
      }

      // attach SoftDrop quantities as jet properties
      jet->set_property(Jet::PROPERTY::prop_zg, sd_jet.structure_of<fastjet::contrib::SoftDrop>().symmetry());
      jet->set_property(Jet::PROPERTY::prop_Rg, sd_jet.structure_of<fastjet::contrib::SoftDrop>().delta_R());
      jet->set_property(Jet::PROPERTY::prop_mu, sd_jet.structure_of<fastjet::contrib::SoftDrop>().mu());
    }

    // copy components into output jet
    std::vector<fastjet::PseudoJet> comps = fastjets[ijet].constituents();
    for (auto & comp : comps)
    {
      Jet* particle = particles[comp.user_index()];

      for (Jet::Iter iter = particle->begin_comp();
           iter != particle->end_comp();
           ++iter)
      {
        jet->insert_comp(iter->first, iter->second);
      }
    }

    jets.push_back(jet);
  }

  if (m_Verbosity > 1) std::cout << "FastJetAlgo::process_event -- exited" << std::endl;

  return jets;
}

