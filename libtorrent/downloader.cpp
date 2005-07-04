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
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "file.h"
#include "log.h"
#include "downloader.h"
#include "chunkmanager.h"
#include "torrent.h"
#include "peermanager.h"
#include "error.h"
#include "chunkdownload.h"
#include "sha1hash.h"
#include "peer.h"
#include "piece.h"
#include "peerdownloader.h"
#include "torrentmonitor.h"
#include "packetwriter.h"

namespace bt
{
	
	

	Downloader::Downloader(Torrent & tor,PeerManager & pman,ChunkManager & cman) 
	: tor(tor),pman(pman),cman(cman),downloaded(0),tmon(0)
	{
		Uint32 total = tor.getFileLength();
		downloaded = (total - cman.bytesLeft());
		endgame_mode = false;
		pdowners.setAutoDelete(true);
		current_chunks.setAutoDelete(true);
		connect(&pman,SIGNAL(newPeer(Peer* )),this,SLOT(onNewPeer(Peer* )));
		connect(&pman,SIGNAL(peerKilled(Peer* )),this,SLOT(onPeerKilled(Peer*)));
	}


	Downloader::~Downloader()
	{
	}
	
	void Downloader::pieceRecieved(const Piece & p)
	{
		if (cman.bytesLeft() == 0)
			return;
		
		for (CurChunkItr j = current_chunks.begin();j != current_chunks.end();j++)
		{
			if (p.getIndex() != j->first)
				continue;
			
			ChunkDownload* cd = j->second;
			downloaded += p.getLength();
			if (cd->piece(p))
			{
				finished(cd);
				if (tmon)
					tmon->downloadRemoved(cd);
				current_chunks.erase(p.getIndex());
				return;
			}
		}
	}
	
	void Downloader::update()
	{
		if (cman.bytesLeft() == 0)
			return;
		
		endgame_mode = 
				cman.chunksLeft() <= current_chunks.count() &&
				cman.chunksLeft() < 20;

		if (endgame_mode)
			endgameUpdate();
		else
			normalUpdate();
	}
	
	void Downloader::normalUpdate()
	{
		PtrMap<Peer*,PeerDownloader>::iterator i;
		for (i = pdowners.begin(); i != pdowners.end();i++)
		{
			PeerDownloader* pd = i->second;
			pd->downloadUnsent();
			if (!pd->isNull() && !pd->isChoked())
			{
				if (pd->getNumGrabbed() == 0 || (pd->getNumGrabbed() == 1 && pd->getNumRequests() < 4))
					downloadFrom(pd);
			}
		}
	}
	
	void Downloader::endgameUpdate()
	{
		for (CurChunkItr j = current_chunks.begin();j != current_chunks.end();j++)
		{			
			ChunkDownload* cd = j->second;
			PtrMap<Peer*,PeerDownloader>::iterator i;
			for (i = pdowners.begin(); i != pdowners.end();i++)
			{
				PeerDownloader* pd = i->second;
					
				if (!pd->isNull() && !pd->isChoked() &&
					pd->hasChunk(cd->getChunk()->getIndex()) &&
					pd->getNumRequests() < 8)
				{
					cd->assignPeer(pd,true);
				}
			}
		}
	}
	
	void Downloader::downloadFrom(PeerDownloader* pd)
	{
		// first see if there are ChunkDownload's which need a PeerDownloader
		for (CurChunkItr j = current_chunks.begin();j != current_chunks.end();j++)
		{
			ChunkDownload* cd = j->second;
			bool ok_to_down = pd->hasChunk(cd->getChunk()->getIndex());
			if (!ok_to_down)
				continue;

			// if cd hasn't got a downloader or when the current
			// downloader has snubbed him
			// assign him pd
			const Peer* p = cd->getCurrentPeer();
			if (cd->getNumDownloaders() == 0 || (p && p->isSnubbed()))
			{
				pd->grab();
				cd->assignPeer(pd,false);
				return;
			}
		}
		
		if (current_chunks.count() > 2*pdowners.count())
			return;

		// cap the maximum chunk to download
		// to not get monster writes to the cache file
		Uint32 max_c = cman.getMaxAllowedChunk();
		if (max_c > tor.getNumChunks())
			max_c = tor.getNumChunks();

		// pick a random chunk to download, by picking
		// a random starting value in the range 0 .. max_c
		Uint32 s = rand() % max_c;
		Uint32 i = s; 
		BitSet bs;
		cman.toBitSet(bs);

		do
		{
			// pd has to have the selected chunk
			// and we don't have it
			if (pd->hasChunk(i) && !current_chunks.find(i) && !bs.get(i))
			{
				ChunkDownload* cd = new ChunkDownload(cman.getChunk(i));
				current_chunks.insert(i,cd);
				pd->grab();
				cd->assignPeer(pd,false);
				if (tmon)
					tmon->downloadStarted(cd);
				return;
			}
			i = (i + 1) % max_c;
			// we stop this loop if i becomse equal to it's starting value
			// no infinite loops, thank you
		}while (s != i);
	}
	
	void Downloader::onNewPeer(Peer* peer)
	{
		if (cman.chunksLeft() == 0)
			return;
		
		// add a PeerDownloader for every Peer
		PeerDownloader* pd = new PeerDownloader(peer);
		connect(pd,SIGNAL(downloaded(const Piece& )),
				this,SLOT(pieceRecieved(const Piece& )));
		pdowners.insert(peer,pd);
	}

	void Downloader::onPeerKilled(Peer* peer)
	{
		// Peer killed so remove it's PeerDownloader
		PeerDownloader* pd = pdowners.find(peer);
		if (pd)
		{
			for (CurChunkItr i = current_chunks.begin();i != current_chunks.end();i++)
			{
				ChunkDownload* cd = i->second;
				cd->peerKilled(pd);
			}
			pdowners.erase(peer);
		}
	}

	void Downloader::clearDownloaders()
	{
		if (current_chunks.count() == 0)
			pdowners.clear();
	}
	
	void Downloader::finished(ChunkDownload* cd)
	{
		Chunk* c = cd->getChunk();
		// verify the data
		SHA1Hash h = SHA1Hash::generate(c->getData(),c->getSize());
		if (tor.verifyHash(h,c->getIndex()))
		{
			// hash ok so save it
			try
			{
				cman.saveChunk(c->getIndex());
				Out() << "Chunk " << c->getIndex() << " downloaded " << endl;
				// tell everybody we have the Chunk
				for (Uint32 i = 0;i < pman.getNumConnectedPeers();i++)
				{
					pman.getPeer(i)->getPacketWriter().sendHave(c->getIndex());
				}
			}
			catch (Error & e)
			{
				Out() << "Error " << e.toString() << endl;
			}
		}
		else
		{
			Out() << "Hash verification error on chunk "  << c->getIndex() << endl;
			Out() << "Is        : " << h << endl;
			Out() << "Should be : " << tor.getHash(c->getIndex()) << endl;
		}
		
	}
	
	void Downloader::clearDownloads()
	{
		current_chunks.clear();
	}
	
	Uint32 Downloader::downloadRate() const
	{
		// sum of the download rate of each peer
		Uint32 rate = 0;
		for (Uint32 i = 0;i < pman.getNumConnectedPeers();i++)
		{
			Peer* p = pman.getPeer(i);
			rate += p->getDownloadRate();
		}
		return rate;
	}
	
	void Downloader::setMonitor(TorrentMonitor* tmo)
	{
		tmon = tmo;
		if (!tmon)
			return;

		for (CurChunkItr i = current_chunks.begin();i != current_chunks.end();i++)
		{
			ChunkDownload* cd = i->second;
			tmon->downloadStarted(cd);
		}
	}

	void Downloader::saveDownloads(const QString & file)
	{
		File fptr;
		if (!fptr.open(file,"wb"))
			return;

		// Save all the current downloads to a file
		Uint32 num = current_chunks.count();
		fptr.write(&num,sizeof(Uint32));

		Out() << "Saving " << current_chunks.count() << " chunk downloads" << endl;
		for (CurChunkItr i = current_chunks.begin();i != current_chunks.end();i++)
		{
			Uint32 ch = i->first;
			fptr.write(&ch,sizeof(Uint32));
			ChunkDownload* cd = i->second;
			cd->save(fptr);
		}
	}

	void Downloader::loadDownloads(const QString & file)
	{
		// Load all partial downloads
		File fptr;
		if (!fptr.open(file,"rb"))
			return;

		// recalculate downloaded bytes
		downloaded = (tor.getFileLength() - cman.bytesLeft());

		Uint32 num = 0;
		fptr.read(&num,sizeof(Uint32));

		Out() << "Loading " << num  << " active chunk downloads" << endl;
		for (Uint32 i = 0;i < num;i++)
		{
			Uint32 ch = 0;
			fptr.read(&ch,sizeof(Uint32));
			Out() << "Loading chunk " << ch << endl;
	
			if (!cman.getChunk(ch) || current_chunks.contains(ch))
			{
				Out() << "Illegal chunk " << ch << endl;
				return;
			}
			ChunkDownload* cd = new ChunkDownload(cman.getChunk(ch));
			current_chunks.insert(ch,cd);
			cd->load(fptr);
			downloaded += cd->bytesDownloaded();
			if (tmon)
				tmon->downloadStarted(cd);
		}
	}
}
#include "downloader.moc"
