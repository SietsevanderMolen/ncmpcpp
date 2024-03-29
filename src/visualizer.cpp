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

#include "visualizer.h"

#ifdef ENABLE_VISUALIZER

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include "global.h"
#include "settings.h"
#include "status.h"
#include "statusbar.h"
#include "title.h"
#include "screen_switcher.h"

using Global::MainStartY;
using Global::MainHeight;

Visualizer *myVisualizer;

const int Visualizer::WindowTimeout = 1000/25; /* 25 fps */

Visualizer::Visualizer()
: Screen(NC::Window(0, MainStartY, COLS, MainHeight, "", Config.visualizer_color, NC::Border::None))
{
	ResetFD();
	m_samples = Config.visualizer_in_stereo ? 4096 : 2048;
#	ifdef HAVE_FFTW3_H
	m_fftw_results = m_samples/2+1;
	m_freq_magnitudes = new unsigned[m_fftw_results];
	m_fftw_input = static_cast<double *>(fftw_malloc(sizeof(double)*m_samples));
	m_fftw_output = static_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex)*m_fftw_results));
	m_fftw_plan = fftw_plan_dft_r2c_1d(m_samples, m_fftw_input, m_fftw_output, FFTW_ESTIMATE);
#	endif // HAVE_FFTW3_H
	
	FindOutputID();
}

void Visualizer::switchTo()
{
	SwitchTo::execute(this);
	w.clear();
	SetFD();
	m_timer = { 0, 0 };
	if (m_fifo >= 0)
		Global::wFooter->setTimeout(WindowTimeout);
	drawHeader();
}

void Visualizer::resize()
{
	size_t x_offset, width;
	getWindowResizeParams(x_offset, width);
	w.resize(width, MainHeight);
	w.moveTo(x_offset, MainStartY);
	hasToBeResized = 0;
}

std::wstring Visualizer::title()
{
	return L"Music visualizer";
}

void Visualizer::update()
{
	if (m_fifo < 0)
		return;
	
	// PCM in format 44100:16:1 (for mono visualization) and 44100:16:2 (for stereo visualization) is supported
	int16_t buf[m_samples];
	ssize_t data = read(m_fifo, buf, sizeof(buf));
	if (data < 0) // no data available in fifo
		return;
	
	if (m_output_id != -1 && Global::Timer.tv_sec > m_timer.tv_sec+Config.visualizer_sync_interval)
	{
		Mpd.DisableOutput(m_output_id);
		usleep(50000);
		Mpd.EnableOutput(m_output_id);
		m_timer = Global::Timer;
	}
	
	void (Visualizer::*draw)(int16_t *, ssize_t, size_t, size_t);
#	ifdef HAVE_FFTW3_H
	if (!Config.visualizer_use_wave)
		draw = &Visualizer::DrawFrequencySpectrum;
	else
#	endif // HAVE_FFTW3_H
		draw = &Visualizer::DrawSoundWave;
	
	w.clear();
	ssize_t samples_read = data/sizeof(int16_t);
	if (Config.visualizer_in_stereo)
	{
		int16_t buf_left[samples_read/2], buf_right[samples_read/2];
		for (ssize_t i = 0, j = 0; i < samples_read; i += 2, ++j)
		{
			buf_left[j] = buf[i];
			buf_right[j] = buf[i+1];
		}
		size_t half_height = MainHeight/2;
		(this->*draw)(buf_left, samples_read/2, 0, half_height);
		(this->*draw)(buf_right, samples_read/2, half_height+(draw == &Visualizer::DrawSoundWave ? 1 : 0), half_height+(draw != &Visualizer::DrawSoundWave ? 1 : 0));
	}
	else
		(this->*draw)(buf, samples_read, 0, MainHeight);
	w.refresh();
}

void Visualizer::spacePressed()
{
#	ifdef HAVE_FFTW3_H
	Config.visualizer_use_wave = !Config.visualizer_use_wave;
	Statusbar::msg("Visualization type: %s", Config.visualizer_use_wave ? "Sound wave" : "Frequency spectrum");
#	endif // HAVE_FFTW3_H
}

void Visualizer::DrawSoundWave(int16_t *buf, ssize_t samples, size_t y_offset, size_t height)
{
	const int samples_per_col = samples/w.getWidth();
	const int half_height = height/2;
	double prev_point_pos = 0;
	const size_t win_width = w.getWidth();
	for (size_t i = 0; i < win_width; ++i)
	{
		double point_pos = 0;
		for (int j = 0; j < samples_per_col; ++j)
			point_pos += buf[i*samples_per_col+j];
		point_pos /= samples_per_col;
		point_pos /= std::numeric_limits<int16_t>::max();
		point_pos *= half_height;
		w << NC::XY(i, y_offset+half_height+point_pos) << Config.visualizer_chars[0];
		if (i && abs(prev_point_pos-point_pos) > 2)
		{
			// if gap is too big. intermediate values are needed
			// since without them all we see are blinking points
			const int breakpoint = std::max(prev_point_pos, point_pos);
			const int half = (prev_point_pos+point_pos)/2;
			for (int k = std::min(prev_point_pos, point_pos)+1; k < breakpoint; k += 2)
				w << NC::XY(i-(k < half), y_offset+half_height+k) << Config.visualizer_chars[0];
		}
		prev_point_pos = point_pos;
	}
}

#ifdef HAVE_FFTW3_H
void Visualizer::DrawFrequencySpectrum(int16_t *buf, ssize_t samples, size_t y_offset, size_t height)
{
	for (unsigned i = 0, j = 0; i < m_samples; ++i)
	{
		if (j < samples)
			m_fftw_input[i] = buf[j++];
		else
			m_fftw_input[i] = 0;
	}
	
	fftw_execute(m_fftw_plan);
	
	// count magnitude of each frequency and scale it to fit the screen
	for (unsigned i = 0; i < m_fftw_results; ++i)
		m_freq_magnitudes[i] = sqrt(m_fftw_output[i][0]*m_fftw_output[i][0] + m_fftw_output[i][1]*m_fftw_output[i][1])/1e5*height/5;
	
	const size_t win_width = w.getWidth();
	const int freqs_per_col = m_fftw_results/win_width /* cut bandwidth a little to achieve better look */ * 7/10;
	for (size_t i = 0; i < win_width; ++i)
	{
		size_t bar_height = 0;
		for (int j = 0; j < freqs_per_col; ++j)
			bar_height += m_freq_magnitudes[i*freqs_per_col+j];
		bar_height = std::min(bar_height/freqs_per_col, height);
		const size_t start_y = y_offset > 0 ? y_offset : height-bar_height;
		const size_t stop_y = std::min(bar_height+start_y, w.getHeight());
		for (size_t j = start_y; j < stop_y; ++j)
			w << NC::XY(i, j) << Config.visualizer_chars[1];
	}
}
#endif // HAVE_FFTW3_H

void Visualizer::SetFD()
{
	if (m_fifo < 0 && (m_fifo = open(Config.visualizer_fifo_path.c_str(), O_RDONLY | O_NONBLOCK)) < 0)
		Statusbar::msg("Couldn't open \"%s\" for reading PCM data: %s", Config.visualizer_fifo_path.c_str(), strerror(errno));
}

void Visualizer::ResetFD()
{
	m_fifo = -1;
}

void Visualizer::FindOutputID()
{
	m_output_id = -1;
	if (!Config.visualizer_output_name.empty())
	{
		size_t idx = 0;
		Mpd.GetOutputs([this, &idx](MPD::Output &&output) {
			if (output.name() == Config.visualizer_output_name)
				m_output_id = idx;
			++idx;
		});
		if (m_output_id == -1)
			Statusbar::msg("There is no output named \"%s\"", Config.visualizer_output_name.c_str());
	}
}

#endif // ENABLE_VISUALIZER

