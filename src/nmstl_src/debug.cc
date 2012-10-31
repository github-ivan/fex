// -*- mode: C++; -*-

/*
 * NMSTL, the Networking, Messaging, Servers, and Threading Library for C++
 * Copyright (c) 2002 Massachusetts Institute of Technology
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <iostream>

#include <nmstl/debug>
#include <nmstl/thread>

#include <signal.h>
#include <unistd.h>
#include <string.h>

#undef NMSTL_DEBUG_FILE_H
#define NMSTL_DEBUG_FILE_H "nmstl/debug"

NMSTL_NAMESPACE_BEGIN;

debug debug::the_debug;

const char *debug_msg::level_to_string(int level) {
    // --nosub--
    switch (level) {
      case coredump: return "core";
      case fatal:    return "fatal";
      case alert:    return "alert";
      case crit:     return "crit";
      case error:    return "error";
      case warn:     return "warn";
      case notice:   return "notice";
      case info:     return "info";
      case debug:    return "debug";
    }
    
    return "";
    // --endnosub--
}

void debug_msg::write(ostream& o, bool show_thread) const {
    string out;

    // Only print after last '/' (if any)
    const char *slash = strrchr(file, '/');
    const char *file_str = slash ? slash + 1 : file;

    //out += "\x1B[7m";
    out += level_to_string(level);
    //out += "\x1B[0m";
    out += " [";
    out += file_str;
    out += ":";
    out += to_string(line);
    out += "] ";
    /*
    if (show_thread) {
        out += "[";
	out += thread::self();
        out += "] ";
    }
    */
    out += str;
    out += "\n";
    o << out;
}

debug::debug() : level(debug_msg::notice), show_thread(false), out(&cout)
{
    char *debugstr = getenv("DEBUG");

    if (debugstr) {
        string d(debugstr);
        istringstream iss(d);

        string token;
        while (iss >> token) {
            if (token.size() && token[0] == '+') {
                if (token == "+thr" "ead") {
                    show_thread = true;
                } else {
                    WARN << "Invalid switch \"" << token << "\" in DEBUG environment variable";
                }
            } else if (token.find_first_not_of("-0123456789") == string::npos) {
                level = atoi(token.c_str());
            } else {
                unsigned int sep = token.find_last_of(":=");
                if (sep == string::npos)
                    WARN << "Invalid token \"" << token << "\" in DEBUG environment variable";
                else {
                    string name = token.substr(0, sep);
                    string lstr = token.substr(sep + 1);
                    int l = atoi(lstr.c_str());
                    files[strdup(name.c_str())] = l;
                }
            }
        }
    }
}

debug::~debug() {
    for (Files::iterator i = files.begin(); i != files.end(); ++i)
        free(const_cast<char*>(i->first));
}

int debug::get_level_for_file(const char *file) {
    while (file[0] == '.' || file[0] == '/')
        ++file;

    const char *element2 = 0;    // second-to-last element of path
    const char *element1 = file; // last element of path
    while (const char *ch = strchr(element1, '/')) {
        element2 = element1;
        element1 = ch + 1;
    }

    Files::const_iterator i = files.find(element1);
    if (i != files.end()) {
        return i->second;
    }

    if (element2 && !strncasecmp(element2, "nmstl/", 6)) {
        return 0;
    }

    return level;
}

bool debug::operator ^ (const debug_msg& msg) {
  //    static mutex lock;

  //locking (lock) {
        msg.write(*out, show_thread);

        if (msg.get_level() == debug_msg::coredump) {
#if 0
            // Dump stack trace
            void* trace[ 64 ];
            char** messages = NULL;
            int trace_size = 0;
            
            trace_size = backtrace( trace, 64 );
            messages = backtrace_symbols( trace, trace_size );
            for( int i = 0; i < trace_size; ++i ) {
                cerr << "Traced: " << messages[i] << endl;
            }
#endif
            
            kill(getpid(), SIGABRT);
            exit(1);
        }

        if (msg.get_level() == debug_msg::fatal)
            exit(1);
	//}

    return true;
}

NMSTL_NAMESPACE_END;
