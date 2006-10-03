/***************************************************************************
 *   Copyright (C) 2006 by Ivan Vasić   								   *
 *   ivasic@gmail.com   												   *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 ***************************************************************************/
#include "timeestimator.h"
#include "torrentcontrol.h"

#include <torrent/globals.h>
#include <util/log.h>
#include <util/constants.h>

namespace bt
{	
	TimeEstimator::TimeEstimator(TorrentControl* tc)
			: m_tc(tc)
	{
		m_samples = new SampleQueue(20);
		m_lastAvg = 0;
	}


	TimeEstimator::~TimeEstimator()
	{
		delete m_samples;
	}

	Uint32 TimeEstimator::estimate()
	{
		const TorrentStats& s = m_tc->getStats();

		Uint32 sample = (Uint32) s.download_rate;
		
		//push new sample
		m_samples->push(sample);
		
		int percentage = (int) (s.bytes_downloaded / s.total_bytes) * 100;
		 
		if(s.bytes_downloaded < 1024*1024*100 && sample > 0) // < 100KB
		{
			m_lastETA = estimateGASA();
			return m_lastETA;
		}
		
		if(percentage >= 99 && sample > 0 && s.bytes_left_to_download <= 10*1024^3) //1% of a very large torrent could be hundreds of MB so limit it to 10MB
		{
			m_lastETA = estimateCSA();	
			return m_lastETA;
		}
		
		if(!m_samples->isFull())
		{
			m_lastETA = estimateWINX();
			
			if(m_lastETA == -1)
				m_lastETA = estimateGASA();

			return m_lastETA;
		}
		else
		{
			m_lastETA = estimateMAVG();
			
			if(m_lastETA == -1)
				m_lastETA = estimateGASA();
		}

		
		return m_lastETA;
	}
	
	Uint32 TimeEstimator::estimateCSA()
	{
		const TorrentStats& s = m_tc->getStats();
		
		if(s.download_rate == 0)
			return -1;
		
		return (int)floor( (float)s.bytes_left_to_download / (float)s.download_rate );
	}

	Uint32 TimeEstimator::estimateGASA()
	{
		const TorrentStats& s = m_tc->getStats();
		
		if(m_tc->getRunningTimeDL() > 0 && s.bytes_downloaded > 0)
		{
			double avg_speed = (double) s.bytes_downloaded / (double) m_tc->getRunningTimeDL();
			return (Uint32) floor ( (double) s.bytes_left_to_download / avg_speed);
		}
		
		return -1;
	}
	
	Uint32 TimeEstimator::estimateWINX()
	{
		const TorrentStats& s = m_tc->getStats();
		if(m_samples->sum() > 0 && m_samples->count() > 0)
			return (Uint32) floor ( (double) s.bytes_left_to_download / ((double) m_samples->sum() / (double) m_samples->count()) );
		
		return -1;
	}
	
	Uint32 TimeEstimator::estimateMAVG()
	{
		const TorrentStats& s = m_tc->getStats();
		
		if(m_samples->count() > 0)
		{
		
			double lavg = m_lastAvg - ( (double) m_samples->first() / (double) m_samples->count() ) + ( (double) m_samples->last() / (double) m_samples->count() );
		
			m_lastAvg = (Uint32) floor (lavg);
			
			if(lavg > 0)
				return (Uint32) floor ( (double) s.bytes_left_to_download / ( (lavg+estimateGASA()) / 2 ) );
			
			return -1;
		}
		
		return -1;
	}

}

bt::SampleQueue::SampleQueue(int max)
	:m_count(0), m_size(max)
{
	m_samples = new Uint32[max];
	for(int i=0; i<m_size; ++i)
		m_samples[i] = 0;
	
	m_end=-1;
	m_start=0;
}

bt::SampleQueue::~ SampleQueue()
{
	delete [] m_samples;
}

void bt::SampleQueue::push(Uint32 sample)
{
	if(m_count<m_size)
	{
		//it's not full yet
		m_samples[ (++m_end) % m_size ] = sample;
		m_count++;
		 
		return;
	}
	
	//since it's full I'll just replace the oldest value with new one and update all variables.
	m_end = (++m_end) % m_size;
	m_start = (++m_start) % m_size;
	m_samples[m_end] = sample;
}

Uint32 bt::SampleQueue::first()
{
	return m_samples[m_start];
}

Uint32 bt::SampleQueue::last()
{
	return m_samples[m_end];
}

bool bt::SampleQueue::isFull()
{
	return m_count >= m_size;
}

int bt::SampleQueue::count()
{
	return m_count;
}

Uint32 bt::SampleQueue::sum()
{
	Uint32 s = 0;
	
	for(int i=0; i<m_count; ++i)
		s+=m_samples[i];
	
	return s;
}
