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

#include <osgEarthSymbology/boostBuffer.h>

#ifdef OSGEARTH_HAVE_BOOST_GEO
using namespace osgEarth::Symbology;

void BoostBufferParameters::setJoinStrategy(const BufferParameters::JoinStyle osgErthJoinStrategy)
{
    switch (osgErthJoinStrategy)
    {
    case BufferParameters::JOIN_ROUND:
        _join_strategy = std::pair<BufferParameters::JoinStyle, boost::any>(osgErthJoinStrategy, bg::strategy::buffer::join_round(_points_per_circle));
        break;
    case BufferParameters::JOIN_MITRE:
        _join_strategy = std::pair<BufferParameters::JoinStyle, boost::any>(osgErthJoinStrategy, bg::strategy::buffer::join_miter());   // use default value
        break;
    case BufferParameters::JOIN_BEVEL:
        _join_strategy = std::pair<BufferParameters::JoinStyle, boost::any>(osgErthJoinStrategy, bg::strategy::buffer::join_round_by_divide()); // use default value
        break;
    default:
        OE_WARN << "Unhandled case";
        break;
    }
}

void BoostBufferParameters::setEndStrategy(const BufferParameters::CapStyle osgErthEndStrategy)
{
    switch (osgErthEndStrategy)
    {
    case BufferParameters::CAP_DEFAULT:
    case BufferParameters::CAP_ROUND:
        _end_strategy = std::pair<BufferParameters::CapStyle, boost::any>(osgErthEndStrategy, bg::strategy::buffer::end_round(_points_per_circle));
        break;
    case BufferParameters::CAP_SQUARE:
    case BufferParameters::CAP_FLAT:
        _end_strategy = std::pair<BufferParameters::CapStyle, boost::any>(osgErthEndStrategy, bg::strategy::buffer::end_flat());   // use default value
        break;
    default:
        OE_WARN << "Unhandled case";
        break;
    }
}

BoostBufferParameters::BoostBufferParameters(const size_t points_per_circle, const double distance, const bool singleSided, const bool leftSide)
    : _points_per_circle(points_per_circle)
{
    if(singleSided) {
        if(leftSide) {
            _distance_strategy = new bg::strategy::buffer::distance_asymmetric<double>(distance, 0);
        }
        else {
            _distance_strategy = new bg::strategy::buffer::distance_asymmetric<double>(0, distance);
        }
    }
    else {
        _distance_strategy = new bg::strategy::buffer::distance_asymmetric<double>(distance, distance);
    }

}

BoostBufferParameters::~BoostBufferParameters()
{
    delete _distance_strategy;
}

std::list<polygon2d_t> BoostBufferParameters::compute(const Geometry::Type geoType, const Geometry::Type subGeoType, const boost::any& inGeo)
{
    try {
        switch(_end_strategy.first) {
        case BufferParameters::CAP_SQUARE:
        case BufferParameters::CAP_FLAT:
            return computeWithEndStartegy<bg::strategy::buffer::end_flat>(boost::any_cast<bg::strategy::buffer::end_flat>(_end_strategy.second),
                                                                                 geoType,
                                                                                 subGeoType,
                                                                                 inGeo);
        case BufferParameters::CAP_DEFAULT:
        case BufferParameters::CAP_ROUND:
            return computeWithEndStartegy<bg::strategy::buffer::end_round>(boost::any_cast<bg::strategy::buffer::end_round>(_end_strategy.second),
                                                                                geoType,
                                                                                subGeoType,
                                                                                inGeo);
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
        OE_WARN << "Unknown error in boostBuffer compute";
    }

    return std::list<polygon2d_t>{};
}
#endif // OSGEARTH_HAVE_BOOST_GEO
