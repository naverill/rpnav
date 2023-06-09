/***************************************************************************
 *
 *   Copyright (C) 2003 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Pulsar/Config.h"
#include "Pulsar/FITSArchive.h"
#include "Pulsar/Integration.h"
#include "Pulsar/Profile.h"

#include "Pulsar/ProfileColumn.h"
#include "Pulsar/FITSHdrExtension.h"
#include "Pulsar/FITSSUBHdrExtension.h"
#include "Pulsar/ObsDescription.h"
#include "Pulsar/ObsExtension.h"
#include "Pulsar/ITRFExtension.h"
#include "Pulsar/CalInfoExtension.h"
#include "Pulsar/Receiver.h"
#include "Pulsar/WidebandCorrelator.h"
#include "Pulsar/DigitiserStatistics.h"
#include "Pulsar/DigitiserCounts.h"
#include "Pulsar/ProcHistory.h"
#include "Pulsar/Passband.h"
#include "Pulsar/PolnCalibratorExtension.h"
#include "Pulsar/FluxCalibratorExtension.h"
#include "Pulsar/CalibratorStokes.h"
#include "Pulsar/CalibrationInterpolatorExtension.h"
#include "Pulsar/IntegrationOrder.h"
#include "Pulsar/CoherentDedispersion.h"
#include "Pulsar/SpectralKurtosis.h"
#include "Pulsar/CrossCovarianceMatrix.h" 

#include "Pulsar/Telescopes.h"
#include "Pulsar/Telescope.h"
#include "Pulsar/ThresholdMatch.h"

#include "Pulsar/Predictor.h"

#include "psrfitsio.h"
#include "strutil.h"

#include <string.h>

using namespace std;

//! null constructor
// //////////////////////////
// //////////////////////////


void Pulsar::FITSArchive::init ()
{
  psrfits_version = 0.0;
  chanbw = 0.0; 

  scale_cross_products = false;
  correct_P236_reference_epoch = false;

  // folded mode by default
  search_mode = false;

  // no auxiliary profiles
  naux_profile = 0;
  aux_nsample = 0;
  
  // on construction, the data have not been loaded from fits file
  loaded_from_fits = false;

  // default fraction of the pulse period recorded (in turns)
  gate_duty_cycle = 1.0;

  read_fptr = 0;
  read_filename.clear();
}

//
//
//
Pulsar::FITSArchive::FITSArchive()
{ 
  if (verbose > 2)
    cerr << "FITSArchive default construct" << endl;

  add_extension(new FITSHdrExtension());

  init ();
}

//
//
//
Pulsar::FITSArchive::FITSArchive (const FITSArchive& arch)
{
  if (verbose > 2)
    cerr << "FITSArchive copy construct" << endl;
  
  init ();
  Archive::copy (arch); // results in call to FITSArchive::copy
}

//
//
//
Pulsar::FITSArchive::~FITSArchive()
{
  if (verbose > 2)
    cerr << "Pulsar::FITSArchive dtor this=" << this << endl;
  if (read_fptr)
  {
    int status = 0;
    fits_close_file (read_fptr, &status);
  
    if (status)
      throw FITSError (status, "Pulsar::FITSArchive::~FITSArchive",
         "fits_close_file");
    read_fptr = 0;
  }
}

//
//
//
const Pulsar::FITSArchive&
Pulsar::FITSArchive::operator = (const FITSArchive& arch)
{
  if (verbose > 2)
    cerr << "FITSArchive assignment operator" << endl;

  Archive::copy (arch); // results in call to FITSArchive::copy
  return *this;
}

//
//
//
Pulsar::FITSArchive::FITSArchive (const Archive& arch)
{
  if (verbose > 2)
    cerr << "FITSArchive base copy construct" << endl;

  init ();
  Archive::copy (arch); // results in call to FITSArchive::copy
}

/*! The Integration subset can contain anywhere between none and all of
   integrations in the source Archive */
void Pulsar::FITSArchive::copy (const Archive& archive)
{
  if (verbose > 2)
    cerr << "FITSArchive::copy" << endl;

  if (this == &archive)
    return;
  
  Archive::copy (archive);

  if (verbose > 2)
    cerr << "FITSArchive::copy dynamic cast call" << endl;

  const FITSArchive* farchive = dynamic_cast<const FITSArchive*>(&archive);
  if (!farchive)
    return;
  
  if (verbose > 2)
    cerr << "FITSArchive::copy another FITSArchive" << endl;
  
  chanbw = farchive->chanbw;
  scale_cross_products = farchive->scale_cross_products;
  loaded_from_fits = farchive->loaded_from_fits;
  gate_duty_cycle = farchive->gate_duty_cycle;

  if (verbose > 2)
    cerr << "FITSArchive::copy exit" << endl;
}

//! Returns a pointer to a new copy-constructed FITSArchive instance
Pulsar::FITSArchive* Pulsar::FITSArchive::clone () const
{
  if (verbose > 2)
    cerr << "FITSArchive::clone" << endl;

  return new FITSArchive (*this);
}

// ////////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////////
//! Read FITS header info from a file into a FITSArchive object.
//

void Pulsar::FITSArchive::load_header (const char* filename) try
{
  int status = 0;
  
  // Open the data file  
  if (verbose > 2)
    cerr << "FITSArchive::load_header fits_open_file ("<< filename <<")"<<endl;
  
  fits_open_file (&read_fptr, filename, READONLY, &status);
  
  if (status != 0)
    throw FITSError (status, "FITSArchive::load_header", 
		     "fits_open_file(%s)", filename);

  read_filename.assign(filename);

  // These Extensions must exist in order to load

  ObsExtension*     obs_ext = getadd<ObsExtension>();
  FITSHdrExtension* hdr_ext = getadd<FITSHdrExtension>();

  // Pulsar FITS header definiton version

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading FITS header version" << endl;

  string tempstr;
  string dfault;

  dfault = hdr_ext->hdrver;
  psrfits_read_key (read_fptr, "HDRVER", &tempstr, dfault, verbose > 2);
  hdr_ext->hdrver = tempstr;
  psrfits_version = fromstring<float>( tempstr );
  if (sscanf (hdr_ext->hdrver.c_str(), "%d.%d", 
	      &hdr_ext->major_version, &hdr_ext->minor_version) != 2)
    throw Error (InvalidParam, "FITSARchive::load_header",
		 "could not parse header version from " + hdr_ext->hdrver);
 
  if (verbose > 2)
    cerr << "Got: Version " << tempstr << endl;
  
  // File creation date
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading file creation date" << endl;

  dfault = hdr_ext->creation_date;
  psrfits_read_key (read_fptr, "DATE", &tempstr, dfault, verbose > 2);
  hdr_ext->creation_date = tempstr;

  // Name of observer
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading observer name" << endl;

  dfault = obs_ext->observer;
  psrfits_read_key (read_fptr, "OBSERVER", &tempstr, dfault, verbose > 2);
  obs_ext->observer = tempstr;
  
  if (verbose > 2)
    cerr << "Got observer: " << tempstr << endl;
  
  // Project ID
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading project ID" << endl;

  dfault = obs_ext->project_ID;
  psrfits_read_key (read_fptr, "PROJID", &tempstr, dfault, verbose > 2);
  obs_ext->project_ID = tempstr;

  if (verbose > 2)
    cerr << "Got PID: " << tempstr << endl;
  
  // Telescope name
    
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading telescope name" << endl;

  dfault = get_telescope();
  psrfits_read_key (read_fptr, "TELESCOP", &tempstr, dfault, verbose > 2);

  tempstr = stringtok (tempstr, " ");
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header TELESCOP=" << tempstr << endl;
  
  set_telescope ( tempstr );

  Telescope* telescope = getadd<Telescope>();

  try
  {
    if (verbose > 2)
      cerr << "FITSArchive::load_header calling Telescopes::set_telescope_info" << endl;

    Telescopes::set_telescope_info (telescope, this);
  }
  catch (Error& error)
  {
    if (verbose > 2)
      cerr << "FITSArchive::load_header WARNING"
	" could not set telescope info for " << get_telescope() << "\n\t"
	   << error.get_message() << endl;
  }

  // RA
    
  psrfits_read_key( read_fptr, "RA", &tempstr, dfault, verbose > 2 );
  hdr_ext->set_ra( tempstr );
  
  // DEC
  
  psrfits_read_key( read_fptr, "DEC", &tempstr, dfault, verbose > 2 );
  hdr_ext->set_dec( tempstr );

  // Antenna ITRF coordinates

  load_ITRFExtension (read_fptr);

  // Receiver parameters

  load_Receiver (read_fptr);

  // WidebandCorrelator parameters

  load_WidebandCorrelator (read_fptr);

  Backend* backend = get<Backend>();
  if (backend && strstr (backend->get_name().c_str(), "BPP"))
  {
    if (verbose > 2)
      cerr << "FITSArchive::load_header using BPP matching policy" << endl;
    ThresholdMatch::set_BPP (this);
  }

  //
  //
  // Figure out what kind of observation it was
  //
  //

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading OBS_MODE" << endl;

  psrfits_read_key (read_fptr, "OBS_MODE", &tempstr);
  string obs_mode = tempstr;
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header OBS_MODE='" << obs_mode << "'" << endl;

  hdr_ext->set_obs_mode( obs_mode );

  if (obs_mode == "PSR" || obs_mode == "LEVPSR")
  {
    set_type ( Signal::Pulsar );
    if (verbose > 2)
      cerr << "FITSArchive::load_header using Signal::Pulsar" << endl;
  }
  else if (obs_mode == "CAL" || obs_mode == "LEVCAL")
    set_type ( Signal::PolnCal );
  else if (obs_mode == "FOF")
    set_type ( Signal::FluxCalOff );
  else if (obs_mode == "FON")
    set_type ( Signal::FluxCalOn );
  else if (obs_mode == "PCM")
    set_type ( Signal::Calibrator );

  else if (obs_mode == "SEARCH" || obs_mode == "SRCH")
  {
    search_mode = true;
    set_type ( Signal::Unknown );
    if (verbose > 2)
      cerr << "FITSArchive::load_header search mode file" << endl;
  }
  else
  {
    if (verbose > 2)
      cerr << "FITSArchive::load_header WARNING unknown OBSTYPE = " 
	   << tempstr <<endl;
    set_type ( Signal::Unknown );
  }

  //
  //
  // Read the centre frequency of the observation
  //
  //
  {
    double dfault = 0.0;
    double centre_frequency;

    psrfits_read_key (read_fptr, "OBSFREQ", &centre_frequency, dfault, verbose > 2);
    set_centre_frequency( centre_frequency );
    hdr_ext->set_obsfreq( centre_frequency );
  }

  // Read the bandwidth of the observation
  {
    double dfault = 0.0;
    double bandwidth;

    psrfits_read_key (read_fptr, "OBSBW", &bandwidth, dfault, verbose > 2);
    set_bandwidth( bandwidth );
    hdr_ext->set_obsbw( bandwidth );
  }

  // Read the number of channels of the observation
  {
    int obsnchan;

    psrfits_read_key (read_fptr, "OBSNCHAN", &obsnchan, 0, verbose > 2);
    hdr_ext->set_obsnchan( obsnchan );
  }

  // Read the online DM value
  {
    double chan_dm;

    psrfits_read_key (read_fptr, "CHAN_DM", &chan_dm, FITSHdrExtension::unset_dm, 
        verbose > 2);
    hdr_ext->set_chan_dm( chan_dm );
  }

  // Read the name of the source

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading source name" << endl;

  psrfits_read_key (read_fptr, "SRC_NAME", &tempstr);
  set_source ( tempstr );
  
  // Read where the telescope was pointing
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading coordinates" << endl;

  dfault = hdr_ext->coordmode;
  psrfits_read_key (read_fptr, "COORD_MD", &tempstr, dfault, verbose > 2);
  hdr_ext->coordmode = tempstr;

  if (verbose > 2)
    cerr << "Got coordinate type: " << tempstr << endl;

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading equinox" << endl;

  // Read the equinox of the coordinate systembandwidth of the observation
  {
    double dfault = 2000.0;
    double equinox;

    psrfits_read_key (read_fptr, "EQUINOX", &equinox, dfault, verbose > 2);
    hdr_ext->equinox = equinox;
  }

  //
  // PSRFITS definitons versions before 2.8 have
  //   COORD_MD = J2000
  //   and no EQUINOX attribute
  //
  // PSRFITS definition versions 2.8 to 2.10 have
  //   COORD_MD = EQUAT
  //   EQUINOX = J2000
  //
  // PSRFITS definition version 2.11 has
  //   COORD_MD = J2000 (again)
  //   EQUINOX = 2000.0
  //

  if (hdr_ext->coordmode == "EQUAT")
    hdr_ext->coordmode = "J2000";

  //
  // ... also, since PSRFITS definition version 2.8, "Gal" is "GAL"
  //
  if (hdr_ext->coordmode == "Gal" || hdr_ext->coordmode == "GALACTIC")
    hdr_ext->coordmode = "GAL";

  //
  // ... also, if unset assume J2000
  //
  if (hdr_ext->coordmode == "UNSET")
  {
    warning << "FITSArchive::load_header WARNING"
      " assuming J2000 coordinates" << endl;
    hdr_ext->coordmode = "J2000";
  }

  sky_coord coord;
  
  if (hdr_ext->coordmode == "J2000")
  {
    dfault = "";

    psrfits_read_key (read_fptr, "RA", &tempstr, dfault, verbose > 2);

    string stt_crd1 = "";
    psrfits_read_key (read_fptr, "STT_CRD1", &stt_crd1, dfault, verbose > 2);
    hdr_ext->set_stt_crd1(stt_crd1);

    // If RA exists, set the ra from the value of RA.
    // Otherwise, set the ra to the value of STT_CRD1
    const string hms = !tempstr.empty() ? tempstr : stt_crd1;

    psrfits_read_key (read_fptr, "DEC", &tempstr, dfault, verbose > 2);

    string stt_crd2 = "";
    psrfits_read_key (read_fptr, "STT_CRD2", &stt_crd2, dfault, verbose > 2);
    hdr_ext->set_stt_crd2(stt_crd2);

    // If DEC exists, set the dec from the value of DEC.
    // Otherwise, set the dec to the value of STT_CRD2
    const string dec = !tempstr.empty() ? tempstr : stt_crd2;
    coord.setHMSDMS (hms.c_str(), dec.c_str());

    psrfits_read_key (read_fptr, "STP_CRD1", &tempstr, dfault, verbose > 2);
    if (!tempstr.empty())
      hdr_ext->set_stp_crd1(tempstr);

    psrfits_read_key (read_fptr, "STP_CRD2", &tempstr, dfault, verbose > 2);
    if (!tempstr.empty())
      hdr_ext->set_stp_crd2(tempstr);
  }
  else if (hdr_ext->coordmode == "GAL")
  {
    double dfault = 0.0;
    double co_ord1, co_ord2;

    psrfits_read_key (read_fptr, "STT_CRD1", &co_ord1, dfault, verbose > 2);
    psrfits_read_key (read_fptr, "STT_CRD2", &co_ord2, dfault, verbose > 2);
    AnglePair temp;
    temp.setDegrees(co_ord1,co_ord2);
    coord.setGalactic(temp);
  }
  else if (verbose > 2)
    cerr << "FITSArchive::load_header WARNING COORD_MD="
	 << hdr_ext->coordmode << " not implemented" << endl;
  
  set_coordinates (coord);
  
  if (get_type() != Signal::Pulsar && get_type() != Signal::Unknown)
    load_CalInfoExtension (read_fptr);

  // Track mode

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading track mode" << endl;

  dfault = hdr_ext->trk_mode;
  psrfits_read_key (read_fptr, "TRK_MODE", &tempstr, dfault, verbose > 2);
  hdr_ext->trk_mode = tempstr;

  //
  // Since PSRFITS version 2.8, the UTC date and time are stored in one string
  //

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading observation date" << endl;

  dfault = "pre version 2.8";
  psrfits_read_key (read_fptr, "DATE-OBS", &tempstr, dfault, verbose > 2);

  if ((tempstr == dfault) || (tempstr.empty()))
  {
    //
    // Before version 2.8, the UTC date and time were stored separately
    //

    // Read the start UT date

    if (verbose > 2)
      cerr << "FITSArchive::load_header reading start date" << endl;

    dfault = hdr_ext->stt_date;
    psrfits_read_key (read_fptr, "STT_DATE", &tempstr, dfault, verbose > 2);
    hdr_ext->stt_date = tempstr;
    
    // Read the start UT

    if (verbose > 2)
      cerr << "FITSArchive::load_header reading start UT" << endl;
    
    dfault = hdr_ext->stt_time;
    psrfits_read_key (read_fptr, "STT_TIME", &tempstr, dfault, verbose > 2);
    
    // strip off any fractional seconds, if present
    size_t decimal = tempstr.find('.');
    if (decimal != string::npos)
      tempstr = tempstr.substr (0, decimal);

    hdr_ext->stt_time = tempstr;
  }
  else
  {
    //
    // Since version 2.8, the UTC date and time are stored as one string
    //

    //
    // Separate the date and time at the expected point in the string
    //
    const unsigned date_length = strlen ("YYYY-MM-DD");
    if (tempstr.length() >= date_length)
      hdr_ext->stt_date = tempstr.substr(0,date_length);
    if (tempstr.length() > date_length)
      hdr_ext->stt_time = tempstr.substr(date_length+1);

    if (verbose > 2)
      cerr << "FITSArchive::load_header DATE-0BS parsed into\n"
	" date='" << hdr_ext->stt_date << "'\n"
	" time='" << hdr_ext->stt_time << "'\n";
  }
  
  // Read the bpa
   
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading BPA" << endl;
    
  dfault = "0.0";
  psrfits_read_key( read_fptr, "BPA", &tempstr, dfault, verbose > 2 );
  if( tempstr == "*" )
    hdr_ext->set_bpa( 0 );
  else
    hdr_ext->set_bpa( fromstring<double>(tempstr) );
    
    // Read the bmaj
    
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading BMAJ" << endl;
    
  dfault = "0.0";
  psrfits_read_key( read_fptr, "BMAJ", &tempstr, dfault, verbose > 2 );
  if( tempstr == "*" )
    hdr_ext->set_bpa( 0 );
  else
    hdr_ext->set_bmaj( fromstring<double>(tempstr) );
    
    // Read the bmin
    
  if(verbose > 2 )
    cerr << "FITSArchive::load_header reading BMIN" << endl;
    
  dfault = "0.0";
  psrfits_read_key( read_fptr, "BMIN", &tempstr, dfault, verbose > 2 );
  if( tempstr == "*" )
    hdr_ext->set_bpa( 0 );
  else
    hdr_ext->set_bmin( fromstring<double>(tempstr) );

  // /////////////////////////////////////////////////////////////////
  
  // Read start MJD  

  long day;
  long sec;
  double frac;
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header reading MJDs" << endl;
  
  psrfits_read_key (read_fptr, "STT_IMJD", &day, (long)0, verbose > 2);
  psrfits_read_key (read_fptr, "STT_SMJD", &sec, (long)0, verbose > 2);
  psrfits_read_key (read_fptr, "STT_OFFS", &frac, 0.0, verbose > 2);
  
  hdr_ext->set_start_time( MJD ((int)day, (int)sec, frac) );

  if (verbose > 2)
    cerr << "FITSArchive::load_header"
      " MJD=" << hdr_ext->get_start_time().printall() << endl;

  // Read the start LST (in seconds)

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading start LST" << endl;

  psrfits_read_key (read_fptr, "STT_LST", &(hdr_ext->stt_lst), 0.0, verbose > 2);

  // Read the IBEAM value (for multibeam data)

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading IBEAM" << endl;

  // If the keyword does not exist, set ibeam value to a blank string

  dfault = "";
  psrfits_read_key (read_fptr, "IBEAM", &tempstr, dfault, verbose > 2);
  hdr_ext->ibeam = tempstr;

  if (verbose > 2)
    cerr << "FITSArchive::load_header IBEAM='" << hdr_ext->ibeam << "'" << endl;

  // Read the PNT_ID (for multibeam data)

  if (verbose > 2)
    cerr << "FITSArchive::load_header reading PNT_ID" << endl;

  // If PNT_ID does not exist, set pnt_id to ""

  string default_pnt_id_value  = "";
  psrfits_read_key (read_fptr, "PNT_ID", &(hdr_ext->pnt_id), default_pnt_id_value,
      verbose > 2);

  if (verbose > 2)
    cerr << "FITSArchive::load_header PNT_ID='" << hdr_ext->pnt_id << "'" << endl;



  // ////////////////////////////////////////////////////////////////
  
  // Finished with primary header information   
  
  // ////////////////////////////////////////////////////////////////
  
  if (verbose > 2)
    cerr << "FITSArchive::load_header finished with primary HDU" << endl;
  
  // Load the processing history
  load_ProcHistory (read_fptr);

  // Load the observation description
  load_ObsDescription (read_fptr);

  // Load the digitiser statistics
  load_DigitiserStatistics (read_fptr);
  
  // Load the digitiser counts
  load_DigitiserCounts(read_fptr );
  
  // Load the original bandpass data
  load_Passband (read_fptr);

  // Load the coherent dedispersion extension
  load_CoherentDedispersion (read_fptr);

  // Load the flux calibrator extension
  load_FluxCalibratorExtension (read_fptr);

  // Load the calibrator stokes parameters
  load_CalibratorStokes (read_fptr);

  // Load the calibration model description
  load_PolnCalibratorExtension (read_fptr);
  
  // Load the calibration interpolator
  load_CalibrationInterpolatorExtension (read_fptr);
  
  // Load the parameters from the SUBINT HDU
  load_FITSSUBHdrExtension( read_fptr );

  // Load the Cross Covariance Matrix Data from COV_MAT
  load_CrossCovarianceMatrix (read_fptr);

  // Load the pulsar parameters
  if (get_type() == Signal::Pulsar)
    load_Parameters (read_fptr);

  // Load the pulse phase predictor
  load_Predictor (read_fptr);
  hdr_model = model;

  if (correct_P236_reference_epoch)
    P236_reference_epoch_correction ();

  load_integration_state (read_fptr);

#if 0
  status = 0;
  
  // Finished with the file for now
   fits_close_file (read_fptr, &status);
  
  if (status)
    throw FITSError (status, "Pulsar::FITSArchive::load_header",
		     "fits_close_file");
#endif

  loaded_from_fits = true;

  if (verbose > 2)
    cerr << "FITSArchive::load_header exit" << endl;
  
}
catch (Error& error)
{
  throw error += "FITSArchive::load_header";
}

//
// End of load_header function
// /////////////////////////////////////////////////////////////////////
// /////////////////////////////////////////////////////////////////////

// /////////////////////////////////////////////////////////////////////
// /////////////////////////////////////////////////////////////////////
//! An unload function to write FITSArchive data to a FITS file on disk.

// To create a new file we need a FITS file template to provide the format

#if HAVE_PTHREAD
static ThreadContext* context = new ThreadContext;
#else
static ThreadContext* context = 0;
#endif

string Pulsar::FITSArchive::get_template_name ()
{
  ThreadContext::Lock lock (context);

  static char* template_defn = getenv ("PSRFITSDEFN");

  if (template_defn)
  {
    if (verbose > 2)
      cerr << "FITSArchive::get_template_name using\n"
	"  PSRFITSDEFN=" << template_defn << endl;

    return template_defn;
  }

  // load the definition from the configured installation directory
  string template_name = Pulsar::Config::get_runtime() + "/psrheader.fits";

  if (verbose > 2)
    cerr << "FITSArchive::get_template_name using\n"
      "  installed=" << template_name << endl;

  return template_name;
}

void Pulsar::FITSArchive::unload_file (const char* filename) const try
{
  if (!filename)
    throw Error (InvalidParam, string(), "filename unspecified");

  fits_version_check( verbose > 2 );

  if (verbose > 2)
    cerr << "FITSArchive::unload_file (" << filename << ")" << endl
	 << "  with " << get_nextension() << " Extensions" << endl;

  fitsfile* fptr = 0;

  // status returned by FITSIO routines
  int status = 0;

  // To create a new file we need a FITS file template to provide the format

  string template_name = get_template_name();
  
  if (verbose > 2)
    cerr << "FITSArchive::unload_file creating file " 
	 << filename << endl << "   using template " << template_name << endl;

  string clobbername = "!";
  clobbername += filename;


#ifdef FITS_CREATE_TEMPLATE_WORKS

  fits_create_template (&fptr, clobbername.c_str(),
			template_name.c_str(), &status);
  if (status)
    throw FITSError (status, "FITSArchive::unload_file",
		     "fits_create_template "
		     "(" + clobbername + ", " + template_name + ")");

#else

  /* the following three commands:

     fits_create_file
     fits_execute_template
     fits_movabs_hdu

     are equivalent to:

     fits_create_template

     except that they do not cause segmentation faults or processes to hang
  */

  if (verbose > 2)
    cerr << "FITSArchive::unload_file call fits_create_file "
      "(" << clobbername << ")" << endl;

  fits_create_file (&fptr, clobbername.c_str(), &status);
  if (status)
    throw FITSError (status, "FITSArchive::unload_file",
		     "fits_create_file (%s)", clobbername.c_str());

  if (verbose > 2)
    cerr << "FITSArchive::unload_file call fits_execute_template "
      "(" << template_name << ")" << endl;

  fits_execute_template (fptr, (char*)template_name.c_str(), &status);
  if (status)
    throw FITSError (status, "FITSArchive::unload_file",
		     "fits_execute_template (%s)", template_name.c_str());

  fits_movabs_hdu (fptr, 1, 0, &status);
  if (status)
    throw FITSError (status, "FITSArchive::unload_file",
		     "fits_moveabs_hdu");

#endif

  if (verbose > 2)
    cerr << "FITSArchive::unload_file TELESCOP=" << get_telescope() << endl;

  psrfits_update_key (fptr, "TELESCOP", get_telescope());

  if (verbose > 2)
    cerr << "FITSArchive::unload_file SRC_NAME=" << get_source() << endl;

  psrfits_update_key (fptr, "SRC_NAME", get_source());
    
  AnglePair radec = get_coordinates().getRaDec();
  string RA = radec.angle1.getHMS();
  string DEC = radec.angle2.getDMS();

  string coord1, coord2;
  const FITSHdrExtension* hdr_ext = get<FITSHdrExtension>();

  Reference::To<FITSHdrExtension> hdr;
  
  if (!hdr_ext)
  {
    if (verbose > 2)
      cerr << "Pulsar::FITSArchive::unload_file"
	" creating temporary FITSHdrExtension" << endl;

    hdr = new FITSHdrExtension;

    // If no FITSHdrExtension is present, assume J2000 Equatorial
    hdr->set_coordmode( "J2000" );
    hdr->set_equinox( 2000.0 );
    hdr->set_stt_crd1( RA );
    hdr->set_stt_crd2( DEC );

    // If no FITSHdrExtension is present, assume current parameters
    hdr->set_obsbw( get_bandwidth() );
    hdr->set_obsfreq( get_centre_frequency() );
    hdr->set_obsnchan( get_nchan() );
    
    hdr_ext = hdr;

    // take the epoch of the first Integration with a duration
    for (unsigned jsubint = 0; jsubint < get_nsubint(); jsubint++)
        if (get_Integration(jsubint)->get_duration() != 0.0)
        {
            reference_epoch = get_Integration(jsubint)->get_epoch();
            if (verbose > 2)
                cerr << "FITSArchive::unload_file subint=" << jsubint
                    << " reference epoch=" << reference_epoch.printdays (13)
                    << endl;
            break;
        }


  } else {
      reference_epoch = hdr_ext->get_start_time();
      if (verbose > 2)
          cerr << "FITSArchive::unload_file FITSHdr reference epoch="
              << reference_epoch.printdays (13) << endl;
  }


  if (verbose > 2)
    cerr << "Pulsar::FITSArchive::unload_file FITSHdrExtension" << endl;
    
  unload (fptr, hdr_ext);

  // Virtual Observatory compatibility (redundant)
  psrfits_update_key (fptr, "RA", RA);
  psrfits_update_key (fptr, "DEC", DEC); 

  string obs_mode;
  
  if (get_type() == Signal::Pulsar)
    obs_mode = "PSR";
  else if (get_type() == Signal::PolnCal)
    obs_mode = "CAL";
  else if (get_type() == Signal::FluxCalOn)
    obs_mode = "FON";
  else if (get_type() == Signal::FluxCalOff)
    obs_mode = "FOF";
  else if (get_type() == Signal::Calibrator)
    obs_mode = "PCM";
  else
  {
    if (search_mode)
      obs_mode = "SEARCH";
    else
      obs_mode = "UNKNOWN";
  }
  
  psrfits_update_key (fptr, "OBS_MODE", obs_mode);

  {
    const ObsExtension* ext = get<ObsExtension>();
    if (ext) 
      unload (fptr, ext);
  }

  {
    const WidebandCorrelator* ext = get<WidebandCorrelator>();
    if (ext)
    {
      if (verbose > 2)
	cerr << "FITSArchive::unload WidebandCorrelator extension" << endl;
      unload (fptr, ext);
    }
  }

  {
    const Backend* backend = get<Backend>();

    if (backend)
    {
      if (verbose > 2)
	cerr << "FITSArchive::unload " << backend->get_extension_name()
	     << " BACKEND=" << backend->get_name() 
	     << " BE_DCC=" << backend->get_downconversion_corrected()
	     << " BE_PHASE=" << backend->get_argument() 
	     << " BE_DELAY=" << backend->get_delay() << endl;
      
      psrfits_update_key (fptr, "BACKEND",  backend->get_name());
      
      int be_dcc = backend->get_downconversion_corrected();
      psrfits_update_key (fptr, "BE_DCC", be_dcc);
 
      int be_phase = backend->get_argument();
      psrfits_update_key (fptr, "BE_PHASE", be_phase);
      
      double be_delay = backend->get_delay();
      psrfits_update_key (fptr, "BE_DELAY", be_delay );
    }
  }

  {
    const ITRFExtension* ext = get<ITRFExtension>();
    if (ext) 
      unload (fptr, ext);
  }

  {
    const Receiver* ext = get<Receiver>();
    if (ext) 
      unload (fptr, ext);
  }

  {
    const CalInfoExtension* ext = get<CalInfoExtension>();
    if (ext) 
      unload (fptr, ext);
  }
  

  if (reference_epoch == MJD::zero && verbose > 1)
    cerr << "FITSArchive::unload_file WARNING reference epoch == 0" << endl;
    
  long day = reference_epoch.intday();
  long sec = reference_epoch.get_secs();
  double frac = reference_epoch.get_fracsec();

  // do not return comments in fits_read_key
  char* comment = 0;

  fits_update_key (fptr, TLONG, "STT_IMJD", &day, comment, &status);
  fits_update_key (fptr, TLONG, "STT_SMJD", &sec, comment, &status);
  fits_update_key (fptr, TDOUBLE, "STT_OFFS", &frac, comment, &status);

  if (status)
    throw FITSError (status, "FITSArchive::unload_file",
		     "fits_update_key STT_MJD");

  if (verbose > 2)
    cerr << "FITSArchive::unload_file finished in primary header" << endl;
  
  // Finished with primary header information
  
  // /////////////////////////////////////////////////////////////////
  //  
  // For optimal CFITSIO performance, HDUs should be unloaded in the
  // order that they appear in the psrheader.fits template
  //
  // /////////////////////////////////////////////////////////////////
  
  const_cast<FITSArchive*>(this)->update_history();
  
  const ProcHistory* history = get<ProcHistory>();
  
  if (!history)
    throw Error (InvalidState, "Pulsar::FITSArchive::unload",
		 "no ProcHistory");

  unload (fptr, history);
  
  if (verbose > 2)
    cerr << "FITSArchive::unload_file finished with processing history" 
	 << endl;

  unload <ObsDescription> (fptr, "OBSDESCR");

  if (ephemeris)
    unload_Parameters (fptr);    
  else
    delete_hdu (fptr, "PSRPARAM");

  unload_Predictor (fptr);

  // /////////////////////////////////////////////////////////////////
  //  
  // For optimal CFITSIO performance, HDUs should be unloaded in the
  // order that they appear in the psrheader.fits template
  //
  // /////////////////////////////////////////////////////////////////
  

  // Unload some of the other HDU's
  
  unload <CoherentDedispersion> (fptr, "COHDDISP");

  unload <Passband> (fptr, "BANDPASS");

  unload <FluxCalibratorExtension> (fptr, "FLUX_CAL");

  unload <CalibratorStokes> (fptr, "CAL_POLN");

  unload <PolnCalibratorExtension> (fptr, "FEEDPAR");

  unload <CalibrationInterpolatorExtension> (fptr, "PCMINTER");

  unload <CrossCovarianceMatrix> (fptr, "COV_MAT");

  // Write the Spectral Kurtosis integrations to file

  const SpectralKurtosis * ske = 0;
  if (nsubint > 0)
    ske = get_Integration (0)->get<SpectralKurtosis>();

  if (ske)
    unload_sk_integrations (fptr);
  else
    delete_hdu (fptr, "SPECKURT");

  // /////////////////////////////////////////////////////////////////
  //  
  // For optimal CFITSIO performance, HDUs should be unloaded in the
  // order that they appear in the psrheader.fits template
  //
  // /////////////////////////////////////////////////////////////////
  

  // Unload extra subint parameters.

  const FITSSUBHdrExtension *sub_hdr = get<FITSSUBHdrExtension>();
  if( sub_hdr )
  {
    // older version files may have SUBINT headers but not all parameters set

    if( sub_hdr->get_nsblk() == -1 && search_mode )
      const_cast<FITSSUBHdrExtension*>(sub_hdr)->set_nsblk( 1 );

    unload( fptr, sub_hdr );
  }
  
  // Now write the integrations to file

  if (nsubint > 0)
    unload_integrations (fptr);
  else
    delete_hdu (fptr, "SUBINT");

  unload <DigitiserStatistics> (fptr, "DIG_STAT");
  unload <DigitiserCounts> (fptr, "DIG_CNTS");
  
  // /////////////////////////////////////////////////////////////////
  //  
  // For optimal CFITSIO performance, HDUs should be unloaded in the
  // order that they appear in the psrheader.fits template
  //
  // /////////////////////////////////////////////////////////////////
  
  fits_close_file (fptr, &status);

  if (status)
    throw FITSError (status, "Pulsar::FITSArchive::unload_file",
		     "fits_close_file");

  if (verbose > 2)
    cerr << "FITSArchive::unload_file fits_close_file " << "(" << filename 
	 << ")" << " complete" << endl;
}
catch (Error& error)
{
  throw error += "FITSArchive::unload_file";
}


// End of unload_file function
// //////////////////////////////////////////
// //////////////////////////////////////////

template<class Ext>
void Pulsar::FITSArchive::unload (fitsfile* fptr,
				  const char* hdu_name) const try
{
  const Ext* extension = get<Ext>();
  if (extension && extension->has_data())
    unload (fptr, extension);
  else
    delete_hdu (fptr, hdu_name);
}
catch( Error& e )
{
  cerr << e << endl;
  delete_hdu (fptr, hdu_name);
}

// !retreive the offs_sub

double Pulsar::FITSArchive::get_offs_sub( unsigned int isub ) const
{
  Reference::To<const Integration> integ = get_Integration( isub );
  
  if( !integ )
    return -1;
  
  return (integ->get_epoch() - reference_epoch).in_seconds();
}

// EOF
// ///
    
    











