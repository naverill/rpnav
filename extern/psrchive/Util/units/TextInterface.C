/***************************************************************************
 *
 *   Copyright (C) 2004-2009 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "TextInterface.h"
#include "NestedTextInterface.h"
#include "ModifyRestore.h"
#include "stringtok.h"
#include "pad.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

using namespace std;

bool TextInterface::label_elements = false;

// #define _DEBUG

void TextInterface::Parser::set_delimiter (const std::string& text)
{
  delimiter = text;

  size_t found = 0;

#ifdef _DEBUG
  cerr << "TextInterface::Parser::set_delimiter text='" << text << "'" << endl;
#endif

  // transform backslash-n into newline
  while ((found = delimiter.find( "\\n" )) != string::npos)
    delimiter.replace ( found, 2, 1, '\n' );

  // transform backslash-t into tab
  while ((found = delimiter.find( "\\t" )) != string::npos)
    delimiter.replace ( found, 2, 1, '\t' );
}

string TextInterface::Parser::process (const string& command)
{
#ifdef _DEBUG
  cerr << "TextInterface::Parser::process command='" << command << "'" << endl;
#endif

  if (command == "help")
    return help ();

  string::size_type set = command.find('=');

  // if no equals sign is present, assume that command is get key
  if (set == string::npos)
  {
    string name = command;
    string value = get_name_value (name);

    string prefix;
    if (prefix_name)
      prefix = name + "=";

    return indentation + prefix + value;
  }

  // string before the equals sign
  string before = command.substr (0,set);
  // string after the equals sign
  string after = command.substr (set+1);

  // remove any white space from the key
  string param = stringtok (before, " \t");

  try
  {
    set_value (param, after);
  }
  catch (Error& error)
  {
    if (error.get_code() == HelpMessage)
      throw error;
    else
      throw Error (InvalidParam, "TextInterface::Parser::process",
		   "failed to parse '" + after + "' as '" + param + "'");
  }

  return "";
}

string TextInterface::Parser::process (const vector<string>& commands)
{
  string result;
  for (unsigned i=0; i < commands.size(); i++)
    result += process (commands[i]) + "\n";

  return result;
}

string TextInterface::Parser::help (bool default_value,
				    bool print_header,
				    const char* indent)
{
  unsigned i = 0;

  string name_label = "Attribute Name";
  string description_label = "Description";
  string value_label = (default_value) ? "Value" : "";

  unsigned max_namelen = name_label.length();
  unsigned max_descriptionlen = description_label.length();

#ifdef _DEBUG
  cerr << "TextInterface::Parser::help nvalue=" << get_nvalue() << endl;
#endif

  // find the maximum string length of the name and description
  for (i=0; i<get_nvalue(); i++)
  {
    if (get_name(i).length() > max_namelen)
      max_namelen = get_name(i).length();
    if (get_description(i).length() > max_descriptionlen)
      max_descriptionlen = get_description(i).length();
  }

  max_namelen += 3;
  max_descriptionlen += 3;

  // repeat the dash an appropriate number of times
  string sep (max_namelen + max_descriptionlen + value_label.length(), '-');

  string help_str;

  // make a nice header
  if (print_header)
  {
    if (indent)
      help_str += indent;

    help_str +=
      pad(max_namelen, name_label) + 
      pad(max_descriptionlen, description_label) + value_label + "\n" +
      sep + "\n";
  }

  for (i=0; i<get_nvalue(); i++)
  {
    if (indent)
      help_str += indent;

    help_str += 
      pad(max_namelen, get_name(i)) + 
      pad(max_descriptionlen, get_description(i)) +
      ((default_value) ? (get_value(get_name(i)) + "\n") : "\n");
  }

  return help_str;
}

static bool alphabetical_lt (const Reference::To<TextInterface::Value>& v1, 
			     const Reference::To<TextInterface::Value>& v2)
{
  return v1->get_name() < v2->get_name();
}


//! Add a new value interface
void TextInterface::Parser::add_value (Value* value)
{
#ifdef _DEBUG
  cerr << "TextInterface::Parser::add_value this=" << this << " name="
       << value->get_name() << endl;
#endif

  value->set_parent (this);
  values.push_back (value);

  if (alphabetical)
    sort (values.begin(), values.end(), alphabetical_lt);
}

//! Remove the named value interface
void TextInterface::Parser::remove (const string& name)
{
  delete find (name);
  clean_invalid ();
}

void TextInterface::Parser::clean_invalid () 
{
  unsigned i=0; 
  while ( i < values.size() )
    if (!values[i])
      values.erase( values.begin() + i );
    else 
      i++;
}

TextInterface::Parser::Parser ()
{
  alphabetical = false;
  import_filter = false;
  prefix_name = true;
  delimiter = ",";
}

//! Get the value of the value
string TextInterface::Parser::get_value (const string& name) const
{
  string temp = name;
  return get_name_value (temp);
}

//! Get the value of the value
string TextInterface::Parser::get_name_value (string& name) const
{
  // an optional precision may be specified
  string::size_type dot = name.find('%');
  string modifiers;

  if (dot != string::npos)
  {
    // strip off the modifiers
    modifiers = name.substr(dot+1);
    name = name.substr(0,dot);
  }
   
  Value* value = find(name);

  if (modifiers.length() > 0)
    value->set_modifiers (modifiers);

  std::string result = value->get_value();

  value->reset_modifiers();

  return result;
}

void TextInterface::Value::set_modifiers (const std::string& modifiers) const
{
#ifdef _DEBUG
  cerr << "TextInterface::Value::set_modifiers " << modifiers << endl;
#endif
  
  tostring_precision = fromstring<unsigned> (modifiers);

#ifdef _DEBUG
  cerr << "TextInterface::Value::set_modifiers precision="
       << tostring_precision << endl;
#endif
}

void TextInterface::Value::reset_modifiers () const
{
  tostring_precision = 0;
}

//! Set the value of the value
void TextInterface::Parser::set_value (const string& name, const string& value)
{
  find(name)->set_value(value);
}

//! Get the number of values
unsigned TextInterface::Parser::get_nvalue () const
{
  return values.size(); 
}

//! Get the name of the value
string TextInterface::Parser::get_name (unsigned i) const 
{
  return values[i]->get_name();
}

//! Get the name of the value
string TextInterface::Parser::get_value (unsigned i) const 
{
  const_cast<Parser*>(this)->setup( values[i] );
  return values[i]->get_value();
}

//! Get the description of the value
string TextInterface::Parser::get_description (unsigned i) const
{
  return values[i]->get_description();
}

//! Return true if the named value is found
bool TextInterface::Parser::found (const string& name) const
{
  return find (name, false);
}

//! Return true if prefix:name is found
bool TextInterface::Parser::found (const string& p, const string& name) const
{
  if (p.length())
    return found (p + ":" + name);
  else
    return found (name);
}


//! Return a pointer to the named class value interface
TextInterface::Value* 
TextInterface::Parser::find (const string& name, bool throw_exception) const
{
#ifdef _DEBUG
  cerr << "TextInterface::Parser::find (" << name << ")"
    " size=" << values.size() << endl;
#endif

  string key = name;

  if (aliases)
    key = aliases->substitute (key);

  for (unsigned i=0; i<values.size(); i++)
  {
    const_cast<Parser*>(this)->setup( values[i] );
    if (values[i]->matches (key))
    {
#ifdef _DEBUG
      cerr << "TextInterface::Parser::find value[" << i << "]"
	"=" << values[i]->get_name() << " matches" << endl;
#endif
      return values[i];
    }
  }

  if (throw_exception)
    throw Error (InvalidParam, "TextInterface::Parser::find",
		 "no value named '%s'", name.c_str());

  return 0;
}

//! Insert the interface into self
void TextInterface::Parser::insert (Parser* other)
{
  if (!other)
    return;

  for (unsigned i=0; i < other->values.size(); i++)
    if (!import_filter || !find(other->values[i]->get_name(),false))
    {
      other->setup( other->values[i] );
      add_value( other->values[i] );
    }
}

//! Import a nested interface
void TextInterface::Parser::insert (const string& prefix, Parser* other)
{
  if (!other)
    return;

  for (unsigned i=0; i < other->values.size(); i++)
  {
    Value* value = other->values[i];

#ifdef _DEBUG
    cerr << "TextInterface::Parser::insert " << prefix + ":" + value->get_name()
	 << endl;
#endif

    if ( !import_filter ||
	 !find( prefix + ":" + value->get_name(), false ) )
    {
      other->setup( value );
      add_value( new NestedValue(prefix, value) );
    }
  }
}

//! Clear all nested interfaces
void TextInterface::Parser::clean ()
{
  for (unsigned i=0; i < values.size(); )
  {
    NestedValue* nested = dynamic_kast<NestedValue>( values[i] );
    if (nested)
      values.erase( values.begin() + i );
    else
      i++;
  }
}


bool TextInterface::matches (const string& name,
			     const string& prefix,
			     const Value* value)
{
#ifdef _DEBUG
  cerr << "TextInterface::matches prefix=" << prefix 
       << " name=" << name;
#endif

  string::size_type length = prefix.length();

  if (length)
  {
    if ( name.length() <= length || name[length] != ':' )
    {
#ifdef _DEBUG
      cerr << " false (no colon)" << endl;
#endif
      return false;
    }

    if ( strncmp (name.c_str(), prefix.c_str(), length) != 0 )
    {
#ifdef _DEBUG
      cerr << " false (prefix mismatch)" << endl;
#endif
      return false;
    }

    length ++;
  }

  string remainder = name.substr (length);

#ifdef _DEBUG
  cerr << "TextInterface::matches nested name="
       << value->get_name() << " remain=" << remainder << endl;
#endif

  bool result = value->matches (remainder);

#ifdef _DEBUG
  cerr << "TextInterface::matches result=" << result << endl;
#endif
  return result;
}


/*! Parses text into key, range, and remainder
  \retval true if key matches name
*/
bool TextInterface::match (const string& name, const string& text,
			   string* range, string* remainder)
{
#ifdef _DEBUG
  cerr << "TextInterface::match name=" << name << " text=" << text;
#endif

  string::size_type length = name.length();

  if ( length && strncmp (text.c_str(), name.c_str(), length) != 0 )
  {
#ifdef _DEBUG
    cerr << " false" << endl;
#endif
    return false;
  }

  string::size_type end = text.find (':', length);
  if (end == string::npos)
  {
    if (remainder)
    {
#ifdef _DEBUG
      cerr << " false" << endl; 
#endif
      return false;
    }
    else
      end = text.length();
  }

#ifdef _DEBUG
  cerr << " maybe" << endl;
#endif

  // the range is everything between the end of the variable name and the colon
  *range = text.substr (length, end-length);

#ifdef _DEBUG
  cerr << "TextInterface::match range=" << *range << endl;
#endif

  // the remainder is everything following the colon
  if (remainder)
  {
    *remainder = text.substr (end+1);
#ifdef _DEBUG
    cerr << "TextInterface::match remainder=" << *remainder << endl;
#endif
  }

  // a map may have no name
  if (!length)
    return true;

  // the range may be a wildcard
  if ((*range) == "*" || (*range) == "?")
    return true;

  length = range->length();

  // a range may be empty ...
  if (!length)
    return true;

  // .. or must be enclosed in square brackets
  if ((*range)[0] == '[' && (*range)[length-1] == ']')
    return true;

#ifdef _DEBUG
  cerr << "FALSE" << endl;
#endif

  return false;
}

