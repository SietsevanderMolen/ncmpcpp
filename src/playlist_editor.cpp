/***************************************************************************
 *   Copyright (C) 2008-2012 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
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
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <cassert>
#include <algorithm>

#include "charset.h"
#include "display.h"
#include "global.h"
#include "helpers.h"
#include "playlist.h"
#include "playlist_editor.h"
#include "mpdpp.h"
#include "regex_filter.h"
#include "status.h"
#include "statusbar.h"
#include "tag_editor.h"
#include "utility/comparators.h"
#include "title.h"
#include "screen_switcher.h"

using namespace std::placeholders;

using Global::MainHeight;
using Global::MainStartY;

PlaylistEditor *myPlaylistEditor;

namespace {//

size_t LeftColumnStartX;
size_t LeftColumnWidth;
size_t RightColumnStartX;
size_t RightColumnWidth;

std::string SongToString(const MPD::Song &s);
bool PlaylistEntryMatcher(const Regex &rx, const std::string &playlist);
bool SongEntryMatcher(const Regex &rx, const MPD::Song &s);

}

PlaylistEditor::PlaylistEditor()
{
	LeftColumnWidth = COLS/3-1;
	RightColumnStartX = LeftColumnWidth+1;
	RightColumnWidth = COLS-LeftColumnWidth-1;
	
	Playlists = NC::Menu<std::string>(0, MainStartY, LeftColumnWidth, MainHeight, Config.titles_visibility ? "Playlists" : "", Config.main_color, NC::Border::None);
	Playlists.setHighlightColor(Config.active_column_color);
	Playlists.cyclicScrolling(Config.use_cyclic_scrolling);
	Playlists.centeredCursor(Config.centered_cursor);
	Playlists.setSelectedPrefix(Config.selected_item_prefix);
	Playlists.setSelectedSuffix(Config.selected_item_suffix);
	Playlists.setItemDisplayer([](NC::Menu<std::string> &menu) {
		menu << Charset::utf8ToLocale(menu.drawn()->value());
	});
	
	Content = NC::Menu<MPD::Song>(RightColumnStartX, MainStartY, RightColumnWidth, MainHeight, Config.titles_visibility ? "Playlist content" : "", Config.main_color, NC::Border::None);
	Content.setHighlightColor(Config.main_highlight_color);
	Content.cyclicScrolling(Config.use_cyclic_scrolling);
	Content.centeredCursor(Config.centered_cursor);
	Content.setSelectedPrefix(Config.selected_item_prefix);
	Content.setSelectedSuffix(Config.selected_item_suffix);
	if (Config.columns_in_playlist_editor)
		Content.setItemDisplayer(std::bind(Display::SongsInColumns, _1, contentProxyList()));
	else
		Content.setItemDisplayer(std::bind(Display::Songs, _1, contentProxyList(), Config.song_list_format));
	
	w = &Playlists;
}

void PlaylistEditor::resize()
{
	size_t x_offset, width;
	getWindowResizeParams(x_offset, width);
	
	LeftColumnStartX = x_offset;
	LeftColumnWidth = width/3-1;
	RightColumnStartX = LeftColumnStartX+LeftColumnWidth+1;
	RightColumnWidth = width-LeftColumnWidth-1;
	
	Playlists.resize(LeftColumnWidth, MainHeight);
	Content.resize(RightColumnWidth, MainHeight);
	
	Playlists.moveTo(LeftColumnStartX, MainStartY);
	Content.moveTo(RightColumnStartX, MainStartY);
	
	hasToBeResized = 0;
}

std::wstring PlaylistEditor::title()
{
	return L"Playlist editor";
}

void PlaylistEditor::refresh()
{
	Playlists.display();
	mvvline(MainStartY, RightColumnStartX-1, 0, MainHeight);
	Content.display();
}

void PlaylistEditor::switchTo()
{
	SwitchTo::execute(this);
	markSongsInPlaylist(contentProxyList());
	drawHeader();
	refresh();
}

void PlaylistEditor::update()
{
	if (Playlists.reallyEmpty() || m_playlists_update_requested)
	{
		m_playlists_update_requested = false;
		Playlists.clearSearchResults();
		withUnfilteredMenuReapplyFilter(Playlists, [this]() {
			size_t idx = 0;
			Mpd.GetPlaylists([this, &idx](std::string &&playlist) {
				if (idx < Playlists.size())
					Playlists[idx].value() = playlist;
				else
					Playlists.addItem(playlist);
				++idx;
			});
			if (idx < Playlists.size())
				Playlists.resizeList(idx);
			std::sort(Playlists.beginV(), Playlists.endV(),
				LocaleBasedSorting(std::locale(), Config.ignore_leading_the));
		});
		Playlists.refresh();
	}
	
	if (!Playlists.empty() && (Content.reallyEmpty() || m_content_update_requested))
	{
		m_content_update_requested = false;
		Content.clearSearchResults();
		withUnfilteredMenuReapplyFilter(Content, [this]() {
			size_t idx = 0;
			Mpd.GetPlaylistContent(Playlists.current().value(), [this, &idx](MPD::Song &&s) {
				if (idx < Content.size())
				{
					Content[idx].value() = s;
					Content[idx].setBold(myPlaylist->checkForSong(s));
				}
				else
					Content.addItem(s, myPlaylist->checkForSong(s));
				++idx;
			});
			if (idx < Content.size())
				Content.resizeList(idx);
			std::string title;
			if (Config.titles_visibility)
			{
				title = "Playlist content";
				title += " (";
				title += boost::lexical_cast<std::string>(Content.size());
				title += " item";
				if (Content.size() == 1)
					title += ")";
				else
					title += "s)";
				title.resize(Content.getWidth());
			}
			Content.setTitle(title);
		});
		Content.display();
	}
	
	if (isActiveWindow(Content) && Content.reallyEmpty())
	{
		Content.setHighlightColor(Config.main_highlight_color);
		Playlists.setHighlightColor(Config.active_column_color);
		w = &Playlists;
	}
	
	if (Playlists.empty() && Content.reallyEmpty())
	{
		Content.Window::clear();
		Content.Window::display();
	}
}

bool PlaylistEditor::isContentFiltered()
{
	if (Content.isFiltered())
	{
		Statusbar::msg("Function currently unavailable due to filtered playlist content");
		return true;
	}
	return false;
}

ProxySongList PlaylistEditor::contentProxyList()
{
	return ProxySongList(Content, [](NC::Menu<MPD::Song>::Item &item) {
		return &item.value();
	});
}

void PlaylistEditor::AddToPlaylist(bool add_n_play)
{
	MPD::SongList list;
	
	if (isActiveWindow(Playlists) && !Playlists.empty())
	{
		if (Mpd.LoadPlaylist(Playlists.current().value()))
		{
			Statusbar::msg("Playlist \"%s\" loaded", Playlists.current().value().c_str());
			if (add_n_play)
				myPlaylist->PlayNewlyAddedSongs();
		}
	}
	else if (isActiveWindow(Content) && !Content.empty())
		addSongToPlaylist(Content.current().value(), add_n_play);
	
	if (!add_n_play)
		w->scroll(NC::Scroll::Down);
}

void PlaylistEditor::enterPressed()
{
	AddToPlaylist(true);
}

void PlaylistEditor::spacePressed()
{
	if (Config.space_selects)
	{
		if (isActiveWindow(Playlists))
		{
			if (!Playlists.empty())
			{
				Playlists.current().setSelected(!Playlists.current().isSelected());
				Playlists.scroll(NC::Scroll::Down);
			}
		}
		else if (isActiveWindow(Content))
		{
			if (!Content.empty())
			{
				Content.current().setSelected(!Content.current().isSelected());
				Content.scroll(NC::Scroll::Down);
			}
		}
	}
	else
		AddToPlaylist(false);
}

void PlaylistEditor::mouseButtonPressed(MEVENT me)
{
	if (!Playlists.empty() && Playlists.hasCoords(me.x, me.y))
	{
		if (!isActiveWindow(Playlists))
		{
			if (previousColumnAvailable())
				previousColumn();
			else
				return;
		}
		if (size_t(me.y) < Playlists.size() && (me.bstate & (BUTTON1_PRESSED | BUTTON3_PRESSED)))
		{
			Playlists.Goto(me.y);
			if (me.bstate & BUTTON3_PRESSED)
			{
				size_t pos = Playlists.choice();
				spacePressed();
				if (pos < Playlists.size()-1)
					Playlists.scroll(NC::Scroll::Up);
			}
		}
		else
			Screen<WindowType>::mouseButtonPressed(me);
		Content.clear();
	}
	else if (!Content.empty() && Content.hasCoords(me.x, me.y))
	{
		if (!isActiveWindow(Content))
		{
			if (nextColumnAvailable())
				nextColumn();
			else
				return;
		}
		if (size_t(me.y) < Content.size() && (me.bstate & (BUTTON1_PRESSED | BUTTON3_PRESSED)))
		{
			Content.Goto(me.y);
			if (me.bstate & BUTTON1_PRESSED)
			{
				size_t pos = Content.choice();
				spacePressed();
				if (pos < Content.size()-1)
					Content.scroll(NC::Scroll::Up);
			}
			else
				enterPressed();
		}
		else
			Screen<WindowType>::mouseButtonPressed(me);
	}
}

/***********************************************************************/

bool PlaylistEditor::allowsFiltering()
{
	return true;
}

std::string PlaylistEditor::currentFilter()
{
	std::string filter;
	if (isActiveWindow(Playlists))
		filter = RegexFilter<std::string>::currentFilter(Playlists);
	else if (isActiveWindow(Content))
		filter = RegexFilter<MPD::Song>::currentFilter(Content);
	return filter;
}

void PlaylistEditor::applyFilter(const std::string &filter)
{
	if (isActiveWindow(Playlists))
	{
		Playlists.showAll();
		auto rx = RegexFilter<std::string>(filter, Config.regex_type, PlaylistEntryMatcher);
		Playlists.filter(Playlists.begin(), Playlists.end(), rx);
	}
	else if (isActiveWindow(Content))
	{
		Content.showAll();
		auto rx = RegexFilter<MPD::Song>(filter, Config.regex_type, SongEntryMatcher);
		Content.filter(Content.begin(), Content.end(), rx);
	}
}

/***********************************************************************/

bool PlaylistEditor::allowsSearching()
{
	return true;
}

bool PlaylistEditor::search(const std::string &constraint)
{
	bool result = false;
	if (isActiveWindow(Playlists))
	{
		auto rx = RegexFilter<std::string>(constraint, Config.regex_type, PlaylistEntryMatcher);
		result = Playlists.search(Playlists.begin(), Playlists.end(), rx);
	}
	else if (isActiveWindow(Content))
	{
		auto rx = RegexFilter<MPD::Song>(constraint, Config.regex_type, SongEntryMatcher);
		result = Content.search(Content.begin(), Content.end(), rx);
	}
	return result;
}

void PlaylistEditor::nextFound(bool wrap)
{
	if (isActiveWindow(Playlists))
		Playlists.nextFound(wrap);
	else if (isActiveWindow(Content))
		Content.nextFound(wrap);
}

void PlaylistEditor::prevFound(bool wrap)
{
	if (isActiveWindow(Playlists))
		Playlists.prevFound(wrap);
	else if (isActiveWindow(Content))
		Content.prevFound(wrap);
}

/***********************************************************************/

ProxySongList PlaylistEditor::proxySongList()
{
	auto ptr = ProxySongList();
	if (isActiveWindow(Content))
		ptr = contentProxyList();
	return ptr;
}

bool PlaylistEditor::allowsSelection()
{
	return true;
}

void PlaylistEditor::reverseSelection()
{
	if (isActiveWindow(Playlists))
		reverseSelectionHelper(Playlists.begin(), Playlists.end());
	else if (isActiveWindow(Content))
		reverseSelectionHelper(Content.begin(), Content.end());
}

MPD::SongList PlaylistEditor::getSelectedSongs()
{
	MPD::SongList result;
	if (isActiveWindow(Playlists))
	{
		bool any_selected = false;
		for (auto it = Playlists.begin(); it != Playlists.end(); ++it)
		{
			if (it->isSelected())
			{
				any_selected = true;
				Mpd.GetPlaylistContent(it->value(), [&result](MPD::Song &&s) {
					result.push_back(s);
				});
			}
		}
		// we don't check for empty result here as it's possible that
		// all selected playlists are empty.
		if (!any_selected && !Content.empty())
		{
			withUnfilteredMenu(Content, [this, &result]() {
				result.insert(result.end(), Content.beginV(), Content.endV());
			});
		}
	}
	else if (isActiveWindow(Content))
	{
		for (auto it = Content.begin(); it != Content.end(); ++it)
			if (it->isSelected())
				result.push_back(it->value());
		// if no item is selected, add current one
		if (result.empty() && !Content.empty())
			result.push_back(Content.current().value());
	}
	return result;
}

/***********************************************************************/

bool PlaylistEditor::previousColumnAvailable()
{
	if (isActiveWindow(Content))
	{
		if (!Playlists.reallyEmpty())
			return true;
	}
	return false;
}

void PlaylistEditor::previousColumn()
{
	if (isActiveWindow(Content))
	{
		Content.setHighlightColor(Config.main_highlight_color);
		w->refresh();
		w = &Playlists;
		Playlists.setHighlightColor(Config.active_column_color);
	}
}

bool PlaylistEditor::nextColumnAvailable()
{
	if (isActiveWindow(Playlists))
	{
		if (!Content.reallyEmpty())
			return true;
	}
	return false;
}

void PlaylistEditor::nextColumn()
{
	if (isActiveWindow(Playlists))
	{
		Playlists.setHighlightColor(Config.main_highlight_color);
		w->refresh();
		w = &Content;
		Content.setHighlightColor(Config.active_column_color);
	}
}

/***********************************************************************/

void PlaylistEditor::Locate(const std::string &name)
{
	update();
	for (size_t i = 0; i < Playlists.size(); ++i)
	{
		if (name == Playlists[i].value())
		{
			Playlists.highlight(i);
			Content.clear();
			break;
		}
	}
	switchTo();
}

namespace {//

std::string SongToString(const MPD::Song &s)
{
	std::string result;
	if (Config.columns_in_playlist_editor)
		result = s.toString(Config.song_in_columns_to_string_format, Config.tags_separator);
	else
		result = s.toString(Config.song_list_format_dollar_free, Config.tags_separator);
	return result;
}

bool PlaylistEntryMatcher(const Regex &rx, const std::string &playlist)
{
	return rx.match(playlist);
}

bool SongEntryMatcher(const Regex &rx, const MPD::Song &s)
{
	return rx.match(SongToString(s));
}

}
