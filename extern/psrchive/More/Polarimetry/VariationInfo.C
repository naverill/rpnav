/***************************************************************************
 *
 *   Copyright (C) 2007 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/
#include "Pulsar/VariationInfo.h"

#include <assert.h>

using namespace std;

//! Constructor
Pulsar::VariationInfo::VariationInfo (const SystemCalibrator* cal, Which w)
{
  if (Calibrator::verbose)
    cerr << "Pulsar::VariationInfo::VariationInfo" << endl;

  calibrator = cal;
  which = w;
}

const MEAL::Scalar* Pulsar::VariationInfo::get_Scalar (unsigned ichan) const
try
{
  const Calibration::SignalPath* model = calibrator->get_model( ichan );

  if (!model->get_valid())
    return 0;

  switch (which)
  {
  case Gain:
    return model->get_gain_variation();
  case Boost:
    return model->get_diff_gain_variation();
  case Rotation:
    return model->get_diff_phase_variation();
  default:
    return 0;
  }
}
catch (Error& error)
{
  return 0;
}

//! Return the number of frequency channels
unsigned Pulsar::VariationInfo::get_nchan () const
{
  return calibrator->get_nchan();
}

 
//! Return the name of the specified class
string Pulsar::VariationInfo::get_name (unsigned iclass) const
{
  switch (which)
  {
  case Gain:
    return "\\fiG\\fr";
  case Boost:
    return "\\gb";
  case Rotation:
    return "\\gf";
  default:
    return "";
  }
}

