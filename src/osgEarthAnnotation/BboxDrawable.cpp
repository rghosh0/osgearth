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
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include <osgEarthAnnotation/BboxDrawable>
#include <osgEarthAnnotation/AnnotationUtils>

#include <osg/LineWidth>

using namespace osgEarth;
using namespace osgEarth::Annotation;


//------------------------------------------------------------------------

BboxDrawable::BboxDrawable( const osg::BoundingBox& box, const BBoxSymbol &bboxSymbol, float sideMargin ) :
osg::Geometry()
{
    osg::BoundingBox boxBBox(box);
    boxBBox.xMin() -= sideMargin;
    boxBBox.xMax() += sideMargin;
    build(boxBBox, bboxSymbol);
}


BboxDrawable::BboxDrawable( const osg::BoundingBox& boxImg, const osg::BoundingBox& boxText, const BBoxSymbol& boxSymbol, float sideMargin )
{
    if ( boxSymbol.group() == BBoxSymbol::BboxGroup::GROUP_ICON_ONLY )
    {
        build(boxImg, boxSymbol);
    }

    else if ( boxSymbol.group() == BBoxSymbol::BboxGroup::GROUP_TEXT_ONLY )
    {
        osg::BoundingBox box(boxText);
        box.xMin() -= sideMargin;
        box.xMax() += sideMargin;
        build(box, boxSymbol);
    }

    else if ( boxSymbol.group() == BBoxSymbol::BboxGroup::GROUP_ICON_AND_TEXT )
    {
        osg::BoundingBox groupBBox;
        groupBBox.expandBy( boxImg );
        groupBBox.expandBy( boxText );
        groupBBox.xMax() += sideMargin;
        _widthReduction = groupBBox.xMax() - boxImg.xMax();
        _sizeReduced = false;
        build(groupBBox, boxSymbol);
    }
}


void
BboxDrawable::setHighlight( bool highlight )
{
    if ( _isHighlight == highlight )
        return;

    _isHighlight = highlight;

    osg::Vec4Array* c = static_cast<osg::Vec4Array*>(getColorArray());

    // update bbox fill color
    (*c)[0] = _isHighlight ? _highlightFillColor : _originalFillColor;

    // update the stroke color
    if ( _withBorder )
    {
        (*c)[1] = _isHighlight ? _highlightStrokeColor : _originalStrokeColor;
    }

    // add the stroke
    else if ( _isHighlight )
    {
        c->push_back( _highlightStrokeColor );
        //getOrCreateStateSet()->setAttribute( new osg::LineWidth( 4 ));
        addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, 0, static_cast<int>(getVertexArray()->getNumElements())) );
    }

    // remove the stroke
    else
    {
        c->pop_back();
        removePrimitiveSet(getNumPrimitiveSets()-1);
    }
}

void
BboxDrawable::setReducedSize(bool sizeReduced)
{
    if (sizeReduced == _sizeReduced) return;

    osg::Vec3Array* v = static_cast<osg::Vec3Array*>(getVertexArray());

    // case move the right part of the box to the left
    if (sizeReduced)
    {
        if ( v->size() == 4 )
        {
            v->at(0).x() -= _widthReduction;
            v->at(3).x() -= _widthReduction;
        }
        else
        {
            for (unsigned int it = 0; it <= _indexEndRightSide; it++)
            {
                v->at(it).x() -= _widthReduction;
            }
        }
        _sizeReduced = true;
    }

    // case move the right part of the box to the right
    else
    {
        if ( v->size() == 4 )
        {
            v->at(0).x() += _widthReduction;
            v->at(3).x() += _widthReduction;
        }
        else
        {
            for (unsigned int it = 0; it <= _indexEndRightSide; it++)
            {
                v->at(it).x() += _widthReduction;
            }
        }
        _sizeReduced = false;
    }
    v->dirty();
    dirtyBound();
}


void
BboxDrawable::build( const osg::BoundingBox& box, const BBoxSymbol &bboxSymbol )
{
    setUseVertexBufferObjects(true);

    float margin = bboxSymbol.margin().isSet() ? bboxSymbol.margin().value() : 2.f;
    osg::Vec3Array* v = new osg::Vec3Array();

    if ( bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_BOX_ROUNDED) || bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_BOX_ROUNDED_INNER) )
    {
        osg::Vec3 dump;
        const int nbSteps = 9;
        float angleStep = osg::PI / nbSteps;
        float radius = ((box.yMax()-box.yMin())/2.f) + margin;
        float boxSemiHeight = (box.yMax() - box.yMin()) / 2.f/* + margin*/;
        osg::Vec3 center (box.xMax(), (box.yMax()+box.yMin())/2.f, 0.f);
        if ( bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_BOX_ROUNDED_INNER) )
            center.x() -= boxSemiHeight;

        v->push_back( osg::Vec3(center.x(), box.yMin()-margin, 0) );
        float angle = - osg::PIf / 2.f;
        for (int step = 1 ; step < nbSteps ; step++ )
        {
            angle += angleStep;
            v->push_back(center + osg::Vec3((cosf(angle)*radius), sinf(angle)*radius, 0.f));
        }
        v->push_back( osg::Vec3(center.x(), box.yMax()+margin, 0.f) );

        int sizeBeforePush = v->size();
        _indexEndRightSide = v->size() - 1;
        float leftShift = box.xMax()-box.xMin();
        if (bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_BOX_ROUNDED_INNER))
            leftShift -= 2.f * boxSemiHeight;

        for (int i = sizeBeforePush-1; i >= 0; --i)
        {
            dump = v->at(i);
            v->push_back(osg::Vec3(2*center.x()-dump.x()-leftShift, dump.y(), 0.f));
        }
    }

    else if ( bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_BOX_ROUNDED_ORIENTED) )
    {
        const float hMed = (box.yMax() - box.yMin() + 2.f * margin) * 0.5f;
        const float arrowEdgeX = hMed / 1.375f;

        // Bottom right
        v->push_back( osg::Vec3(box.xMax() + margin, box.yMin() - margin, 0.f) );
        // Right angle shape
        v->push_back( osg::Vec3(box.xMax() + margin + arrowEdgeX, box.yMax() + margin - hMed, 0.f) );
        // Top right
        v->push_back( osg::Vec3(box.xMax() + margin, box.yMax() + margin, 0.f) );

        // Top left
        v->push_back( osg::Vec3(box.xMin() - margin, box.yMax() + margin, 0.f) );

        constexpr int nbSteps = 9;
        constexpr float angleStep = osg::PI / nbSteps;
        const float radius = ((box.yMax() - box.yMin()) / 2.f) + margin;
        osg::Vec3 center(box.xMin(), (box.yMax() + box.yMin()) / 2.f, 0.f);
        float angle = osg::PIf / 2.f;

        // Left rounded shape
        for (int step = 1; step < nbSteps; step++ )
        {
            angle += angleStep;
            v->push_back(center + osg::Vec3((std::cosf(angle) * radius), std::sinf(angle) * radius, 0.f));
        }
        // Bottom left
        v->push_back( osg::Vec3(box.xMin() - margin, box.yMin() - margin, 0.f) );
    }

    else if ( bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_STAIR) )
    {
        float xMax = std::max( box.xMax(), -box.xMin() );
        v->push_back( osg::Vec3( xMax + margin, box.yMax() + margin, 0.) );
        v->push_back( osg::Vec3( 0., box.yMax() + margin, 0.) );
        v->push_back( osg::Vec3( 0., box.yMin() - margin, 0.) );
        v->push_back( osg::Vec3( - xMax - margin, box.yMin() - margin, 0.) );
    }

    else
    {
        float shiftRight = 0.f;
        float hMed = (box.yMax()-box.yMin()+2.f*margin) * 0.5f;
        float arrowEdgeX = hMed / 1.375f;
        std::string dir = bboxSymbol.direction().isSet() ? bboxSymbol.direction()->eval() : "";
        // possible values for the direction are 'F' (forward), 'B' (Backward) or Blank (Both directions)
        bool drawRightDir = bboxSymbol.geom().isSet()
                        && (bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED
                          || bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_SYM
                          || ( bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_2WAYS && dir != "B") );
        bool drawLeftDir = bboxSymbol.geom().isSet()
                        && bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_2WAYS && (dir.empty() || dir == "B");

        // draw arrow to the right
        if ( drawRightDir )
        {
            if( bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_SYM )
                shiftRight = - arrowEdgeX;

            v->push_back( osg::Vec3(box.xMax()+margin+arrowEdgeX+shiftRight, box.yMax()+margin-hMed, 0.f) );
        }

        v->push_back( osg::Vec3(box.xMax()+margin+shiftRight, box.yMax()+margin, 0.f) );
        v->push_back( osg::Vec3(box.xMin()-margin, box.yMax()+margin, 0.f) );
        // draw arrow to the left if required
        if( drawLeftDir )
            v->push_back( osg::Vec3(box.xMin()-margin-arrowEdgeX, box.yMax()+margin-hMed, 0.f) );
        v->push_back( osg::Vec3(box.xMin()-margin, box.yMin()-margin, 0.f) );
        v->push_back( osg::Vec3(box.xMax()+margin+shiftRight, box.yMin()-margin, 0.f) );
    }

    setVertexArray(v);

    if ( v->getVertexBufferObject() )
        v->getVertexBufferObject()->setUsage(GL_STATIC_DRAW_ARB);

    osg::Vec4Array* c = new osg::Vec4Array(osg::Array::BIND_PER_PRIMITIVE_SET);
    if ( bboxSymbol.fill().isSet() )
    {
        _originalFillColor = bboxSymbol.fill()->color();
        c->push_back( _originalFillColor );
        osg::DrawElements* de = new osg::DrawElementsUByte(GL_TRIANGLE_STRIP);
        if ( v->size() == 4 )
        {
            de->addElement(0);
            de->addElement(1);
            de->addElement(3);
            de->addElement(2);
        }
        else
        {
            de->addElement(0);
            int prevIndex = 0;
            int nextIndex = v->size() - 1;
            bool up = true;
            int step = 0;
            while ( abs(nextIndex - prevIndex) >= 1)
            {
                de->addElement(nextIndex);
                prevIndex = nextIndex;
                up = !up;
                if( ! up )
                    step += 1;
                nextIndex = up ? v->size() - 1 - step : step;
            }
        }
        addPrimitiveSet(de);
    }

    if ( bboxSymbol.border().isSet() )
    {
        _withBorder = true;
        _originalStrokeColor = bboxSymbol.border()->color();
        c->push_back( _originalStrokeColor );
        if ( bboxSymbol.border()->width().isSet() )
            getOrCreateStateSet()->setAttribute( new osg::LineWidth( bboxSymbol.border()->width().value() ));
        GLenum lineMode ( bboxSymbol.geom().isSetTo(BBoxSymbol::GEOM_STAIR) ? GL_LINE_STRIP : GL_LINE_LOOP );
        addPrimitiveSet( new osg::DrawArrays(lineMode, 0, static_cast<int>(v->getNumElements())) );
    }

    setColorArray( c );

    // Disable culling since this bounding box will eventually be drawn in screen space.
    setCullingActive(false);
}
