/***************************************************************************
 *
 *   Copyright (C) 2002-2014 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "Pulsar/psrchive.h"
#include "Pulsar/Interpreter.h"
#include "Pulsar/Archive.h"

#include "Pulsar/Application.h"
#include "Pulsar/UnloadOptions.h"

#include "strutil.h"

using namespace std;

//
//! Command language interpreter interface
/*! 
  Can be used as either an interactive shell or a shell script command
  processor.
 */
class psrsh : public Pulsar::Application
{
public:

  //! Default constructor
  psrsh ();

  //! Saving is optional
  bool must_save () { return false; }

  //! The main loop
  void run ();

  //! Process the given archive
  void process (Pulsar::Archive*);

protected:

  //! Add command line options
  void add_options (CommandLine::Menu&);

  //! The result of the process method
  Reference::To<Pulsar::Archive> processed;

  //! Return pointer to result of process method
  Pulsar::Archive* result () { return processed; }

  //! The interpreter used to parse commands
  Reference::To<Pulsar::Interpreter> interpreter;

  //! Convenience interface to interpreter
  Pulsar::Interpreter* get_interpreter (bool refresh = false);

  //! Create a new Interpreter for each input Archive
  bool refresh_foreach_archive;

  //! Load data from filenames provided as arguments
  bool load_files;

  //! Lines read from the script
  vector<string> script_lines;

  void interpreter_help (const string& cmd);
};

int main (int argc, char** argv)
{
  psrsh program;
  return program.main (argc, argv);
}

psrsh::psrsh ()
  : Application ("psrsh", "command language interpreter")
{
  stow_script = true;
  has_manual = true;
  version = "$Id: psrsh.C,v 1.23 2011/01/12 04:13:18 straten Exp $";

  load_files = true;
  refresh_foreach_archive = true;
 
  add( new Pulsar::UnloadOptions );
}

void psrsh::add_options (CommandLine::Menu& menu)
{
  CommandLine::Argument* arg;

  arg = menu.add (this, &psrsh::interpreter_help, 'H');
  arg->set_long_name ("cmd");
  arg->set_help ("list available commands");
  arg->set_has_arg (optional_argument);

  arg = menu.add (load_files, 'n');
  arg->set_long_name ("nofiles");
  arg->set_help ("no filenames required");

  menu.set_help_footer
    ("\n"
     "If no files are specified, psrsh enters the interactive shell mode \n"
     "\n"
     "Otherwise, psrsh enters the shell script command processor mode: \n"
     "the first file must be the script, and all subsequent archive files \n"
     "will be processed using this script \n"
     + menu.get_help_footer ());
}

void psrsh::run ()
{
  if (!script.empty())
  {
    loadlines (script, script_lines);

    if (script == "-")
      name = "STDIN";
    else
      name = basename (script);

    if (load_files)
      Application::run ();
    else
      get_interpreter()->script( script_lines );
  }
  else
  {
    // no arguments: interactive mode
    get_interpreter()->set_reply( true );
    get_interpreter()->standard_input ("psrsh");
  }
}

void psrsh::process (Pulsar::Archive* archive)
{
  get_interpreter(refresh_foreach_archive)->set( archive );
  get_interpreter()->script( script_lines );
  processed = get_interpreter()->get ();
}

void psrsh::interpreter_help (const string& cmd)
{
  cout << endl << get_interpreter()->help (cmd);
  exit (0);
}

Pulsar::Interpreter* psrsh::get_interpreter (bool refresh)
{
  if (!interpreter || refresh)
    interpreter = standard_shell ();

  return interpreter;
}

