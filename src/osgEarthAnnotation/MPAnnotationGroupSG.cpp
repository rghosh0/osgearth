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

#include <osgEarthAnnotation/MPAnnotationGroupSG.>
#include <osgEarthAnnotation/MPAnnotationDrawable>
#include <osgEarthAnnotation/AnnotationUtils>
#include <osgEarth/VirtualProgram>
#include <osgEarth/Lighting>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/GeoMath>
#include <osgEarth/GLUtils>
#include <osgEarthFeatures/GeometryUtils>
#include <osg/Depth>

#define LC "[MPAnnotationGroupSG] "

using namespace osgEarth;
using namespace osgEarth::Annotation;


osg::observer_ptr<MPStateSetFontAltas> MPAnnotationGroupSG::s_atlasStateSet;


namespace
{
    const osg::Node::NodeMask nodeNoMask = 0xffffffff;
    const float PIby2 = osg::PI / 2.f;


    // Callback to properly cull the MPAnnotationGroup2
    class AnnotationNodeGroupCullCallbackSG : public osg::NodeCallback
    {
    public:
        AnnotationNodeGroupCullCallbackSG( bool backCull = false ) : _backCull(backCull) { };

        virtual ~AnnotationNodeGroupCullCallbackSG() { }

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

                osg::ref_ptr<MPAnnotationGroupSG> annoGroup = static_cast<MPAnnotationGroupSG*>(node);

                for (unsigned int i = 0 ; i < annoGroup->getNumChildren() ; ++i)
                {
                    osg::ref_ptr<MPAnnotationDrawable> annoDrawable = static_cast<MPAnnotationDrawable*>(annoGroup->getChild(i));

                    annoDrawable->_cull_anchorOnScreen = annoDrawable->_anchorPoint * MVPW;
                    annoDrawable->_cull_bboxSymetricOnScreen.set(annoDrawable->_cull_anchorOnScreen + annoDrawable->getBBoxSymetric()._min,
                                                                 annoDrawable->_cull_anchorOnScreen + annoDrawable->getBBoxSymetric()._max);


                    // check opposite side of the globe
                    if ( _backCull && (cullVisitor->getEyePoint() - annoDrawable->_anchorPoint).length2() > cullVisitor->getEyePoint().length2() )
                    {
                        annoDrawable->setNodeMask(0);
                        continue;
                    }

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
                        osg::Vec3 anchorToProj = annoDrawable->_lineEnd * MVPW;
                        anchorToProj -= annoDrawable->_cull_anchorOnScreen;
                        annoDrawable->_cull_rotationRadOnScreen = atan2(anchorToProj.y(), anchorToProj.x());
                        annoDrawable->setInverted( annoDrawable->_cull_rotationRadOnScreen < - PIby2 || annoDrawable->_cull_rotationRadOnScreen > PIby2);
                        if ( annoDrawable->isInverted() )
                        {
                            if ( annoDrawable->_cull_rotationRadOnScreen > 0. )
                                annoDrawable->_cull_rotationRadOnScreen -= osg::PI;
                            else
                                annoDrawable->_cull_rotationRadOnScreen += osg::PI;
                        }
                    }
                }

                traverse(node, nv);
            }
        }

        bool _backCull{false};
    };
}



// Need a custom bounding sphere for the culling process of the whole tile
osg::BoundingSphere MPAnnotationGroupSG::computeBound () const
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

MPAnnotationGroupSG::MPAnnotationGroupSG(const osgDB::Options *readOptions , TextSymbol *textSym) : MPAnnotationGroup()
{
    // build the shared state set
    if ( ! s_atlasStateSet.lock(_atlasStateSet) )
    {
        static Threading::Mutex s_mutex;
        Threading::ScopedMutexLock lock(s_mutex);

        if ( ! s_atlasStateSet.lock(_atlasStateSet) )
        {

            if ( ! MPScreenSpaceLayoutSG::isExtensionLoaded()
                 || ! MPScreenSpaceLayoutSG::getOptions().fontAltas().isSet()
                 || ! MPScreenSpaceLayoutSG::getOptions().iconAltas().isSet() )
            {
                OE_WARN << LC << "Impossible to create StateSet because MPScreenSpaceLayoutSGis not well defined." << "\n";
                return;
            }

            std::string atlasFontPath = MPScreenSpaceLayoutSG::getOptions().fontAltas().get();
            std::string atlasIconPath = MPScreenSpaceLayoutSG::getOptions().iconAltas().get();
            s_atlasStateSet = _atlasStateSet = new MPStateSetFontAltas(atlasFontPath, atlasIconPath, readOptions);
            // draw in the screen-space bin
            MPScreenSpaceLayoutSG::activate(_atlasStateSet.get());
            // no depth test in screen space layout
            _atlasStateSet->setMode( GL_DEPTH_TEST, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE );
        }
    }

    setStateSet(_atlasStateSet.get());

    bool backCull = textSym && textSym->backEarthCull().isSetTo(true);
    addCullCallback(new AnnotationNodeGroupCullCallbackSG(  backCull ));

    // This group makes its own shaders
    ShaderGenerator::setIgnoreHint(this, true);
}

long MPAnnotationGroupSG::addAnnotation(const Style& style, Geometry *geom, const osgDB::Options* readOptions)
{
    if ( ! _atlasStateSet.valid() )
        return -1;

    // unique id for this annotation
    static long id{0};
    long localId = ++id;

    // buid the single geometry which will gather all sub items and LODs
    MPAnnotationDrawable* annoDrawable = new MPAnnotationDrawable(style, readOptions, _atlasStateSet.get());
    annoDrawable->setId(localId);
    annoDrawable->setCullingActive(false);
    annoDrawable->setDataVariance(DataVariance::DYNAMIC);

    // compute the anchor point as the centroid of the geometry
    const osg::Vec3d center = geom->getCentroid();
    GeoPoint pos( osgEarth::SpatialReference::get("wgs84"), center.x(), center.y(), center.z(), ALTMODE_ABSOLUTE );
    osg::Vec3d p0;
    pos.toWorld(p0);
    annoDrawable->setAnchorPoint(p0);
    //osg::BoundingSphere bSphere(annoDrawable->getBound());

    // priority
    const TextSymbol* ts = style.get<TextSymbol>();
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
    _mainGeomDrawableList[localId] = annoDrawable;
    return localId;
}


void
MPAnnotationGroupSG::setHighlight( long id, bool highlight )
{
    MainGeomList::const_iterator itr = _mainGeomDrawableList.find( id );
    if ( itr != _mainGeomDrawableList.end() )
        static_cast<MPAnnotationDrawable*>( itr->second.get() )->setHighlight(highlight);

}


void
MPAnnotationGroupSG::clearHighlight()
{
    for (osg::NodeList::iterator itr = _children.begin(); itr != _children.end(); ++itr)
        static_cast<MPAnnotationDrawable*>((*itr).get())->setHighlight(false);
}

void
MPAnnotationGroupSG::setIconColor(long id, const Color& color)
{
    MainGeomList::const_iterator itr = _mainGeomDrawableList.find( id );
    if ( itr != _mainGeomDrawableList.end() )
        static_cast<MPAnnotationDrawable*>( itr->second.get() )->setIconColor(color);
}
