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

#include <osgEarthAnnotation/MPAnnotationGroup>
#include <osgEarthAnnotation/MPAnnotationDrawable>
#include <osgEarthAnnotation/AnnotationUtils>
#include <osgEarthAnnotation/BboxDrawable>
#include <osgEarth/VirtualProgram>
#include <osgEarth/Lighting>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/GeoMath>
#include <osgEarth/GLUtils>
#include <osgEarthFeatures/GeometryUtils>
#include <osg/Depth>

#define LC "[MPAnnotationGroup] "

using namespace osgEarth;
using namespace osgEarth::Annotation;


namespace
{
    const osg::Node::NodeMask nodeNoMask = 0xffffffff;
}


// Callback to properly cull the MPAnnotationGroup
class AnnotationNodeGroupCullCallback : public osg::NodeCallback
{
public:
    void operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osg::ref_ptr<osgUtil::CullVisitor> cullVisitor = nv->asCullVisitor();
        if ( ! cullVisitor->isCulled(node->getBound()) )
        {
            const osg::Matrix& MVPW = *(cullVisitor->getMVPW());
            float vpXmin = cullVisitor->getViewport()->x();
            float vpXmax = cullVisitor->getViewport()->x() + cullVisitor->getViewport()->width();
            float vpYmin = cullVisitor->getViewport()->y();
            float vpYmax = cullVisitor->getViewport()->y() + cullVisitor->getViewport()->height();
            double alt = DBL_MAX;
            cullVisitor->getCurrentCamera()->getUserValue("altitude", alt);

            osg::ref_ptr<MPAnnotationGroup> annoGroup = static_cast<MPAnnotationGroup*>(node);

            for (unsigned int i = 0 ; i < annoGroup->getNumChildren() ; ++i)
            {
                osg::ref_ptr<MPAnnotationDrawable> annoDrawable = static_cast<MPAnnotationDrawable*>(annoGroup->getChild(i));

                annoDrawable->_cull_anchorOnScreen = annoDrawable->_anchorPoint * MVPW;
                annoDrawable->_cull_bboxSymetricOnScreen.set(annoDrawable->_cull_anchorOnScreen + annoDrawable->getBBoxSymetric()._min,
                                                             annoDrawable->_cull_anchorOnScreen + annoDrawable->getBBoxSymetric()._max);

                if ( ! annoDrawable->isAutoFollowLine() )
                {
                    // out of viewport
                    if ( osg::maximum(annoDrawable->_cull_bboxSymetricOnScreen.xMin(), vpXmin) > osg::minimum(annoDrawable->_cull_bboxSymetricOnScreen.xMax(), vpXmax) ||
                         osg::maximum(annoDrawable->_cull_bboxSymetricOnScreen.yMin(), vpYmin) > osg::minimum(annoDrawable->_cull_bboxSymetricOnScreen.yMax(), vpYmax) )
                    {
                        annoDrawable->setNodeMask(0);
                        continue;
                    }
                }

                // in viewport
                annoDrawable->setAltitude( alt );
                bool annoIsHidden = annoDrawable->isFullyHidden();
                annoDrawable->setNodeMask( annoIsHidden ? 0 : nodeNoMask );

                // compute the screen angle if necessary
                if ( ! annoIsHidden && annoDrawable->isAutoRotate() )
                {
                    osg::Vec3d anchorToProj = annoDrawable->_lineEnd * MVPW;
                    anchorToProj -= annoDrawable->_cull_anchorOnScreen;
                    annoDrawable->_cull_rotationRadOnScreen = atan2(anchorToProj.y(), anchorToProj.x());
                }
            }

            traverse(node, nv);
        }
    }
};

// Need a custom bounding sphere for the culling process of the whole tile
osg::BoundingSphere MPAnnotationGroup::computeBound () const
{
    osg::BoundingSphere bsphere;

    for(osg::NodeList::const_iterator itr = _children.begin(); itr!=_children.end(); ++itr)
    {
        if (itr->valid())
        {
            osg::ref_ptr<const MPAnnotationDrawable> annoDrawable = static_cast<MPAnnotationDrawable*>(itr->get());
            bsphere.expandBy(annoDrawable->getAnchorPoint());
            if (annoDrawable->isAutoFollowLine())
            {
                bsphere.expandBy(annoDrawable->getLineStartPoint());
                bsphere.expandBy(annoDrawable->getLineEndPoint());
            }
        }
    }
    return bsphere;
}

MPAnnotationGroup::MPAnnotationGroup( bool lineSmooth ) : osg::Group()
{
    _rootStateSet = getOrCreateStateSet();

    // draw in the screen-space bin
    MPScreenSpaceLayout::activate(_rootStateSet.get());

    // stateset stuff
    _rootStateSet->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0, 1, false), 1);
    _rootStateSet->setDefine( OE_LIGHTING_DEFINE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE );
    _rootStateSet->setMode( GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED );
    _rootStateSet->setMode( GL_BLEND, osg::StateAttribute::ON );
    if ( lineSmooth )
        GLUtils::setLineSmooth(_rootStateSet, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE );

    addCullCallback(new AnnotationNodeGroupCullCallback());

    // This group makes its own shaders
    ShaderGenerator::setIgnoreHint(this, true);
}

long MPAnnotationGroup::addAnnotation(const Style& style, Geometry *geom, const osgDB::Options* readOptions)
{
    // unique id for this annotation
    static long id{0};
    long localId = ++id;

    // buid the single geometry which will gather all sub items and LODs
    const TextSymbol* ts = style.get<TextSymbol>();
    MPAnnotationDrawable* annoDrawable = new MPAnnotationDrawable(style, readOptions);
    annoDrawable->setId(localId);
    annoDrawable->setCullingActive(false);

    // compute the anchor point as the centroid of the geometry
    const osg::Vec3d center = geom->getCentroid();
    GeoPoint pos( osgEarth::SpatialReference::get("wgs84"), center.x(), center.y(), center.z(), ALTMODE_ABSOLUTE );
    osg::Vec3d p0;
    pos.toWorld(p0);
    annoDrawable->setAnchorPoint(p0);
    osg::BoundingSphere bSphere(annoDrawable->getBound());

    // priority and pixel offset
    if (ts && ts->priority().isSet())
        annoDrawable->setPriority(static_cast<float>(style.getSymbol<TextSymbol>()->priority()->eval()));

    // orientation
    // technic is to create a at 2500m from the anchor with the given bearing
    // then the projection in screenspace of both points will be used to compute the screen-space angle
    if (ts && (ts->geographicCourse().isSet() || (ts->autoRotateAlongLine().isSetTo(true) && ! ts->autoOffsetGeomWKT().isSet() )) )
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
        annoDrawable->setLineEndPoint(p1);
        annoDrawable->setAutoRotate(true);
    }

    // sliding label

    Geometry* geomSupport = nullptr;
    LineString* geomLineString = nullptr;

    if( ts && ts->autoOffsetAlongLine().get() )
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

    if( ts && (ts->autoOffsetGeomWKT().isSet() || ts->autoOffsetAlongLine().get() || ts->autoRotateAlongLine().get())
            && geomSupport && geomSupport->getComponentType() == Geometry::TYPE_LINESTRING )
    {
        if( geomSupport->getType() == Geometry::TYPE_LINESTRING)
        {
            geomLineString = dynamic_cast<LineString*>( geomSupport );
        }
        else if (const MultiGeometry* geomMulti = dynamic_cast<MultiGeometry*>(geomSupport))
        {
            geomLineString = dynamic_cast<LineString*>( geomMulti->getComponents().front().get() );
        }
    }

    if ( ts && geomLineString && (ts->autoOffsetAlongLine().get() || ts->autoRotateAlongLine().get()) )
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

        annoDrawable->setLineStartPoint(p1);
        annoDrawable->setLineEndPoint(p2);
        if( ts->autoOffsetGeomWKT().isSet() )
            annoDrawable->setAnchorPoint(annoDrawable->getLineStartPoint());

        annoDrawable->setAutoFollowLine( ts->autoOffsetAlongLine().get() );
        annoDrawable->setAutoRotate( ts->autoRotateAlongLine().get() );
    }

    this->addChild( annoDrawable );
    return localId;
}

void
MPAnnotationGroup::setHighlight( long id, bool highlight )
{
    for ( auto anno : getDrawableList()[id] )
        if (anno.type == MPAnnotationGroup::Bbox || anno.type == MPAnnotationGroup::BboxGroup)
        {
            BboxDrawable* bbox = static_cast<BboxDrawable*>(getChild(anno.index));
            if ( bbox && bbox->isHighlight() != highlight )
            {
                anno.globalSsld->_priority = highlight ? FLT_MAX - 1 : anno.globalSsld->_originalPriority;
                bbox->setHighlight(highlight);
            }
        }
}


void
MPAnnotationGroup::clearHighlight()
{
    for ( auto const &anno : getDrawableList() )
        setHighlight(anno.first, false);
}

void MPAnnotationGroup::setIconColor(long id, Color color){
    
    for ( auto anno : getDrawableList()[id] )
        if (anno.type == MPAnnotationGroup::Symbol)
        {
               osg::Geometry* icon = static_cast<osg::Geometry*>(getChild(anno.index));
               osg::Vec4Array* c = static_cast<osg::Vec4Array*>(icon->getColorArray());
               
               (*c)[0]=color;
        }    
}
