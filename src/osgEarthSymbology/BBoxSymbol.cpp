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
#include <osgEarthSymbology/BBoxSymbol>
#include <osgEarthSymbology/Style>

using namespace osgEarth;
using namespace osgEarth::Symbology;

OSGEARTH_REGISTER_SIMPLE_SYMBOL(bbox, BBoxSymbol);

BBoxSymbol::BBoxSymbol( const Config& conf ) :
                                             Symbol                ( conf ),
                                             _fill                 ( Fill( 1, 1, 1, 1 ) ),
                                             _border               ( Stroke( 0.3, 0.3, 0.3, 1) ),
                                             _margin               ( 3 ),
                                             _bboxGeom             ( GEOM_BOX ),
                                             _bboxGroup            ( GROUP_TEXT_ONLY )
{
    mergeConfig(conf);
}

BBoxSymbol::BBoxSymbol(const BBoxSymbol& rhs,const osg::CopyOp& copyop):
                                                                           Symbol(rhs, copyop),
                                                                           _fill                 ( rhs._fill ),
                                                                           _border               ( rhs._border ),
                                                                           _margin               ( rhs._margin ),
                                                                           _bboxGeom             ( rhs._bboxGeom ),
                                                                           _bboxGroup            ( rhs._bboxGroup ),
                                                                           _direction            ( rhs._direction )
{

}

Config
BBoxSymbol::getConfig() const
{
    Config conf = Symbol::getConfig();
    conf.key() = "bbox";
    conf.set( "fill", _fill );
    conf.set( "border", _border );
    conf.set( "margin", _margin );

    conf.set( "geom", "box", _bboxGeom, GEOM_BOX );
    conf.set( "geom", "box_no_pick", _bboxGeom, GEOM_BOX_NO_PICK );
    conf.set( "geom", "box_oriented", _bboxGeom, GEOM_BOX_ORIENTED );
    conf.set( "geom", "box_oriented_symetric", _bboxGeom, GEOM_BOX_ORIENTED_SYM );
    conf.set( "geom", "box_oriented_2ways", _bboxGeom, GEOM_BOX_ORIENTED_2WAYS );
    conf.set( "geom", "box_rounded", _bboxGeom, GEOM_BOX_ROUNDED );
    conf.set( "geom", "box_rounded_inner", _bboxGeom, GEOM_BOX_ROUNDED_INNER );
    conf.set( "geom", "stair", _bboxGeom, GEOM_STAIR );
    conf.set( "geom", "box_rounded_oriented", _bboxGeom, GEOM_BOX_ROUNDED_ORIENTED );
    conf.set( "geom", "box_stroke_sides", _bboxGeom, GEOM_BOX_STROKE_SIDES );

    conf.set( "group", "none", _bboxGroup, GROUP_NONE );
    conf.set( "group", "text-only", _bboxGroup, GROUP_TEXT_ONLY );
    conf.set( "group", "icon-only", _bboxGroup, GROUP_ICON_ONLY );
    conf.set( "group", "icon-and-text", _bboxGroup, GROUP_ICON_AND_TEXT );

    conf.set( "direction", _direction );

    return conf;
}

void
BBoxSymbol::mergeConfig( const Config& conf )
{
    conf.get( "fill", _fill );
    conf.get( "border", _border );
    conf.get( "margin", _margin );

    conf.get( "geom", "box", _bboxGeom, GEOM_BOX );
    conf.get( "geom", "box_no_pick", _bboxGeom, GEOM_BOX_NO_PICK );
    conf.get( "geom", "box_oriented", _bboxGeom, GEOM_BOX_ORIENTED );
    conf.get( "geom", "box_oriented_symetric", _bboxGeom, GEOM_BOX_ORIENTED_SYM );
    conf.get( "geom", "box_oriented_2ways", _bboxGeom, GEOM_BOX_ORIENTED_2WAYS );
    conf.get( "geom", "box_rounded", _bboxGeom, GEOM_BOX_ROUNDED );
    conf.get( "geom", "box_rounded_inner", _bboxGeom, GEOM_BOX_ROUNDED_INNER );
    conf.get( "geom", "stair", _bboxGeom, GEOM_STAIR );
    conf.get( "geom", "box_rounded_oriented", _bboxGeom, GEOM_BOX_ROUNDED_ORIENTED );
    conf.get( "geom", "box_stroke_sides", _bboxGeom, GEOM_BOX_STROKE_SIDES );

    conf.get( "group", "none", _bboxGroup, GROUP_NONE );
    conf.get( "group", "text-only", _bboxGroup, GROUP_TEXT_ONLY );
    conf.get( "group", "icon-only", _bboxGroup, GROUP_ICON_ONLY );
    conf.get( "group", "icon-and-text", _bboxGroup, GROUP_ICON_AND_TEXT );

    conf.get( "direction", _direction );
}

void
BBoxSymbol::parseSLD(const Config& c, Style& style)
{
    if ( match(c.key(), "text-bbox-fill") ) {
        style.getOrCreate<BBoxSymbol>()->fill()->color() = Color(c.value());
    }
    else if ( match(c.key(), "text-bbox-border") ) {
        style.getOrCreate<BBoxSymbol>()->border()->color() = Color(c.value());
    }
    else if ( match(c.key(), "text-bbox-border-width") ) {
        style.getOrCreate<BBoxSymbol>()->border()->width() = as<float>( c.value(), 1.0f );
    }
    else if ( match(c.key(), "text-bbox-border-smooth") ) {
        style.getOrCreate<BBoxSymbol>()->border()->smooth() = as<bool>(c.value(), false);
    }
    else if ( match(c.key(), "text-bbox-margin") ) {
        style.getOrCreate<BBoxSymbol>()->margin() = as<float>(c.value(), 3.0f);
    }
    else if ( match(c.key(), "text-bbox-geom") ) {
        if      ( match(c.value(), "box") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX;
        }
        else if ( match(c.value(), "box_no_pick") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_NO_PICK;
        }
        else if ( match(c.value(), "box_oriented") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_ORIENTED;
        }
        else if ( match(c.value(), "box_oriented_symetric") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_ORIENTED_SYM;
        }
        else if ( match(c.value(), "box_oriented_2ways") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_ORIENTED_2WAYS;
        }
        else if ( match(c.value(), "box_rounded") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_ROUNDED;
        }
        else if ( match(c.value(), "box_rounded_inner") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_ROUNDED_INNER;
        }
        else if ( match(c.value(), "stair") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_STAIR;
        }
        else if ( match(c.value(), "box_rounded_oriented") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_ROUNDED_ORIENTED;
        }
        else if ( match(c.value(), "box_stroke_sides") ) {
            style.getOrCreate<BBoxSymbol>()->geom() = GEOM_BOX_STROKE_SIDES;
        }
    }
    else if ( match(c.key(), "text-bbox-group") ) {
        if      ( match(c.value(), "none") ) {
            style.getOrCreate<BBoxSymbol>()->group() = GROUP_NONE;
        }
        else if ( match(c.value(), "text-only") ) {
            style.getOrCreate<BBoxSymbol>()->group() = GROUP_TEXT_ONLY;
        }
        else if ( match(c.value(), "icon-only") ) {
            style.getOrCreate<BBoxSymbol>()->group() = GROUP_ICON_ONLY;
        }
        else if ( match(c.value(), "icon-and-text") ) {
            style.getOrCreate<BBoxSymbol>()->group() = GROUP_ICON_AND_TEXT;
        }
    }
    else if ( match(c.key(),"text-bbox-direction") ) {
        style.getOrCreate<BBoxSymbol>()->direction() = StringExpression(c.value());
    }
}
