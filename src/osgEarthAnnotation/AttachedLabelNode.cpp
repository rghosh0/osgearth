#include "AttachedLabelNode"
#include <osg/Depth>
#include <osgEarth/ECEF>
#include <osgEarth/Lighting>
#include <osgEarth/ShaderGenerator>
#include <osgEarthAnnotation/AnnotationRegistry>
#include <osgEarthAnnotation/AnnotationUtils>
#include <osgEarthAnnotation/BboxDrawable>

#if defined(OSGEARTH_HAVE_GEOS)
#include <geos/geom/Geometry.h>
#include <geos/geom/GeometryFactory.h>
#include <geos/operation/buffer/BufferOp.h>
#include <geos/operation/buffer/BufferBuilder.h>
#include <geos/operation/overlay/OverlayOp.h>
#include <geos/simplify/DouglasPeuckerLineSimplifier.h>
#include <geos/geom/Coordinate.h>
using namespace geos;
using namespace geos::operation;
#endif

#define GEOS_OUT OE_DEBUG

#define LC "[LinkedLabelNode] "

using namespace osgEarth;
using namespace osgEarth::Annotation;
using namespace osgEarth::Symbology;

namespace
{
osg::observer_ptr<osg::StateSet> s_geodeStateSet;

#if defined(OSGEARTH_HAVE_GEOS)
osgEarth::Symbology::Geometry* simplifyGeometry(osgEarth::Symbology::Geometry* src, double tolerance)
{
    auto count = src->getTotalPointCount();
    geos::simplify::DouglasPeuckerLineSimplifier::CoordsVect coordvecs{};
    coordvecs.reserve(count);
    for(auto n = 0; n < count; ++n)
    {
        auto p = (*src)[n];
        coordvecs.push_back(geos::geom::Coordinate{p.x(), p.y(), p.z()});
    }
    auto resultCoordvecs = geos::simplify::DouglasPeuckerLineSimplifier::simplify(coordvecs, tolerance);
    osgEarth::Symbology::Vec3dVector resultVecs{};
    resultVecs.reserve(resultCoordvecs->size());
    for(auto& p : *resultCoordvecs)
    {
        resultVecs.push_back(osg::Vec3d{p.x, p.y, p.z});
    }
    return osgEarth::Symbology::Geometry::create(osgEarth::Symbology::Geometry::Type::TYPE_LINESTRING, &resultVecs);
}
#else
osgEarth::Symbology::Geometry* simplifyGeometry(osgEarth::Symbology::Geometry* src, double tolerance)
{
    return src;
}
#endif
}

AttachedLabelNode::AttachedLabelNode()
{
    _construct();
    _compile();
}

AttachedLabelNode::AttachedLabelNode(osgEarth::Features::FilterContext& context,
                                     osgEarth::Features::Feature* feature,
                                     const Style& style)
    : GeoPositionNode{}
{
    _construct();
    updateGeometry(context, feature);
    _style = style;
    _compile();
}

AttachedLabelNode::AttachedLabelNode(const AttachedLabelNode& other, const osg::CopyOp& copyOp)
{
    _construct();
    _text = other._text;
    _geometry = other._geometry;
    _style = other._style;
    _compile();
}

void AttachedLabelNode::dirty()
{
    GeoPositionNode::dirty();
    _updateLayoutData();
}

void AttachedLabelNode::setPriority(float value)
{
    GeoPositionNode::setPriority(value);
    _updateLayoutData();
}

void AttachedLabelNode::setDynamic(bool dynamic)
{
    GeoPositionNode::setDynamic(dynamic);
    if (_geode.valid() == false)
        return;

    for (auto n = uint32_t{0}; n < _geode->getNumChildren(); ++n)
    {
        auto node = _geode->getChild(n);
        if (node == nullptr)
            continue;
        node->setDataVariance((dynamic == true)? osg::Object::DYNAMIC : osg::Object::STATIC);
    }
}

void AttachedLabelNode::setStyle(const Style& style)
{
    _style = style;
    _compile();
}

const Style& AttachedLabelNode::getStyle() const
{
    return _style;
}

void AttachedLabelNode::updateGeometry(osgEarth::Features::FilterContext& context,
                                       osgEarth::Features::Feature* feature,
                                       double decimationTolerance)
{
    auto srcGeom = (feature->srcGeom != nullptr)? feature->srcGeom.get() : feature->getGeometry();
    _geometry = simplifyGeometry(srcGeom, decimationTolerance);

    for(auto n = 0; n < (srcGeom->size() - 1); ++n)
    {
        _srcGeomLines.push_back(_Line{(*srcGeom)[n], (*srcGeom)[n + 1]});
    }
}

void AttachedLabelNode::_construct()
{
    // This class makes its own shaders
    ShaderGenerator::setIgnoreHint(this, true);

    // Initialize the shared stateset as necessary
    osg::ref_ptr<osg::StateSet> geodeStateSet;
    if (s_geodeStateSet.lock(geodeStateSet) == false)
    {
        static Threading::Mutex s_mutex;
        Threading::ScopedMutexLock lock(s_mutex);

        if (s_geodeStateSet.lock(geodeStateSet) == false)
        {
            s_geodeStateSet = geodeStateSet = new osg::StateSet();

            // draw in the screen-space bin
            ScreenSpaceLayout::activate(geodeStateSet.get());

            // completely disable depth buffer
            geodeStateSet->setAttributeAndModes( new osg::Depth(osg::Depth::ALWAYS, 0, 1, false), 1 );

            // Disable lighting for place label bbox
            geodeStateSet->setDefine(OE_LIGHTING_DEFINE, osg::StateAttribute::OFF | osg::StateAttribute::PROTECTED);
        }
    }

    _geode = new osg::Geode();
    _geode->setStateSet(geodeStateSet.get());

    // ensure that (0,0,0) is the bounding sphere control/center point.
    // useful for things like horizon culling.
    _geode->setComputeBoundingSphereCallback(new ControlPointCallback());

    getPositionAttitudeTransform()->addChild( _geode.get());
}

void AttachedLabelNode::_compile()
{
    _geode->removeChildren(0, _geode->getNumChildren());
    auto textSymbol = _style.get<TextSymbol>();
    auto text = AnnotationUtils::createTextDrawable( _text, textSymbol, osg::Vec3(0,0,0) );

    auto bboxsymbol = _style.get<BBoxSymbol>();
    if ( (bboxsymbol != nullptr) && (text != nullptr) )
    {
        auto bboxGeom = new BboxDrawable(text->getBoundingBox(), *bboxsymbol );
        if (bboxGeom != nullptr)
        {
            _geode->addDrawable(bboxGeom);
        }
    }

    if (text != nullptr)
    {
        if (_dynamic == true)
        {
            text->setDataVariance(osg::Object::DYNAMIC);
        }
        _geode->addDrawable(text);
    }

    applyStyle( _style );
    _updateLayoutData();
    dirty();
}

void AttachedLabelNode::_updateLayoutData()
{
    if (_dataLayout.valid() == false)
    {
        _dataLayout = new ScreenSpaceLayoutData();
    }
    // re-apply annotation drawable-level stuff as neccesary.
    for (auto n = 0u; n < _geode->getNumChildren(); ++n)
    {
        _geode->getChild(n)->setUserData(_dataLayout.get());
    }

    _dataLayout->setPriority(getPriority());

    GeoPoint location = getPosition();
    location.makeGeographic();
    _geoPointLoc.set(osgEarth::SpatialReference::get("wgs84"),
                     //location.getSRS(),
                     location.x(),
                     location.y(),
                     0,
                     osgEarth::ALTMODE_ABSOLUTE);

    const TextSymbol* ts = getStyle().get<TextSymbol>();
    if (ts)
    {
        _dataLayout->setPixelOffset(ts->pixelOffset().get());
    }
}
