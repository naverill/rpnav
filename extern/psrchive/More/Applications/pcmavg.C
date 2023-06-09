/***************************************************************************
 *
 *   Copyright (C) 2011 by Jonathan Khoo
 *   Licensed under the Academic Free License version 3.0
 *
 ***************************************************************************/

#include "Pulsar/Application.h"
#include "Pulsar/Archive.h"
#include "Pulsar/CalibratorStokes.h"
#include "Pulsar/PolnCalibratorExtension.h"
#include "Pulsar/ProcHistory.h"
#include "Pulsar/Receiver.h"
#include "Pulsar/StandardOptions.h"
#include "Pulsar/UnloadOptions.h"

#include "UnaryStatistic.h"
#include "EstimateStats.h"
#include "Error.h"

#include <vector>

using namespace std;

class pcmavg : public Pulsar::Application
{
  public:

    //! Default constructor
    pcmavg();

    //! Process the given archive: averages DATA values values
    //  from the FEEDPAR table
    void process(Pulsar::Archive*);

    //! Calculates the DATA weights.
    void finalize();

  protected:

    //! Add command line options
    void add_options(CommandLine::Menu&);

    //! Name of the output file
    string unload_name;

    //! Unload options
    Reference::To<Pulsar::UnloadOptions> unload;

  private:

    //! Enter command into the HISTORY table. Required because
    //  Pulsar::Application sets the command after average is cloned???
    void set_history_command();

    bool has_same_attributes(Pulsar::Archive* archive) const;

    void average_pce(Pulsar::Archive* archive);
    void average_cse(Pulsar::Archive* archive);

    bool compute_weighted_median;

    //! Record PolnCalibrator values used to compute weighted median
    void record_pce (Pulsar::Archive* archive);

    //! Compute the weighted median of PolnCalibrator values
    void weighted_median_pce ();

    //! An array of estimates for each ichan and pce iparam
    vector< vector< vector< Estimate<double> > > > pce_params;
    //! An array of chi-squared values for each ichan
    vector< vector<double> > chisq_vals;
    //! An array of nfree values for each ichan
    vector< vector<unsigned> > nfree_vals;
    
    //! Record CalibratorStokes values used to compute weighted median
    void record_cse(Pulsar::Archive* archive);

    //! Compute the weighted median of CalibratorStokes values
    void weighted_median_cse ();

    //! An array of estimates for each ichan and cse iparam
    vector< vector< vector< Estimate<double> > > > cse_params;

      
    void setup(Pulsar::Archive* archive);

    //!
    //  Extract - methods used to extract the must-be-consistent parameters.
    //

    //! Get the receiver/frontend of the file archive. Retrives the value
    //  from the Pulsar::Receiver extension.
    string get_receiver(Pulsar::Archive* archive) const;

    //! Get the centre frequency of the file.
    double get_frequency(Pulsar::Archive* archive) const;

    //! Get the bandwidth of the file.
    double get_bw(Pulsar::Archive* archive) const;

    //! Get the number of frequency channels of the fluxca.
    unsigned get_nchan(Pulsar::Archive* archive) const;

    //!
    //  Access - getters and settings to access the parameters 
    //  of the first pcm file.
    //

    void set_average_centre_frequency(const double frequency);

    double get_average_centre_frequency() const;

    void set_average_receiver(const string receiver);

    string get_average_receiver() const;

    void set_average_bw(const double _bw);

    double get_average_bw() const;

    void set_average_nchan(const unsigned _nchan);

    unsigned get_average_nchan() const;

    //! The weighted average archive - a copy of the first file.
    Reference::To<Pulsar::Archive> average;

    //! Centre frequency of the first file.
    double average_centre_frequency;

    //! Receiver of the first file.
    string average_receiver;

    //! Bandwidth of the first file.
    double bw;

    //! Number of frequency chnanels of the first file.
    unsigned nchan;
};

string pcmavg::get_receiver(Pulsar::Archive* archive) const
{
  Reference::To<Pulsar::Receiver> ext = archive->get<Pulsar::Receiver>();

  if (ext) {
    const string receiver = ext->get_name();
    return receiver;
  }

  return "";
}

double pcmavg::get_frequency(Pulsar::Archive* archive) const
{
  const double centre_frequency = average->get_centre_frequency();
  return centre_frequency;
}

double pcmavg::get_bw(Pulsar::Archive* archive) const
{
  const double bw = archive->get_bandwidth();
  return bw;
}

unsigned pcmavg::get_nchan(Pulsar::Archive* archive) const
{
  Reference::To<Pulsar::PolnCalibratorExtension> ext =
    archive->get<Pulsar::PolnCalibratorExtension>();

  if (ext) {
    const unsigned nchan = ext->get_nchan();
    return nchan;
  }

  return 0;
}

void pcmavg::set_average_nchan(const unsigned _nchan)
{
  nchan = _nchan;
}

unsigned pcmavg::get_average_nchan() const
{
  return nchan;
}

void pcmavg::set_average_bw(const double _bw)
{
  bw = _bw;
}

double pcmavg::get_average_bw() const
{
  return bw;
}

void pcmavg::set_average_centre_frequency(const double frequency)
{
  average_centre_frequency = frequency;
}

double pcmavg::get_average_centre_frequency() const
{
  return average_centre_frequency;
}

void pcmavg::set_average_receiver(const string receiver)
{
  average_receiver = receiver;
}

string pcmavg::get_average_receiver() const
{
  return average_receiver;
}

bool pcmavg::has_same_attributes(Pulsar::Archive* archive) const
{
  return
    get_receiver(average) == get_receiver(archive) &&
    get_frequency(average) == get_frequency(archive) &&
    get_bw(average) == get_bw(archive) &&
    get_nchan(average) == get_nchan(archive);
}

void pcmavg::average_pce(Pulsar::Archive* archive)
{
  Reference::To<Pulsar::PolnCalibratorExtension> average_ext =
    average->get<Pulsar::PolnCalibratorExtension>();

  Reference::To<Pulsar::PolnCalibratorExtension> ext =
    archive->get<Pulsar::PolnCalibratorExtension>();

  const unsigned nchan  = ext->get_nchan();
  const unsigned nparam = ext->get_nparam();

  for (unsigned ichan = 0; ichan < nchan; ++ichan) {
    // Add the chisq value for the transformation (each channel).
    const double chisq = ext->get_transformation(ichan)->get_chisq();
    average_ext->get_transformation(ichan)->set_chisq(chisq);

    // Add the nfree value for the transformation (each channel).
    const unsigned nfree = ext->get_transformation(ichan)->get_nfree();
    average_ext->get_transformation(ichan)->set_nfree(nfree);

    for (unsigned iparam = 0; iparam < nparam; ++iparam) {

      MeanRadian<double, double> average_value =
        average_ext->get_transformation(ichan)->get_Estimate(iparam);

      MeanRadian<double, double> value =
        ext->get_transformation(ichan)->get_Estimate(iparam);

      average_value += value;
      average_ext->get_transformation(ichan)->set_Estimate(iparam, average_value);
    }
  }
}

void pcmavg::record_pce(Pulsar::Archive* archive)
{
  Reference::To<Pulsar::PolnCalibratorExtension> ext =
    archive->get<Pulsar::PolnCalibratorExtension>();

  const unsigned nchan  = ext->get_nchan();
  const unsigned nparam = ext->get_nparam();

  if (nchan != pce_params.size())
    throw Error (InvalidState, "pcmavg::record_pce",
		 "archive nchan=%u != target nchan=%u",
		 nchan, pce_params.size());

  if (nchan == 0)
    return;
  
  if (nparam != pce_params[0].size())
    throw Error (InvalidState, "pcmavg::record_pce",
		 "archive nparam=%u != target nparam=%u",
		 nparam, pce_params[0].size());

  for (unsigned ichan = 0; ichan < nchan; ++ichan)
  {
    const double chisq = ext->get_transformation(ichan)->get_chisq();
    chisq_vals[ichan].push_back (chisq);

    const unsigned nfree = ext->get_transformation(ichan)->get_nfree();
    nfree_vals[ichan].push_back (nfree);

    for (unsigned iparam = 0; iparam < nparam; ++iparam)
    {
      Estimate<double> val = ext->get_Estimate (iparam, ichan);
      if (val.var == 0.0)
	continue;

      pce_params[ichan][iparam].push_back (val);
    }
  }
}
				  
void pcmavg::weighted_median_pce ()
{
  Reference::To<Pulsar::PolnCalibratorExtension> ext =
    average->get<Pulsar::PolnCalibratorExtension>();

  const unsigned nchan  = ext->get_nchan();
  const unsigned nparam = ext->get_nparam();

  if (nchan != pce_params.size())
    throw Error (InvalidState, "pcmavg::weighted_median_pce",
		 "archive nchan=%u != target nchan=%u",
		 nchan, pce_params.size());

  if (nchan == 0)
    return;
  
  if (nparam != pce_params[0].size())
    throw Error (InvalidState, "pcmavg::weighted_median_pce",
		 "archive nparam=%u != target nparam=%u",
		 nparam, pce_params[0].size());

  for (unsigned ichan = 0; ichan < nchan; ++ichan)
  {
    double chisq = median (chisq_vals[ichan]);
    ext->get_transformation(ichan)->set_chisq (chisq);

    unsigned nfree = median (nfree_vals[ichan]);
    ext->get_transformation(ichan)->set_nfree (nfree);

    for (unsigned iparam = 0; iparam < nparam; ++iparam)
    {
      Estimate<float> val = weighted_median (pce_params[ichan][iparam]);
      ext->set_Estimate (iparam, ichan, val);
    }
  }
}



void pcmavg::average_cse(Pulsar::Archive* archive)
{
  Reference::To<Pulsar::CalibratorStokes> avg_cs =
    average->get<Pulsar::CalibratorStokes>();

  Reference::To<Pulsar::CalibratorStokes> cs =
    archive->get<Pulsar::CalibratorStokes>();

  const unsigned nchan = avg_cs->get_nchan();
  for (unsigned i = 0; i < nchan; ++i) {
    // Get Stokes of ith channel of the average.
    Stokes<MeanEstimate<float> > avg_stokes = avg_cs->get_stokes(i);

    // The Vector of the Stokes values - average.
    Vector<3, MeanEstimate<float> > avg_stokes_vec = avg_stokes.get_vector();

    // Get Stokes of ith channel of the current.
    Stokes<MeanEstimate<float> > stokes = cs->get_stokes(i);

    // The Vector of the Stokes values - current.
    Vector<3, MeanEstimate<float> > stokes_vec = stokes.get_vector();

    // Weighted average of the average and current stokes parameters
    avg_stokes_vec += stokes_vec;
    avg_stokes.set_vector(avg_stokes_vec);

    avg_cs->set_stokes(i, avg_stokes);
  }
}

void pcmavg::record_cse(Pulsar::Archive* archive)
{
  Reference::To<Pulsar::CalibratorStokes> ext =
    archive->get<Pulsar::CalibratorStokes>();

  const unsigned nchan  = ext->get_nchan();
  const unsigned nparam = 3;

  if (nchan != cse_params.size())
    throw Error (InvalidState, "pcmavg::record_cse",
		 "archive nchan=%u != target nchan=%u",
		 nchan, cse_params.size());

  if (nchan == 0)
    return;
  
  if (nparam != cse_params[0].size())
    throw Error (InvalidState, "pcmavg::record_cse",
		 "archive nparam=%u != target nparam=%u",
		 nparam, cse_params[0].size());

  for (unsigned ichan = 0; ichan < nchan; ++ichan)
  {
    for (unsigned iparam = 0; iparam < nparam; ++iparam)
    {
      Estimate<double> val = ext->get_Estimate (iparam, ichan);
      if (val.var == 0.0)
	continue;

      cse_params[ichan][iparam].push_back (val);
    }
  }
}

void pcmavg::weighted_median_cse ()
{
  Reference::To<Pulsar::CalibratorStokes> ext =
    average->get<Pulsar::CalibratorStokes>();

  const unsigned nchan  = ext->get_nchan();
  const unsigned nparam = 3;

  if (nchan != cse_params.size())
    throw Error (InvalidState, "pcmavg::record_cse",
		 "archive nchan=%u != target nchan=%u",
		 nchan, cse_params.size());

  if (nchan == 0)
    return;
  
  if (nparam != cse_params[0].size())
    throw Error (InvalidState, "pcmavg::record_cse",
		 "archive nparam=%u != target nparam=%u",
		 nparam, cse_params[0].size());

  for (unsigned ichan = 0; ichan < nchan; ++ichan)
  {
    for (unsigned iparam = 0; iparam < nparam; ++iparam)
    {
      Estimate<float> val = weighted_median (cse_params[ichan][iparam]);
      ext->set_Estimate (iparam, ichan, val);
    }
  }
}

void pcmavg::setup (Pulsar::Archive* archive)
{
  if (verbose)
    cerr << "Using " << archive->get_filename() <<
      " as a storage container." << endl;

  // Retain a copy of the first cal to act as a container of the weighted
  // average pcm file.
  average = archive->clone();

  // Store the parameters that are required to be consistent with
  // following pcm files.
  set_average_receiver(get_receiver(average));
  set_average_centre_frequency(get_frequency(average));
  set_average_bw(get_bw(average));
  set_average_nchan(get_nchan(average));

  if (!compute_weighted_median)
    return;
  
  Reference::To<Pulsar::PolnCalibratorExtension> ext =
    archive->get<Pulsar::PolnCalibratorExtension>();

  const unsigned nchan  = ext->get_nchan();
  const unsigned nparam = ext->get_nparam();

  pce_params.resize (nchan);
  cse_params.resize (nchan);
  chisq_vals.resize (nchan);
  nfree_vals.resize (nchan);

  for (unsigned ichan=0; ichan < nchan; ichan++)
  {
    pce_params[ichan].resize (nparam);
    cse_params[ichan].resize (3);
  }
}

//! Enter command into the HISTORY table. This required because
//  Pulsar::Application sets the command after average is cloned???
void pcmavg::set_history_command()
{
  Reference::To<Pulsar::ProcHistory> ext =
    average->get<Pulsar::ProcHistory>();

  if (ext) {
    ext->set_command_str(command);
  }
}

void pcmavg::process(Pulsar::Archive* archive)
{
  if (verbose)
    cerr << "Processing: " << archive->get_filename() << endl;

  // Ensure that the loaded archive is a pcm file (by checking if the FEEDPAR
  // table exists).
  Reference::To<Pulsar::PolnCalibratorExtension> ext =
    archive->get<Pulsar::PolnCalibratorExtension>();

  if (!ext)
  {
    cerr << archive->get_filename() << " is not a pcm file... " << endl;
    return;
  }

  if (!average)
  {
    // If first file, extract parameters and store them for easy comparison.
    setup (archive);
    return;
  }

  // Compare parameters if not first file.
  if (has_same_attributes(archive))
  {
    if (compute_weighted_median)
    {
      record_cse(archive);
      record_pce(archive);
    }
    else
    {
      average_cse(archive);
      average_pce(archive);
    }
  } else {
    throw Error(InvalidState, "pcmavg::process",
        "file attributes (%s) differ from first pcm file",
        archive->get_filename().c_str());
  }
}


void pcmavg::finalize()
{
  if (compute_weighted_median)
  {
    if (verbose)
      cerr << "pcmavg::finalize computing weighted medians" << endl;
    weighted_median_cse ();
    weighted_median_pce ();
  }
  
  // If no user-specified unload name has been given, use filename of the first
  // pcm file and append as the extension.
  if (unload_name.empty())
    unload_name = unload->get_output_filename(average);

  cerr << "pcmavg: unloading '" << unload_name << "'" << endl;

  set_history_command();
  average->unload(unload_name);
}

void pcmavg::add_options(CommandLine::Menu& menu)
{
  CommandLine::Argument* arg;

  arg = menu.add (unload_name, 'o', "fname");
  arg->set_help ("output result to 'fname'");

  arg = menu.add (compute_weighted_median, "med");
  arg->set_help ("compute the weighted median of each parameter");
}

static pcmavg program;

int main(int argc, char *argv[])
{
  return program.main(argc, argv);
}

pcmavg::pcmavg() :
  Pulsar::Application("pcmavg", "averages pcm files"),
  average(NULL)
{
  update_history = true;

  add(new Pulsar::StandardOptions);

  unload = new Pulsar::UnloadOptions;
  unload->set_output_each(false);
  unload->set_extension("avg");
  add(unload);
}

