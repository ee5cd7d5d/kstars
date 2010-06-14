/***************************************************************************
                          starcomponent.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2005/14/08
    copyright            : (C) 2005 by Thomas Kabelmann
    email                : thomas.kabelmann@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "deepstarcomponent.h"

#include <QPixmap>
#include <QPainter>

#include <QRectF>
#include <QFontMetricsF>
#include <kglobal.h>

#include "Options.h"
#include "kstarsdata.h"
#include "skymap.h"
#include "skyobjects/starobject.h"
#include "skymesh.h"
#include "binfilehelper.h"
#include "starblockfactory.h"
#include "starcomponent.h"

#include "skypainter.h"
#include "dirtyuglyhack.h"

#include <kde_file.h>
#include "byteorder.h"

DeepStarComponent::DeepStarComponent( SkyComposite *parent, QString fileName, float trigMag, bool staticstars ) :
    ListComponent(parent),
    m_reindexNum( J2000 ),
    triggerMag( trigMag ),
    m_FaintMagnitude(-5.0), 
    staticStars( staticstars ),
    dataFileName( fileName )
{
    fileOpened = false;
    openDataFile();
    if( staticStars )
        loadStaticStars();
    kDebug() << "Loaded catalog file " << dataFileName << "(hopefully)";
}

bool DeepStarComponent::loadStaticStars() {
    FILE *dataFile;

    if( !staticStars )
        return true;
    if( !fileOpened )
        return false;

    dataFile = starReader.getFileHandle();
    rewind( dataFile );

    if( !starReader.readHeader() ) {
        kDebug() << "Error reading header of catalog file " << dataFileName << ": " << starReader.getErrorNumber() << ": " << starReader.getError() << endl;
        return false;
    }

    if( starReader.guessRecordSize() != 16 && starReader.guessRecordSize() != 32 ) {
        kDebug() << "Cannot understand catalog file " << dataFileName << endl;
        return false;
    }

    KDE_fseek(dataFile, starReader.getDataOffset(), SEEK_SET);

    qint16 faintmag;
    quint8 htm_level;
    quint16 t_MSpT;

    fread( &faintmag, 2, 1, dataFile );
    if( starReader.getByteSwap() )
        faintmag = bswap_16( faintmag );
    fread( &htm_level, 1, 1, dataFile );
    fread( &t_MSpT, 2, 1, dataFile ); // Unused
    if( starReader.getByteSwap() )
        faintmag = bswap_16( faintmag );


    // TODO: Read the multiplying factor from the dataFile
    m_FaintMagnitude = faintmag / 100.0;

    if( htm_level != m_skyMesh->level() )
        kDebug() << "WARNING: HTM Level in shallow star data file and HTM Level in m_skyMesh do not match. EXPECT TROUBLE" << endl;

    for(Trixel i = 0; i < m_skyMesh->size(); ++i) {

        Trixel trixel = i;
        StarBlock *SB = new StarBlock( starReader.getRecordCount( i ) );
        if( !SB )
            kDebug() << "ERROR: Could not allocate new StarBlock to hold shallow unnamed stars for trixel " << trixel << endl;
        m_starBlockList.at( trixel )->setStaticBlock( SB );
        
        for(unsigned long j = 0; j < (unsigned long) starReader.getRecordCount(i); ++j) {
            bool fread_success;
            if( starReader.guessRecordSize() == 32 )
                fread_success = fread( &stardata, sizeof( starData ), 1, dataFile );
            else if( starReader.guessRecordSize() == 16 )
                fread_success = fread( &deepstardata, sizeof( deepStarData ), 1, dataFile );

            if( !fread_success ) {
                kDebug() << "ERROR: Could not read starData structure for star #" << j << " under trixel #" << trixel << endl;
            }

            /* Swap Bytes when required */            
            if( starReader.getByteSwap() ) {
                if( starReader.guessRecordSize() == 32 )
                    byteSwap( &stardata );
                else
                    byteSwap( &deepstardata );
            }

            /* Initialize star with data just read. */
            StarObject* star;
            if( starReader.guessRecordSize() == 32 )
                star = SB->addStar( stardata );
            else
                star = SB->addStar( deepstardata );
            
            if( star ) {
                KStarsData* data = KStarsData::Instance();
                star->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
                if( star->getHDIndex() != 0 )
                    m_CatalogNumber.insert( star->getHDIndex(), star );
            } else {
                kDebug() << "CODE ERROR: More unnamed static stars in trixel " << trixel << " than we allocated space for!" << endl;
            }
        }
    }

    return true;
}

DeepStarComponent::~DeepStarComponent() {
  if( fileOpened )
    starReader.closeFile();
  fileOpened = false;
}

bool DeepStarComponent::selected() {
    return Options::showStars() && fileOpened;
}

bool openIndexFile( ) {
    // TODO: Work out the details
    /*
    if( hdidxReader.openFile( "Henry-Draper.idx" ) )
        kDebug() << "Could not open HD Index file. Search by HD numbers for deep stars will not work." << endl;
    */
    return 0;
}

//This function is empty for a reason; we override the normal 
//update function in favor of JiT updates for stars.
void DeepStarComponent::update( KSNumbers * )
{}

// TODO: Optimize draw, if it is worth it.
void DeepStarComponent::draw( QPainter& psky ) {
    if ( !fileOpened ) return;

    SkyPainter *skyp = DirtyUglyHack::painter();
    SkyMap *map = SkyMap::Instance();
    KStarsData* data = KStarsData::Instance();
    UpdateID updateID = data->updateID();

    float radius = map->fov();
    if ( radius > 90.0 ) radius = 90.0;

    if ( m_skyMesh != SkyMesh::Instance() && m_skyMesh->inDraw() ) {
        printf("Warning: aborting concurrent DeepStarComponent::draw()");
    }
    bool checkSlewing = ( map->isSlewing() && Options::hideOnSlew() );

    //shortcuts to inform whether to draw different objects
    bool hideFaintStars( checkSlewing && Options::hideStars() );
    double hideStarsMag = Options::magLimitHideStar();

    //adjust maglimit for ZoomLevel
//    double lgmin = log10(MINZOOM);
//    double lgmax = log10(MAXZOOM);
//    double lgz = log10(Options::zoomFactor());
    // TODO: Enable hiding of faint stars

    float maglim = StarComponent::zoomMagnitudeLimit();

    if( maglim < triggerMag )
        return;

    m_zoomMagLimit = maglim;

    m_skyMesh->inDraw( true );

    SkyPoint* focus = map->focus();
    m_skyMesh->aperture( focus, radius + 1.0, DRAW_BUF ); // divide by 2 for testing

    MeshIterator region(m_skyMesh, DRAW_BUF);

    magLim = maglim;

    StarBlockFactory *m_StarBlockFactory = StarBlockFactory::Instance();
    //    m_StarBlockFactory->drawID = m_skyMesh->drawID();
    //    kDebug() << "Mesh size = " << m_skyMesh->size() << "; drawID = " << m_skyMesh->drawID();
    QTime t;
    int nTrixels = 0;

    t_dynamicLoad = 0;
    t_updateCache = 0;
    t_drawUnnamed = 0;

    visibleStarCount = 0;

    t.start();

    // Mark used blocks in the LRU Cache. Not required for static stars
    if( !staticStars ) {
        while( region.hasNext() ) {
            Trixel currentRegion = region.next();
            for( int i = 0; i < m_starBlockList.at( currentRegion )->getBlockCount(); ++i ) {
                StarBlock *prevBlock = ( ( i >= 1 ) ? m_starBlockList.at( currentRegion )->block( i - 1 ) : NULL );
                StarBlock *block = m_starBlockList.at( currentRegion )->block( i );
                
                if( i == 0  &&  !m_StarBlockFactory->markFirst( block ) )
                    kDebug() << "markFirst failed in trixel" << currentRegion;
                if( i > 0   &&  !m_StarBlockFactory->markNext( prevBlock, block ) )
                    kDebug() << "markNext failed in trixel" << currentRegion << "while marking block" << i;
                if( i < m_starBlockList.at( currentRegion )->getBlockCount() 
                    && m_starBlockList.at( currentRegion )->block( i )->getFaintMag() < maglim )
                    break;
                    
            }
        }
        t_updateCache = t.restart();
        region.reset();
    }

    while ( region.hasNext() ) {
        ++nTrixels;
        Trixel currentRegion = region.next();

        // NOTE: We are guessing that the last 1.5/16 magnitudes in the catalog are just additions and the star catalog
        //       is actually supposed to reach out continuously enough only to mag m_FaintMagnitude * ( 1 - 1.5/16 )
        // TODO: Is there a better way? We may have to change the magnitude tolerance if the catalog changes
        // Static stars need not execute fillToMag

	if( !staticStars && !m_starBlockList.at( currentRegion )->fillToMag( maglim ) && maglim <= m_FaintMagnitude * ( 1 - 1.5/16 ) ) {
            kDebug() << "SBL::fillToMag( " << maglim << " ) failed for trixel " 
                     << currentRegion << " !"<< endl;
	}

        t_dynamicLoad += t.restart();

        //        kDebug() << "Drawing SBL for trixel " << currentRegion << ", SBL has " 
        //                 <<  m_starBlockList[ currentRegion ]->getBlockCount() << " blocks" << endl;
        for( int i = 0; i < m_starBlockList.at( currentRegion )->getBlockCount(); ++i ) {
            StarBlock *block = m_starBlockList.at( currentRegion )->block( i );
            //            kDebug() << "---> Drawing stars from block " << i << " of trixel " << 
            //                currentRegion << ". SB has " << block->getStarCount() << " stars" << endl;
            for( int j = 0; j < block->getStarCount(); j++ ) {

                StarObject *curStar = block->star( j );

                //                kDebug() << "We claim that he's from trixel " << currentRegion 
                //<< ", and indexStar says he's from " << m_skyMesh->indexStar( curStar );

                if ( curStar->updateID != updateID )
                    curStar->JITupdate( data );

                float mag = curStar->mag();

                if ( mag > maglim || ( hideFaintStars && mag > hideStarsMag ) )
                    break;

                if( skyp->drawStar(curStar, mag, curStar->spchar() ) )
                    visibleStarCount++;
            }
        }

        // DEBUG: Uncomment to identify problems with Star Block Factory / preservation of Magnitude Order in the LRU Cache
        //        verifySBLIntegrity();        
        t_drawUnnamed += t.restart();

    }
    m_skyMesh->inDraw( false );

}

bool DeepStarComponent::openDataFile() {

    if( starReader.getFileHandle() )
        return true;

    starReader.openFile( dataFileName );
    fileOpened = false;
    if( !starReader.getFileHandle() )
        kDebug() << "WARNING: Failed to open deep star catalog " << dataFileName << ". Disabling it." << endl;
    else if( !starReader.readHeader() )
        kDebug() << "WARNING: Header read error for deep star catalog " << dataFileName << "!! Disabling it!" << endl;
    else {
        qint16 faintmag;
        quint8 htm_level;
        fread( &faintmag, 2, 1, starReader.getFileHandle() );
        if( starReader.getByteSwap() )
            faintmag = bswap_16( faintmag );
        if( starReader.guessRecordSize() == 16 )
            m_FaintMagnitude = faintmag / 1000.0;
        else
            m_FaintMagnitude = faintmag / 100.0;
        fread( &htm_level, 1, 1, starReader.getFileHandle() );
        kDebug() << "Processing " << dataFileName << ", HTMesh Level" << htm_level;
        m_skyMesh = SkyMesh::Instance( htm_level );
        if( !m_skyMesh ) {
            if( !( m_skyMesh = SkyMesh::Create( htm_level ) ) ) {
                kDebug() << "Could not create HTMesh of level " << htm_level << " for catalog " << dataFileName << ". Skipping it.";
                return false;
            }
        }
        meshLevel = htm_level;
        fread( &MSpT, 2, 1, starReader.getFileHandle() );
        if( starReader.getByteSwap() )
            MSpT = bswap_16( MSpT );
        fileOpened = true;
        kDebug() << "  Sky Mesh Size: " << m_skyMesh->size();
        for (long int i = 0; i < m_skyMesh->size(); i++) {
            StarBlockList *sbl = new StarBlockList( i, this );
            if( !sbl ) {
                kDebug() << "NULL starBlockList. Expect trouble!";
            }
            m_starBlockList.append( sbl );
        }
        m_zoomMagLimit = 0.06;
    }

    return fileOpened;
}


SkyObject *DeepStarComponent::findByHDIndex( int HDnum ) {
    // Currently, we only handle HD catalog indexes
    return m_CatalogNumber.value( HDnum, NULL ); // TODO: Maybe, make this more general.
}

// This uses the main star index for looking up nearby stars but then
// filters out objects with the generic name "star".  We could easily
// build an index for just the named stars which would make this go
// much faster still.  -jbb
//


SkyObject* DeepStarComponent::objectNearest( SkyPoint *p, double &maxrad )
{
    StarObject *oBest = 0;

    if( !fileOpened )
        return NULL;

    m_skyMesh->index( p, maxrad + 1.0, OBJ_NEAREST_BUF);

    MeshIterator region( m_skyMesh, OBJ_NEAREST_BUF );

    while ( region.hasNext() ) {
        Trixel currentRegion = region.next();
        for( int i = 0; i < m_starBlockList.at( currentRegion )->getBlockCount(); ++i ) {
            StarBlock *block = m_starBlockList.at( currentRegion )->block( i );
            for( int j = 0; j < block->getStarCount(); ++j ) {
                StarObject* star =  block->star( j );
                if( !star ) continue;
                if ( star->mag() > m_zoomMagLimit ) continue;
                
                double r = star->angularDistanceTo( p ).Degrees();
                if ( r < maxrad ) {
                    oBest = star;
                    maxrad = r;
                }
            }
        }
    }

    // TODO: What if we are looking around a point that's not on
    // screen? objectNearest() will need to keep on filling up all
    // trixels around the SkyPoint to find the best match in case it
    // has not been found. Ideally, this should be implemented in a
    // different method and should be called after all other
    // candidates (eg: DeepSkyObject::objectNearest()) have been
    // called.
    
    return oBest;
}

void DeepStarComponent::byteSwap( deepStarData *stardata ) {
    stardata->RA = bswap_32( stardata->RA );
    stardata->Dec = bswap_32( stardata->Dec );
    stardata->dRA = bswap_16( stardata->dRA );
    stardata->dDec = bswap_16( stardata->dDec );
    stardata->B = bswap_16( stardata->B );
    stardata->V = bswap_16( stardata->V );
}

void DeepStarComponent::byteSwap( starData *stardata ) {
    stardata->RA = bswap_32( stardata->RA );
    stardata->Dec = bswap_32( stardata->Dec );
    stardata->dRA = bswap_32( stardata->dRA );
    stardata->dDec = bswap_32( stardata->dDec );
    stardata->parallax = bswap_32( stardata->parallax );
    stardata->HD = bswap_32( stardata->HD );
    stardata->mag = bswap_16( stardata->mag );
    stardata->bv_index = bswap_16( stardata->bv_index );
}

bool DeepStarComponent::verifySBLIntegrity() {
    float faintMag = -5.0;
    bool integrity = true;
    for(Trixel trixel = 0; trixel < m_skyMesh->size(); ++trixel) {
        for(int i = 0; i < m_starBlockList[ trixel ]->getBlockCount(); ++i) {
            StarBlock *block = m_starBlockList[ trixel ]->block( i );
            if( i == 0 )
                faintMag = block->getBrightMag();
            // NOTE: Assumes 2 decimal places in magnitude field. TODO: Change if it ever does change
            if( block->getBrightMag() != faintMag && ( block->getBrightMag() - faintMag ) > 0.5) {
                kDebug() << "Trixel " << trixel << ": ERROR: faintMag of prev block = " << faintMag 
                         << ", brightMag of block #" << i << " = " << block->getBrightMag();
                integrity = false;
            }
            if( i > 1 && ( !block->prev ) )
                kDebug() << "Trixel " << trixel << ": ERROR: Block" << i << "is unlinked in LRU Cache";
            if( block->prev && block->prev->parent == m_starBlockList[ trixel ] 
                && block->prev != m_starBlockList[ trixel ]->block( i - 1 ) ) {
                kDebug() << "Trixel " << trixel << ": ERROR: SBF LRU Cache linked list seems to be broken at before block " << i << endl;
                integrity = false;
            }
            faintMag = block->getFaintMag();
        }
    }
    return integrity;
}
