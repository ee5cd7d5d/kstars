/*
    Copyright (C) 2010 Henry de Valence <hdevalence@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include "projector.h"

#include <math.h>

#include "ksutils.h"
#include "kstarsdata.h"
#include "skycomponents/skylabeler.h"

namespace {
    void toXYZ(SkyPoint* p, double *x, double *y, double *z) {
        double sinRa, sinDec, cosRa, cosDec;

        p->ra().SinCos(  sinRa,  cosRa );
        p->dec().SinCos( sinDec, cosDec );
        *x = cosDec * cosRa;
        *y = cosDec * sinRa;
        *z = sinDec;
    }

    inline SkyPoint pointAt(double az, KStarsData* data) {
        SkyPoint p;
        p.setAz( az );
        p.setAlt( 0.0 );
        p.HorizontalToEquatorial( data->lst(), data->geo()->lat() );
        return p;
    }
}

Projector::Projector(const ViewParams& p)
{
    m_data = KStarsData::Instance();
    setViewParams(p);
}

Projector::~Projector()
{

}

void Projector::setViewParams(const ViewParams& p)
{
    m_vp = p;

    /** Precompute cached values */
    //Find Sin/Cos for focus point
    m_sinY0 = 0;
    m_cosY0 = 0;
    if( m_vp.useAltAz ) {
        m_vp.focus->alt().SinCos( m_sinY0, m_cosY0 );
    } else {
        m_vp.focus->dec().SinCos( m_sinY0, m_cosY0 );
    }
    //Find FOV in radians
    m_fov = sqrt( m_vp.width*m_vp.width + m_vp.height*m_vp.height )
                    / ( 2 * m_vp.zoomFactor * dms::DegToRad );
    //Set checkVisibility variables
    double Ymax;
    if ( m_vp.useAltAz ) {
        m_xrange = 1.2*m_fov/cos( m_vp.focus->alt().radians() );
        Ymax = fabs( m_vp.focus->alt().Degrees() ) + m_fov;
    } else {
        m_xrange = 1.2*m_fov/cos( m_vp.focus->dec().radians() );
        Ymax = fabs( m_vp.focus->dec().Degrees() ) + m_fov;
    }
    m_isPoleVisible = (Ymax >= 90.0);
}

double Projector::fov() const
{
    return m_fov;
}

QPointF Projector::toScreen(SkyPoint* o, bool oRefract, bool* onVisibleHemisphere) const
{
    return KSUtils::vecToPoint( toScreenVec(o, oRefract, onVisibleHemisphere) );
}

bool Projector::onScreen(const QPointF& p) const
{
    return (0 <= p.x() && p.x() <= m_vp.width &&
            0 <= p.y() && p.y() <= m_vp.height);
}

bool Projector::onScreen(const Vector2f& p) const
{
    return (0 <= p.x() && p.x() <= m_vp.width &&
            0 <= p.y() && p.y() <= m_vp.height);
}

QPointF Projector::clipLine( SkyPoint *p1, SkyPoint *p2 ) const
{
    return KSUtils::vecToPoint( clipLineVec(p1,p2));
}

Vector2f Projector::clipLineVec( SkyPoint *p1, SkyPoint *p2 ) const
{
    /* ASSUMES p1 was not clipped but p2 was.
     * Return the QPoint that barely clips in the line twixt p1 and p2.
     */
    //TODO: iteration = ceil( 0.5*log2( w^2 + h^2) )??
    //      also possibly rewrite this
    //     --hdevalence
    int iteration = 15;          // For "perfect" clipping:
    // 2^interations should be >= max pixels/line
    bool isVisible = true;       // so we start at midpoint
    SkyPoint mid;
    Vector2f oMid;
    double x, y, z, dx, dy, dz, ra, dec;
    int newx, newy, oldx, oldy;
    oldx = oldy = -10000;        // any old value that is not the first omid

    toXYZ( p1, &x, &y, &z );
    // -jbb printf("\np1: %6.4f %6.4f %6.4f\n", x, y, z);

    toXYZ( p2, &dx, &dy, &dz );

    // -jbb printf("p2: %6.4f %6.4f %6.4f\n", dx, dy, dz);
    dx -= x;
    dy -= y;
    dz -= z;
    // Successive approximation to point on line that just clips.
    while(iteration-- > 0) {
        dx *= .5;
        dy *= .5;
        dz *= .5;
        if ( ! isVisible ) {              // move back toward visible p1
            x -= dx;
            y -= dy;
            z -= dz;
        }
        else {                        // move out toward clipped p2
            x += dx;
            y += dy;
            z += dz;
        }

        // -jbb printf("  : %6.4f %6.4f %6.4f\n", x, y, z);
        // [x, y, z] => [ra, dec]
        ra = atan2( y, x );
        dec = asin( z / sqrt(x*x + y*y + z*z) );

        mid = SkyPoint( ra * 12. / dms::PI, dec * 180. / dms::PI );
        mid.EquatorialToHorizontal( m_data->lst(), m_data->geo()->lat() );

        oMid = toScreenVec( &mid, false, &isVisible );
        newx = (int) oMid.x();
        newy = (int) oMid.y();

        // -jbb printf("new x/y: %4d %4d", newx, newy);
        if ( (oldx == newx) && (oldy == newy) ) {
            break;
        }
        oldx = newx;
        oldy = newy;
    }
    return  oMid;
}

bool Projector::checkVisibility( SkyPoint *p ) const
{
    //TODO deal with alternate projections
    //not clear how this depends on projection
    //FIXME do these heuristics actually work?
    
    double dX, dY;

    //Skip objects below the horizon if using Horizontal coords,
    //and the ground is drawn
    if( m_vp.useAltAz && m_vp.fillGround && p->alt().Degrees() < -1.0 )
        return false;

    if ( m_vp.useAltAz ) {
        dY = fabs( p->altRefracted().Degrees() - m_vp.focus->alt().Degrees() );
    } else {
        dY = fabs( p->dec().Degrees() - m_vp.focus->dec().Degrees() );
    }
    if( m_isPoleVisible )
        dY *= 0.75; //increase effective FOV when pole visible.
    if( dY > m_fov )
        return false;
    if( m_isPoleVisible )
        return true;

    if ( m_vp.useAltAz ) {
        dX = fabs( p->az().Degrees() - m_vp.focus->az().Degrees() );
    } else {
        dX = fabs( p->ra().Degrees() - m_vp.focus->ra().Degrees() );
    }
    if ( dX > 180.0 )
        dX = 360.0 - dX; // take shorter distance around sky

    return dX < m_xrange;
}

double Projector::findPA( SkyObject *o, float x, float y ) const
{
    //Find position angle of North using a test point displaced to the north
    //displace by 100/zoomFactor radians (so distance is always 100 pixels)
    //this is 5730/zoomFactor degrees
    KStarsData *data = KStarsData::Instance();
    double newDec = o->dec().Degrees() + 5730.0/m_vp.zoomFactor;
    if ( newDec > 90.0 )
        newDec = 90.0;
    SkyPoint test( o->ra().Hours(), newDec );
    if ( m_vp.useAltAz )
        test.EquatorialToHorizontal( data->lst(), data->geo()->lat() );
    Vector2f t = toScreenVec( &test );
    float dx = t.x() - x;
    float dy = y - t.y(); //backwards because QWidget Y-axis increases to the bottom
    float north;
    if ( dy ) {
        north = atan2f( dx, dy )*180.0/dms::PI;
    } else {
        north = (dx > 0.0 ? -90.0 : 90.0);
    }

    return ( north + o->pa() );
}

QVector< Vector2f > Projector::groundPoly(SkyPoint* labelpoint, bool *drawLabel) const
{
    KStarsData *data = KStarsData::Instance();
    QVector<Vector2f> ground;
    
    static const QString horizonLabel = i18n("Horizon");
    float marginLeft, marginRight, marginTop, marginBot;
    SkyLabeler::Instance()->getMargins( horizonLabel, &marginLeft, &marginRight,
                                        &marginTop, &marginBot );

    //daz is 1/2 the width of the sky in degrees
    double daz = 90.;
    if ( m_vp.useAltAz ) {
        daz = 0.5*m_vp.width*57.3/m_vp.zoomFactor; //center to edge, in degrees
        if ( type() == SkyMap::Orthographic ) {
            daz = daz * 1.4;
        }
        daz = qMin(90.0, daz);
    }

    double faz = m_vp.focus->az().Degrees();
    double az1 = faz -daz;
    double az2 = faz +daz;

    bool allGround = true;
    bool allSky = true;

    double inc = 1.0;
    //Add points along horizon
    for(double az = az1; az <= az2 + inc; az += inc) {
        SkyPoint p = pointAt(az,data);
        bool visible = false;
        Vector2f o = toScreenVec(&p, false, &visible);
        if( visible ) {
            ground.append( o );
            //Set the label point if this point is onscreen
            if ( labelpoint && o.x() < marginRight && o.y() > marginTop && o.y() < marginBot )
                *labelpoint = p;
            
            if ( o.y() > 0. ) allGround = false;
            if ( o.y() < m_vp.height ) allSky = false;
        }
    }

    if( allSky ) {
        if( drawLabel)
            *drawLabel = false;
        return QVector<Vector2f>();
    }

    if( allGround ) {
        ground.clear();
        ground.append( Vector2f( -10., -10. ) );
        ground.append( Vector2f( m_vp.width +10., -10. ) );
        ground.append( Vector2f( m_vp.width +10., m_vp.height +10. ) );
        ground.append( Vector2f( -10., m_vp.height +10. ) );
        if( drawLabel)
            *drawLabel = false;
        return ground;
    }

    //In Gnomonic projection, or if sufficiently zoomed in, we can complete
    //the ground polygon by simply adding offscreen points
    if ( daz < 25.0 || type() == SkyMap::Gnomonic ) {
        ground.append( Vector2f( m_vp.width + 10.f, ground.last().y() ) );
        ground.append( Vector2f( m_vp.width + 10.f, m_vp.height + 10.f ) );
        ground.append( Vector2f( -10.f, m_vp.height + 10.f ) );
        ground.append( Vector2f( -10.f, ground.first().y() ) );
    } else {
        double r = radius();
        double t1 = atan2( -1.*(ground.last().y() - 0.5*m_vp.height), ground.last().x() - 0.5*m_vp.width )/dms::DegToRad;
        double t2 = t1 - 180.;
        for ( double t=t1; t >= t2; t -= inc ) {  //step along circumference
            dms a( t );
            double sa(0.), ca(0.);
            a.SinCos( sa, ca );
            ground.append( Vector2f( 0.5*m_vp.width + r*ca, 0.5*m_vp.height - r*sa) );
        }
    }

    if( drawLabel)
        *drawLabel = true;
    return ground;
}
