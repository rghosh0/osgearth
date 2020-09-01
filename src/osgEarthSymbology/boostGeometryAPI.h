///
/// [NAVBLUE] ("NAVBLUE") CONFIDENTIAL
/// Copyright (c) 2020 [NAVBLUE], All Rights Reserved.
///
/// NOTICE:  All information contained herein is, and remains the property of NAVBLUE and/or its licensors.
/// The intellectual and technical concepts contained herein are proprietary to NAVBLUE and/or its licensors
/// and may be covered by French and foreign patents, patents in process, which is protected by trade secret
/// or copyright law. Dissemination of this information, reproduction of this material and/or access to the
/// source code is strictly forbidden unless prior written consent is obtained from NAVBLUE and subject to the
/// conditions of such consent. The copyright notice above does not evidence any actual or intended
/// publication or disclosure  of  this source code, which includes information that is confidential and/or
/// proprietary, and is a trade secret, of NAVBLUE. ANY REPRODUCTION, MODIFICATION, DISTRIBUTION, PUBLIC
/// PERFORMANCE, OR PUBLIC DISPLAY OF THE SOURCE CODE OR ANY INFORMATION THROUGH USE OF THIS  SOURCE CODE
/// WITHOUT  THE EXPRESS WRITTEN CONSENT OF NAVBLUE IS STRICTLY PROHIBITED.  THE RECEIPT OR POSSESSION OF THE
/// SOURCE CODE AND/OR RELATED INFORMATION DOES NOT CONVEY OR IMPLY ANY RIGHTS TO REPRODUCE, DISCLOSE OR
/// DISTRIBUTE ITS CONTENTS, OR TO MANUFACTURE, USE, OR SELL ANYTHING THAT IT MAY DESCRIBE, IN WHOLE OR IN
/// PART.
///

/**
 * @copyright Copyright (c) 2020 [NAVBLUE], All Rights Reserved..
 */


#ifndef OSGEARTHSYMBOLOGY_BOOST_GEOMETRY_H
#define OSGEARTHSYMBOLOGY_BOOST_GEOMETRY_H 1

#ifdef OSGEARTH_HAVE_BOOST_GEO

#include <osgEarthSymbology/Geometry>
#include "osgEarthSymbology/boostGeoTypes.h"
#include <boost/any.hpp>

namespace osgEarth { namespace Symbology
{
using namespace osgEarth;
namespace bg = boost::geometry;

//! @brief geometry convertion class from osgearth to boost and vice versa
class boostGeometryContext
{
public:
    //! @brief export a geometry
    //! convert a boost geometry into an osgearth geometry.
    template<typename TGeoIn>
    Geometry *exportGeometry(void *input )
    {
        Symbology::GeometryCollection parts;
        // point case
        if (std::is_same_v<TGeoIn, point_t>) {
            auto inputTmp = static_cast<std::list<point_t>*>(input);
            Symbology::PointSet* part = new Symbology::PointSet( static_cast<int>(inputTmp->size()) );
            for (auto&& point: *inputTmp) {
                part->push_back( osg::Vec3d(point.get<0>(), point.get<1>(), 0.0 ));
            }
            parts.push_back(part);
        }
        // linestring case
        else if (std::is_same_v<TGeoIn, linestring_t>) {
            auto inputTmp = static_cast<std::list<linestring_t>*>(input);
            for (auto&& lineString: *inputTmp) {
                Symbology::LineString* part = new Symbology::LineString( static_cast<int>(lineString.size()) );
                for (auto&& point: lineString) {
                    part->push_back( osg::Vec3d(point.get<0>(), point.get<1>(), 0) );
                }
                parts.push_back(part);
            }
        }
        // multi polygon case
        else if (std::is_same_v<TGeoIn, mpolygon2d_t>) {
            auto inputTmp = static_cast<mpolygon2d_t*>(input);
            for (auto&& poly: *inputTmp) {
                Symbology::Geometry* polyPart = exportPolygon( poly );
                if ( polyPart ) {
                    parts.push_back( polyPart );
                }
            }
        }
        // polygon case
        else if (std::is_same_v<TGeoIn, polygon2d_t>) {
            auto inputTmp = static_cast<std::list<polygon2d_t>*>(input);
            for (auto&& poly: *inputTmp) {
                Symbology::Geometry* polyPart = exportPolygon( poly );
                if ( polyPart ) {
                    parts.push_back( polyPart );
                }
            }
        }
        else {
            OE_WARN << "Export geometry fail: Unhandled type" <<  std::endl;
        }

        // Convert parts into osgearth geometry
        if ( parts.size() == 1 )
        {
            osg::ref_ptr<Symbology::Geometry> part = parts.front().get();
            parts.clear();
            return part.release();
        }
        else if ( parts.size() > 1 ) {
            return new Symbology::MultiGeometry( parts );
        }
        else {
            return nullptr;
        }
    }

    //! @brief import a geometry
    //! convert an osgearth geometry into a boost geometry.
    //! return validity and boost geometry
    std::pair<bool, boost::any> importGeometry( const Symbology::Geometry* input )
    {
        try {
            switch(input->getType()) {
            case Geometry::TYPE_UNKNOWN:
                OE_DEBUG << "TYPE_UNKNOWN can't be imported" << std::endl;
                break;
            case Geometry::TYPE_MULTI:
            {
                const Symbology::MultiGeometry* multi = static_cast<const Symbology::MultiGeometry*>( input );
                // In osgearth, all geometry in the multigeometry have the same type (retreived from first element).
                Symbology::Geometry::Type compType = multi->getComponentType();

                std::vector<boost::any> children;
                for( Symbology::GeometryCollection::const_iterator it = multi->getComponents().begin(); it != multi->getComponents().end(); ++it )
                {
                    auto geo = importGeometry( it->get() );

                    // if imported geometry are invalid
                    if(geo.first == true) {
                        children.push_back( geo.second );
                    }
                    else {
                        OE_WARN << "Invalid geometry in multigeometry";
                        return std::make_pair(false, nullptr);
                    }
                }
                if ( !children.empty() )
                {
                    if (( compType == Symbology::Geometry::TYPE_POLYGON ) ||
                        ( compType == Symbology::Geometry::TYPE_RING )){
                        mpolygon2d_t boostGeo;
                        for(auto it =children.begin(); it != children.end(); it++ ) {
                            boostGeo.push_back(boost::any_cast<polygon2d_t>(*it));
                        }
                        return std::make_pair(true, std::move(boostGeo));
                    }
                    else if ( compType == Symbology::Geometry::TYPE_LINESTRING )
                    {
                        mlinestring_t boostGeo;
                        for(auto it =children.begin(); it != children.end(); it++ ) {
                            boostGeo.push_back(boost::any_cast<linestring_t>(*it));
                        }
                        return std::make_pair(true, std::move(boostGeo));
                    }
                    else if ( compType == Symbology::Geometry::TYPE_POINTSET )
                    {
                        mpoint_xyz_t boostGeo;
                        for(auto it =children.begin(); it != children.end(); it++ ) {
                            boostGeo.push_back(boost::any_cast<point_t>(*it));
                        }
                        return std::make_pair(true, std::move(boostGeo));
                    }
                    else {
                        OE_WARN << "Not handle type";
                    }
                }
            }
                break;
            case Geometry::TYPE_POINTSET:
            {
                point_t boostGeo;
                std::vector<point_t>&& vCoordinates = vec3dArray2CoordSeq(input);
                // Just one point
                if (vCoordinates.size() == 1) {
                    boostGeo = vCoordinates.front();
                }
                return std::make_pair(true, std::move(boostGeo));
            }
            case Geometry::TYPE_LINESTRING:
            {
                linestring_t boostGeo;
                auto&& vCoordinates = vec3dArray2CoordSeq(input);
                bg::assign_points(boostGeo, vCoordinates);
                return std::make_pair(true, std::move(boostGeo));
            }
            case Geometry::TYPE_RING:
            {
                ring_t boostGeo;
                auto&& vCoordinates = vec3dArray2CoordSeq(input);
                // assig_point automaticaly close geometry if needed.
                bg::assign_points(boostGeo, vCoordinates);
                // Correct geometry orientation
                bg::correct(boostGeo);
                return std::make_pair(true, std::move(boostGeo));
            }
            case Geometry::TYPE_POLYGON:
            {
                polygon2d_t boostGeo;
                auto&& vCoordinates = vec3dArray2CoordSeq(input);
                // assig_point automaticaly close geometry if needed.
                bg::assign_points(boostGeo, vCoordinates);

                const Polygon* osgPoly = static_cast<const Polygon*> (input);
                if ( osgPoly->getHoles().size() > 0 )
                {
                    boostGeo.inners().resize(osgPoly->getHoles().size());
                    int i = 0;
                    for (auto h : osgPoly->getHoles())
                    {
                        auto&& v = vec3dArray2CoordSeq(h);
                        bg::assign_points(boostGeo.inners()[i], v);
                        i++;
                    }
                }
                // Correct geometry orientation
                bg::correct(boostGeo);
                return std::make_pair(true, std::move(boostGeo));
            }
            default:
                OE_WARN << "Unhandled case";
                break;
            }
        }
        catch (const boost::bad_any_cast &e) {
            OE_DEBUG << e.what();
        }
        catch (const bg::empty_input_exception  &e) {
            OE_DEBUG << e.what();
        }
        catch (const bg::invalid_input_exception  &e) {
            OE_DEBUG << e.what();
        }
        catch (...) {
            OE_WARN << "Unknown error in boost_crop";
        }

        return std::make_pair(false, nullptr);
    }

private:
    //! @brief extract corrdinates from an osgearth geometry
    std::vector<point_t> vec3dArray2CoordSeq( const Symbology::Geometry* input);

    //! @brief export polygon geometry
    //! convert a boost polygon into an osgearth polygon
    Symbology::Geometry* exportPolygon( const polygon2d_t & input );

};
}
} // namespace

#endif // OSGEARTH_HAVE_BOOST_GEO

#endif // OSGEARTHSYMBOLOGY_BOOST_GEOMETRY_H

