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
    const std::string undef = "-32765";
    const Color magenta(1., 135./255., 195./255.);
}


// Callback to properly cull the MPAnnotationGroup
class AnnotationNodeGroupCullCallback : public osg::NodeCallback
{
public:
    void operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        osgUtil::CullVisitor* cullVisitor = nv->asCullVisitor();
        if(! cullVisitor->isCulled(node->getBound()))
        {  OE_DEBUG << LC <<"AnnotationNodeGroupCullCallback"<<std::endl;
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

                    for (auto iAnno : anno.second)
                    {
                        if (iAnno.type == MPAnnotationGroup::BboxGroup)
                        {
                            osg::Node* child = annoGroup->getChild(iAnno.index);
                            BboxDrawable* bbox = static_cast<BboxDrawable*>(child);
                            
                            bbox->setNodeMask(iAnno.isVisible?nodeNoMask:0);
                            bbox->setReducedSize(alt >= iAnno.minRange);
                        }
                        else
                        {
                            annoGroup->getChild(iAnno.index)->setNodeMask(alt < iAnno.minRange ? (iAnno.isVisible?nodeNoMask:0) : 0);
                        }
                        
                            OE_DEBUG << LC << "annoGroup->getChild(iAnno.index)->getNodeMask():"<<annoGroup->getChild(iAnno.index)<<iAnno.index<<iAnno.isVisible<<"mask"<< annoGroup->getChild(iAnno.index)->getNodeMask()<<std::endl;
                    }
                }
            }

            traverse(node, nv);
        }
    }
};


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

    // check if there is a predefined organization
    const TextSymbol* textSymbol = style.get<TextSymbol>();
    bool predefinedOrganisation = textSymbol && textSymbol->predefinedOrganisation().isSet();
    StringVector textList;

    // ----------------------
    // Build image

    osg::ref_ptr<osg::Geometry> imageDrawable;
    osg::BoundingBox imageBox(0, 0, 0, 0, 0, 0);
    StringVector iconList;
    osg::ref_ptr<const InstanceSymbol> instance = style.get<InstanceSymbol>();
    const IconSymbol* icon = nullptr;
    if (instance.valid())
        icon = instance->asIcon();

    if (icon && icon->url().isSet())
    {
        // check if there is a list of icons
        std::string iconTxt = icon->url()->eval();
        if ( iconTxt.find(";") != std::string::npos )
        {
            StringTokenizer splitter( ";", "" );
            splitter.tokenize( iconTxt, iconList );
            if (! iconList.empty() )
                iconTxt = iconList[0];
        }

        imageDrawable = AnnotationUtils::createImageGeometry(iconTxt, icon, readOptions);
        if (imageDrawable.valid())
            imageBox = imageDrawable->getBoundingBox();
    }


    // ----------------------
    // Build text

    osg::ref_ptr<osgText::Text> textDrawable;
    if ( textSymbol )
    {
        TextSymbol::Alignment textAlignment = TextSymbol::Alignment::ALIGN_LEFT_CENTER;
        if ( imageDrawable.valid() && textSymbol->alignment().isSet() )
            textAlignment = textSymbol->alignment().value();

        osg::BoundingBox imageBoxWithMargin{imageBox};

        if ( icon && icon->margin().isSet() )
        {
            const float margin{icon->margin().value()};
            imageBoxWithMargin.expandBy({imageBox.xMin() - margin, imageBox.yMin() - margin, imageBox.zMin()});
            imageBoxWithMargin.expandBy({imageBox.xMax() + margin, imageBox.yMax() + margin, imageBox.zMax()});
        }

        std::string text = textSymbol->content()->eval();
        if ( predefinedOrganisation )
        {
            StringTokenizer splitter( ";", "" );
            splitter.tokenize( text, textList );
            if (! textList.empty() )
                text = textList[0];
        }
        if ( ! text.empty() )
            textDrawable = AnnotationUtils::createTextDrawable( text, textSymbol, imageBoxWithMargin );
    }


    // ----------------------
    // Create the other images if needed
    // and organize them left to right

    osg::NodeList imagesDrawable;
    if ( iconList.size() > 1 && ( textDrawable.valid() || imageDrawable.valid() ) )
    {
        float xOffset = textDrawable.valid() ? textDrawable->getBoundingBox().xMax() : imageDrawable->getBoundingBox().xMax();
        for ( unsigned int i=1 ; i<iconList.size() ; ++i )
        {
            if ( osg::Geometry* subImageDrawable = AnnotationUtils::createImageGeometry(iconList[i], icon, readOptions, xOffset) )
            {
                imagesDrawable.push_back( subImageDrawable );
                xOffset = subImageDrawable->getBoundingBox().xMax();
            }
        }
    }


    // ----------------------
    // Build BBox

    // The bounding box can enclose either the text, or the image, or both
    osg::ref_ptr<osg::Drawable> bboxDrawable;
    const BBoxSymbol* bboxsymbol = style.get<BBoxSymbol>();
    if ( bboxsymbol )
    {
        float sideMargin = icon && icon->margin().isSet() ? icon->margin().value() : 5.f;

        if ( bboxsymbol->group() == BBoxSymbol::GROUP_ICON_ONLY && imageDrawable.valid() )
            bboxDrawable = new BboxDrawable( imageDrawable->getBoundingBox(), *bboxsymbol );
        else if ( bboxsymbol->group() == BBoxSymbol::GROUP_TEXT_ONLY && textDrawable.valid() )
            bboxDrawable = new BboxDrawable( textDrawable->getBoundingBox(), *bboxsymbol, sideMargin );
        else if ( bboxsymbol->group() == BBoxSymbol::GROUP_ICON_AND_TEXT && textDrawable.valid() && imageDrawable.valid() )
        {
            if ( imagesDrawable.empty() )
                bboxDrawable = new BboxDrawable( imageDrawable->getBoundingBox(), textDrawable->getBoundingBox(), *bboxsymbol, sideMargin );
            else
                bboxDrawable = new BboxDrawable( imageDrawable->getBoundingBox(),
                                                 static_cast<osg::Drawable*>(imagesDrawable.back().get())->getBoundingBox(), *bboxsymbol, sideMargin );
        }
    }

    // ----------------------
    // Create the other texts if needed
    // only airway organisation treated for now

    osg::NodeList textsDrawable;
    if ( predefinedOrganisation && textSymbol->predefinedOrganisation().isSetTo("airway") && textList.size() == 7 && bboxDrawable.valid())
    {
        double margin = textSymbol->predefinedOrganisationMargin().get();
        if ( bboxsymbol && bboxsymbol->border().isSet() && bboxsymbol->border().get().width().isSet() )
            margin += bboxsymbol->border().get().width().get();
        else
            margin += 1.;
        osg::Vec3 marginVec(margin, margin, margin);
        osg::BoundingBox refBBox( bboxDrawable->getBoundingBox()._min - marginVec, bboxDrawable->getBoundingBox()._max + marginVec);       

        //------> TODO This part should be out of MPAnnotationGroup ...
        TextSymbol::Alignment alignList[] = { TextSymbol::ALIGN_LEFT_BOTTOM, TextSymbol::ALIGN_LEFT_BOTTOM_BASE_LINE, TextSymbol::ALIGN_LEFT_TOP,
                                             TextSymbol::ALIGN_RIGHT_TOP, TextSymbol::ALIGN_RIGHT_BOTTOM_BASE_LINE, TextSymbol::ALIGN_RIGHT_BOTTOM };

        float fontSizeOrg = textSymbol->size().isSet() ? textSymbol->size()->eval() : 16.f;
        float fontSizeSmaller = fontSizeOrg / 18.f * 15.f;
        float fontSizeList[] = {fontSizeSmaller, fontSizeSmaller, fontSizeSmaller, fontSizeSmaller, fontSizeSmaller, fontSizeSmaller};

        Color colorOrg = textSymbol->fill().isSet() ? textSymbol->fill().get().color() : Color::White;
        Color colorList[] = {colorOrg, colorOrg, colorOrg, magenta, colorOrg, colorOrg};

        bool nativeBBox[] = {true, false, false, false, false, false};
        //<------

        // for each text (except the first one)
        for ( unsigned int i=1 ; i<textList.size() ; ++i )
        {
            if ( textList[i].empty() || textList[i] == undef)
                continue;

            // specific treatments
            if ( textList[i].find(":") == 1 )
            {
                // F: means that the value must be display with format FLxxx
                // D: means that the value must be display with format xxx°
                // R: means that the value must be display with format Rxxx
                StringVector textSplit;
                StringTokenizer splitter( ":", "" );
                splitter.tokenize( textList[i], textSplit );
                if ( textSplit.size() == 2 && textSplit[1] != undef)
                {
                    // convert to int
                    int val;
                    std::istringstream(textSplit[1]) >> val;

                    // add the FL symbol if required
                    if ( textSplit[0] == "F" )
                    {
                        textList[i] = "FL" + std::to_string(val/100);
                    }
                    // add the R symbol if required
                    else if ( textSplit[0] == "R" )
                    {
                        textList[i] = "R" + std::to_string(val);
                    }
                    // add the ° symbol if required and make sure it has always three digits
                    else if ( textSplit[0] == "D" )
                    {
                        if ( val < 10 ) textList[i] = "00" + textSplit[1] + "°";
                        else if ( val < 100 ) textList[i] = "0" + textSplit[1] + "°";
                        else textList[i] = textSplit[1] + "°";
                    }
                    else
                    {
                        textList[i] = textSplit[1];
                    }
                }
                else
                {
                    continue;
                }
            }

            // initialiaze the symbol by copy (to copy the font for example)
            osg::ref_ptr<TextSymbol> subTextSym = new TextSymbol(*textSymbol);
            subTextSym->alignment() = alignList[i-1];
            subTextSym->size() = fontSizeList[i-1];
            subTextSym->fill() = colorList[i-1];
            if (subTextSym->font().isSet())
            {
                std::string font(subTextSym->font().get());
                replaceIn(font, "Bold", "Regular"); // all sub items must be regular font
                subTextSym->font() = font;
            }

            osgText::Text* subText = AnnotationUtils::createTextDrawable( textList[i], subTextSym.get(), refBBox, nativeBBox[i-1] );
            textsDrawable.push_back( subText );
        }
    }


    // ----------------------
    // Common settings

    double minRange = textSymbol->minRange().isSet() ? textSymbol->minRange().value() : DBL_MAX;
    double minRange2ndlevel = textSymbol->minRange2ndlevel().isSet() ? textSymbol->minRange2ndlevel().value() : DBL_MAX;
    if ( imageDrawable.valid() )
    {
        imageDrawable->setCullingActive(false);
        imageDrawable->setDataVariance(osg::Object::DYNAMIC);
        imageDrawable->setUserData(dataLayout);
        this->addChild( imageDrawable );
        _drawableList[id].push_back(AnnoInfo(Symbol, this->getNumChildren()-1, dataLayout,true));
    }
    for ( auto node : imagesDrawable )
    {
        node->setCullingActive(false);
        node->setDataVariance(osg::Object::DYNAMIC);
        node->setUserData(dataLayout);
        this->addChild( node );
        _drawableList[id].push_back(AnnoInfo(Text, this->getNumChildren()-1, dataLayout, minRange,true));
    }
    if (  textDrawable.valid() )
    {
        textDrawable->setCullingActive(false);
        textDrawable->setDataVariance(osg::Object::DYNAMIC);
        textDrawable->setUserData(dataLayout);
        this->addChild( textDrawable );
        _drawableList[id].push_back(AnnoInfo(Text, this->getNumChildren()-1, dataLayout, minRange,true));
    }
    for ( auto node : textsDrawable )
    {
        node->setCullingActive(false);
        node->setDataVariance(osg::Object::DYNAMIC);
        node->setUserData(dataLayout);
        this->addChild( node );
        _drawableList[id].push_back(AnnoInfo(Text, this->getNumChildren()-1, dataLayout, minRange2ndlevel,true));
    }
    if ( bboxDrawable.valid() )
    {
        bboxDrawable->setCullingActive(false);
        bboxDrawable->setDataVariance(osg::Object::DYNAMIC);
        bboxDrawable->setUserData(dataLayout);
        this->addChild( bboxDrawable );
        if ( bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_AND_TEXT )
            _drawableList[id].push_back(AnnoInfo(BboxGroup, this->getNumChildren()-1, dataLayout, minRange,true));
        else if ( bboxsymbol->group() == BBoxSymbol::BboxGroup::GROUP_ICON_ONLY )
            _drawableList[id].push_back(AnnoInfo(Bbox, this->getNumChildren()-1, dataLayout,true));
        else
            _drawableList[id].push_back(AnnoInfo(Bbox, this->getNumChildren()-1, dataLayout, minRange,true));
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
