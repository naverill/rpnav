/***************************************************************************
 *
 *   Copyright (C) 2006-2008 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "psrfitsio.h"

#include <stdarg.h>

#include <memory>

using namespace std;

bool psrfits_verbose = false;

static void update_tdim (fitsfile* ffptr, int column, unsigned ndim, ...)
{
  vector<unsigned> dims (ndim);

  va_list args;
  va_start(args, ndim);

  for (unsigned i=0; i < ndim; i++)
    dims[i] = va_arg ( args, unsigned );

  va_end(args);

  psrfits_update_tdim (ffptr, column, dims);
}


void psrfits_update_tdim (fitsfile* ffptr, int column, unsigned dim)
{
  update_tdim (ffptr, column, 1, dim);
}

void psrfits_update_tdim (fitsfile* ffptr, int column,
			  unsigned dim1, unsigned dim2)
{
  update_tdim (ffptr, column, 2, dim1, dim2);
}

void psrfits_update_tdim (fitsfile* ffptr, int column,
			  unsigned dim1, unsigned dim2, unsigned dim3)
{
  update_tdim (ffptr, column, 3, dim1, dim2, dim3);
}

void psrfits_update_tdim (fitsfile* ffptr, int column,
			  unsigned dim1, unsigned dim2,
			  unsigned dim3, unsigned dim4)
{
  update_tdim (ffptr, column, 4, dim1, dim2, dim3, dim4);
}

/*

  Wvs - 6 June 2008

  IMPORTANT NOTE:

  fits_write_tdim does not look for any existing TDIMn entry and
  update its value.  Rather, it leaves the uninitialized value and
  adds a new one.  This function works around this problem by creating
  an appropriate key/value pair and calling fits_update_key.

  Many thanks to Paul Demorest for this insight.

*/
void psrfits_update_tdim (fitsfile* ffptr, int column,
			  const vector<unsigned>& dims)
{
  string result = "(";

  for (unsigned i=0; i < dims.size(); i++)
  {
    result += tostring(dims[i]);
    if (i < dims.size()-1)
      result += ",";
  }

  result += ")";

  psrfits_update_key (ffptr, "TDIM", column, result);
}

void psrfits_update_key (fitsfile* fptr,
			 const char* name,
			 int column,
			 const std::string& data,
			 const char* comment)
{
  char keyword [FLEN_KEYWORD+1];
  int status = 0;
  fits_make_keyn (const_cast<char*>(name), column, keyword, &status);
  if (status)
    throw FITSError (status, "psrfits_update_key", "fits_make_keyn");

  psrfits_update_key (fptr, keyword, data, comment);
}

void psrfits_read_key (fitsfile* fptr,
                       const char* name,
                       int column,
                       std::string& val,
                       std::string& comnt)
{
  // double the length just to be sure to avoid stack overflow
  char keyword [FLEN_KEYWORD*2];
  char value   [FLEN_VALUE*2];
  char comment [FLEN_COMMENT*2];

  int status = 0;
  fits_make_keyn (name, column, keyword, &status);
  if (status)
    throw FITSError (status, "psrfits_read_key", "fits_make_keyn");

  fits_read_key (fptr, TSTRING, keyword, value, comment, &status);
  if (status)
    throw FITSError (status, "psrfits_read_key", "fits_read_key");

  if (psrfits_verbose)
    cerr << "psrfits_read_key colnum=" << column 
         << " value='" << value << "'"
         << " comment='" << comment << "'" << endl;

  val = value;
  comnt = comment;
}

void psrfits_update_key (fitsfile* fptr, const char* name,
			 const std::string& data,
			 const char* comment)
{
  psrfits_update_key (fptr, name, data.c_str(), comment);
}

void psrfits_update_key (fitsfile* fptr, const char* name, const char* text,
			 const char* comment)
{
  // status
  int status = 0;
  
  fits_update_key (fptr, TSTRING,
		   const_cast<char*>(name), 
		   const_cast<char*>(text),
		   const_cast<char*>(comment),
		   &status);
  
  if (status)
    throw FITSError (status, "psrfits_update_key", name);
}

void psrfits_read_key_work (fitsfile* fptr, const char* name, string* data,
			    int* status)
{
  // no comment
  char* comment = 0;

  char temp [FLEN_VALUE];

  fits_read_key (fptr, TSTRING, const_cast<char*>(name), temp, 
		 comment, status);

  if (*status == 0)
    *data = temp;
}

//! Specialization for string
void psrfits_read_col_work( fitsfile *fptr, const char *name,
			    string *data,
			    int row, string& null, int* status)
{
  int colnum = 0;
  fits_get_colnum (fptr, CASEINSEN, const_cast<char*>(name), &colnum, status);
 
  int typecode = 0;
  long repeat = 0;
  long width = 0;
  
  fits_get_coltype (fptr, colnum, &typecode, &repeat, &width, status);
  if (*status != 0)
    return; 

  if (typecode != TSTRING)
    throw Error (InvalidState, "psrfits_read_col_work",
		 "%s typecode != TSTRING", name);

  char* nullstr = const_cast<char*>( null.c_str() );
  char* tempstr = new char[repeat + 1];

  int anynul = 0;
  fits_read_col( fptr, TSTRING,
		 colnum, row,
		 1, 1, &nullstr, &tempstr, 
		 &anynul, status );

  if (*status == 0)
    *data = tempstr;

  delete [] tempstr;
}

void* FITS_void_ptr (const string& txt)
{
  // not thread-safe
  static char* ptr;
  ptr = const_cast<char*> (txt.c_str());
  return &ptr;
}


void psrfits_init_hdu (fitsfile *fptr, const char *name, unsigned nrow)
{
  psrfits_move_hdu (fptr, name);
  psrfits_set_rows (fptr, nrow);
}

/**
 * psrfits_move_hdu           Simple wrapper function for fits_movnam_hdu, assumes defaults for table type and version.
 *
 * @param fptr                The file to find the hdu in
 * @param hdu_name            The name of the hdu we want
 * @param table_type          The type of table, ours are always BINARY_TBL
 * @param version             Some version number ???, we always use 0
 **/

bool psrfits_move_hdu (fitsfile *fptr, const char *hdu_name, bool optional,
		       int table_type, int version)
{
  int status = 0;

  fits_movnam_hdu (fptr, table_type, const_cast<char*>(hdu_name), 
		   version, &status);

  if ( status == BAD_HDU_NUM && optional )
    return false;

  if ( status != 0 )
    throw FITSError( status, "psrfits_move_hdu",
		     "fits_movnam_hdu (%s)", hdu_name );

  return true;
}

void psrfits_clean_rows (fitsfile* ffptr)
{
  long rows = 0;
  int status = 0;

  fits_get_num_rows (ffptr, &rows, &status);

  if (status)
    throw FITSError (status, "psrfits_clean_rows", "fits_get_num_rows");

  if (!rows)
    return;

  fits_delete_rows (ffptr, 1, rows, &status);

  if (status)
    throw FITSError (status, "psrfits_clean_rows", "fits_delete_rows");
}

void psrfits_set_rows (fitsfile* ffptr, unsigned nrow)
{
  psrfits_clean_rows (ffptr);

  int status = 0;

  fits_insert_rows (ffptr, 0, nrow, &status);

  if (status != 0)
    throw FITSError (status, "psrfits_set_rows", "fits_insert_rows");
}

void psrfits_insert_row (fitsfile* fptr)
{
  int status = 0;

  fits_insert_rows (fptr, 0, 1, &status);

  if (status != 0)
    throw FITSError (status, "psrfits_insert_row", "fits_insert_rows");
}

void psrfits_delete_col (fitsfile* fptr, const char* name)
{
  int status = 0;
  int colnum = 0;

  fits_get_colnum (fptr, CASEINSEN, const_cast<char*>(name), &colnum, &status);

  fits_delete_col (fptr, colnum, &status);

  if (status != 0)
    throw FITSError (status, "psrfits_delete_col", "fits_delete_col %s", name);
}
