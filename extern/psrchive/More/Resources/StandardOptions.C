/***************************************************************************
 *
 *   Copyright (C) 2008 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "Pulsar/StandardOptions.h"
#include "Pulsar/Interpreter.h"
#include "Pulsar/Archive.h"

#include "strutil.h"
#include "separate.h"

using namespace std;

Pulsar::StandardOptions::StandardOptions ()
{
}

void Pulsar::StandardOptions::add_options (CommandLine::Menu& menu)
{
  CommandLine::Argument* arg;

  menu.add ("\n" "Preprocessing options:");

  arg = menu.add (this, &StandardOptions::add_job, 'j', "commands");
  arg->set_help ("execute pulsar shell preprocessing commands");

  arg = menu.add (this, &StandardOptions::add_script, 'J', "script");
  arg->set_help ("execute pulsar shell preprocessing script");
}

void Pulsar::StandardOptions::add_job (const std::string& arg)
{
  separate (arg, jobs, ",");
}

void Pulsar::StandardOptions::add_script (const std::string& arg)
{
  loadlines (arg, jobs);
}

void Pulsar::StandardOptions::add_default_job (const std::string& job)
{
  default_jobs.push_back (job);
}

//! Preprocessing tasks implemented by partially derived classes
void Pulsar::StandardOptions::process (Archive* archive)
{
  if (jobs.size() == 0)
    jobs = default_jobs;

  the_result = 0;

  if (jobs.size() == 0)
    return;

  interpreter = get_interpreter();

  if (application && application->get_verbose())
    cerr << application->get_name() << ": interpreter processing "
	 << archive->get_filename() << endl;

  interpreter->set (archive);
  interpreter->script (jobs);

  the_result = interpreter->get();
}

//! Return the top of the interpreter stack
Pulsar::Archive* Pulsar::StandardOptions::result ()
{
  return the_result.ptr();
}

//! Provide access to the interpreter
Pulsar::Interpreter* Pulsar::StandardOptions::get_interpreter ()
{
  return standard_shell();
}

