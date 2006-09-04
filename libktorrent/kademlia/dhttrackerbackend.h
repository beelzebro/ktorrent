/***************************************************************************
 *   Copyright (C) 2005 by Joris Guisson                                   *
 *   joris.guisson@gmail.com                                               *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ***************************************************************************/
#ifndef DHTDHTTRACKERBACKEND_H
#define DHTDHTTRACKERBACKEND_H

#include <qtimer.h>
#include <interfaces/peersource.h>
#include "task.h"

namespace kt
{
	class TorrentInterface;
}


namespace dht
{
	class DHTBase;
	class AnnounceTask;
	

	/**
		@author Joris Guisson <joris.guisson@gmail.com>
	*/
	class DHTTrackerBackend : public kt::PeerSource,public TaskListener
	{
		Q_OBJECT
	public:
		DHTTrackerBackend(DHTBase & dh_table,kt::TorrentInterface* tor);
		virtual ~DHTTrackerBackend();

		virtual void start();
		virtual void stop();
		virtual void manualUpdate();
		virtual void onFinished(Task* t);
		virtual void onDataReady(Task* t);
		virtual void onDestroyed(Task* t);
	
	private slots:
		void onTimeout();
		bool doRequest();
		
		
	private:
		DHTBase & dh_table;
		AnnounceTask* curr_task;
		kt::TorrentInterface* tor;
		QTimer timer;
		bool started;
	};

}

#endif
