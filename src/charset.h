/***************************************************************************
 *   Copyright (C) 2008 by Andrzej Rybczak   *
 *   electricityispower@gmail.com   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef _CHARSET_H
#define _CHARSET_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if !defined(_UTF8) && defined(HAVE_ICONV_H)

#include <string>

void init_current_locale();

void utf_to_locale(std::string &);
void locale_to_utf(std::string &);

void str_pool_utf_to_locale(char *&);
void str_pool_locale_to_utf(char *&);

#else

#define init_current_locale();

#define utf_to_locale(x);
#define locale_to_utf(x);

#define str_pool_utf_to_locale(x);
#define str_pool_locale_to_utf(x);

#endif // !_UTF8 && HAVE_ICONV_H

#endif
