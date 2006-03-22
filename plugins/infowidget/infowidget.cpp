/***************************************************************************
 *   Copyright (C) 2005 by                                                 *
 *   Joris Guisson <joris.guisson@gmail.com>                               *
 *   Ivan Vasic <ivasic@gmail.com>                                         *
 *   Marcello Maggioni <marcello.maggioni@gmail.com>                       *
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
#include <klistview.h>
#include <kglobal.h>
#include <klocale.h>
#include <kiconloader.h>
#include <kmimetype.h>
#include <kpopupmenu.h>
#include <krun.h>
#include <qlabel.h>
#include <qstring.h>
#include <qcheckbox.h>
#include <qpainter.h>
#include <qtabwidget.h>
#include <qlayout.h>
#include <qlineedit.h>
#include <util/functions.h>
#include <interfaces/functions.h>
#include <util/log.h>
#include <interfaces/torrentfileinterface.h>
#include <interfaces/torrentinterface.h>
#include <torrent/globals.h>
#include "ktorrentmonitor.h"
#include "infowidget.h"
#include "peerview.h"
#include "chunkdownloadview.h"
#include "trackerview.h"
#include "functions.h"
#include "downloadedchunkbar.h"
#include "availabilitychunkbar.h"
#include "iwfiletreeitem.h"
#include "iwfiletreediritem.h"
#include "infowidgetpluginsettings.h"

using namespace bt;
using namespace kt;

namespace kt
{
	
	InfoWidget::InfoWidget(bool seed, QWidget* parent, const char* name, WFlags fl)
		: InfoWidgetBase(parent,name,fl),peer_view(0),cd_view(0), tracker_view(0), m_seed(seed)
	{
		multi_root = 0;
		monitor = 0;
		curr_tc = 0;
	
		KIconLoader* iload = KGlobal::iconLoader();
		context_menu = new KPopupMenu(this);
		preview_id = context_menu->insertItem(iload->loadIconSet("frame_image",KIcon::Small), i18n("Preview"));
		context_menu->setItemEnabled(1, false);
	
		connect(m_file_view,SIGNAL(contextMenu(KListView*, QListViewItem*, const QPoint& )),
				this,SLOT(showContextMenu(KListView*, QListViewItem*, const QPoint& )));
		connect(context_menu, SIGNAL ( activated ( int ) ), this, SLOT ( contextItem ( int ) ) );
		
		setEnabled(false);
		showPeerView( InfoWidgetPluginSettings::showPeerView() );
		showChunkView( InfoWidgetPluginSettings::showChunkView() );
		showTrackerView(InfoWidgetPluginSettings::showTrackersView());
		
		if(!m_seed)
			KGlobal::config()->setGroup("InfoWidget");
		else
			KGlobal::config()->setGroup("SeedInfoWidget");
		
		if (KGlobal::config()->hasKey("InfoWidgetSize"))
		{
			QSize s = KGlobal::config()->readSizeEntry("InfoWidgetSize",0);
			resize(s);
		}
	}
	
	InfoWidget::~InfoWidget()
	{
		if(!m_seed)
			KGlobal::config()->setGroup("InfoWidget");
		else
			KGlobal::config()->setGroup("SeedInfoWidget");
		
		KGlobal::config()->writeEntry("InfoWidgetSize",size());
		if (cd_view)
			cd_view->saveLayout(KGlobal::config(),"ChunkDownloadView");
		if (peer_view)
			peer_view->saveLayout(KGlobal::config(),"PeerView");
		KGlobal::config()->sync();
		delete monitor;
	}
	
	void InfoWidget::showPeerView( bool show )
	{
		if( peer_view == 0 && show)
		{
			peer_page = new QWidget(m_tabs);
			QHBoxLayout* peer_page_layout = new QHBoxLayout(peer_page, 11, 6);
	
			peer_view = new PeerView(peer_page);
			peer_page_layout->add(peer_view);
	
			m_tabs->addTab(peer_page,i18n("Peers"));;
			peer_view->setEnabled(curr_tc != 0);
			setEnabled(curr_tc != 0);
			peer_view->restoreLayout(KGlobal::config(),"PeerView");
		}
		else if (!show && peer_view != 0)
		{
			peer_view->saveLayout(KGlobal::config(),"PeerView");
			m_tabs->removePage( peer_page );
			peer_page->reparent(0,QPoint(),false);
			delete peer_page;
			peer_view = 0;
		}
	
		if (monitor && curr_tc)
		{
			delete monitor;
			monitor = 0;
			if (peer_view)
				peer_view->removeAll();
			if (cd_view)
				cd_view->removeAll();
			if (curr_tc)
				monitor = new KTorrentMonitor(curr_tc,peer_view,cd_view);
		}
	}
	
	void InfoWidget::showChunkView( bool show )
	{
		if( cd_view == 0 && show)
		{
			cd_page = new QWidget(m_tabs);
			QHBoxLayout* cd_page_layout = new QHBoxLayout(cd_page, 11, 6);
	
			cd_view = new ChunkDownloadView(cd_page);
			cd_page_layout->add(cd_view);
	
			m_tabs->addTab(cd_page,i18n("Chunks"));
			cd_view->setEnabled(curr_tc != 0);
			setEnabled(curr_tc != 0);
			cd_view->restoreLayout(KGlobal::config(),"ChunkDownloadView");
		}
		else if (!show && cd_view != 0)
		{
			cd_view->saveLayout(KGlobal::config(),"ChunkDownloadView");
			m_tabs->removePage( cd_page );
			cd_page->reparent(0,QPoint(),false);
			delete cd_page;
			cd_view = 0;
		}
	
		if (monitor && curr_tc)
		{
			delete monitor;
			monitor = 0;
			if (peer_view)
				peer_view->removeAll();
			if (cd_view)
				cd_view->removeAll();
			if (curr_tc)
				monitor = new KTorrentMonitor(curr_tc,peer_view,cd_view);
		}
	}
	
	void InfoWidget::showTrackerView(bool show)
	{
		if( tracker_view == 0 && show)
		{
			tracker_page = new QWidget(m_tabs);
			QHBoxLayout* tracker_page_layout = new QHBoxLayout(tracker_page, 11, 6);
	
			tracker_view = new TrackerView(curr_tc, tracker_page);
			tracker_page_layout->add(tracker_view);
	
			m_tabs->addTab(tracker_page,i18n("Trackers"));;
			tracker_view->setEnabled(curr_tc != 0);
			setEnabled(curr_tc != 0);
// 			tracker_view->restoreLayout(KGlobal::config(),"TrackerView");
		}
		else if (!show && tracker_view != 0)
		{
// 			tracker_view->saveLayout(KGlobal::config(),"TrackerView");
			m_tabs->removePage( tracker_page );
			tracker_page->reparent(0,QPoint(),false);
			delete tracker_page;
			tracker_view = 0;
		}
	}
	
	void InfoWidget::fillFileTree()
	{
		multi_root = 0;
		m_file_view->clear();
	
		if (!curr_tc)
			return;
	
		if (curr_tc->getStats().multi_file_torrent)
		{
			IWFileTreeDirItem* root = new IWFileTreeDirItem(
					m_file_view,curr_tc->getStats().torrent_name);
			
			for (Uint32 i = 0;i < curr_tc->getNumFiles();i++)
			{
				TorrentFileInterface & file = curr_tc->getTorrentFile(i);
				root->insert(file.getPath(),file);
			}
			root->setOpen(true);
			m_file_view->setRootIsDecorated(true);
			multi_root = root;
		}
		else
		{
			const TorrentStats & s = curr_tc->getStats();
			m_file_view->setRootIsDecorated(false);
			KListViewItem* item = new KListViewItem(
					m_file_view,
					s.torrent_name,
					BytesToString(s.total_bytes));
	
			item->setPixmap(0,KMimeType::findByPath(s.torrent_name)->pixmap(KIcon::Small));
		}
	}
	
	void InfoWidget::changeTC(kt::TorrentInterface* tc)
	{
		if (tc == curr_tc)
			return;
	
		curr_tc = tc;
		if (monitor)
		{
/*			if(!curr_tc)
				monitor->destroyed();
			*/
			delete monitor;
			monitor = 0;
			if (peer_view)
				peer_view->removeAll();
			if (cd_view)
				cd_view->removeAll();
		}
	
		if (tc)
			monitor = new KTorrentMonitor(curr_tc,peer_view,cd_view);
	
		fillFileTree();
		if(!m_seed)
		{
			m_chunk_bar->setTC(tc);
			m_av_chunk_bar->setTC(tc);
		}
		setEnabled(tc != 0);
		if (peer_view)
		{
			peer_page->setEnabled(tc != 0);
			peer_view->setEnabled(tc != 0);
		}
		if (cd_view)
		{
			cd_page->setEnabled(tc != 0);
			cd_view->setEnabled(tc != 0);
		}
		if(tracker_view)
		{
			tracker_page->setEnabled(tc != 0);
			tracker_view->setEnabled(tc != 0);
			tracker_view->torrentChanged(tc);
		}
		
		if (curr_tc)
		{
			float ratio = curr_tc->getMaxShareRatio();
			if(ratio > 0)
			{
				useLimit->setChecked(true);
				maxRatio->setText(QString("%1").arg(ratio,0,'f',2));
			}
			else
			{
				maxRatio->setText("0.00");
				useLimit->setChecked(false);
				maxRatio->setEnabled(false);
			}
		}
		else
			maxRatio->clear();
		
		update();
	}
	
	void InfoWidget::readyPercentage()
	{
		if(curr_tc->getStats().multi_file_torrent)
		{
			multi_root->updatePercentageInformation(curr_tc);
		}
		else
		{
			QListViewItemIterator it(m_file_view);
			if (!it.current())
				return;

			Uint32 index, end, total;
			end = curr_tc->downloadedChunksBitSet().getNumBits();
			total = 0;
			for(index = 0; index < end; index++)
			{
				if (curr_tc->downloadedChunksBitSet().get(index))
					total++;
			}
			double percent = (double)total/(double)(end)*100.0;
			if (percent < 0.0)
				percent = 0.0;
			else if (percent > 100.0)
				percent = 100.0;
			KLocale* loc = KGlobal::locale();
			it.current()->setText(4,i18n("%1 %").arg(loc->formatNumber(percent,2)));
			
		}
	}

	void InfoWidget::readyPreview()
	{
		if(curr_tc->getStats().multi_file_torrent)
		{
			multi_root->updatePreviewInformation(curr_tc);
		}
		else
		{
			QListViewItemIterator it(m_file_view);
			if (!it.current())
				return;
			
			if (IsMultimediaFile(curr_tc->getStats().output_path))
			{
				if ( curr_tc->readyForPreview() )
					it.current()->setText(3, i18n("Available"));
				else
					it.current()->setText(3, i18n("Pending"));
			}
			else
				it.current()->setText(3, i18n("No"));
			
		}
	}
	
	void InfoWidget::update()
	{
		if (!curr_tc)
			return;
	
		const TorrentStats & s = curr_tc->getStats();
		m_chunks_downloading->setText(QString::number(s.num_chunks_downloading));
		m_chunks_downloaded->setText(QString::number(s.num_chunks_downloaded));
		m_total_chunks->setText(QString::number(s.total_chunks));
		m_excluded_chunks->setText(QString::number(s.num_chunks_excluded));
		if(!m_seed)
		{
			m_chunk_bar->updateBar();
			m_av_chunk_bar->updateBar();
		}
		
		if( s.chunk_size / 1024 < 1024 )
			m_size_chunks->setText(QString::number(s.chunk_size / 1024) + "." + QString::number((s.chunk_size % 1024) / 100) + " KB");
		else
			m_size_chunks->setText(QString::number(s.chunk_size / 1024 / 1024) + "." + QString::number(((s.chunk_size / 1024) % 1024) / 100) + " MB");
		if (peer_view)
			peer_view->update();
		if (cd_view)
			cd_view->update();
		if(tracker_view)
			tracker_view->update(curr_tc);
		
		if (s.running)
		{
			QTime t;
			t = t.addSecs(curr_tc->getTimeToNextTrackerUpdate());
			m_tracker_update_time->setText(t.toString("mm:ss"));
		}
		else
		{
			m_tracker_update_time->setText("");
		}
		
		m_tracker_status->setText(s.trackerstatus);
		
		m_seeders->setText(QString("%1 (%2)")
				.arg(s.seeders_connected_to).arg(s.seeders_total));
	
		m_leechers->setText(QString("%1 (%2)")
				.arg(s.leechers_connected_to).arg(s.leechers_total));
	
		float ratio = 0.0f;
		if (s.bytes_downloaded > 0)
			ratio = (float) s.bytes_uploaded / (float)s.bytes_downloaded;
		
		if(!maxRatio->hasFocus() && useLimit->isChecked())
			maxRatioUpdate();
		
		m_share_ratio->setText(QString("<font color=\"%1\">%2</font>").arg(ratio <= 0.8 ? "#ff0000" : "#00ff00").arg(KGlobal::locale()->formatNumber(ratio,2)));
	
		Uint32 secs = curr_tc->getRunningTimeUL(); 
		if (secs == 0)
		{
			m_avg_up->setText(KBytesPerSecToString(0));
			m_avg_down->setText(KBytesPerSecToString(0));
		}
		else
		{
			double r = (double)s.bytes_uploaded / 1024.0;
			m_avg_up->setText(KBytesPerSecToString(r / secs));
			r = (double)(s.bytes_downloaded - s.imported_bytes)/ 1024.0;
			secs = curr_tc->getRunningTimeDL();
			m_avg_down->setText(KBytesPerSecToString(r / secs));
		}
		readyPreview();
		readyPercentage();
	}
	
	void InfoWidget::showContextMenu(KListView* ,QListViewItem* item,const QPoint & p)
	{
		const TorrentStats & s = curr_tc->getStats();
		// don't show a menu if item is 0 or if it is a directory
		if (!item || item->childCount() > 0)
			return;
		
		if(s.multi_file_torrent)
		{
			kt::TorrentFileInterface & file = ((FileTreeItem*)item)->getTorrentFile();
			if (!file.isNull() && file.isMultimedia())
			{
				if ( curr_tc->readyForPreview(file.getFirstChunk(), file.getFirstChunk()+1) )
				{
					context_menu->setItemEnabled(preview_id, true);
					this->preview_path = "cache" + bt::DirSeparator() + file.getPath();
				}
				else
					context_menu->setItemEnabled(preview_id, false);
			}
			else
			{
				context_menu->setItemEnabled(preview_id, false);
			}
		}
		else
		{
			if ( curr_tc->readyForPreview() && IsMultimediaFile(s.output_path))
			{
				context_menu->setItemEnabled(preview_id, true);
				preview_path = "cache";
			}
			else
				context_menu->setItemEnabled(preview_id, false);
		}
		context_menu->popup(p);
	}
	
	void InfoWidget::contextItem(int id)
	{
		if(id == this->preview_id)
			new KRun(this->curr_tc->getTorDir()+preview_path, 0, true, true);
	}
	
	void InfoWidget::maxRatio_returnPressed()
	{
		if(!curr_tc)
			return;
		
		bool ok;
		double ratio = maxRatio->text().toFloat(&ok);
		
		if(!ok)
		{
			maxRatio->setText(QString("%1").arg(curr_tc->getMaxShareRatio(),0,'f',2));
			return;
		}
		
		curr_tc->setMaxShareRatio(ratio);
	}
	
	void InfoWidget::useLimit_toggled(bool state)
	{
		if(!curr_tc)
			return;
		
		maxRatio->setEnabled(state);
		if(!state)
		{
			curr_tc->setMaxShareRatio(0.00f);
			maxRatio->setText("0.00");
		}
		else
		{
			if(curr_tc->getMaxShareRatio() == 0.00f)
			{	
				curr_tc->setMaxShareRatio(1.00f);
				maxRatio->setText(QString("%1").arg(1.00f,0,'f',2));
			}
		}
	}
	
	void InfoWidget::maxRatioUpdate()
	{
		if(!curr_tc)
			return;
		
		float ratio = curr_tc->getMaxShareRatio();
		if(ratio > 0)
		{
			maxRatio->setEnabled(true);
			useLimit->setChecked(true);
			maxRatio->setText(QString("%1").arg(ratio,0,'f',2));
		}
		else
		{
			maxRatio->setEnabled(false);
			useLimit->setChecked(false);
			maxRatio->setText("0.00");
		}
	}
}

#include "infowidget.moc"
