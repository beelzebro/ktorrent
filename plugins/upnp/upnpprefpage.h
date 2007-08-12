/***************************************************************************
 *   Copyright (C) 2005-2007 by Joris Guisson                              *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 ***************************************************************************/
#ifndef KTUPNPPREFPAGE_H
#define KTUPNPPREFPAGE_H


#include <qmap.h>
#include <interfaces/prefpageinterface.h>
#include "ui_upnpwidget.h"
#include "upnprouter.h"

namespace bt
{
	class WaitJob;
}

namespace kt
{
	class UPnPMCastSocket;

	/**
	 * @author Joris Guisson
	 * 
	 * Page in the preference dialog for the UPnP plugin.
	 */
	class UPnPPrefPage : public PrefPageInterface,public Ui_UPnPWidget,public net::PortListener
	{
		Q_OBJECT

		UPnPMCastSocket* sock;
	public:
		UPnPPrefPage(UPnPMCastSocket* sock,QWidget* parent);
		virtual ~UPnPPrefPage();

		virtual void loadSettings();
		virtual void loadDefaults();
		void shutdown(bt::WaitJob* job);

	public slots:
		/**
		 * Add a device to the list. 
		 * @param r The device
		 */
		void addDevice(kt::UPnPRouter* r);
	
	protected slots:
		void onForwardBtnClicked();
		void onUndoForwardBtnClicked();
		void onRescanClicked();
		void updatePortMappings();
		
	private:
		virtual void portAdded(const net::Port & port);
		virtual void portRemoved(const net::Port & port);
		
	private:
		QMap<QTreeWidgetItem*,UPnPRouter*> itemmap;
		UPnPRouter* def_router;
	};
}

#endif
