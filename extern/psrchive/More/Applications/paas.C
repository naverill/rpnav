/***************************************************************************
 *
 *   Copyright (C) 2006 by Russell Edwards
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/
//
// paas -- Pulsar archive analytic standard  maker
//  (Deze programma heeft niks te maken met het christelijke feest, Pasen! :)


#include "Pulsar/ComponentModel.h"

#include "Pulsar/Archive.h"
#include "Pulsar/Integration.h"
#include "Pulsar/Profile.h"
#include "Pulsar/Index.h"

#include "Error.h"
#include "dirutil.h"
#include "strutil.h"

#include <cpgplot.h>

#include <fstream>
#include <iostream>

#include <unistd.h>
#include <string.h>

using namespace MEAL;
using namespace Pulsar;
using namespace std;

void usage ()
{
  cout << "paas: Pulsar archive analytic standard maker\n"
       "Usage: paas [options] archive\n"
       "Input: an archive [and optionally a model]\n"
       "Output: lekkere chocolade eiren\n";

  cout << "Options:  \n" 
    "  -r filename   Read model from file        [Default: use empty model]\n"
    "  -w filename   Write model to file         [Default: paas.m] \n"
    "  -S            Report width as standard deviation instead of concentration \n"
    "  -c \"a b c\"    Add component (a=centre, b=concentration, d=height)\n"
    "  -f            Fit model to pulse profile  \n"
    "  -L            log(height) -> force all component heights to be > 0 \n"
    "  -F flags      Set parameters in/out of fit, e.g. -F 011101 fits only\n"
    "                   for concentration_1, height_1, centre_2 and height_2\n"
    "                   [Default: fit all parameters]\n"
    "  -W            Fit derivative of profile instead (i.e. weight by harm. num.)\n"
    "  -t threshold  Delta_rms/rms fit convergence criterion [Default: 1e-6]\n"
    "  -d device     Plot results on specified PGPLOT device\n"
    "  -D            Plot results on PGPLOT device /xs\n"
    "  -l            Plot with lines instead of steps\n"
    "  -b factor     Sum groups of \"factor\" adjacent bins before plotting results\n"
    "  -s filename   Write analytic standard to given filename [Default: paas.std]\n"
    "  -i            Interactively select components \n"
    "  -C            Centre profile using ephemeris\n"
    "  -p            Rotate profile to place peak at bin 0\n"
    "  -P            Fix the relative phase of the components as in pat \n"
    "  -R phase      Rotate profile by specified number of turns\n"
    "  -z ph1,ph2    Zoom in on pulse phase region \n"
    "  -a            After loading a model, rotate it to align to profile\n"
    "  -j filename   Output details to given filename    [Default: paas.txt]\n";
}

// Text output
void write_details_to_file (ComponentModel& m,
			    Archive* observation, Archive* model,
			    const string& filename);

void plot (ComponentModel& model, unsigned npts,
	   bool line_plot, int icomp_selected = -1);

void plot_difference (ComponentModel& model, const Profile *profile,
		      bool line_plot);

Index ipol;

int main (int argc, char** argv) try
{
  // the multiple component model
  ComponentModel model;

  string model_filename_in;
  string model_filename_out = "paas.m";
  string details_filename   = "paas.txt";
  string std_filename       = "paas.std";

  bool fit = false;
  vector<string> new_components;
  string fit_flags;

  int bscrunch = 1;

  string pgdev;

  bool line_plot=false;
  bool interactive = false;
  bool centre_model = false, rotate_peak=false;
  float rotate_amount = 0.0;
  bool align = false;
  
  float xmin=0.0, xmax=0.0;

  ipol.set_integrate (true);
  
  const char* args = "ab:c:Cd:DfF:hiI:j:lLpPr:R:s:St:Vw:Wz:";
  int c;

  while ((c = getopt(argc, argv, args)) != -1)
    switch (c) {

    case 'h':
      usage ();
      return 0;

    case 'I':
      ipol = fromstring<Index>(optarg);
      break;
      
    case 'r':
      model_filename_in = optarg;
      break;
      
    case 'w':
      model_filename_out = optarg;
      break;
      
    case 'f':
      fit = true;
      break;

    case 'W':
      model.set_fit_derivative (true);
      break;

    case 'c':
      new_components.push_back(optarg);
      break;

    case 'F':
      fit_flags = optarg;
      break;

    case 'b':
      bscrunch = atoi (optarg);
      break;

    case 't':
      model.set_threshold( atof(optarg) );
      break;

    case 'd':
      pgdev = optarg;
      break;

    case 'i':
      interactive = true;
    case 'D':
      pgdev = "/xs";
      break;
      
    case 'l':
      line_plot = true;
      break;

    case 'L':
      model.set_log_height(true);
      break;

    case 's':
      std_filename = optarg;
      break;

    case 'S':
      model.set_report_widths (true);
      break;
      
    case 'C':
      centre_model = true;
      break;

    case 'p':
      rotate_peak = true;
      break;

    case 'P':
      model.fix_relative_phases();
      break;

    case 'R':
      rotate_amount = atof(optarg);
      break;

    case 'a':
      align = true;
      break;

    case 'j':
      details_filename = optarg;
      break;

	case 'V':
	    ComponentModel::verbose = true;
	    break;

    case 'z':
    {
      char separator = 0;
      if (sscanf (optarg, "%f%c%f", &xmin, &separator, &xmax) != 3)
      {
	cerr << "paas: could not parse phase zoom from '" << optarg << "'"
	     << endl;
	return -1;
      }
      break;
    }

   default:
      cerr << "invalid param '" << c << "'" << endl;
    }

  if (optind != argc-1)
  {
    cerr << "paas: please specify one filename" << endl;
    return -1;
  }

  if (!pgdev.empty())
  {
    cpgopen(pgdev.c_str());
    cpgask(0);
    cpgsvp(0.1, 0.9, 0.1, 0.9);
  }
  
  Reference::To<Archive> archive = Archive::load (argv[optind]);

  // preprocess0
  archive->fscrunch();
  archive->tscrunch();
  if (ipol.get_integrate())
    archive->pscrunch();

  // phase up as requested
  if (centre_model)
    archive->centre();
  else if (rotate_peak)
    archive->centre_max_bin(0.0);
  
  if (rotate_amount != 0.0)
    archive->rotate_phase(-rotate_amount);
  
  archive->remove_baseline();

  double gate = archive->get_Integration(0)->get_gate_duty_cycle();
  model.set_gate_duty_cycle (gate);
  
  if (xmin == xmax)
  {
    xmin = 0.0;
    xmax = gate;
  }

  Reference::To<const Profile> constprofile = get_Profile (archive, 0, ipol, 0);
  Reference::To<Profile> profile = constprofile->clone();
    
  // load from file if specified
  if (!model_filename_in.empty())
  {
    cerr << "paas: loading model from " << model_filename_in << endl;
    model.load(model_filename_in.c_str());
    
    // align to profile first if asked
    if (align)
    {
      cerr << "paas: roughly aligning model to data" << endl;
      model.align( profile );
    }
  }

  // add any new components specified
  unsigned ic, nc=new_components.size();
  double centre, concentration, height;
  int name_start;
  for (ic=0; ic < nc; ic++)
  {
    if (sscanf(new_components[ic].c_str(), 
	       "%lf %lf %lf %n", &centre, &concentration, &height,
	       &name_start)!=3)
    {
      cerr << "Could not parse component " << ic << endl;
      return -1;
    }

    model.add_component(centre, concentration, height, 
			new_components[ic].c_str()+name_start);
  }

  bool iterate = true;

  if (model.get_report_absolute_phases ())
  {
    cerr << "paas: align profile to model (report absolute phases)" << endl;
    model.align_to_model ( profile );
  }
  
  while (iterate)
  {
    iterate = false;

    // fit if specified
    if (fit)
    {
      // set fit flags
      model.set_infit(fit_flags.c_str());
      // fit
      model.fit( profile );

      cerr << "paas: reduced chisq=" << model.get_chisq() / model.get_nfree()
	   << endl;
    }
 
    // plot
    if (!pgdev.empty())
    {
      Reference::To<Pulsar::Archive> scrunched;
      scrunched = archive->clone();
      if (bscrunch > 1)
	scrunched->bscrunch(bscrunch);

      cpgpage();

      Reference::To<const Profile> prof = get_Profile (archive, 0, ipol, 0);
      float ymin = prof->min();
      float ymax = prof->max();
      float extra = 0.05*(ymax-ymin);

      ymin -= extra;
      ymax += extra;
      cpgswin (xmin, xmax, ymin, ymax);
      cpgbox("bcnst", 0, 0, "bcnst", 0, 0);
      cpglab("Pulse phase", "Intensity", "");
      unsigned i, npts=prof->get_nbin();
      cpgsci(14);
      for (ic=0; ic < model.get_ncomponents(); ic++)
	plot(model, npts, true, ic);
      vector<float> xvals(npts);
      cpgsci(4);
      for (i=0; i < npts; i++)
	xvals[i] = i * gate / npts;
      if (line_plot)
	cpgline(npts, &xvals[0], prof->get_amps());
      else
	cpgbin(npts, &xvals[0], prof->get_amps(), 0);
      cpgsci(2);
      plot_difference(model, prof, line_plot);
      cpgsci(1);
      plot(model, npts, true);
    }
    
    
    while (interactive)
    {
      iterate = true;
      fit = false;

      cerr << "Enter command ('h' for help)" << endl;

      float curs_x=0, curs_y=0;
      char key = ' ';
      if (cpgband(6,0,0,0,&curs_x, &curs_y, &key) != 1 || key == 'q')
      {
	cerr << "Quitting ..." << endl;
	iterate = false;
	break;
      }

      if (key == 'h')
      {
	cerr << 
	  "\n"
	  "z key      - start phase zoom (any key to finish) \n"
	  "r          - reset phase zoom (0 -> 1) \n"
	  "left click - add a component at cursor position\n"
	  "f key      - fit current set of components\n"
	  "q key      - save model and quit\n"
	  "\n"
	  "After left click:\n"
	  "   1) any key - select width of new component, then \n"
	  "   2) any key - select height of new component \n"
	  "\n"
	  "   right click or <Escape> to abort addition of new component \n"
	     << endl;
	continue;
      }
      
      if (key == 'f')
      {
	cerr << "Fitting components" << endl;
	fit = true;
	break;
      }
      
      if (key == 'r')
      {
	xmin = 0;
	xmax = 1;
	break;
      }

      if (key == 'z')
      {
	float new_x = curs_x;
	cerr << "Select other end of zoom region and hit any key" << endl;
	if (cpgband(4, 1, curs_x, curs_y, &curs_x, &curs_y, &key) != 1) {
	  cerr << "paas: cpgband error" << endl;
	  return -1;
	}

	// right click or escape to abort
	if (key == 88 || key == 27)
	  continue;
	
	xmax = std::max (new_x, curs_x);
	xmin = std::min (new_x, curs_x);

	break;
      }

      if (key == 'a' || key == 65)
      {
	cerr << "Adding component centred at pulse phase " << curs_x << endl;
	double centre = curs_x;

	cerr << "Select width and hit any key" << endl;
	if (cpgband(4, 1, curs_x, curs_y, &curs_x, &curs_y, &key) != 1) {
	  cerr << "paas: cpgband error" << endl;
	  return -1;
	}
	
	// right click or escape to abort
	if (key == 88 || key == 27)
	  continue;
	
	double width = fabs(centre - curs_x);
	
	cerr << "Select height and hit any key" << endl;
	if (cpgband(5,0,0,0,&curs_x, &curs_y, &key) != 1) {
	  cerr << "paas: cpgband error" << endl;
	  return -1;
	}
	
	// right click or escape to abort
	if (key == 88 || key == 27)
	  continue;
	
	double height = curs_y;

	if (width == 0)
	{
	  cerr << "ignoring new component with zero width" << endl;
	  break;
	}
	
	if (model.get_log_height() && height <=0)
	{
	  cerr << "ignoring new component with -ve height" << endl;
	  break;
	}
	
	model.add_component (centre, 0.25/(width*width), height, "");
	
	break;
	
      }
      
      cerr << "Unrecognized command" << endl;
      
    }
    
  }
  
  // write out if specified
  if (!model_filename_out.empty())
  {
    cerr << "paas: writing model to " << model_filename_out << endl;
    model.unload (model_filename_out.c_str());
  }
  
  Reference::To<Pulsar::Archive> copy = archive->clone();

  // write out standard
  model.evaluate (archive->get_Integration(0)->get_Profile(0,0)->get_amps(),
		  archive->get_nbin());

  write_details_to_file (model, copy, archive, details_filename);
  
  cerr << "paas: writing standard to " << std_filename << endl;
  archive->unload (std_filename);

  if (!pgdev.empty())
    cpgend();

  return 0;
}
catch (Error& error)
{
  cerr << error << endl;
  return -1;
}

void write_details_to_file (ComponentModel& m, Archive* input_archive,
			    Archive* model, const string& filename)
{
  ofstream out (filename.c_str());

  if (!out)
    throw Error (FailedSys, "ComponentModel::write_details_to_file",
		 "Unable to open file=" + filename);

  Profile* input_profile = input_archive->get_Integration(0)->get_Profile(0,0);
  
  if (m.get_report_absolute_phases())
  {
    double shift_in_rad = m.get_absolute_phase();
    input_profile->rotate_phase (shift_in_rad/ (2*M_PI));
  }
  
  const unsigned nbin = input_archive->get_nbin();
  const unsigned ncomp = m.get_ncomponents();

  out << "# " << input_archive->get_filename() << " " <<
    input_archive->get_source() << " " << 
    input_archive->get_centre_frequency() << " " << nbin << " " <<
    ncomp << " " << ( m.get_chisq() / m.get_nfree() ) << endl;

  float* in_bin = input_profile->get_amps();
  float* model_bin = model->get_Profile(0,0,0)->get_amps();
  
  vector<vector<float> > values(ncomp);
  for (unsigned icomp = 0; icomp < ncomp; ++icomp)
  {
    values[icomp].resize(nbin);
    m.evaluate(&values[icomp][0], nbin, icomp);
  }

  for (unsigned ibin = 0; ibin < nbin; ++ibin, ++in_bin, ++model_bin)
  {
    out << ibin << " " << *in_bin << " " << *model_bin;

    for (unsigned icomp = 0; icomp < ncomp; ++icomp)
      out << " " << values[icomp][ibin];

    out << endl;
  }
}

void plot (ComponentModel& model, unsigned npts,
	   bool line_plot, int icomp_selected) 
{
  // evaluate
  vector<float> xvals(npts);
  vector<float> yvals(npts);

  model.evaluate (&yvals[0], npts, icomp_selected);

  double gate = model.get_gate_duty_cycle ();
  
  for (unsigned i=0; i < npts; i++)
    xvals[i] = i * gate / npts;

  //plot
  if (line_plot)
    cpgline(npts, &xvals[0], &yvals[0]);
  else
    cpgbin(npts, &xvals[0], &yvals[0], 0);
}

void plot_difference (ComponentModel& model, const Profile *profile,
		      bool line_plot) 
{
  double gate = model.get_gate_duty_cycle ();
  
  // evaluate
  unsigned i, npts = profile->get_nbin();
  vector<float> xvals(npts);
  vector<float> yvals(npts);

  model.evaluate (&yvals[0], npts);

  for (i=0; i < npts; i++)
  { 
    xvals[i] = i * gate / npts;
    yvals[i] =  profile->get_amps()[i]- yvals[i];
  }

  //plot
  if (line_plot)
    cpgline(npts, &xvals[0], &yvals[0]);
  else
    cpgbin(npts, &xvals[0], &yvals[0], 0);
}
