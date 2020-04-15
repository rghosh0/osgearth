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

#ifndef OSGEARTHSYMBOLOGY_BOOSTBUFFER_H
#define OSGEARTHSYMBOLOGY_BOOSTBUFFER_H 1

#ifdef OSGEARTH_HAVE_BOOST_GEO

#include <osgEarthSymbology/boostGeoTypes.h>
#include <osgEarthSymbology/BufferParameters.h>
#include <boost/any.hpp>
#include <boost/geometry.hpp>
#include <osgEarthSymbology/Geometry>

namespace osgEarth { namespace Symbology
{

namespace bg = boost::geometry;

//! @brief BoostBufferParameters perform a geometyric buffer operation,
//! using boost, with defined strategy.
class BoostBufferParameters
{
public:    

    //! @brief constructor
    BoostBufferParameters(size_t points_per_circle, const double distance, const bool singleSided, const bool leftSide = false);

    //! @brief destructor
    ~BoostBufferParameters();

    //! @brief set Join strategy
    void setJoinStrategy(const BufferParameters::JoinStyle osgErthJoinStrategy);

    //! @brief set End strategy
    void setEndStrategy(const BufferParameters::CapStyle osgErthEndStrategy);

    //! @brief Perform buffet operation
    std::list<polygon2d_t> compute(const Geometry::Type geoType, const Geometry::Type subGeoType, const boost::any& inGeo);

private:

    //! @brief Perform buffet operation
	template<typename TEnd, typename TJoin>
    std::list<polygon2d_t> computeWithAllStartegy(TEnd end_strategy,
                                TJoin join_strategy,
                                const Geometry::Type geoType,
                                const Geometry::Type subGeoType,
                                const boost::any& inGeo)
    {
        bg::strategy::buffer::point_circle circle_strategy(_points_per_circle);
        bg::strategy::buffer::side_straight side_strategy;

		// Declare output
        mpolygon2d_t boost_output;

        switch(geoType){
        case Symbology::Geometry::TYPE_POINTSET:
        {
            // Convert single point into line string. Point type not handle in boost buffer operation..
            linestring_t tmpGeo;
            bg::assign_points(tmpGeo, boost::any_cast<point_t>(geoType));
            bg::buffer(tmpGeo, boost_output,
                        *_distance_strategy,
                        side_strategy,
                        join_strategy,
                        end_strategy,
                        circle_strategy);
        }
            break;
        case Symbology::Geometry::TYPE_LINESTRING:
            bg::buffer(boost::any_cast<linestring_t>(inGeo), boost_output,
                        *_distance_strategy,
                        side_strategy,
                        join_strategy,
                        end_strategy,
                        circle_strategy);
            break;
        case Symbology::Geometry::TYPE_RING:
        case Symbology::Geometry::TYPE_POLYGON:
            bg::buffer(boost::any_cast<polygon2d_t>(inGeo), boost_output,
                                    *_distance_strategy,
                                    side_strategy,
                                    join_strategy,
                                    end_strategy,
                                    circle_strategy);
        break;
        case Symbology::Geometry::TYPE_MULTI:
        {
            switch(subGeoType) {
                case Symbology::Geometry::TYPE_POINTSET:
                    bg::buffer(boost::any_cast<mpoint_xyz_t>(inGeo), boost_output,
                                            *_distance_strategy,
                                            side_strategy,
                                            join_strategy,
                                            end_strategy,
                                            circle_strategy);
                break;
                case Symbology::Geometry::TYPE_LINESTRING:
                    bg::buffer(boost::any_cast<mlinestring_t>(inGeo), boost_output,
                                            *_distance_strategy,
                                            side_strategy,
                                            join_strategy,
                                            end_strategy,
                                            circle_strategy);
                break;
                case Symbology::Geometry::TYPE_RING:
                case Symbology::Geometry::TYPE_POLYGON:
                    bg::buffer(boost::any_cast<mpolygon2d_t>(inGeo), boost_output,
                                            *_distance_strategy,
                                            side_strategy,
                                            join_strategy,
                                            end_strategy,
                                            circle_strategy);
                break;
                default:
                    OE_WARN << "Unhandled sub geometry type";
                break;
            }
        }
            break;
        default:
            OE_WARN << "Unhandled geometry type";
            break;
        }

        // Convert multi polygon into a list of polygon
        std::list<polygon2d_t> listPolyOutput;
        for(auto&& poly: boost_output){
            listPolyOutput.push_back(poly);
        }
        return listPolyOutput;
	}

    //! @brief Resolve join strategy to perform buffet operation
    template<typename TEnd>
    std::list<polygon2d_t> computeWithEndStartegy(TEnd endStrategy,
                                const Geometry::Type geoType,
                                const Geometry::Type subGeoType,
                                const boost::any& inGeo) {
		switch(_join_strategy.first) {
		case BufferParameters::JOIN_ROUND:
            return computeWithAllStartegy<TEnd, bg::strategy::buffer::join_round>(endStrategy,
                                                                           boost::any_cast<bg::strategy::buffer::join_round>(_join_strategy.second),
                                                                           geoType,
                                                                           subGeoType,
                                                                           inGeo);
		case BufferParameters::JOIN_MITRE:
             return computeWithAllStartegy<TEnd, bg::strategy::buffer::join_miter>(endStrategy,
                                                                            boost::any_cast<bg::strategy::buffer::join_miter>(_join_strategy.second),
                                                                            geoType,
                                                                            subGeoType,
                                                                            inGeo);
		case BufferParameters::JOIN_BEVEL:
            return computeWithAllStartegy<TEnd, bg::strategy::buffer::join_round_by_divide>(endStrategy,
                                                                                     boost::any_cast<bg::strategy::buffer::join_round_by_divide>(_join_strategy.second),
                                                                                     geoType,
                                                                                     subGeoType,
                                                                                     inGeo);
        default:
            OE_WARN << "Unhandled case";
            break;
		}
        return std::list<polygon2d_t>{};
	}

    //! @brief join strategy
	std::pair<BufferParameters::JoinStyle, boost::any> _join_strategy;

    //! @brief end strategy
	std::pair<BufferParameters::CapStyle, boost::any> _end_strategy;

    //! @brief distance strategy
    bg::strategy::buffer::distance_asymmetric<coordinate_type>* _distance_strategy{nullptr};

    //! @brief points per circle
    size_t _points_per_circle{0};
};
}} // namespace osgEarth / Symbology

#endif // OSGEARTH_HAVE_BOOST_GEO

#endif // OSGEARTHSYMBOLOGY_BOOSTBUFFER_H
