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

#ifndef NMSTL_H_NMSTL
#define NMSTL_H_NMSTL




/** \file
 * Internal declarations.
 */

#define NMSTL_NAMESPACE_BEGIN namespace std {}; namespace nmstl { using namespace ::std;
#define NMSTL_NAMESPACE_END };

#define NMSTL_ARITH_OPS(T, m) \
bool operator == (const T& x) const { return m == x.m; } \
bool operator != (const T& x) const { return m != x.m; } \
bool operator < (const T& x) const { return m < x.m; } \
bool operator > (const T& x) const { return m > x.m; } \
bool operator <= (const T& x) const { return m <= x.m; } \
bool operator >= (const T& x) const { return m >= x.m; }

#define NMSTL_ARITH_OPS_T1(T, U, m) \
template<typename T> bool operator == (const U& x) const { return m == x.m; } \
template<typename T> bool operator != (const U& x) const { return m != x.m; } \
template<typename T> bool operator < (const U& x) const { return m < x.m; } \
template<typename T> bool operator > (const U& x) const { return m > x.m; } \
template<typename T> bool operator <= (const U& x) const { return m <= x.m; } \
template<typename T> bool operator >= (const U& x) const { return m >= x.m; }

#define NMSTL_TO_STRING_INTL(T) \
    inline string to_string(const T& t) { return t.as_string(); } \
    inline ostream& operator << (ostream& out, const T& t) { return out << t.as_string(); } \
    inline string operator + (const char * in, const T& t) { return string(in) + to_string(t); } \
    inline string operator + (const string& in, const T& t) { return in + to_string(t); } \
    inline string& operator += (string& in, const T& t) { return in += to_string(t); }

#define NMSTL_TO_STRING(T) NMSTL_TO_STRING_INTL(T) using ::nmstl::to_string; using ::nmstl::operator +;

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <cstdio>
#include <typeinfo>
#include <map>
#include <vector>

#include <cxxabi.h>

NMSTL_NAMESPACE_BEGIN;


#define NMSTL_TO_STRING_APPEND(A) \
inline string operator + (const string& t, const A& u) { return t + to_string(u); }

#define NMSTL_TO_STRING_APPEND_T1(A) \
template<typename T> inline string operator + (const string& t, const A& u) { return t + to_string(u); }

#define NMSTL_TO_STRING_APPEND_T2(A,B) \
template<typename T, typename U> inline string operator + (const string& t, const A,B& u) { return t + to_string(u); }





#define NMSTL_TO_STRING_STREAM(A) \
NMSTL_TO_STRING_APPEND(A) \
inline ostream& operator << (ostream& out, const A& u) { return out << to_string(u); }

#define NMSTL_TO_STRING_STREAM_T1(A) \
NMSTL_TO_STRING_APPEND_T1(A) \
template<typename T> inline ostream& operator << (ostream& out, const A& u) { return out << to_string(u); }

#define NMSTL_TO_STRING_STREAM_T2(A,B) \
NMSTL_TO_STRING_APPEND_T2(A,B) \
template<typename T, typename U> inline ostream& operator << (ostream& out, const A,B& u) { return out << to_string(u); }









template<class T>
inline string to_string(const T* t) {
    char buf[64];
    sprintf(buf, "&(%p)", t);
    return buf;
}

inline string to_string(const char* ch) {
    return string(ch);
}

inline string to_string(const string& s) {
    return s;
}

inline string to_string(const void* t) {
    char buf[64];
    sprintf(buf, "&%p", t);
    return buf;
}
NMSTL_TO_STRING_APPEND(void*);

inline string to_string(const bool& i) {
    static string t = "true";
    static string f = "false";
    return i ? t : f;
}
NMSTL_TO_STRING_APPEND(bool);

template<typename T, typename U>
inline string to_string(const map<T,U>& m) {
    string out = "{";

    for (typename map<T, U>::const_iterator i = m.begin(); i != m.end(); ++i) {
        if (i != m.begin()) out.append("; ");
        out.append(to_string(i->first)).append("->").append(to_string(i->second));
    }

    out.append("}");
    return out;
}
NMSTL_TO_STRING_STREAM_T2(map<T,U>);

template<typename T>
inline string to_string(const vector<T>& m) {
    string out = "{";

    for (unsigned int i = 0; i < m.size(); ++i) {
        if (i != 0) out.append("; ");
        out.append(to_string(m[i]));
    }

    out.append("}");
    return out;
}
NMSTL_TO_STRING_STREAM_T2(vector<T,U>);

string to_string(const type_info& i);

#define NMSTL_SCALAR_TO_STRING(T) \
inline string to_string(const T& t) { ostringstream o; o << t; return o.str(); } NMSTL_TO_STRING_APPEND(T)

NMSTL_SCALAR_TO_STRING(char);
NMSTL_SCALAR_TO_STRING(unsigned char);
NMSTL_SCALAR_TO_STRING(short);
NMSTL_SCALAR_TO_STRING(unsigned short);
NMSTL_SCALAR_TO_STRING(int);
NMSTL_SCALAR_TO_STRING(unsigned int);
NMSTL_SCALAR_TO_STRING(long);
NMSTL_SCALAR_TO_STRING(unsigned long);
NMSTL_SCALAR_TO_STRING(long long);
NMSTL_SCALAR_TO_STRING(unsigned long long);

inline string to_string(const float& t) { ostringstream o; o << setprecision(99) << t; return o.str(); }
NMSTL_TO_STRING_APPEND(float);
inline string to_string(const double& t) { ostringstream o; o << setprecision(99) << t; return o.str(); }
NMSTL_TO_STRING_APPEND(double);

string to_human_readable(const void *data, int length);
inline string to_human_readable(const string& s) {
    return to_human_readable(s.data(), s.length());
}

string to_hex_string(const void *data, int length);
inline string to_hex_string(const string& s) {
    return to_hex_string(s.data(), s.length());
}

template <typename Int>
inline string to_hex_string(Int l) {
    bool written = false;
    string out = "0x";

    for (int shift = (sizeof(Int) - 1) * 8; shift >= 0; shift -= 8) {
        unsigned char ch = (unsigned char)(l >> shift);
        if (ch) written = true;
        if (written) {
            out += "0123456789ABCDEF"[ch >> 4];
            out += "0123456789ABCDEF"[ch & 0xF];
        }
    }

    if (!written) out += "0";
    return out;
}

string to_escaped_string(const void *data, int length);
inline string to_escaped_string(const string& s) {
    return to_escaped_string(s.data(), s.length());
}

NMSTL_NAMESPACE_END;




#ifdef NMSTL_DMALLOC

#include "dmalloc.h"
#include <new>

inline void* operator new(std::size_t bytes, const char *file, unsigned int line) throw(std::bad_alloc) {
    return _malloc_leap(file, line, bytes);
}
inline void* operator new[](std::size_t bytes, const char *file, unsigned int line) throw(std::bad_alloc) {
    return _malloc_leap(file, line, bytes);
}

inline void operator delete(void *pnt) { _free_leap(0, 0, pnt); }
inline void operator delete[](void *pnt) { _free_leap(0, 0, pnt); }

#define new new(__FILE__, __LINE__)

#endif

#endif
