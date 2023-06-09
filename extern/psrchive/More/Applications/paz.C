/***************************************************************************
 *
 *   Copyright (C) 2003 by Aidan Hotan
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

// A simple command line tool for zapping RFI

#include "Pulsar/Application.h"
#include "Pulsar/StandardOptions.h"
#include "Pulsar/UnloadOptions.h"

// Update to remove variable baseline MJK2013, adapted from Patrick Weltevrede:
#include "Pulsar/RemoveVariableBaseline.h"

#include "Pulsar/RFIMitigation.h"
#include "Pulsar/ChannelZapMedian.h"
#include "Pulsar/LawnMower.h"

#include "Pulsar/StandardSNR.h"
#include "Pulsar/IntegrationExpert.h"
#include "Pulsar/ProfileStrategies.h"
#include "Pulsar/ProcHistory.h"

#include "Error.h"
#include "dirutil.h"
#include "strutil.h"

#include <unistd.h>
#include <string.h>

#include "BoxMuller.h"

using namespace std;

struct FreqRange {
  double lo;
  double hi;
};

//! Pulsar Archive Zapping application
class paz: public Pulsar::Application
{
public:

  //! Default constructor
  paz ();

  //! Verify setup
  void setup ();

  //! Process the given archive
  void process (Pulsar::Archive*);

  void add_subints_to_mow (const std::string& arg);

  vector<unsigned> bins_to_zap;
  void range_bins_to_zap (const std::string& arg);

  vector<unsigned> chans_to_zero;
  void add_chans_to_zero (const std::string& arg);
  void range_chans_to_zero (const std::string& arg);

  vector<unsigned> subs_to_zero;
  void add_subs_to_zero (const std::string& arg);
  void range_subs_to_zero (const std::string& arg);

  vector<unsigned> subs_to_delete;
  void add_subs_to_delete (const std::string& arg);
  void range_subs_to_delete (const std::string& arg);

  vector<unsigned> subs_no_delete;
  void add_subs_no_delete (const std::string& arg);
  void range_subs_no_delete (const std::string& arg);

  vector<double> freqs_to_zero;
  void add_freqs_to_zero (const std::string& arg);
  vector<FreqRange> freq_ranges_to_zero;
  void range_freqs_to_zero (const std::string& arg);

  void parse_periodic_zap (const std::string& arg);

protected:

  //! Add command line options
  void add_options (CommandLine::Menu&);
};

int main (int argc, char** argv)
{
  paz program;
  return program.main (argc, argv);
}

paz::paz () : Pulsar::Application ("paz", "zaps RFI in archives")
{
  version = "$Id: paz.C,v 1.63 2010/07/05 13:27:13 straten Exp $";

  has_manual = true;
  update_history = true;

  add( new Pulsar::StandardOptions );
  add( new Pulsar::UnloadOptions );
}

bool eightBinZap = false;

string killfile;

bool zero_intersection = false;

float edge_zap_percent = 0.0;

bool simple = false;

double ston_cutoff = 0.0;

string std_filename;
Reference::To < Pulsar::Profile > thestd;

bool periodic_zap = false;
int periodic_zap_period = 8, periodic_zap_phase = 0, periodic_zap_width = 1;

float dropout_sigma = 0.0;

Pulsar::LawnMower * mower = 0;
vector<unsigned> subints_to_mow;
bool mow_all_subints = false;

Pulsar::StandardSNR standard_snr;

Pulsar::ChannelZapMedian * median_zapper = 0;
bool median_zap = false;
bool median_zap_bybin = false;
unsigned median_zap_window = 0;

//Update to remove variable baseline MJK2013, adapted from Patrick Weltevrede
bool removeVariableBaseline = false;

int pol_to_delete = -1;

void paz::add_options (CommandLine::Menu& menu)
{
  CommandLine::Argument* arg = 0;

  // backward compatibility: -u == -O
  menu.add( new CommandLine::Alias( menu.find("O"), 'u' ) );

  menu.add ("\n" "Manual zapping options:");

  arg = menu.add (zero_intersection, 'I');
  arg->set_help ("Zero weight the intersection of -[wW] and -[zZk]");

  arg = menu.add (killfile, 'k', "filename");
  arg->set_help ("Zero weight chans listed in this kill file");

  arg = menu.add (this, &paz::range_bins_to_zap, 'B', "\"a b\"");
  arg->set_help ("Zap pulse phase bins between a and b inclusive");

  arg = menu.add (this, &paz::add_chans_to_zero, 'z', "\"a b c ...\"");
  arg->set_help ("Zero weight these particular channels");

  arg = menu.add (this, &paz::range_chans_to_zero, 'Z', "\"a b\"");
  arg->set_help ("Zero weight chans between a & b inclusive");

  arg = menu.add (this, &paz::add_freqs_to_zero, 'f', "\"a b c ...\"");
  arg->set_help ("Zero weight channels with these frequencies (MHz)");

  arg = menu.add (this, &paz::range_freqs_to_zero, 'F', "\"a b\"");
  arg->set_help ("Zero weight this frequency range, inclusive (MHz)");

  arg = menu.add (this, &paz::add_subs_no_delete, 'x', "\"a b c ...\"");
  arg->set_help ("Delete all sub-integrations except these");

  arg = menu.add (this, &paz::range_subs_no_delete, 'X', "\"a b\"");
  arg->set_help ("Delete all sub-ints except a to b inclusive");

  arg = menu.add (edge_zap_percent, 'E', "percent");
  arg->set_help ("Zero weight this much of each band edge");

  arg = menu.add (this, &paz::add_subs_to_delete, 's', "\"a b c ...\"");
  arg->set_help ("Delete these sub-integrations");

  arg = menu.add (this, &paz::range_subs_to_delete, 'S', "\"a b\"");
  arg->set_help ("Delete sub-ints between a & b inclusive");

  arg = menu.add (this, &paz::add_subs_to_zero, 'w', "\"a b c ...\"");
  arg->set_help ("Zero weight these sub-integrations");

  arg = menu.add (this, &paz::range_subs_to_zero, 'W', "\"a b\"");
  arg->set_help ("Zero weight sub-ints between a & b inclusive");

  arg = menu.add (this, &paz::parse_periodic_zap, 'p', "\"p i\"");
  arg->set_help ("Interpolate over every p-th phase bin, start at i");

  arg = menu.add (pol_to_delete, "poln", "0/1");
  arg->set_help ("Delete the specified polarization");

  menu.add ("\n" "Automatic zapping algorithms:");

  arg = menu.add (median_zap, 'r');
  arg->set_help ("Zap channels using median smoothed difference");

  arg = menu.add (median_zap_window, 'R', "size");
  arg->set_help ("Set the size of the median smoothing window");

  arg = menu.add (this, &paz::add_subints_to_mow, 'l', "subint");
  arg->set_help ("Mow the lawn of the specified subint");

  arg = menu.add (mow_all_subints, 'L');
  arg->set_help ("Mow the lawn of all subints");

  arg = menu.add (simple, 'd');
  arg->set_help ("Zero weight chans using mean offset rejection");

  arg = menu.add (ston_cutoff, 'C', "cutoff");
  arg->set_help ("Zero weight chans based on S/N (std optional)");

  arg = menu.add (std_filename, 'P', "stdfile");
  arg->set_help ("Use this standard profile");

  arg = menu.add (dropout_sigma, 'o', "cutoff");
  arg->set_help ("Zero weight subints with negative dropouts");

  arg = menu.add (eightBinZap, '8');
  arg->set_help ("Fix ATNF WBCORR 8 bin problem (see also -p)");

 //Update to remove variable baseline MJK2013, adapted from Patrick Weltevrede
  arg = menu.add (removeVariableBaseline, 'b');
  arg->set_help ("Remove variable baseline");

  menu.add
    ("\n"
     "The format of the kill file used with the -k option is simply\n"
     "a list of channel numbers, separated by white space\n"
     "\n"
     "The cutoff S/N value used with -C is largely arbitrary. You will\n"
     "need to experiment to find the best value for your archives");
}

const char* whitespace = " ,\n\t";

template<typename T>
void parse_array (std::vector<T>& array, std::string text)
{
  char* c_arg = const_cast<char*>( text.c_str() );

  char* key = strtok (c_arg, whitespace);

  while (key)
  {
    array.push_back( fromstring<T>(key) );
    key = strtok (NULL, whitespace);
  }
}

void parse_range (std::vector<unsigned>& array, std::string text)
{
  unsigned first = 1;
  unsigned last = 0;
  char delimiter = 0;

  if (sscanf (text.c_str(), "%u%c%u", &first, &delimiter, &last) != 3)
    throw Error (InvalidState, "paz::parse",
		 "range '" + text + "' not understood");

  for (unsigned i = first; i <= last; i++)
    array.push_back (i);
}

void paz::add_subints_to_mow (const std::string& arg)
{
  parse_array (subints_to_mow, arg);
}

void paz::range_bins_to_zap (const std::string& arg)
{
  parse_range (bins_to_zap, arg);
}


void paz::add_chans_to_zero (const std::string& arg)
{
  parse_array (chans_to_zero, arg);
}
void paz::range_chans_to_zero (const std::string& arg)
{
  parse_range (chans_to_zero, arg);
}


void paz::add_subs_to_delete (const std::string& arg)
{
  parse_array (subs_to_delete, arg);
}
void paz::range_subs_to_delete (const std::string& arg)
{
  parse_range (subs_to_delete, arg);
}


void paz::add_subs_no_delete (const std::string& arg)
{
  parse_array (subs_no_delete, arg);
}
void paz::range_subs_no_delete (const std::string& arg)
{
  parse_range (subs_no_delete, arg);
}


void paz::add_subs_to_zero (const std::string& arg)
{
  parse_array (subs_to_zero, arg);
}
void paz::range_subs_to_zero (const std::string& arg)
{
  parse_range (subs_to_zero, arg);
}


void paz::add_freqs_to_zero (const std::string& arg)
{
  parse_array (freqs_to_zero, arg);
}
void paz::range_freqs_to_zero (const std::string& arg)
{
  FreqRange range;

  char separator = 0;
  if (sscanf (arg.c_str(), "%lf%c%lf", &range.lo, &separator, &range.hi) != 3)
    throw Error (InvalidState, "paz::parse",
		 "Invalid parameter to option -F");

  if (range.lo > range.hi)
  { 
    double tmp = range.lo;
    range.lo = range.hi;
    range.hi = tmp;
  }

  freq_ranges_to_zero.push_back(range);
}


void paz::parse_periodic_zap (const std::string& arg)
{
  periodic_zap = true;

  int rv = sscanf (arg.c_str(), "%d %d %d", &periodic_zap_period,
		   &periodic_zap_phase, &periodic_zap_width);

  if (rv<2 || rv>3)
    throw Error (InvalidState, "paz::parse",
		 "Invalid parameter to option -p");
  else if (rv==2) 
    periodic_zap_width = 1;
}

bool zero_channels = false;

void paz::setup ()
{
  if (!killfile.empty ())
  {
    FILE *fptr = fopen (killfile.c_str (), "r");

    if (!fptr)
      throw Error (FailedSys, "paz::setup", "fopen " + killfile);

    char *buffer = new char[4096];
    while (fgets (buffer, 4096, fptr))
    {
      char* key = strtok (buffer, whitespace);
      // parse tokens until a comment is started
      while (key && key[0] != '#')
      {
	chans_to_zero.push_back (fromstring<unsigned>(key));
	key = strtok (NULL, whitespace);
      }
    }
  }

  zero_channels = chans_to_zero.size()
    || freqs_to_zero.size() || freq_ranges_to_zero.size();

  bool zero_subints = subs_to_zero.size ();

  if (zero_intersection && !(zero_channels && zero_subints))
    throw Error (InvalidState, "paz::setup",
		 "must use -I with both -s|S|k *and* -w|W");
  
  if (mow_all_subints || subints_to_mow.size())
    mower = new Pulsar::LawnMower;
  
  if (median_zap || median_zap_bybin || median_zap_window)
  {
    median_zapper = new Pulsar::ChannelZapMedian;
    median_zapper->set_bybin (median_zap_bybin);
    median_zapper->set_paz_report (true);

    if (median_zap_window)
      median_zapper->set_window_size (median_zap_window);
  }

  if (edge_zap_percent < 0.0 || edge_zap_percent >= 100.0)
    throw Error (InvalidState, "paz::setup",
		 "invalid parameter to option -E");

  if (!std_filename.empty())
  {
    Reference::To<Pulsar::Archive> data = Pulsar::Archive::load (std_filename);
    data->pscrunch ();
    data->fscrunch ();
    data->tscrunch ();
    thestd = data->get_Profile (0, 0, 0);
    
    standard_snr.set_standard (thestd);
    Pulsar::StrategySet::get_default_snratio() = new Pulsar::StandardSNR;
  }
}

// Instantiate an RFIMitigation class
Pulsar::RFIMitigation* zapper = new Pulsar::RFIMitigation;

void zap_periodic_spikes (Pulsar::Profile * profile,
			  int period, int phase, int width=1);

void binzap (Pulsar::Archive * arch, Pulsar::Integration * integ, int subint,
	     int lower_bin, int upper_bin, int lower_range, int upper_range);


void zap_bin_in_archive(Pulsar::Archive* archive, const unsigned bin);

void zap_bin_in_profile(Pulsar::Profile *profile, const unsigned bin);

const char* periodic_zap_warning =
  "Warning! Periodic spike zapping on frequency-scrunched dedispersed data\n"
  "may not be what you intended! (spikes will wash out)\n";

void paz::process (Pulsar::Archive* arch)
{
  if (periodic_zap)
  {
    if (arch->get_nchan () == 1)
      cout << periodic_zap_warning << endl;

    for (unsigned pol = 0; pol < arch->get_npol (); pol++)
      for (unsigned chan = 0; chan < arch->get_nchan (); chan++)
	for (unsigned subint = 0; subint < arch->get_nsubint (); subint++)
	  zap_periodic_spikes (arch->get_Profile (subint, pol, chan),
			       periodic_zap_period, periodic_zap_phase,
                               periodic_zap_width);
  }

  if (pol_to_delete != -1)
  {
    /*
      Delete either PP=(pol 0) or QQ=(pol 1) by resizing to npol = 1

      If PP is to be deleted, PP and QQ are first swapped.
    */

    if (pol_to_delete != 0 && pol_to_delete != 1)
      throw Error (InvalidState, "paz::process",
		   "--pol = either 0 or 1, not %d", pol_to_delete);

    if (arch->get_state() == Signal::Stokes)
      arch->convert_state( Signal::Coherence );

    Signal::State state = arch->get_state();

    if (state != Signal::Coherence && state != Signal::PPQQ)
      throw Error (InvalidState, "paz::process",
		   "cannot delete polarization when state = %s", 
		   Signal::state_string(state));

    if (verbose)
      cerr << "paz: deleting ipol=" << pol_to_delete 
	   << " current state=" << Signal::state_string(state) << endl;

    const unsigned nchan = arch->get_nchan ();
    const unsigned nsub = arch->get_nsubint ();

    for (unsigned isub = 0; isub < nsub; isub++)
    {
      // ensure that the sub-integration is loaded from file
      Pulsar::Integration* subint = arch->get_Integration (isub);

      if (pol_to_delete == 0)
      {
        // Swap pol 0 and pol 1 in each sub-integration and frequency channel
	for (unsigned ichan = 0; ichan < nchan; ichan++)
	  subint->expert()->swap_profiles (0, ichan, 1, ichan);
      }
    }

    // resize to npol = 1
    arch->resize (nsub, 1);
    arch->set_state( Signal::Intensity );
  } 

  // To fix early wide-band correlator problem
  if (eightBinZap)
  {
    for (unsigned pol = 0; pol < arch->get_npol (); pol++) {
      for (unsigned chan = 0; chan < arch->get_nchan (); chan++) {
	for (unsigned subint = 0; subint < arch->get_nsubint (); subint++) {
	  Pulsar::Profile * prof = arch->get_Profile (subint, pol, chan);
	  float *amps = prof->get_amps ();
	  for (unsigned i = 5; i < arch->get_nbin (); i += 8) {
	    // Interpolate between previous and later point if possible
	    if (i == arch->get_nbin () - 1)
	      amps[i] = amps[i - 1];
	    else {
	      float amp1 = amps[i - 1];
	      float amp2 = amps[i + 1];
	      amps[i] = 0.5 * (amp1 + amp2);
	    }
	  }
	  prof->set_amps (amps);
	}
      }
    }
  }

  unsigned nchan = arch->get_nchan ();
  double chan_bw = fabs(arch->get_bandwidth() / (double)nchan);

  if (subs_to_delete.size())
  {
    unsigned zapped = 0;
    unsigned orig_nsubint = arch->get_nsubint ();

    for (unsigned i = 0; i < orig_nsubint; i++)
      for (unsigned j = 0; j < subs_to_delete.size (); j++)
	if (subs_to_delete[j] == i)
	{
	  if (verbose)
	    cout << "Deleting subint " << i << endl;

	  // after each subint is zapped, it is necessary to offset the index
	  arch->erase (i - zapped);
	  zapped ++;
	}

    if (verbose)
      cerr << "paz: deletion completed" << endl;
  }

  if (subs_no_delete.size())
  {
    unsigned zapped = 0;
    unsigned orig_nsubint = arch->get_nsubint ();

    for (unsigned i = 0; i < orig_nsubint; i++)
    {
      bool keep = false;
      for (unsigned j = 0; j < subs_no_delete.size (); j++)
	if (subs_no_delete[j] == i)
	  keep = true;

      if (!keep)
      {
	if (verbose)
	  cout << "Deleting subint " << i << endl;

	// after each subint is zapped, it is necessary to offset the index
	arch->erase (i - zapped);
	zapped ++;
      }
    }

    if (verbose)
      cerr << "paz: deletion completed" << endl;
  }

  if (median_zapper)
  {
    cout << "Using median smoothed difference zapper" << endl;
    (*median_zapper) (arch);
  }

  if (simple)
  {
    cout << "Using simple mean offset zapper" << endl;
    zapper->zap_chans (arch);
    cout << "Zapping complete" << endl;
  }

  if (subs_to_zero.size() && !zero_intersection)
  {
    vector<float> mask (nchan, 0.0);
    zapper->zap_very_specific (arch, mask, subs_to_zero);
  }

  if (bins_to_zap.size ())
  {
    for (unsigned i = 0; i < bins_to_zap.size(); ++i)
      zap_bin_in_archive(arch, bins_to_zap[i]);
  }

  if (zero_channels)
  {
    vector<float> mask (nchan, 1.0);
    for (unsigned i = 0; i < chans_to_zero.size (); i++)
    {
      if (chans_to_zero[i] >= nchan)
        throw Error (InvalidRange, "paz::process",
            "channel %d is out of range (nchan=%d)",
           chans_to_zero[i], nchan); 
      mask[chans_to_zero[i]] = 0.0;
    }

    for (unsigned i = 0; i < freqs_to_zero.size (); i++)
    {
      for (unsigned ic=0; ic<nchan; ic++)
      {
	double chan_freq=arch->get_Integration(0)->get_centre_frequency(ic);
	if ( freqs_to_zero[i] > chan_freq - chan_bw/2.0 
	     && freqs_to_zero[i] < chan_freq + chan_bw/2.0 )
	  mask[ic] = 0.0;
      }
    }

    for (unsigned i=0; i< freq_ranges_to_zero.size(); i++)
    {
      for (unsigned ic=0; ic<nchan; ic++)
      {
	FreqRange chan;
	chan.lo=arch->get_Integration(0)->get_centre_frequency(ic) 
	  - chan_bw/2.0;
	chan.hi=arch->get_Integration(0)->get_centre_frequency(ic) 
	  + chan_bw/2.0;

	/*
	  much simpler overlap logic fixes bug #422: 
	  "paz -F fails when frequency range exactly matches single channel" 
	*/

	if (freq_ranges_to_zero[i].hi > chan.lo &&
	    freq_ranges_to_zero[i].lo < chan.hi)
	  mask[ic] = 0.0;
      }
    }
    
    if (zero_intersection)
      zapper->zap_very_specific (arch, mask, subs_to_zero);
    else
      zapper->zap_specific (arch, mask);
  }
  
  if (edge_zap_percent)
  {
    float fraction = edge_zap_percent / 100.0;
    unsigned buffer = unsigned (float (nchan) * fraction);

    vector < float >mask (nchan, 0.0);

    for (unsigned i = buffer; i < (nchan - buffer); i++)
      mask[i] = 1.0;

    zapper->zap_specific (arch, mask);
  }

  if (ston_cutoff > 0.0)
  {
    double theston = 0.0;
    Reference::To<Pulsar::Archive> cloned = arch->clone();
    cloned->pscrunch ();
    for (unsigned isub = 0; isub < arch->get_nsubint (); isub++) {
      for (unsigned ichan = 0; ichan < arch->get_nchan (); ichan++) {
	theston = cloned->get_Profile (isub, 0, ichan)->snr ();
	if (theston < ston_cutoff) {
	  arch->get_Integration (isub)->set_weight (ichan, 0.0);
	}
      }
    }
  }

  if (dropout_sigma)
  {
    Reference::To < Pulsar::Archive > cloned = arch->clone ();
    Reference::To < Pulsar::Profile > testprof = 0;

    cloned->pscrunch ();
    cloned->fscrunch ();
    cloned->remove_baseline ();

    vector < double >mins;
    for (unsigned isub = 0; isub < arch->get_nsubint (); isub++) {
      testprof = cloned->get_Profile (isub, 0, 0);
      mins.push_back (fabs (testprof->min ()));
    }

    cloned->tscrunch ();

    double mean, vari, varm;

    testprof = cloned->get_Profile (0, 0, 0);
    testprof->stats (&mean, &vari, &varm);

    for (unsigned isub = 0; isub < arch->get_nsubint (); isub++) {
      if (mins[isub] > dropout_sigma * sqrt (vari)) {
	cerr << "Zapping integration " << isub << endl;
	arch->get_Integration (isub)->uniform_weight (0.0);
      }
    }
  }

  if (mower)
  {
    if (mow_all_subints)
    {
      subints_to_mow.resize (arch->get_nsubint ());
      for (unsigned isub = 0; isub < arch->get_nsubint (); isub++)
	subints_to_mow[isub] = isub;
    }

    for (unsigned isub = 0; isub < subints_to_mow.size (); isub++)
    {
      cerr << "paz: mowing subint " << subints_to_mow[isub] << endl;
      mower->transform (arch->get_Integration (subints_to_mow[isub]));
    }
  }

  //Update to remove variable baseline MJK2013, adapted from Patrick Weltevrede
  if (removeVariableBaseline) {
    fprintf(stderr, "Set remove_baseline_strategy.\n");
    Pulsar::Archive::remove_baseline_strategy.set (new Pulsar::RemoveVariableBaseline, &Pulsar::RemoveVariableBaseline::transform);
    arch->remove_baseline();
  }
}

/**
 * Zaps (assigns the mean) to a single bin across each profile in the archive.
 */
void zap_bin_in_archive(Pulsar::Archive* archive, const unsigned bin)
{
  for (unsigned isub = 0; isub < archive->get_nsubint(); ++isub)
    for (unsigned ipol = 0; ipol < archive->get_npol(); ++ipol)
      for (unsigned ichan = 0; ichan < archive->get_nchan(); ++ichan)
        zap_bin_in_profile(archive->get_Profile(isub, ipol, ichan), bin);
}

/**
 * Zaps (zero-values) the input bin in the input profile.
 */
void zap_bin_in_profile(Pulsar::Profile *profile, const unsigned bin)
{
  // Take the mean from the centre of the profile.
  const float mean = profile->mean(0.5);
  profile->get_amps()[bin] = mean;
}
