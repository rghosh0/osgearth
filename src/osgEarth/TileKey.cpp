/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
 * Copyright 2019 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <osgEarth/TileKey>

using namespace osgEarth;

//------------------------------------------------------------------------

TileKey TileKey::INVALID( 0, 0, 0, 0L );

//------------------------------------------------------------------------

TileKey::TileKey(unsigned int lod, unsigned int tile_x, unsigned int tile_y, const Profile* profile)
{
    _x = tile_x;
    _y = tile_y;
    _lod = lod;
    _profile = profile;

    double width, height;
    if ( _profile.valid() )
    {
        _profile->getTileDimensions(lod, width, height);

        double xmin = _profile->getExtent().xMin() + (width * (double)_x);
        double ymax = _profile->getExtent().yMax() - (height * (double)_y);
        double xmax = xmin + width;
        double ymin = ymax - height;

        _extent = GeoExtent( _profile->getSRS(), xmin, ymin, xmax, ymax );

        _key = Stringify() << _lod << "/" << _x << "/" << _y;
        setBands( 0, 0, 0, 0);
    }
    else
    {
        _extent = GeoExtent::INVALID;
        _key = "invalid";
        _keyFull = "invalid";
    }
}

TileKey::TileKey( const TileKey& rhs ) :
_key( rhs._key ),
_keyFull( rhs._keyFull ),
_lod(rhs._lod),
_x(rhs._x),
_y(rhs._y),
_rBand(rhs._rBand),
_gBand(rhs._gBand),
_bBand(rhs._bBand),
_aBand(rhs._aBand),
_profile( rhs._profile.get() ),
_extent( rhs._extent )
{
    //NOP
}


void
TileKey::setBands( unsigned int rBand, unsigned int gBand, unsigned int bBand, unsigned int aBand )
{
    _rBand = rBand;
    _gBand = gBand;
    _bBand = bBand;
    _aBand = aBand;

    _keyFull = Stringify() << _lod << "/" << _x << "/" << _y << "/"
                       << _rBand << "/" << _gBand << "/" << _bBand << "/" << _aBand;
}

const Profile*
TileKey::getProfile() const
{
    return _profile.get();
}

void
TileKey::getTileXY(unsigned int& out_tile_x,
                   unsigned int& out_tile_y) const
{
    out_tile_x = _x;
    out_tile_y = _y;
}

void
TileKey::getTileRGBA(
    unsigned int& out_tile_r_band,
    unsigned int& out_tile_g_band,
    unsigned int& out_tile_b_band,
    unsigned int& out_tile_a_band) const
{
    out_tile_r_band = _rBand;
    out_tile_g_band = _gBand;
    out_tile_b_band = _bBand;
    out_tile_a_band = _aBand;
}

unsigned
TileKey::getQuadrant() const
{
    if ( _lod == 0 )
        return 0;
    bool xeven = (_x & 1) == 0;
    bool yeven = (_y & 1) == 0;
    return 
        xeven && yeven ? 0 :
        xeven          ? 2 :
        yeven          ? 1 : 3;
}

void
TileKey::getPixelExtents(unsigned int& xmin,
                         unsigned int& ymin,
                         unsigned int& xmax,
                         unsigned int& ymax,
                         const unsigned int &tile_size) const
{
    xmin = _x * tile_size;
    ymin = _y * tile_size;
    xmax = xmin + tile_size;
    ymax = ymin + tile_size; 
}

TileKey
TileKey::createChildKey( unsigned int quadrant ) const
{
    unsigned int lod = _lod + 1;
    unsigned int x = _x * 2;
    unsigned int y = _y * 2;

    if (quadrant == 1)
    {
        x+=1;
    }
    else if (quadrant == 2)
    {
        y+=1;
    }
    else if (quadrant == 3)
    {
        x+=1;
        y+=1;
    }
    return TileKey( lod, x, y, _profile.get());
}

void
TileKey::setupNextAvailableBands( int nbBands )
{
    // check if last band is used
    unsigned int uNbBands = static_cast<unsigned int>(nbBands);
    if ( _rBand == uNbBands || _gBand == uNbBands || _bBand == uNbBands || _aBand == uNbBands)
    {
        setBands( 0, 0, 0, 0);
    }

    // band 'a' is considered the last used band
    else
    {
        unsigned int rBand = _aBand < uNbBands ? _aBand+1 : 0;
        unsigned int gBand = rBand < uNbBands && rBand != 0 ? rBand+1 : 0;
        unsigned int bBand = gBand < uNbBands && gBand != 0 ? gBand+1 : 0;
        unsigned int aBand = bBand < uNbBands && bBand != 0 ? bBand+1 : 0;
        setBands( rBand, gBand, bBand, aBand);
    }
}

TileKey
TileKey::createParentKey() const
{
    if (_lod == 0) return TileKey::INVALID;

    unsigned int lod = _lod - 1;
    unsigned int x = _x / 2;
    unsigned int y = _y / 2;
    return TileKey( lod, x, y, _profile.get());
}

TileKey
TileKey::createAncestorKey( int ancestorLod ) const
{
    if ( ancestorLod > (int)_lod ) return TileKey::INVALID;

    unsigned int x = _x, y = _y;
    for( int i=_lod; i > ancestorLod; i-- )
    {
        x /= 2;
        y /= 2;
    }
    return TileKey( ancestorLod, x, y, _profile.get() );
}

TileKey
TileKey::createNeighborKey( int xoffset, int yoffset ) const
{
    unsigned tx, ty;
    getProfile()->getNumTiles( _lod, tx, ty );

    int sx = (int)_x + xoffset;
    unsigned x =
        sx < 0        ? (unsigned)((int)tx + sx) :
        sx >= (int)tx ? (unsigned)sx - tx :
        (unsigned)sx;

    int sy = (int)_y + yoffset;
    unsigned y =
        sy < 0        ? (unsigned)((int)ty + sy) :
        sy >= (int)ty ? (unsigned)sy - ty :
        (unsigned)sy;

    //OE_NOTICE << "Returning neighbor " << x << ", " << y << " for tile " << str() << " offset=" << xoffset << ", " << yoffset << std::endl;

    return TileKey( _lod, x, y, _profile.get() );
}

namespace
{
    int nextPowerOf2(int x) {
        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        return x+1;
    }
}

TileKey
TileKey::mapResolution(unsigned targetSize,
                       unsigned sourceSize,
                       unsigned minimumLOD) const
{
    // This only works when falling back; i.e. targetSize is smaller than sourceSize.
    if ( getLOD() == 0 || targetSize >= sourceSize )
        return *this;

    // Minimum target tile size.
    if ( targetSize < 2 )
        targetSize = 2;

    int lod = (int)getLOD();
    int targetSizePOT = nextPowerOf2((int)targetSize);

    while(true)
    {
        if (targetSizePOT >= (int)sourceSize)
        {
            return createAncestorKey(lod);
        }

        if ( lod == (int)minimumLOD )
        {
            return createAncestorKey(lod);
        }

        lod--;
        targetSizePOT *= 2;        
    }
}
