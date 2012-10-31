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

#include <nmstl/serial>

#include <fstream>

#undef NMSTL_DEBUG_FILE_H
#define NMSTL_DEBUG_FILE_H "nmstl/serial"

NMSTL_NAMESPACE_BEGIN;

static string simplify_type(string in) {
    if (in == "Ss") return "S";

    if (in.length() > 2 && in[0] == 'S' && in[1] == 't') {
        unsigned int endnum = in.find_first_not_of("0123456789", 2);
        string lenstr = in.substr(2, endnum - 2);

        int len = 0;
        if (!sscanf(lenstr.c_str(), "%d", &len))
            return in;

        return in.substr(endnum, len);
    }

    if (in.length() > 1 && (in[0] == 'N' || (in[0] >= '0' && in[0] <= '9'))) {
        unsigned int start = in[0] == 'N' ? 1 : 0;
        unsigned int last = 0;
        while (1) {
            if (start >= in.length() || in[start] == 'E') return in.substr(last, start - last);

            unsigned int endnum = in.find_first_not_of("0123456789", start);
            string lenstr = in.substr(start, endnum - start);

            int len = 0;
            if (!sscanf(lenstr.c_str(), "%d", &len))
                return in;

            last = endnum;
            start = endnum + len;
        }
    }

    return in;
}

// Always writes out len, even in non-typesafe mode.
void oserial::write_type(const char *type, unsigned int len) {
    if (mode == typesafe || mode == readable) {
        if (mode == typesafe) {
            put(type, strlen(type));
        } else {
            put(simplify_type(type));
        }

        if (len != 0) {
            put('/');
            put(to_string(len));
        }

        put('=');
    } else
        default_freeze(len);
}

void oserial::init() {
    if (!nosig) {
        if (mode == typesafe)
            put('1');
        else if (mode == readable)
            put('~');
        else if (mode == binary)
            put("\0\0\0\1", 4);
        else if (mode == compact)
            put('\1');
    }
}

void iserial::default_unfreeze(dynbuf& s) {
    unsigned int length;

    if (!check_type(typeid(string).name(), length)) return;
    s = dynbuf(length);

    if (!get(s.data(), length)) {
        set_error("Expected databuf of length " + to_string(length));
        return;
    }
    s.set_length(length);
    DEBUG << "Read dynbuf " << s;

    if (mode == typesafe || mode == readable) {
        if (get() != ';')
            set_error("Missing semicolon at end of databuf");
    }
}

iserial& iserial::check_type(const char *type, unsigned int& actual_length) {
    if (mode != typesafe && mode != readable) {
        default_unfreeze(actual_length);
        return *this;
    }

    string actual_type;

    int ch;
    while (1) {
        ch = get();
        if (ch == eof) {
            set_error("Expected type and length but got \"" + actual_type + "\"");
            return *this;
        }
        if (ch == '/' || ch == '=')
            break;
        actual_type += ch;
    }
    if ((mode == typesafe && actual_type != type) ||
        (mode == readable && actual_type != simplify_type(type)))
    {
        set_error("Tried to read type " + string(type) + " but got type " + actual_type);
        return *this;
    }

    actual_length = 0;
    if (ch != '=') {
        while (1) {
            ch = get();
            if (ch == eof) {
                set_error("Expected length and data but got \"" + to_string(actual_length) + "\"");
                return *this;
            }
            if (ch == '=') {
                break;
            }
            if (ch < '0' || ch > '9') {
                set_error("Expected length but got non-digit \"" + to_string(char(ch)) + "\"");
                return *this;
            }
                
            actual_length = actual_length * 10 + (ch - '0');
        }
    }

    return *this;
}

string iserial::read_string(unsigned int length) {
    if (length == 0 && (mode == typesafe || mode == readable)) {
        string out;

        while (1) {
            char ch = get();
            if (ch == ';') break;
            if (ch == eof) {
                set_error("Missing semicolon at end of string");
                return string();
            }
            out += ch;
        }

        return out;
    }

    char *out = new char[length];
    if (!get(out, length)) {
        set_error("Expected string of length " + to_string(length));
        delete[] out;
        return string();
    }

    if (mode == typesafe || mode == readable) {
        char ch = get();

        if (ch != ';') {
            set_error("Missing semicolon at end of string (" + to_string(char(ch)) + " instead)");
            return string();
        }
    }

    string str(out, length);
    delete[] out;
    return str;
}

void iserial::init() {
    if (mode == automatic) {
        int ch = get();

        if (ch == '1')
            mode = typesafe;
        else if (ch == '~')
            mode = readable;
        else if (ch == 0) {
            mode = binary;
            if (get() != 0 || get() != 0 || get() != 1)
                set_error("Unable to read version number");
        } else if (ch == 1) {
            mode = compact;
        } else {
            set_error("Unable to read version number");
        }
    }
}

NMSTL_NAMESPACE_END;
