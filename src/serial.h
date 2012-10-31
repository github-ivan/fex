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
#ifndef SERIAL_H
#define SERIAL_H

#include <string>
#include <ios>

/*
  writes data into and reads data from a stream.
*/
template <class _Stream>
class Serializer
{
public:
  Serializer(_Stream& stream)
    : _M_Stream(stream)
  { }

  ~Serializer()
  { }

  template <class _Container>
  void 
  write(const std::string& key, const _Container& container)
  {
    size_t pos = same_to(key.c_str(), _M_LastKey.c_str());
    size_t size = key.length() + 1 - pos;

    _M_Stream.write((const char*)&pos, sizeof(pos));
    _M_Stream.write(key.c_str() + pos, size);
    _M_Stream.write((const char*)&container, sizeof(_Container));
    _M_LastKey = key;
  }


  template <class _Container>
  bool 
  read(std::string* key, _Container* container)
  {
    size_t pos;
    _M_Stream.read((char*)&pos, sizeof(pos));

    size_t read = _M_Stream.gcount();
    if (read != sizeof(pos)) {
      _M_Stream.setstate(std::ios_base::eofbit);
      return false;
    }
    
    char ch;
    key->clear();
    while(ch = _M_Stream.get())
      *key += ch;

    _M_Stream.read((char*)container, sizeof(_Container));
    *key = _M_LastKey.substr(0, pos) + *key;
    _M_LastKey = *key;

    return true;
  }


  void 
  reset()
  { _M_LastKey.clear(); }


private:
  size_t 
  same_to(const char* str1, const char* str2)
  {
    const char *p1 = str1;
    const char *p2 = str2;

    while(*p1 == *p2 && *p1 && *p2) {
      p1++;
      p2++;
    }
    
    return p1 - str1;
  }

  _Stream&    _M_Stream;
  std::string _M_LastKey;
};



#endif

/** EMACS **
 * Local variables:
 * mode: c++
 * End:
 */
