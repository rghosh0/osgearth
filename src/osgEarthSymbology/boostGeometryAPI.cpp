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
 * @copyright Copyright (c) 2020 [NAVBLUE], All Rights Reserved.
 */

#include <osgEarthSymbology/boostGeometryAPI.h>

#ifdef OSGEARTH_HAVE_BOOST_GEO
using namespace osgEarth::Symbology;

std::vector<point_t> boostGeometryContext::vec3dArray2CoordSeq( const Geometry* input )
{
    std::vector<point_t> coords;
    for( osg::Vec3dArray::const_iterator it = input->begin(); it != input->end(); ++it )
    {
        coords.push_back( point_t( it->x(), it->y() ));
    }
    return coords;
}

Geometry* boostGeometryContext::exportPolygon( const polygon2d_t & input )
{
    Polygon* output = nullptr;
    auto outerRing = input.outer();
    if ( !outerRing.empty() )
    {
        output = new Polygon( static_cast<int>(outerRing.size()) );
        for (auto && coord : outerRing)
        {
            output->push_back( osg::Vec3d( coord.get<0>(), coord.get<1>(), 0.0) );
        }
        // TODO usefull??
        output->rewind( Ring::ORIENTATION_CCW );

        // push the holes
        auto innersRing = input.inners();
        for(auto &&innerHole: innersRing)
        {
            Ring* hole = new Ring( static_cast<int>(innerHole.size()) );
            for(auto &&coord: innerHole)
            {
                coord.get<0>();
                hole->push_back( osg::Vec3d( coord.get<0>(), coord.get<1>(), 0.0) );
            }

            hole->rewind( Ring::ORIENTATION_CW );
            output->getHoles().push_back( hole );
        }
    }
    return output;
}

#endif // OSGEARTH_HAVE_BOOST_GEO
