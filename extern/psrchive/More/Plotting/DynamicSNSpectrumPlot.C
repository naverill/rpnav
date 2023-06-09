/***************************************************************************
 *
 *   Copyright (C) 2009 by Paul Demorest
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/


#include "Pulsar/DynamicSNSpectrumPlot.h"
#include "Pulsar/DynamicSpectrumPlot.h"
#include <Pulsar/Archive.h>
#include <Pulsar/Profile.h>
#include <Pulsar/StandardSNR.h>

Pulsar::DynamicSNSpectrumPlot::DynamicSNSpectrumPlot()
{
}

Pulsar::DynamicSNSpectrumPlot::~DynamicSNSpectrumPlot()
{
}

TextInterface::Parser* Pulsar::DynamicSNSpectrumPlot::get_interface ()
{
  return new Interface (this);
}

// Calculate SNR to fill in to plot array
void Pulsar::DynamicSNSpectrumPlot::get_plot_array( const Archive *data, 
    float *array )
{

  StandardSNR standard_snr;
  Reference::To<Archive> data_std = data->total();
  standard_snr.set_standard( data_std->get_Profile(0,0,0) );

  std::pair<unsigned,unsigned> srange = get_subint_range (data);
  std::pair<unsigned,unsigned> crange = get_chan_range (data);

  int ii = 0;

  for( int ichan = crange.first; ichan < crange.second; ichan++) 
  {
    for( int isub = srange.first; isub < srange.second; isub++ )
    {
      Reference::To<const Profile> prof = 
        get_Profile ( data, isub, ipol, ichan );

      float snr;

      if (prof->get_weight()==0.0)
        snr = 0.0;
      else
        snr = standard_snr.get_snr(prof);

      array[ii] = snr;
      ii++;

    }
  }
}

