/***************************************************************************
 *   Copyright (C) 2005 by Joris Guisson                                   *
 *   joris.guisson@gmail.com                                               *
 *   ivasic@gmail.com                                                      *
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
 *   51 Franklin Steet, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ***************************************************************************/
#include "queuemanager.h"

#include <qstring.h>
#include <kmessagebox.h>
#include <klocale.h>
#include <util/log.h>
#include <util/error.h>
#include <util/sha1hash.h>
#include <util/waitjob.h>
#include <torrent/globals.h>
#include <torrent/torrent.h>
#include <torrent/torrentcontrol.h>
#include <interfaces/torrentinterface.h>

using namespace kt;

namespace bt
{

	QueueManager::QueueManager() : QObject()
	{
		downloads.setAutoDelete(true);
		max_downloads = 0;
		max_seeds = 0; //for testing. Needs to be added to Settings::
		
		keep_seeding = true; //test. Will be passed from Core
	}


	QueueManager::~QueueManager()
	{}

	void QueueManager::append(kt::TorrentInterface* tc)
	{
		downloads.append(tc);
		downloads.sort();
	}

	void QueueManager::remove(kt::TorrentInterface* tc)
	{
		int index = downloads.findRef(tc);
		if(index != -1)
			downloads.remove(index);
		else
			Out() << "Could not delete removed torrent control." << endl;
	}

	void QueueManager::clear()
	{
		Uint32 nd = downloads.count();
		downloads.clear();
		
		// wait for a second to allow all http jobs to send the stopped event
		if (nd > 0)
			SynchronousWait(500);
	}

	void QueueManager::start(kt::TorrentInterface* tc, bool user)
	{
		const TorrentStats & s = tc->getStats();
		bool start_tc = false;
		if (s.completed)
			start_tc = (max_seeds == 0 || getNumRunning(false, true) < max_seeds);
		else 
	    	start_tc = (max_downloads == 0 || getNumRunning(true) < max_downloads);
		
		if (start_tc)
		{
			Out() << "Starting download" << endl;
			try
			{
				float ratio = (float) s.bytes_uploaded / s.bytes_downloaded;
				float max_ratio = tc->getMaxShareRatio();
				if(s.completed && max_ratio > 0 && ratio >= max_ratio)
				{
					if(KMessageBox::questionYesNo(0, i18n("This torrent has reached its maximum share ratio. Ignore the limit and start seeding anyway?"),i18n("Maximum share ratio limit reached.")) == KMessageBox::Yes)
					{
						tc->setMaxShareRatio(0.00f);
						tc->start();
					}
					else
						return;
				}
				else
					tc->start();
			}
			catch (bt::Error & err)
			{
				QString msg =
						i18n("Error starting torrent %1 : %2")
						.arg(s.torrent_name).arg(err.toString());
				KMessageBox::error(0,msg,i18n("Error"));
			}
		}
		else
		{
			if (!tc->getStats().running && !tc->getStats().stopped_by_error && user)
			{
				bool seed = tc->getStats().completed;
				int nr = seed ? max_seeds : max_downloads;
			
				if(!seed)
					KMessageBox::error(0,
									   i18n("Cannot start more than 1 download."
											   " Go to Settings -> Configure KTorrent,"
											   " if you want to change the limit.",
									   "Cannot start more than %n downloads."
											   " Go to Settings -> Configure KTorrent,"
											   " if you want to change the limit.",
									   nr),
									   i18n("Error"));
				else
					KMessageBox::error(0,
									   i18n("Cannot start more than 1 seed."
											   " Go to Settings -> Configure KTorrent,"
											   " if you want to change the limit.",
									   "Cannot start more than %n seeds."
											   " Go to Settings -> Configure KTorrent,"
											   " if you want to change the limit.",
									   nr),
									   i18n("Error"));
			}
		}
	}

	void QueueManager::stop(kt::TorrentInterface* tc, bool user)
	{
		const TorrentStats & s = tc->getStats();
		if (s.started && s.running)
		{
			try
			{
				tc->stop(user);
				if(user)
					tc->setPriority(0);
			}
			catch (bt::Error & err)
			{
				QString msg =
						i18n("Error stopping torrent %1 : %2")
						.arg(s.torrent_name).arg(err.toString());
				KMessageBox::error(0,msg,i18n("Error"));
			}
		}
		
		orderQueue();
	}
	
	void QueueManager::startall()
	{
		QPtrList<kt::TorrentInterface>::iterator i = downloads.begin();
		while (i != downloads.end())
		{
			kt::TorrentInterface* tc = *i;
			start(tc, false);
			i++;
		}
	}

	void QueueManager::stopall()
	{
		QPtrList<kt::TorrentInterface>::iterator i = downloads.begin();
		while (i != downloads.end())
		{
			kt::TorrentInterface* tc = *i;
			if (tc->getStats().running)
				tc->stop(true);
			else //if torrent is not running but it is queued we need to make it user controlled
				tc->setPriority(0); 
			i++;
		}
	}
	
	void QueueManager::startNext()
	{
		orderQueue();
	}

	int QueueManager::countDownloads( )
	{
		int nr = 0;
		QPtrList<TorrentInterface>::const_iterator i = downloads.begin();
		while (i != downloads.end())
		{
			if(!(*i)->getStats().completed)
				++nr;
			++i;
		}
		return nr;
	}

	int QueueManager::countSeeds( )
	{
		int nr = 0;
		QPtrList<TorrentInterface>::const_iterator i = downloads.begin();
		while (i != downloads.end())
		{
			if((*i)->getStats().completed)
				++nr;
			++i;
		}
		return nr;
	}
	
	int QueueManager::getNumRunning(bool onlyDownload, bool onlySeed)
	{
		int nr = 0;
	//	int test = 1;
		QPtrList<TorrentInterface>::const_iterator i = downloads.begin();
		while (i != downloads.end())
		{
			const TorrentInterface* tc = *i;
			const TorrentStats & s = tc->getStats();
			//Out() << "Torrent " << test++ << s.torrent_name << " priority: " << tc->getPriority() << endl;
			if (s.running)
			{
				if(onlyDownload)
				{
					if(!s.completed) nr++;
				}
				else
				{
					if(onlySeed)
					{
						if(s.completed) nr++;
					}
					else
						nr++;
				}
			}
			i++;
		}
	//	Out() << endl;
		return nr;
	}

	QPtrList<kt::TorrentInterface>::iterator QueueManager::begin()
	{
		return downloads.begin();
	}

	QPtrList<kt::TorrentInterface>::iterator QueueManager::end()
	{
		return downloads.end();
	}

	void QueueManager::setMaxDownloads(int m)
	{
		max_downloads = m;
	}

	void QueueManager::setMaxSeeds(int m)
	{
		max_seeds = m;
	}
	
	void QueueManager::setKeepSeeding(bool ks)
	{
		keep_seeding = ks;
	}
	
	bool QueueManager::allreadyLoaded(const SHA1Hash & ih) const
	{
		QPtrList<kt::TorrentInterface>::const_iterator itr = downloads.begin();
		while (itr != downloads.end())
		{
			const TorrentControl* tor = (const TorrentControl*)(*itr);
			if (tor->getTorrent().getInfoHash() == ih)
				return true;
			itr++;
		}
		return false;
	}
	
	void QueueManager::orderQueue()
	{
		if(!downloads.count())
			return;
		
		downloads.sort();
		
		int user_downloading = 0;
		int user_seeding = 0;
		
		QPtrList<TorrentInterface>::const_iterator it = downloads.begin();
		
		// 2. Count user started torrents
		for( it=downloads.begin(); it!=downloads.end(); ++it)
		{
			TorrentInterface* tc = *it;
			const TorrentStats & s = tc->getStats();
			
			if(s.running && s.user_controlled)
			{
				if(s.completed)
					user_seeding++;
				else
					user_downloading++;
			}
		}
		
		// 3. Set max possible started torrents for QM
		int qm_downloads = max_downloads != 0 ? max_downloads - user_downloading : downloads.count();
		int qm_seeds = max_seeds != 0 ? max_seeds - user_seeding : downloads.count();
		
		// 4. Speed&Safety check.
		if(qm_downloads < 0 && qm_seeds < 0)
			return;
		
		// 5. Set the counter to the torrent after which all torrents should be stopped
		int counter = 0;
		int seed_counter = 0;
		int download_counter = 0;
		QPtrList<TorrentInterface>::const_iterator boundary = downloads.begin();
		if(max_downloads < max_seeds)
			for(int i=0;boundary != downloads.end() && i<max_downloads; ++boundary, ++i);
		else
			for(int i=0;boundary != downloads.end() && i<max_seeds; ++boundary, ++i);
		
		for(it=downloads.begin(); it!=boundary && it!=downloads.end(); ++counter, ++it)
		{
			TorrentInterface* tc = *it;		
			const TorrentStats & s = tc->getStats();
			
			if(s.completed)
				++seed_counter;
			else
				++download_counter;
		}
		
		// 6. Stop all torrents from 'counter' to the end
		for( ; it != downloads.end(); ++counter, ++it)
		{
			TorrentInterface* tc = *it;
			const TorrentStats & s = tc->getStats();
			
			if(s.running)
			{	
				if( (s.completed && seed_counter >= max_seeds && max_seeds) || (!s.completed && download_counter >= max_downloads && max_downloads))
					stop(tc);
			}
		
			if(s.completed)
				++seed_counter;
			else
				++download_counter;
		}
		
		// 7. Start torrents that should be running
		counter = 0;
		for(it=downloads.begin(); it!=downloads.end() && (qm_downloads > 0 || qm_seeds > 0); ++counter, ++it)
		{
			TorrentInterface* tc = *it;
			const TorrentStats & s = tc->getStats();
			
			if(counter >= user_downloading && (qm_downloads > 0) && !s.completed && !s.user_controlled)
			{
				if(!s.running)
					start(tc);
				qm_downloads--;
			}
			else if(counter >= user_seeding && (qm_seeds > 0) && s.completed && !s.user_controlled)
			{
				if(!s.running)
					start(tc);
				qm_seeds--;
			}
		}
	}
	
	void QueueManager::torrentFinished(kt::TorrentInterface* tc)
	{
		//dequeue this tc
		tc->setPriority(0);
		//make sure the max_seeds is not reached
		if(max_seeds !=0 && max_seeds < getNumRunning(false,true))
			tc->stop(true);
		
		orderQueue();
	}
	
	void QueueManager::torrentAdded(kt::TorrentInterface* tc)
	{
		QPtrList<TorrentInterface>::const_iterator it = downloads.begin();
		while (it != downloads.end())
		{
			TorrentInterface* _tc = *it;
			int p = _tc->getPriority();
			if(p==0)
				break;
			else
				_tc->setPriority(++p);
			
			++it;
		}
		tc->setPriority(1);
		orderQueue();
	}
	
	void QueueManager::torrentRemoved(kt::TorrentInterface* tc)
	{
		remove(tc);
		orderQueue();
	}
	
	/////////////////////////////////////////////////////////////////////////////////////////////

	
	QueuePtrList::QueuePtrList() : QPtrList<kt::TorrentInterface>()
	{}
	
	QueuePtrList::~QueuePtrList()
	{}
	
	int QueuePtrList::compareItems(QPtrCollection::Item item1, QPtrCollection::Item item2)
	{
		kt::TorrentInterface* tc1 = (kt::TorrentInterface*) item1;
		kt::TorrentInterface* tc2 = (kt::TorrentInterface*) item2;
		
		if(tc1->getPriority() == tc2->getPriority())
			return 0;
		
		if(tc1->getPriority() == 0 && tc2->getPriority() != 0)
			return 1;
		else if(tc1->getPriority() != 0 && tc2->getPriority() == 0)
			return -1;
		
		return tc1->getPriority() > tc2->getPriority() ? -1 : 1;
		return 0;
	}
}
