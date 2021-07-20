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

#include <osgEarth/TileBandKey>

using namespace osgEarth;

//------------------------------------------------------------------------

TileBandKey TileBandKey::INVALID( 0, 0, 0, 0, 0, 0L );

//------------------------------------------------------------------------

void
TileBandKey::makeKey()
{
    // don't use Stringify(), it is too slow (following lines are optimized and tested)
    _key = std::to_string(_lod);
    _key += "/";
    _key += std::to_string(_x);
    _key += "/";
    _key += std::to_string(_y);
    _key += "/";
    _key += std::to_string(_firstBand);
    _key += "/";
    _key += std::to_string(_lastBand);
}

TileBandKey::TileBandKey(unsigned int lod, unsigned int tile_x, unsigned int tile_y,
                         unsigned int first_band, unsigned int last_band, const Profile* profile)
    : TileKey(lod, tile_x, tile_y, profile), _firstBand(first_band), _lastBand(last_band)
{
    if ( _profile.valid() )
        makeKey();
}

TileBandKey::TileBandKey( const TileBandKey& rhs ) :
TileKey( rhs ),
_firstBand(rhs._firstBand),
_lastBand(rhs._lastBand)
{
    if ( _profile.valid() )
        makeKey();
}

void
TileBandKey::getTileBands(unsigned int& out_tile_first_band,
                      unsigned int& out_tile_last_band) const
{
    out_tile_first_band = _firstBand;
    out_tile_last_band = _lastBand;
}

void
TileBandKey::setupNextAvailableBands( unsigned int nbBands, unsigned int maxBandsPerTile )
{
    // check if last band of the tile is also the last band of the raster
    if ( _lastBand == nbBands )
    {
        _firstBand = _lastBand = 0;
        makeKey();
    }

    // setup the next bands
    else
    {
        _firstBand = _lastBand+1;
        _lastBand = osg::minimum(_firstBand+maxBandsPerTile-1, nbBands);
        makeKey();
    }
}
