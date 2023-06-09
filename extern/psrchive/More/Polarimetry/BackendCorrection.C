/***************************************************************************
 *
 *   Copyright (C) 2008 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "Pulsar/BackendCorrection.h"
#include "Pulsar/Backend.h"

#include "Pulsar/Archive.h"
#include "Pulsar/IntegrationExpert.h"
#include "Pulsar/Profile.h"

using namespace std;
using namespace Pulsar;

bool BackendCorrection::must_correct_lsb (const Backend* be, 
                                          const Archive* ar) const
{
  bool corrected = be->get_downconversion_corrected();
  bool result = !corrected && ar->get_bandwidth() < 0;

  if (verbose)
    cerr << "must_correct_lsb: downconversion_corrected=" << corrected
         << " bw=" << ar->get_bandwidth() << " result=" << result << endl;

  return result;
}

bool BackendCorrection::must_correct_phase (const Backend* be) const
{
  bool result = !be->get_corrected() && be->get_argument() == Signal::Conjugate;

  if (verbose)
    cerr << "must_correct_phase: corrected=" << be->get_corrected()
         << " argument=" << be->get_argument() << " result=" << result << endl;

  return result;
}

BackendCorrection::BackendCorrection ()
{
  verbose = Archive::verbose > 1;
}

bool BackendCorrection::required (const Archive* arch) const
{
  const Backend* backend = arch->get<Backend>();

  if (!backend)
    return false;

  Signal::Hand hand = backend->get_hand();
  bool correct_lsb = must_correct_lsb (backend, arch);
  bool correct_phase = must_correct_phase (backend);

  return hand == Signal::Left || correct_phase || correct_lsb;
}

void BackendCorrection::operator () (Archive* arch) const try
{
  Backend* backend = arch->get<Backend>();

  if (!backend || backend->get_corrected())
    return;

  Signal::State state = arch->get_state();
  Signal::Basis basis = arch->get_basis();
  Signal::Hand hand = backend->get_hand();
  Signal::Argument argument = backend->get_argument();

  bool correct_lsb = must_correct_lsb (backend, arch);
  bool correct_phase = must_correct_phase (backend);

  if (verbose)
    cerr << "BackendCorrection::operator basis=" << basis
	 << " hand=" << hand << " phase=" << argument
	 << " lsb=" << correct_lsb << endl;

  /*
    complex conjugation due to lower sideband downconversion and
    backend convention have the same effect; the following lines
    effect an exclusive or operation.
  */
  if (correct_lsb)
  {
    if (verbose)
      cerr << "BackendCorrection::operator down conversion" << endl;
    if (argument == Signal::Conjugate)
      argument = Signal::Conventional;
    else
      argument = Signal::Conjugate;
  }

  unsigned npol = arch->get_npol();

  if (npol < 2)
    return;

  bool swap01 = false;
  bool flip[4] = { false, false, false, false };

  if (npol == 4)
  {
    if (argument == Signal::Conjugate)
    {
      if (verbose)
	cerr << "BackendCorrection::operator phase convention" << endl;
      if (state == Signal::Stokes && basis == Signal::Circular)
	flip[2] = !flip[2];   // Flip Stokes U
      else
	flip[3] = !flip[3];   // Flip Stokes V or Im[AB]
    }

    if (hand == Signal::Left) 
    {
      if (verbose)
	cerr << "BackendCorrection::operator hand" << endl;
      if (state == Signal::Stokes && basis == Signal::Circular)
	flip[2] = !flip[2];   // Flip Stokes U and ...
      else if (state == Signal::Stokes && basis == Signal::Linear)
	flip[1] = !flip[1];   // Flip Stokes Q and ...
      else if (state == Signal::PseudoStokes)
	flip[1] = !flip[1];   // Flip AA-BB and ...
      flip[3] = !flip[3];     // Flip Stokes V or Im[AB]
    }
  }

  // If state == Coherence or PPQQ ...
  if (hand == Signal::Left && 
      (state == Signal::Coherence || state == Signal::PPQQ))
    swap01 = true;

  bool flip_something = false;
  for (unsigned ipol=0; ipol < npol; ipol++)
    if (flip[ipol])
    {
      if (verbose)
	cerr << "BackendCorrection::operator flip " << ipol << endl;
      flip_something = true;
    }

  if (swap01 && verbose)
    cerr << "BackendCorrection::operator swap 0 and 1" << endl;

  if (flip_something || swap01)
  {
    for (unsigned isub=0; isub < arch->get_nsubint(); isub++)
    {
      Integration* integration = arch->get_Integration(isub);
      for (unsigned ichan=0; ichan < arch->get_nchan(); ichan++)
      {
	for (unsigned ipol=0; ipol < npol; ipol++)
	  if (flip[ipol])
	    integration->get_Profile(ipol, ichan)->scale(-1);
	
	if (swap01)
	  integration->expert()->swap_profiles(0, ichan, 1, ichan);
      }
    }
  }

  if (correct_phase)
    backend->set_corrected ();
  if (correct_lsb)
    backend->set_downconversion_corrected ();
  if (hand == Signal::Left)
    backend->set_hand(Signal::Right);

}
catch (Error& error)
{
  throw error += "BackendCorrection::operator";
}
