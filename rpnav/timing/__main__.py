import argparse
import io
import json

import astropy.units as u
from astropy.coordinates import EarthLocation
from pint.observatory.topo_obs import load_observatories

from rpnav.timing.observer import Observer
from rpnav.timing.timing import fit_residuals
from rpnav.timing.visualise import plot_residuals

parser = argparse.ArgumentParser()
parser.add_argument("-t", dest="timfile", type=str, help="Path to .tim file", required=True)
parser.add_argument("-p", dest="parfile", type=str, help="Path to .par file", required=True)
parser.add_argument(
    "--est",
    dest="est",
    type=float,
    nargs="+",
    help="Initial position estimate for observer in WGS84",
    required=True,
)
parser.add_argument(
    "--err", dest="err", type=float, nargs="+", help="Initial position error for observer in WGS84"
)
args = parser.parse_args()

if len(args.est) < 2 or len(args.est) > 3:
    raise parser.error("Position must contain estimates for x and y axes")

msfd = Observer(
    name="msfd_actual",
    itoa_code="MSFD",
    location=EarthLocation.from_geodetic(lat=-33.77290643916046, lon=151.0976937264337),
    origin="Actual location of CSIRO Marsfield site",
)
msfd_est = Observer(
    name="msfd_estimated",
    itoa_code="MSFD_EST",
    location=EarthLocation.from_geodetic(lat=-33.77290643916046, lon=151.0976937264337),
    origin="Estimated location of CSIRO Marsfield site"
    # overwrite=True
)
obs = msfd.to_json()
obs.update(msfd_est.to_json())

load_observatories(io.StringIO(json.dumps(obs)), overwrite=True)
fitter = fit_residuals(args.parfile, args.timfile, args.est, args.err)
fig = plot_residuals(
    fitter.resids.time_resids.to_value(u.us).astype(float),
    fitter.toas.get_mjds().value.astype(float),
    fitter.toas.get_errors().to_value(u.us).astype(float),
)
fig.write_image("test.png")
