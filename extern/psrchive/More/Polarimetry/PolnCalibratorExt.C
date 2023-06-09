/***************************************************************************
 *
 *   Copyright (C) 2003-2009 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "Pulsar/PolnCalibratorExtension.h"
#include "Pulsar/PolnCalibrator.h"

using namespace std;

void copy_name (MEAL::Complex2* to,
		const Pulsar::PolnCalibratorExtension::Transformation* from)
{
  for (unsigned i=0; i<to->get_nparam(); i++)
  {
    string param_name = from->get_param_name(i);
    if (param_name != "" && param_name != to->get_param_name(i))
      throw Error (InvalidState, "copy_name",
		   "iparam=%d name=%s != model=%s",
		   i, param_name.c_str(), to->get_param_name(i).c_str());
  }
}

void copy_name (Pulsar::PolnCalibratorExtension::Transformation* to,
		const MEAL::Complex2* from)
{
  for (unsigned i=0; i<to->get_nparam(); i++)
  {
    to->set_param_name(i, from->get_param_name(i));
    to->set_param_description(i, from->get_param_description(i));
  }
}

template<class T, class F>
void copy (T* to, const F* from)
{
  if (to->get_nparam() != from->get_nparam())
    throw Error (InvalidParam, "copy<To,From>",
		 "to nparam=%d != from nparam=%d",
		 to->get_nparam(), from->get_nparam());

  for (unsigned i=0; i<to->get_nparam(); i++)
    to->set_Estimate(i, from->get_Estimate(i));

  copy_name (to, from);
}

//! Construct from a PolnCalibrator instance
Pulsar::PolnCalibratorExtension::PolnCalibratorExtension
(const PolnCalibrator* calibrator) try
  : CalibratorExtension ("PolnCalibratorExtension") 
{
  if (!calibrator)
    throw Error (InvalidParam, "", "null PolnCalibrator*");

  init ();

  if (Calibrator::verbose > 2)
    cerr << "Pulsar::PolnCalibratorExtension (PolnCalibrator*)" << endl;

  CalibratorExtension::build (calibrator);

  has_covariance = calibrator->has_covariance ();
  has_solver = calibrator->has_solver ();

  vector<double> covariance;

  bool first = true;

  unsigned nchan = get_nchan();

  if (Calibrator::verbose > 2)
    cerr << "Pulsar::PolnCalibratorExtension nchan=" << nchan << endl;

  for (unsigned ichan=0; ichan < nchan; ichan++)
  {
    if ( ! calibrator->get_transformation_valid(ichan) )
    {
      if (Calibrator::verbose > 2)
	cerr << "Pulsar::PolnCalibratorExtension ichan=" << ichan 
	     << " flagged invalid" << endl;
      
      set_valid (ichan, false);
      continue;
    }

    copy( get_transformation(ichan), 
	  calibrator->get_transformation(ichan) );
    
    set_valid (ichan, true);

    if (Calibrator::verbose > 2 && first)
    {
      const MEAL::Function* f = calibrator->get_transformation(ichan);
      for (unsigned i=0; i<f->get_nparam(); i++)
	cerr << "Pulsar::PolnCalibratorExtension name[" << i << "]=" 
	     << f->get_param_name(i) << endl;
    }

    first = false;
    
    if ( has_covariance )
    {
      calibrator->get_covariance( ichan, covariance );
      get_transformation(ichan)->set_covariance( covariance );
    }
    
    if ( has_solver )
    {
      const MEAL::LeastSquares* solver = calibrator->get_solver( ichan );
      get_transformation(ichan)->set_chisq( solver->get_chisq() );
      get_transformation(ichan)->set_nfree( solver->get_nfree() );
      get_transformation(ichan)->set_nfit( solver->get_nparam_infit() );
    }
  }
}
catch (Error& error)
{
  throw error += "Pulsar::PolnCalibratorExtension (PolnCalibrator*)";
}

//! Return a new MEAL::Complex2 instance, based on type attribute
MEAL::Complex2*
Pulsar::new_transformation (const PolnCalibratorExtension* ext, unsigned ichan)
try
{
  if( !ext->get_valid(ichan) )
    return 0;

  MEAL::Complex2* xform = new_transformation( ext->get_type() );

  if (Calibrator::verbose)
    cerr << "Pulsar::new_transformation name=" << xform->get_name() << endl;

  const PolnCalibratorExtension::Transformation* info;
  info = ext->get_transformation(ichan);
  
  copy( xform, info );

  for (unsigned iparam=0; iparam < xform->get_nparam(); iparam++)
    if (info->get_variance(iparam) == 0)
      xform->set_infit (iparam, false);
  
  return xform;
}
catch (Error& error)
{
  throw error += "Pulsar::new_transformation";
}

