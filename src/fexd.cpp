/***************************************************************************
 *   Copyright (C) 2004 by Michael Reithinger                              *
 *   mreithinger@web.de                                                    *
 *                                                                         *
 *   This file is part of fex.                                             *
 *                                                                         *
 *   fex is free software; you can redistribute it and/or modify           *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   fex is distributed in the hope that it will be useful,                *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <log4cpp/Category.hh>
#include <log4cpp/SyslogAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/SimpleLayout.hh>
#include <log4cpp/BasicLayout.hh>
#include "configfile.h"
#include "filelistener.h"
#include "connection.h"
#include <getopt.h>
#include <signal.h>
#include <sys/wait.h>


using namespace std;
using namespace nmstl;
using namespace log4cpp;

char* version_string = "fexd "PACKAGE_VERSION;
Category& lc = Category::getRoot();
io_event_loop MainLoop;
bool do_lock_polling = true;

#if defined(USE_DNOTIFY)
#define HAS_DO_INOTIFY 1
bool do_inotify = true;
#endif

#if defined(USE_POLLING)
#if ! defined(USE_DNOTIFY)
bool do_inotify = true;
#else
#define HAS_DO_DNOTIFY 1
bool do_dnotify = true;
#endif
#endif


static const char* config_file = FEX_CONF;
static int debug_level = 0;
static int verbose_level = 0;
static ofstream logstream;


static void 
parse_options(int argc, char *argv[]);

static void
prepare_log();

static
void
print_help();

static
void 
terminate(int);

static void 
check_children(int);

int
main(int argc, char *argv[])
{
  srand(time(NULL));

  parse_options(argc, argv);
  prepare_log();

  if (!debug_level) {
    if (daemon(0, 0) != 0) {
      lc.error("daemon failed! %s", strerror(errno));
      return 2;
    }
  }

  if (getuid() != 0) {
    cerr << "sorry, must be run as root" << endl;
    return 1;
  }

  lc.notice("%s started", version_string);
  Configuration::get().parse(config_file);
  ConnectionPool::get().start_listening();

  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);
  signal(SIGPIPE, terminate);
  signal(SIGCHLD, check_children);

  lc.notice("daemon start to loop");
 tryagain:
  try
    {
      MainLoop.run();
    }
  catch(exception& error) 
    {
      lc.fatal(error.what());
      goto tryagain;
    }
  catch(...) {
    lc.fatal("a really bad error occured");
  }

  MainLoop.tidy_handlers();
  lc.notice("fexd finished");
  lc.shutdown();
}

  
void 
terminate(int)
{
  lc.debug("server terminated");
  MainLoop.terminate();
}

void 
check_children(int)
{
  pid_t pid = ::wait(NULL);
  if (pid > 0)
    lc.notice("child finished: %i", pid);
}


void 
parse_options(int argc, char *argv[])
{
  static struct option long_options[] = {
        { "debug"     , 0, 0, 'd' },
        { "verbose"   , 0, 0, 'v' },
	{ "no-locks"  , 0, 0, 'l' },
#ifdef HAS_DO_INOTIFY
	{ "no-inotify", 0, 0, 'I' },
#endif
#ifdef HAS_DO_DNOTIFY
	{ "no-dnotify", 0, 0, 'D' },
#endif
	{ "config"    , 0, 0, 'c' },
	{ "help"      , 0, 0, 'h' },
        { 0, 0, 0, 0 }
      };
      
  int option_index = 0;
  int arg;

  while((arg = getopt_long(argc, argv, "dvlIDch",
			   long_options, &option_index)) != -1) {
    switch(arg){
    case 'l':
      do_lock_polling = false;
      break;

    case 'd':
      debug_level++;
      break;

    case 'v':
      verbose_level++;
      break;

#ifdef HAS_DO_INOTIFY
    case 'I':
      do_inotify = false;
      break;
#endif

#ifdef HAS_DO_DNOTIFY
    case 'D':
      do_dnotify = false;
      break;
#endif

    case 'c':
      config_file = argv[optind];
      break;

    default:
      print_help();
      exit(0);
    }
  }
}

#undef WARN
#undef NOTICE
#undef INFO
#undef DEBUG

void
prepare_log()
{
  Appender* app;

  if (debug_level) {
    app = new OstreamAppender("ostream", &cerr);
    app->setLayout(new BasicLayout());
  }
  else {
    app = new SyslogAppender("syslog", "fexd", LOG_DAEMON);
    app->setLayout(new SimpleLayout());
  }

  lc.removeAllAppenders();
  lc.setAppender(app);

  switch(max(debug_level, verbose_level)) {
  case 0 : lc.setPriority(Priority::WARN  ); break;
  case 1 : lc.setPriority(Priority::NOTICE); break;
  case 2 : lc.setPriority(Priority::INFO  ); break;
  default: lc.setPriority(Priority::DEBUG ); break;
  }
}

static
void
print_help()
{
  cout << "fexd "PACKAGE_VERSION << endl;
  cout << "fexd [-d|--debug] [-v|--verbose] [-l|--no-lock ] [-h|--help]"
       << endl;

  cout << "     [-c|--config <config_file>]";
  
#ifdef HAS_DO_INOTIFY
  cout << " [-I|--no-inotify]";
#endif
#ifdef HAS_DO_DNOTIFY
  cout << " [-D|--no-dnotify]";
#endif
  cout << endl;
  cout << "  -d,--debug       don't deamonize, print debug messages to stdout"
       << endl;
  cout << "  -v,--verbose     set verbosity level" << endl;
  cout << "  -n,--no-locks    no lock detection " << endl;
  cout << "  -h,--help        print this message" << endl;

#ifdef HAS_DO_INOTIFY
  cout << "  -I,--no-inotify  don't use inotify mechanism" << endl;
#endif
#ifdef HAS_DO_DNOTIFY
  cout << "  -D,--no-dnotify  don't use dnotify mechanism" << endl;
#endif
  cout << "  -c,--config      path to alternate configuration file" << endl;
  cout << "                   (default is "FEX_CONF")\n" << endl;
}
