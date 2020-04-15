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
#include <osgEarthFeatures/LabelSource>
#include <osgEarthFeatures/FeatureSourceIndexNode>
#include <osgEarthFeatures/FilterContext>

#define LC "[MPAnnotationLabelSource] "

using namespace osgEarth;
using namespace osgEarth::Annotation;
using namespace osgEarth::Features;

// -----------------------------------------------------------
// This class is mainly copied from AnnotationLabelSource
// but with performance improvements
// -----------------------------------------------------------

class MPAnnotationLabelSource : public LabelSource
{

public:

    MPAnnotationLabelSource( const LabelSourceOptions& options )
        : LabelSource( options )
    {
        //nop
    }

    /**
     * Creates a complete set of positioned label nodes from a feature list.
     */
    osg::Node* createNode(
            const FeatureList&   input,
            const Style&         style,
            FilterContext&       context )
    {
        if ( style.get<TextSymbol>() == nullptr && style.get<IconSymbol>() == nullptr )
            return nullptr;

        // provide some performance info
        osg::Timer_t t_start;
        if(osgEarth::isNotifyEnabled( osg::DEBUG_INFO ))
            t_start = osg::Timer::instance()->tick();

        // copy the style so we can (potentially) modify the text symbol.
        Style styleCopy = style;
        TextSymbol* text = styleCopy.get<TextSymbol>();
        BBoxSymbol* bbox = styleCopy.get<BBoxSymbol>();
        IconSymbol* icon = styleCopy.get<IconSymbol>();
        AltitudeSymbol* alt = styleCopy.get<AltitudeSymbol>();

        // attach point for all drawables
        MPAnnotationGroup* root = new MPAnnotationGroup(bbox && bbox->border().isSet() && bbox->border()->smooth().isSetTo(true));

        StringExpression  textContentExpr ( text ? *text->content()  : StringExpression() );
        NumericExpression textPriorityExpr( text ? *text->priority() : NumericExpression() );
        NumericExpression textSizeExpr    ( text ? *text->size()     : NumericExpression() );
        NumericExpression textRotationExpr( text ? *text->onScreenRotation() : NumericExpression() );
        NumericExpression textCourseExpr  ( text ? *text->geographicCourse() : NumericExpression() );
        StringExpression  textOffsetSupportExpr ( text ? *text->autoOffsetGeomWKT()  : StringExpression() );
        StringExpression  bboxDirectionExpr     ( bbox ? *bbox->direction()  : StringExpression() );
        StringExpression  iconUrlExpr     ( icon ? *icon->url()      : StringExpression() );
        NumericExpression iconScaleExpr   ( icon ? *icon->scale()    : NumericExpression() );
        NumericExpression iconHeadingExpr ( icon ? *icon->heading()  : NumericExpression() );
        
        NumericExpression vertOffsetExpr  ( alt  ? *alt->verticalOffset() : NumericExpression() );

        for( FeatureList::const_iterator i = input.begin(); i != input.end(); ++i )
        {
            Feature* feature = i->get();
            if ( !feature )
                continue;

            // run a symbol script if present.
            if ( text && text->script().isSet() )
            {
                StringExpression temp( text->script().get() );
                feature->eval( temp, &context );
            }

            // run a symbol script if present.
            if ( icon && icon->script().isSet() )
            {
                StringExpression temp( icon->script().get() );
                feature->eval( temp, &context );
            }

            const Geometry* geom = feature->getGeometry();
            if ( !geom )
                continue;

            Style tempStyle = styleCopy;
            
            bool isPerVertex=false;

            // evaluate expressions into literals.
            // TODO: Later we could replace this with a generate "expression evaluator" type
            // that we could pass to PlaceNode in the DB options. -gw

            if ( text )
            {
                if ( text->content().isSet() )
                    tempStyle.get<TextSymbol>()->content()->setLiteral( feature->eval( textContentExpr, &context ) );

                if ( text->priority().isSet() )
                    tempStyle.get<TextSymbol>()->priority()->setLiteral( feature->eval( textPriorityExpr, &context ) );

                if ( text->size().isSet() )
                    tempStyle.get<TextSymbol>()->size()->setLiteral( feature->eval(textSizeExpr, &context) );

                if ( text->onScreenRotation().isSet() )
                    tempStyle.get<TextSymbol>()->onScreenRotation()->setLiteral( feature->eval(textRotationExpr, &context) );

                if ( text->geographicCourse().isSet() )
                    tempStyle.get<TextSymbol>()->geographicCourse()->setLiteral( feature->eval(textCourseExpr, &context) );

                if ( text->autoOffsetGeomWKT().isSet() )
                    tempStyle.get<TextSymbol>()->autoOffsetGeomWKT()->setLiteral( feature->eval( textOffsetSupportExpr, &context ) );
            }

            if ( bbox )
            {
                if ( bbox->direction().isSet() )
                    tempStyle.get<BBoxSymbol>()->direction()->setLiteral( feature->eval(bboxDirectionExpr, &context) );
            }

            if ( icon )
            {
                if ( icon->url().isSet() )
                    tempStyle.get<IconSymbol>()->url()->setLiteral( feature->eval(iconUrlExpr, &context) );

                if ( icon->scale().isSet() )
                    tempStyle.get<IconSymbol>()->scale()->setLiteral( feature->eval(iconScaleExpr, &context) );

                if ( icon->heading().isSet() )
                    tempStyle.get<IconSymbol>()->heading()->setLiteral( feature->eval(iconHeadingExpr, &context) );
                
                if ( icon->placement().isSetTo(IconSymbol::PLACEMENT_VERTEX) ) 
                {
                     OE_DEBUG << LC << "mp_icon per vertex\n";
                     isPerVertex=true;
                }
            }
            
            if(isPerVertex &&                 
                feature->getGeometry()->getComponentType() == Geometry::TYPE_LINESTRING) {
                
                
                OE_DEBUG << LC << "per vertex multiline n";
                 LineString * geomLineString ;
                if(feature->getGeometry()->getType() == Geometry::TYPE_MULTI )
                {
                    const MultiGeometry* geomMulti = dynamic_cast<MultiGeometry*>(feature->getGeometry());           
                    geomLineString = dynamic_cast<LineString*>( geomMulti->getComponents().front().get() );
                } else
                {
                    geomLineString = dynamic_cast<LineString*>( feature->getGeometry());
                }
                
                unsigned long long ii=0;
                
                int step=(geomLineString->size()-1);
                for( unsigned long long i =0 ; i<geomLineString->size() ; i=i+step ){
                    
                    osg::ref_ptr<LineString> sampledLine= new LineString();
                    sampledLine->push_back(geomLineString->at(i));
                    int  nextOrPrevious=(i==geomLineString->size()-1)?-step:step;
                    sampledLine->push_back(geomLineString->at((i+nextOrPrevious)));
                            
                    // actually build the scenegraph related to this feature
                    long id = root->addAnnotation(tempStyle,sampledLine, context.getDBOptions(),ii);
                    
                    
                    
                    // tag the drawables for that the feature can be retrieved when picking
                    if ( context.featureIndex() && id >= 0 )
                    {
                        std::vector<MPAnnotationGroup::AnnoInfo> drawableList = root->getDrawableList(id);
                        for (auto iAnno : drawableList )
                        context.featureIndex()->tagDrawable(root->getChild(iAnno.index)->asDrawable(), feature);
                        }
                    
                    ii++;
                
                }
                 
            
                
                
            } else { 

            // actually build the scenegraph related to this feature
            long id = root->addAnnotation(tempStyle, feature->getGeometry(), context.getDBOptions());

            
            
            // tag the drawables for that the feature can be retrieved when picking
            if ( context.featureIndex() && id >= 0 )
            {
                std::vector<MPAnnotationGroup::AnnoInfo> drawableList = root->getDrawableList(id);
                for (auto iAnno : drawableList )
                    context.featureIndex()->tagDrawable(root->getChild(iAnno.index)->asDrawable(), feature);
            }
            
            }
        }

        // may be unnecessary
        root->dirtyBound();

        // provide some performance info
        if ( osgEarth::isNotifyEnabled( osg::DEBUG_INFO ) )
        {
            osg::Timer_t t_end = osg::Timer::instance()->tick();
            double t = osg::Timer::instance()->delta_s(t_start, t_end);
            OE_DEBUG << LC << "Profiling the Annotation group factory:\n";
            OE_DEBUG << LC << "    time to build " << t << "s\n";
            OE_DEBUG << LC << "    num features " << std::dec << input.size() << "\n";
            OE_DEBUG << LC << "    num drawables " << std::dec << root->getNumChildren() << "\n";
        }

        return root;
    }
};


//------------------------------------------------------------------------

class MPAnnotationLabelSourceDriver : public LabelSourceDriver
{
public:
    MPAnnotationLabelSourceDriver()
    {
        supportsExtension( "osgearth_label_mp_screen_annotations", "mission+ annotation label plugin" );
    }

    virtual const char* className() const
    {
        return "mission+ annotation label plugin";
    }

    virtual ReadResult readObject(const std::string& file_name, const Options* options) const
    {
        if ( !acceptsExtension(osgDB::getLowerCaseFileExtension( file_name )))
            return ReadResult::FILE_NOT_HANDLED;

        return new MPAnnotationLabelSource( getLabelSourceOptions(options) );
    }
};

REGISTER_OSGPLUGIN(osgearth_label_mp_screen_annotations, MPAnnotationLabelSourceDriver)
