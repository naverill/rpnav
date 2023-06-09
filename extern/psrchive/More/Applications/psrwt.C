/***************************************************************************
 *
 *   Copyright (C) 2002 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "Pulsar/Archive.h"
#include "Pulsar/Integration.h"
#include "Pulsar/ProfileStrategies.h"

#include "Pulsar/FourierSNR.h"
#include "Pulsar/StandardSNR.h"
#include "Pulsar/AdaptiveSNR.h"
#include "Pulsar/GaussianBaseline.h"
#include "Pulsar/BaselineWindow.h"
#include "Pulsar/PhaseWeight.h"

#include "Pulsar/SmoothMedian.h"
#include "Pulsar/SmoothMean.h"

#include "Error.h"
#include "RealTimer.h"

#include "dirutil.h"
#include "strutil.h"

#include <iostream>
#include <unistd.h>
#include <string.h>

using namespace Pulsar;
using namespace std;

void usage ()
{
  cout << "weight the profiles in Pulsar::Archive(s) in various ways\n"
    "Usage: psrwt [options] file1 [file2 ...] \n"
    "Where the options are as follows \n"
    " -h        this help page \n"
    " -v        verbose output \n"
    " -V        very verbose output \n"
    " -q        quiet output \n"
    " -M meta   meta names a file containing the list of files\n"
    "\n"
    "Weighting options: \n"
    " -s s/n    S/N threshold (weight zero below) default:10\n"
    " -S std    calculate S/N using a standard pulsar archive\n"
    " -m method calculate S/N using the named method: \n"
    "           fourier, adaptive \n"
    " -w width  width of off-pulse baseline used in S/N calculations\n"
    " -c sigma  cut-off sigma used in adaptive S/N method \n"
    " -p phs    phase centre of off-pulse baseline (implies -G)\n"
    " -r        reset weights (ie. ignore zero weights)\n"
    " -t tscr   compute S/N from tscrunched clone \n"
    "\n"
    "Output options:\n"
    " -b scr    add scr phase bins together \n"
    " -F        add all frequency channels after weighting \n"
    " -P        add polarisations together \n"
    " -T        add all Integrations after weighting \n"
    " -n        normalize data by off-pulse rms\n"
    " -G        print giant-pulse search numbers \n"
    // " -o M:w    smooth profile using method, M, and window size, w \n"
       << endl;
}

int main (int argc, char** argv) try
{
  int bscrunch = -1;
  int fscrunch = -1;
  int tscrunch = -1;
  int pscrunch = -1;

  bool verbose = false;
  bool quiet = false;
  bool display = false;
  bool unload_result = true;
  bool normal = true;
  bool normalize_data = false;

  float snr_phase = -1.0;
  float duty_cycle = 0.15;

  char* metafile = NULL;

  double snr_threshold = 10.0;

  bool snr_chosen = false;
  bool reset_weights = false;

  unsigned from_tscrunched = 1;

  FourierSNR fourier_snr;
  StandardSNR standard_snr;
  AdaptiveSNR adaptive_snr;

  GaussianBaseline mask;
  adaptive_snr.set_baseline_estimator (&mask);

  Reference::To<Archive> standard;

  Smooth* smooth = 0;

  int c = 0;
  const char* args = "b:c:DdFGhm:M:no:Pp:qrTt:Us:S:vVw:";

  while ((c = getopt(argc, argv, args)) != -1)
    switch (c) {
      
    case 'b':
      bscrunch = atoi (optarg);
      break;

    case 'c': {
      float cutoff = atof (optarg);
      mask.set_threshold (cutoff);
      break;
    }

    case 'D':
      display = true;
      break;

    case 'd':
      unload_result = false;
      break;

    case 'F':
      fscrunch = 0;
      break;

    case 'G':
      normal = false;
      break;

    case 'h':
      usage ();
      return 0;

    case 'm':

      if (snr_chosen) {
	cerr << "psrwt: cannot use more than one S/N method" << endl;
	return -1;
      }

      StrategySet::get_default_snratio() = SNRatioEstimator::factory (optarg);

      snr_chosen = true;
      break;

    case 'M':
      metafile = optarg;
      break;

    case 'n':
      normalize_data = true;
      break;

    case 'o': {

      char name[256];
      float duty_cycle;

      char* sep = strpbrk (optarg, ":,");
      if (sep) *sep = ' ';

      int i = sscanf (optarg, "%s %f", name, &duty_cycle);

      cerr << "scanned " << i << endl;

      if (i != 2) {
	cerr << "psrwt: smooth option -o requires name:duty_cycle not " << optarg << endl;
	return -1;
      }

      if (strcasecmp (name, "median") == 0)
	smooth = new SmoothMedian;
      else if (strcasecmp (name, "mean") == 0)
	smooth = new SmoothMean;
      else {
	cerr << "psrwt: smooth " << name << " not recognized" << endl;
	return -1;
      }

      smooth->set_turns (duty_cycle);

      cerr << "psrwt: smooth duty_cycle= " << duty_cycle << endl;
      break;

    }

    case 'P':
      pscrunch = 1;
      break;

    case 'p':

      snr_phase = atof (optarg);
      if (snr_phase < 0 || snr_phase > 1) {
	cerr << "psrwt: invalid phase=" << snr_phase << endl;
	return -1;
      }

      cerr << "psrwt: baseline phase window centre = " << snr_phase << endl;
      normal = false;
      break;

    case 'r':
      reset_weights = true;
      break;

    case 's':
      snr_threshold = atof (optarg);
      break;

    case 'S':

      if (snr_chosen) {
	cerr << "psrwt: cannot use more than one S/N method" << endl;
	return -1;
      }
     
      StrategySet::get_default_snratio() = &standard_snr;

      cerr << "psrwt: loading standard from " << optarg << endl;
      standard = Archive::load (optarg);
      standard->convert_state (Signal::Intensity);

      snr_chosen = true;

      break;

    case 'T':
      tscrunch = 0;
      break;

    case 't':
      from_tscrunched = atof (optarg);
      break;

    case 'U':
      Profile::rotate_in_phase_domain = true;
      break;

    case 'w': {

      duty_cycle = atof(optarg);
      cerr << "psrwt: baseline phase window width = " << duty_cycle << endl;

      Pulsar::Profile::default_duty_cycle = duty_cycle;
      fourier_snr.set_baseline_extent (duty_cycle);

      Pulsar::BaselineWindow* bw;
      bw = dynamic_cast<Pulsar::BaselineWindow*>(mask.get_initial_baseline());
      if (bw)
	bw->get_smooth()->set_turns (duty_cycle);

      break;
    }

    case 'V':
      Pulsar::Archive::verbose = true;
      Pulsar::Integration::verbose = true;
      Pulsar::Profile::verbose = true;
    case 'v':
      verbose = true;
      break;

    case 'q':
      quiet = true;
      break;

    default:
      cerr << "invalid param '" << c << "'" << endl;
    }

  vector <string> filenames;

  if (metafile)
    stringfload (&filenames, metafile);
  else 
    for (int ai=optind; ai<argc; ai++)
      dirglob (&filenames, argv[ai]);

  if (filenames.size() == 0) {
    usage ();
    return 0;
  }

  Reference::To<Pulsar::Archive> archive, copy;

  for (unsigned ifile=0; ifile < filenames.size(); ifile++) try
  {
    archive = Pulsar::Archive::load (filenames[ifile]);

    bool channel_standard = false;

    if ( standard )
    {
      if ( standard->get_nchan() > 1 
	   && standard->get_nchan() == archive->get_nchan() )
      {
        if (!quiet)
          cerr << "psrwt: standard profile varies with frequency" << endl;
	channel_standard = true;
      }
      else
      {
	copy = standard;

        if ( standard->get_nchan() > 1 )
          copy = standard->total();
      
	standard_snr.set_standard( copy->get_Profile (0,0,0) );
      }
    }

    copy = archive->clone();

    if (reset_weights)
      copy -> uniform_weight ();

    copy -> pscrunch();

    if (from_tscrunched > 1)
      copy -> tscrunch (from_tscrunched);

    unsigned tscrunch_offset = 0;

    for (unsigned isub=0; isub < copy->get_nsubint(); isub++)
    {
      Pulsar::Integration* subint = copy->get_Integration (isub);

      for (unsigned ichan=0; ichan < copy->get_nchan(); ichan++)
      {
        float snr = 0.0;

        if (subint->get_weight(ichan) > 0.0)
        {
	  if (channel_standard)
	    standard_snr.set_standard( standard->get_Profile (0,0,ichan) );

	  Pulsar::Profile* profile = subint->get_Profile (0,ichan);

          if (standard)
            snr = standard_snr.get_snr( profile );
          else
            snr = profile->snr ();

	  if (!normal)
          {
	    unload_result = false;

	    double mean, variance;

	    if (snr_phase >= 0)
            {
	      // calculate the mean and variance at the specifed phase
	      profile->stats (snr_phase, &mean, &variance, 0, duty_cycle);
	    }
	    else 
            {
	      Pulsar::PhaseWeight weight;

	      mask.set_Profile (profile);
	      mask.get_weight (&weight);

	      weight.stats (profile, &mean, &variance);
	    }

	    // sum the total power above the baseline
	    double snr = profile->sum() - mean * profile->get_nbin();
	    
	    // calculate the rms
	    float rms = sqrt (variance);
	    
	    // find the maximum bin
	    int maxbin = profile->find_max_bin();

	    // get the maximum value
	    float max = profile->get_amps()[maxbin] - mean;
	    
	    double phase = double(maxbin) / double(subint->get_nbin());
            if (!quiet)
              cerr << "phase=" << phase << endl;
	    double seconds = phase * subint->get_folding_period();
            if (!quiet)
              cerr << "seconds=" << seconds << endl;
	    
	    // calculate the epoch of the maximum
	    MJD epoch = subint->get_epoch() + seconds;
	  
	    cout << filenames[ifile] << " epoch=" << epoch << " max/rms="
	         << max/rms << " rms=" << rms << " sum=" << snr;
	  
	    snr /= sqrt ( profile->get_nbin() * variance );
	  
	    cout << " snr=" << snr << endl;
	    cout.flush();

	    continue;
	  }

          cout << filenames[ifile] << "(" << isub << ", " << ichan << ")"
               << " snr=" << snr << endl;

	  if (snr_threshold && snr < snr_threshold)
	    snr = 0.0;

	  if (normalize_data)
          {
	    Pulsar::PhaseWeight weight;

	    mask.set_Profile (profile);
	    mask.get_weight (&weight);
	  
	    double mean, variance;
	    weight.stats (profile, &mean, &variance);

	    double scale = 1.0/sqrt(variance);

	    for (unsigned ipol=0; ipol < archive->get_npol(); ipol++)
	      archive->get_Profile (isub,ipol,ichan)->scale (scale);
	  }
        } // data have weight greater than zero

        for (unsigned jsub=0; jsub < from_tscrunched; jsub ++)
        {
          unsigned ksub = isub*from_tscrunched + jsub;

          if (ksub < archive->get_nsubint())
          {
            Integration* subint = archive->get_Integration (ksub);
            if (subint->get_weight(ichan) > 0.0)
	      subint->set_weight(ichan,snr*snr);
          }
        }

      } // for each channel

    } // for each subint

    if (bscrunch > 0)
      archive -> bscrunch (bscrunch);

    if (fscrunch >= 0)
      archive -> fscrunch (fscrunch);

    if (tscrunch >= 0)
      archive -> tscrunch (tscrunch);

    if (pscrunch > 0)
      archive -> pscrunch ();

    if (unload_result)
      archive -> unload (filenames[ifile] + ".wt");

  }
  catch (Error& error) {
    cerr << error << endl;
  }
  catch (string& error) {
    cerr << error << endl;
  }
 
  return 0;
}
catch (Error& error)
{
  cerr << "psrwt: error" << error << endl;
  return -1;
}


