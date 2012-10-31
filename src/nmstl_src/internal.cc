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

#include <nmstl/internal.h>

#undef NMSTL_DEBUG_FILE_H
#define NMSTL_DEBUG_FILE_H "nmstl/internal"

NMSTL_NAMESPACE_BEGIN;

string to_string(const type_info& i) {
    int status;
    char *buf = abi::__cxa_demangle(i.name(), 0, 0, &status);
    if (buf && status == 0) {
        string out = buf;
        free(buf);
        return out;
    }

    return i.name();
}

inline string to_human_readable(const void *data, int length) {
    string out;
    int dots = 0;
    const char *cdata = static_cast<const char *>(data);

    string readable;

    while (length--) {
        char ch = *cdata++;
        if (ch >= ' ' && ch <= '~') {
            readable += ch;
        } else if (ch == '\n') {
            readable += "\\n";
        } else if (ch == '\r') {
            readable += "\\r";
        } else {
            if (readable.length()) {
                if (readable.length() >= 4) {
                    dots = 0;
                    out += readable;
                }
                readable.erase();
            }
            if (dots < 3) {
                out += ".";
                ++dots;
            }
        }
    }
    out += readable;
    return out;
}

string to_hex_string(const void *data, int length) {
    const unsigned char *cdata = static_cast<const unsigned char *>(data);
    string out;
    while (length--) {
        out += "0123456789ABCDEF"[*cdata >> 4];
        out += "0123456789ABCDEF"[*cdata & 0xF];
        ++cdata;
    }
    return out;
}

string to_escaped_string(const void *data, int length) {
    const unsigned char *cdata = static_cast<const unsigned char *>(data);
    string out;
    while (length--) {
        unsigned char ch = *cdata++;
        switch (ch) {
          case '\"':
          case '\\':
            out += '\\';
            out += ch;
            break;

          case '\n':
            out += "\\n";
            break;

          case '\r':
            out += "\\r";
            break;

          case '\t':
            out += "\\t";
            break;

          default:

            if (ch >= ' ' && ch <= '~')
                out += char(ch);
            else {
                out += '\\';
                if (ch < 8) {
                    out += '0' + ch;
                } else {
                    out += "x";
                    out += "0123456789ABCDEF"[ch >> 4];
                    out += "0123456789ABCDEF"[ch & 0xF];
                }
            }
        }
    }
    return out;	
}

NMSTL_NAMESPACE_END;
