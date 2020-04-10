#ifndef OSGEARTHSYMBOLOGY_BOOSTGEO_TYPES_H
#define OSGEARTHSYMBOLOGY_BOOSTGEO_TYPES_H 1

#ifdef OSGEARTH_HAVE_BOOST_GEO
#include <boost/geometry/geometry.hpp>

namespace bg = boost::geometry;
typedef double coordinate_type;
typedef bg::model::point<coordinate_type, 2, bg::cs::cartesian> point_t;

typedef bg::model::polygon<point_t> polygon2d_t;
typedef polygon2d_t ring_t;
typedef bg::model::linestring<point_t> linestring_t;

typedef bg::model::multi_polygon<polygon2d_t> mpolygon2d_t;
typedef bg::model::multi_linestring<linestring_t> mlinestring_t;
typedef bg::model::multi_point<point_t> mpoint_xyz_t;

#endif // OSGEARTH_HAVE_BOOST_GEO
#endif // OSGEARTHSYMBOLOGY_BOOSTGEO_TYPES_H
