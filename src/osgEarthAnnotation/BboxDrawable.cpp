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

BboxDrawable::BboxDrawable( const osg::BoundingBox& box, const BBoxSymbol &bboxSymbol ) :
osg::Geometry()
{
    setUseVertexBufferObjects(true);

    float margin = bboxSymbol.margin().isSet() ? bboxSymbol.margin().value() : 2.f;
    float shiftRight = 0.f;
    osg::Vec3Array* v = new osg::Vec3Array();

    if ( bboxSymbol.geom().isSet() &&
         ( bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ROUNDED ))
    {
        osg::Vec3 dump;
        float nbStep = 5.f;
        int sizeBeforePush = 0;
        float radius = ((box.yMax()-box.yMin())/2)+margin;
        osg::Vec3 center (box.xMax(),(box.yMax()+box.yMin())/2,0);
        v->push_back( osg::Vec3(box.xMax(), box.yMin()-margin, 0) );
        for (float angle = (3*osg::PI/2.f)+(osg::PI/2.f)/nbStep; angle <2.f*osg::PI ; angle += (osg::PI/2.f)/nbStep)
        {
            v->push_back(center + osg::Vec3((cosf(angle)*radius),sinf(angle)*radius,0));
        }
        v->push_back(center + osg::Vec3((cosf(2.f*osg::PI)*radius),sinf(2.f*osg::PI)*radius,0));
        sizeBeforePush = v->size();

        for (int iterator = sizeBeforePush-2; iterator >= sizeBeforePush-nbStep-1; --iterator)
        {
            dump = v->at(iterator);
            v->push_back(osg::Vec3(dump.x(), 2*center.y()-dump.y(), dump.z()));
        }
        sizeBeforePush = v->size();

        for (int iterator = sizeBeforePush-1; iterator >= sizeBeforePush-nbStep*2-1; --iterator)
        {
            dump = v->at(iterator);
            v->push_back(osg::Vec3(2*center.x()-dump.x()-(box.xMax()-box.xMin()), dump.y(), dump.z()));
        }
    }
    else  {

        if ( bboxSymbol.geom().isSet() && (bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED || bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_SYM) )
        {
            float hMed = (box.yMax()-box.yMin()+2.f*margin) * 0.5f;
            if( bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_SYM )
                shiftRight = - hMed;

            v->push_back( osg::Vec3(box.xMax()+margin+hMed+shiftRight, box.yMax()+margin-hMed, 0) );

            if( bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ORIENTED_SYM )
                shiftRight *= 0.5f; // 22.5 angle instead of 45
        }

        v->push_back( osg::Vec3(box.xMax()+margin+shiftRight, box.yMax()+margin, 0) );
        v->push_back( osg::Vec3(box.xMin()-margin, box.yMax()+margin, 0) );
        v->push_back( osg::Vec3(box.xMin()-margin, box.yMin()-margin, 0) );
        v->push_back( osg::Vec3(box.xMax()+margin+shiftRight, box.yMin()-margin, 0) );
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
        _originalStrokeColor = bboxSymbol.border()->color();
        c->push_back( _originalStrokeColor );
        if ( bboxSymbol.border()->width().isSet() )
            getOrCreateStateSet()->setAttribute( new osg::LineWidth( bboxSymbol.border()->width().value() ));
        addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, 0, static_cast<int>(v->getNumElements())) );
    }

    setColorArray( c );

    // Disable culling since this bounding box will eventually be drawn in screen space.
    setCullingActive(false);
}

void
BboxDrawable::setHighlight( bool highlight )
{
    if ( _isHighlight == highlight )
        return;

    osg::Vec4Array* c = static_cast<osg::Vec4Array*>(getColorArray());

    // update bbox fill color
    (*c)[0] = _isHighlight ? _highlightFillColor : _originalFillColor;

    // update the stroke color if drawn
    if ( c->size() == 2 )
        (*c)[1] = _isHighlight ? _highlightStrokeColor : _originalStrokeColor;

    _isHighlight = highlight;
}
