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

#include "MPAnnotationGroup"

#include <osgEarthAnnotation/AnnotationUtils>
#include <osgEarthAnnotation/BboxDrawable>
#include <osgEarth/VirtualProgram>
#include <osgEarth/Lighting>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/GeoMath>
#include <osgEarthFeatures/GeometryUtils>
#include <osg/Depth>

#define LC "[MPAnnotationGroup] "

using namespace osgEarth;
using namespace osgEarth::Annotation;


osg::observer_ptr<osg::StateSet> MPAnnotationGroup::s_rootStateSet;
std::map<std::string, osg::observer_ptr<osg::StateSet>> MPAnnotationGroup::s_imageStateSet;

namespace
{
const char* iconVS =
        "#version " GLSL_VERSION_STR "\n"
                                     "out vec2 oe_PlaceNode_texcoord; \n"
                                     "void oe_PlaceNode_icon_VS(inout vec4 vertex) \n"
                                     "{ \n"
                                     "    oe_PlaceNode_texcoord = gl_MultiTexCoord0.st; \n"
                                     "} \n";

const char* iconFS =
        "#version " GLSL_VERSION_STR "\n"
                                     "in vec2 oe_PlaceNode_texcoord; \n"
                                     "uniform sampler2D oe_PlaceNode_tex; \n"
                                     "void oe_PlaceNode_icon_FS(inout vec4 color) \n"
                                     "{ \n"
                                     "    color = texture(oe_PlaceNode_tex, oe_PlaceNode_texcoord); \n"
                                     "} \n";
}


// Callback to properly cull the MPAnnotationGroup
class AnnotationNodeGroupCullCallback : public osg::NodeCallback
{
public:
    void operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cullVisitor = nv->asCullVisitor();
        if(! cullVisitor->isCulled(node->getBound()))
        {
            const osg::Matrix& MVPW = *(cullVisitor->getMVPW());
            double vpXmin = cullVisitor->getViewport()->x();
            double vpXmax = cullVisitor->getViewport()->x() + cullVisitor->getViewport()->width();
            double vpYmin = cullVisitor->getViewport()->y();
            double vpYmax = cullVisitor->getViewport()->y() + cullVisitor->getViewport()->height();
            double alt = DBL_MAX;
            cullVisitor->getCurrentCamera()->getUserValue("altitude", alt);

            MPAnnotationGroup* annoGroup = static_cast<MPAnnotationGroup*>(node);

            for ( auto const &anno : annoGroup->getDrawableList() )
            {
                if ( ! anno.second.empty() )
                {
                    ScreenSpaceLayoutData* ssld = anno.second[0].globalSsld;
                    ssld->_cull_anchorOnScreen = ssld->_anchorPoint * MVPW;
                    ssld->_cull_bboxSymOnScreen.set(ssld->_cull_anchorOnScreen + ssld->getBBoxSymetric()._min, ssld->_cull_anchorOnScreen + ssld->getBBoxSymetric()._max);

                    if ( ! ssld->isAutoFollowLine() )
                    {
                        // out of viewport
                        if (osg::maximum(ssld->_cull_bboxSymOnScreen.xMin(), vpXmin) > osg::minimum(ssld->_cull_bboxSymOnScreen.xMax(), vpXmax) ||
                            osg::maximum(ssld->_cull_bboxSymOnScreen.yMin(), vpYmin) > osg::minimum(ssld->_cull_bboxSymOnScreen.yMax(), vpYmax) )
                        {
                            for (auto iAnno : anno.second)
                                annoGroup->getChild(iAnno.index)->setNodeMask(0);
                            continue;
                        }
                    }

                    // in viewport
                    // compute the screen angle if necessary
                    if ( ssld->isAutoRotate() )
                    {
                        osg::Vec3d anchorToProj = ssld->_lineEnd * MVPW;
                        anchorToProj -= ssld->_cull_anchorOnScreen;
                        ssld->_cull_rotationRadOnScreen = atan2(anchorToProj.y(), anchorToProj.x());
                    }

                    static const osg::Node::NodeMask nodeMaskOff = 0xffffffff;
                    for (auto iAnno : anno.second)
                    {
                        osg::Node* child = annoGroup->getChild(iAnno.index);
                        BboxDrawable* bbox = dynamic_cast<BboxDrawable*>(child);
                        if (bbox == nullptr)
                            child->setNodeMask(alt < iAnno.minRange ? nodeMaskOff : 0);
                        else
                        {
                            bbox->setNodeMask(nodeMaskOff);
                            bbox->setReducedSize(alt >= iAnno.minRange);
                        }
                    }
                }
            }

            traverse(node, nv);
        }
    }
};


MPAnnotationGroup::MPAnnotationGroup() : osg::Group()
{
    // build one unique StateSet for all MPAnnotationGroup
    if (! s_rootStateSet.lock(_rootStateSet))
    {
        static Threading::Mutex s_mutex;
        Threading::ScopedMutexLock lock(s_mutex);

        if (! s_rootStateSet.lock(_rootStateSet))
        {
            s_rootStateSet = _rootStateSet = new osg::StateSet();

            // draw in the screen-space bin
            MPScreenSpaceLayout::activate(_rootStateSet.get());

            // stateset stuff
            _rootStateSet->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, false), 1);
            _rootStateSet->setDefine( OE_LIGHTING_DEFINE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE );
            _rootStateSet->setMode( GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED );
            _rootStateSet->setMode( GL_BLEND, osg::StateAttribute::ON );
        }
    }

    setStateSet(_rootStateSet.get());

    addCullCallback(new AnnotationNodeGroupCullCallback());

    // This group makes its own shaders
    ShaderGenerator::setIgnoreHint(this, true);
}

// Need a custom bounding sphere for the culling process
osg::BoundingSphere MPAnnotationGroup::computeBound () const
{
    osg::BoundingSphere bsphere;

    for(osg::NodeList::const_iterator itr = _children.begin(); itr!=_children.end(); ++itr)
    {
        if (itr->valid())
        {
            const ScreenSpaceLayoutData *ssld = static_cast<ScreenSpaceLayoutData*>((*itr)->getUserData());
            if (ssld)
            {
                bsphere.expandBy(ssld->getAnchorPoint());
                if (ssld->isAutoFollowLine())
                {
                    bsphere.expandBy(ssld->getLineStartPoint());
                    bsphere.expandBy(ssld->getLineEndPoint());
                }
            }
        }
    }
    return bsphere;
}

long MPAnnotationGroup::addAnnotation(const Style& style, Geometry *geom, const osgDB::Options* readOptions)
{
    // layout data for screenspace information
    static long id{0};
    osg::ref_ptr<ScreenSpaceLayoutData> dataLayout = new ScreenSpaceLayoutData();
    dataLayout->setId(++id);

    // ----------------------
    // Build image
    osg::ref_ptr<osg::Geometry> imageDrawable;
    osg::ref_ptr<const InstanceSymbol> instance = style.get<InstanceSymbol>();
    const IconSymbol* icon = nullptr;
    if (instance.valid())
        icon = instance->asIcon();

    URI imageURI;
    std::string iconfile;
    osg::ref_ptr<osg::Image> image;
    if ( icon )
    {
        if ( icon->url().isSet() )
        {
            imageURI = icon->url()->evalURI();
            iconfile = icon->url().value().eval();
        }
        else if (icon->getImage())
        {
            image = icon->getImage();
        }
    }
    if ( !imageURI.empty() )
    {
        image = imageURI.getImage( readOptions );
    }

    osg::BoundingBox imageBox(0,0,0,0,0,0);

    // found an image; now format it:
    if ( image.get() )
    {
        // Scale the icon if necessary
        double scale = 1.0;
        if ( icon && icon->scale().isSet() )
            scale = icon->scale()->eval();

        double s = scale * image->s();
        double t = scale * image->t();

        // position te icon
        osg::Vec2s offset;
        if ( !icon || !icon->alignment().isSet() )
        {
            // default to bottom center
            offset.set(0.0, t / 2.0);
        }
        else
        {
            switch (icon->alignment().value())
            {
            case IconSymbol::ALIGN_LEFT_TOP:
                offset.set((s / 2.0), -(t / 2.0));
                break;
            case IconSymbol::ALIGN_LEFT_CENTER:
                offset.set((s / 2.0), 0.0);
                break;
            case IconSymbol::ALIGN_LEFT_BOTTOM:
                offset.set((s / 2.0), (t / 2.0));
                break;
            case IconSymbol::ALIGN_CENTER_TOP:
                offset.set(0.0, -(t / 2.0));
                break;
            case IconSymbol::ALIGN_CENTER_CENTER:
                offset.set(0.0, 0.0);
                break;
            case IconSymbol::ALIGN_CENTER_BOTTOM:
            default:
                offset.set(0.0, (t / 2.0));
                break;
            case IconSymbol::ALIGN_RIGHT_TOP:
                offset.set(-(s / 2.0), -(t / 2.0));
                break;
            case IconSymbol::ALIGN_RIGHT_CENTER:
                offset.set(-(s / 2.0), 0.0);
                break;
            case IconSymbol::ALIGN_RIGHT_BOTTOM:
                offset.set(-(s / 2.0), (t / 2.0));
                break;
            }
        }

        // Apply a rotation to the marker if requested:
        double heading = 0.0;
        if ( icon && icon->heading().isSet() )
            heading = osg::DegreesToRadians( icon->heading()->eval() );

        //We must actually rotate the geometry itself and not use a MatrixTransform b/c the
        //decluttering doesn't respect Transforms above the drawable.
        imageDrawable = AnnotationUtils::createImageGeometry(image.get(), offset, 0, heading, scale);
        if (imageDrawable.valid())
        {
            // shared image stateset
            osg::ref_ptr<osg::StateSet> imageStateSet;
            if ( ! iconfile.empty() )
            {
                osg::observer_ptr<osg::StateSet> &sImageStateSet = s_imageStateSet[iconfile];
                if (! sImageStateSet.lock(imageStateSet))
                {
                    static Threading::Mutex s_mutex;
                    Threading::ScopedMutexLock lock(s_mutex);

                    if (! sImageStateSet.lock(imageStateSet))
                    {
                        sImageStateSet = imageStateSet = imageDrawable->getOrCreateStateSet();
                        VirtualProgram* vp = VirtualProgram::getOrCreate(imageStateSet.get());
                        vp->setName("PlaceNode::imageStateSet");
                        vp->setFunction("oe_PlaceNode_icon_VS", iconVS, ShaderComp::LOCATION_VERTEX_MODEL);
                        vp->setFunction("oe_PlaceNode_icon_FS", iconFS, ShaderComp::LOCATION_FRAGMENT_COLORING);
                        imageStateSet->addUniform(new osg::Uniform("oe_PlaceNode_tex", 0));
                    }
                }
            }

            imageDrawable->setStateSet(s_imageStateSet[iconfile].get());
            imageBox = imageDrawable->getBoundingBox();
        }
    }

    // ----------------------
    // Build text

    osg::ref_ptr<osgText::Text> textDrawable;
    const TextSymbol* textSymbol = style.get<TextSymbol>();
    if ( textSymbol )
    {
        TextSymbol::Alignment textAlignment = TextSymbol::Alignment::ALIGN_LEFT_CENTER;
        if ( image.valid() && textSymbol->alignment().isSet() )
            textAlignment = textSymbol->alignment().value();

        osg::BoundingBox imageBoxWithMargin{imageBox};

        if ( icon && icon->margin().isSet() )
        {
            const float margin{icon->margin().value()};
            imageBoxWithMargin.expandBy({imageBox.xMin() - margin, imageBox.yMin() - margin, imageBox.zMin()});
            imageBoxWithMargin.expandBy({imageBox.xMax() + margin, imageBox.yMax() + margin, imageBox.zMax()});
        }

        std::string text = textSymbol->content()->eval();
        if ( ! text.empty() )
            textDrawable = AnnotationUtils::createTextDrawable( text, textSymbol, imageBoxWithMargin );
    }

    // ----------------------
    // Build BBox

    // The bounding box can enclose either the text, or the image, or both
    osg::ref_ptr<osg::Drawable> bboxDrawable;
    const BBoxSymbol* bboxsymbol = style.get<BBoxSymbol>();
    if ( bboxsymbol && (bboxsymbol->group() != BBoxSymbol::BboxGroup::GROUP_NONE)
         && (textDrawable.valid() || imageDrawable.valid()) )
    {
        osg::BoundingBox groupBBox{};
        float reducedWidth = 0.f;

        if ( imageDrawable.valid() && (bboxsymbol->geom() == BBoxSymbol::BboxGeom::GEOM_BOX_ROUNDED) &&
             ((bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_ONLY) ||
              (!textDrawable.valid() && (bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_AND_TEXT))) )
        {
            // This is the case where only the image should have a rounded
            // background, either by choice or because the text is not
            // available. The way rounded boxes are drawn is basically by
            // adding two semicircles on each side of the original bounding
            // box. The result is a pill shape instead of a circle shape for an
            // original square bounding box. To bypass this behavior, the
            // original bounding box is transformed to have a width of zero and
            // a height equal to the diagonal of the image.

            const osg::BoundingBox imageBB{imageDrawable->getBoundingBox()};

            groupBBox.expandBy( {imageBB.center().x(), imageBB.center().y() - imageBB.radius(), imageBB.center().z(),
                                 imageBB.center().x(), imageBB.center().y() + imageBB.radius(), imageBB.center().z()} );
            reducedWidth = imageBB.xMax() - imageBB.xMin();
        }
        else
        {
            // Enclose text
            if ( textDrawable.valid() && (bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_TEXT_ONLY ||
                                  bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_AND_TEXT) )
            {
                groupBBox.expandBy( textDrawable->getBoundingBox() );
            }

            // Enclose image
            if ( imageDrawable.valid() && (bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_ONLY ||
                                   bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_AND_TEXT) )
            {
                groupBBox.expandBy( imageDrawable->getBoundingBox() );
                reducedWidth = imageDrawable->getBoundingBox().xMax() - imageDrawable->getBoundingBox().xMin();
            }
        }

        bboxDrawable = new BboxDrawable( groupBBox, *bboxsymbol, reducedWidth );
    }

    // ----------------------
    // Common settings

    double minRange = textSymbol->minRange().isSet() ? textSymbol->minRange().value() : DBL_MAX;
    if ( imageDrawable.valid() )
    {
        imageDrawable->setCullingActive(false);
        imageDrawable->setDataVariance(osg::Object::DYNAMIC);
        imageDrawable->setUserData(dataLayout);
        this->addChild( imageDrawable );
        _drawableList[id].push_back(AnnoInfo(this->getNumChildren()-1, dataLayout));
    }
    if (  textDrawable.valid() )
    {
        textDrawable->setCullingActive(false);
        textDrawable->setDataVariance(osg::Object::DYNAMIC);
        textDrawable->setUserData(dataLayout);
        this->addChild( textDrawable );
        _drawableList[id].push_back(AnnoInfo(this->getNumChildren()-1, dataLayout, minRange));
    }
    if ( bboxDrawable.valid() )
    {
        bboxDrawable->setCullingActive(false);
        bboxDrawable->setDataVariance(osg::Object::DYNAMIC);
        bboxDrawable->setUserData(dataLayout);
        this->addChild( bboxDrawable );
        _drawableList[id].push_back(AnnoInfo(this->getNumChildren()-1, dataLayout, minRange));
    }
    // layout data for screenspace information
    updateLayoutData(dataLayout, style, geom);

    return dataLayout->getId();
}


void
MPAnnotationGroup::updateLayoutData(osg::ref_ptr<ScreenSpaceLayoutData>& dataLayout, const Style& style, Geometry* geom)
{
    if (! dataLayout.valid())
        return;

    const TextSymbol* ts = style.get<TextSymbol>();

    // compute the anchor point as the centroid of the geometry
    const osg::Vec3d center = geom->getCentroid();
    GeoPoint pos( osgEarth::SpatialReference::get("wgs84"), center.x(), center.y(), center.z(), ALTMODE_ABSOLUTE );
    osg::Vec3d p0;
    pos.toWorld(p0);
    dataLayout->setAnchorPoint(p0);

    // priority and pixel offset
    if (ts)
    {
        if (ts->priority().isSet())
            dataLayout->setPriority(static_cast<float>(style.getSymbol<TextSymbol>()->priority()->eval()));
        if (ts->pixelOffset().isSet())
            dataLayout->setPixelOffset(ts->pixelOffset().get());
    }

    // orientation
    // technic is to create a at 2500m from the anchor with the given bearing
    // then the projection in screenspace of both points will be used to compute the screen-space angle
    if (ts->geographicCourse().isSet() || (ts->autoRotateAlongLine().isSetTo(true) && ! ts->autoOffsetGeomWKT().isSet() ) )
    {
        double labelRotationRad = DBL_MAX;
        if (ts->geographicCourse().isSet())
        {
            labelRotationRad = osg::DegreesToRadians ( ts->geographicCourse()->eval() );
        }
        else
        {
            // get end point
            osg::Vec3d geomEnd = geom->getType() != Geometry::TYPE_MULTI ? geom->back() : static_cast<MultiGeometry*>(geom)->getComponents().front()->back();
            labelRotationRad = GeoMath::bearing(osg::DegreesToRadians(pos.y()),
                                                osg::DegreesToRadians(pos.x()),
                                                osg::DegreesToRadians(geomEnd.y()),
                                                osg::DegreesToRadians(geomEnd.x()));
        }

        double latRad;
        double longRad;
        GeoMath::destination(osg::DegreesToRadians(pos.y()),
                             osg::DegreesToRadians(pos.x()),
                             labelRotationRad,
                             2500.,
                             latRad,
                             longRad);

        osgEarth::GeoPoint lineEndPoint;
        lineEndPoint.set(osgEarth::SpatialReference::get("wgs84"),
                         osg::RadiansToDegrees(longRad),
                         osg::RadiansToDegrees(latRad),
                         0,
                         osgEarth::ALTMODE_ABSOLUTE);

        osg::Vec3d p1;
        lineEndPoint.toWorld(p1);
        dataLayout->setLineEndPoint(p1);
        dataLayout->setAutoRotate(true);
    }

    // sliding label

    Geometry* geomSupport = nullptr;
    LineString* geomLineString = nullptr;

    if( ts->autoOffsetAlongLine().get() )
    {
        if( ts->autoOffsetGeomWKT().isSet() )
        {
            std::string lineSupport = ts->autoOffsetGeomWKT()->eval();
            if (! lineSupport.empty() )
                geomSupport = osgEarth::Features::GeometryUtils::geometryFromWKT(lineSupport);
        }
        else
        {
            geomSupport = geom;
        }
    }

    if( (ts->autoOffsetGeomWKT().isSet() || ts->autoOffsetAlongLine().get() || ts->autoRotateAlongLine().get())
            && geomSupport && geomSupport->getComponentType() == Geometry::TYPE_LINESTRING )
    {
        if( geomSupport->getType() == Geometry::TYPE_LINESTRING)
        {
            geomLineString = dynamic_cast<LineString*>( geomSupport );
        }
        else
        {
            const MultiGeometry* geomMulti = dynamic_cast<MultiGeometry*>(geomSupport);
            if( geomMulti )
                geomLineString = dynamic_cast<LineString*>( geomMulti->getComponents().front().get() );
        }
    }

    if ( geomLineString &&  (ts->autoOffsetAlongLine().get() || ts->autoRotateAlongLine().get()) )
    {
        osg::Vec3d p1, p2;

        if( geomLineString )
        {
            GeoPoint geoStart( osgEarth::SpatialReference::get("wgs84"), geomLineString->front().x(), geomLineString->front().y(),
                    geomLineString->front().z(), ALTMODE_ABSOLUTE );
            GeoPoint geoEnd( osgEarth::SpatialReference::get("wgs84"), geomLineString->back().x(), geomLineString->back().y(),
                    geomLineString->back().z(), ALTMODE_ABSOLUTE );

            if( ts->autoOffsetGeomWKT().isSet() )
            {
                // Direction to the longest distance
                if( (geoStart.vec3d() - center).length2() > (geoEnd.vec3d() - center).length2() )
                {
                    geoEnd.toWorld(p1);
                    geoStart.toWorld(p2);
                }
                else
                {
                    geoStart.toWorld(p1);
                    geoEnd.toWorld(p2);
                }
            }
            else
            {
                geoEnd.toWorld(p1);
                geoStart.toWorld(p2);
            }
        }

        dataLayout->setLineStartPoint(p1);
        dataLayout->setLineEndPoint(p2);
        if( ts->autoOffsetGeomWKT().isSet() )
            dataLayout->setAnchorPoint(dataLayout->getLineStartPoint());

        dataLayout->setAutoFollowLine( ts->autoOffsetAlongLine().get() );
        dataLayout->setAutoRotate( ts->autoRotateAlongLine().get() );
    }

    // global BBox
    for ( auto i : _drawableList[dataLayout->getId()] )
        dataLayout->expandBboxBy(this->getChild(i.index)->asDrawable()->getBoundingBox());
}
