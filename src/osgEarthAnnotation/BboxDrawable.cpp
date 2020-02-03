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

BboxDrawable::BboxDrawable( const osg::BoundingBox& box, const BBoxSymbol &bboxSymbol, const float& symbolWidth ) :
osg::Geometry()
, _deGlobal(nullptr)
, _deReduced(nullptr)
, _daGlobal(nullptr)
, _daReduced(nullptr)
{
    float reducedWidth = symbolWidth < box.xMax() - box.xMin() ? symbolWidth : box.xMax() - box.xMin();

    setUseVertexBufferObjects(true);

    float margin = bboxSymbol.margin().isSet() ? bboxSymbol.margin().value() : 2.f;
    float shiftRight = 0.f;
    osg::Vec3Array* v = new osg::Vec3Array();

    unsigned int indexMinRightSideFull = 0;
    unsigned int indexMaxRightSideFull = 0;
    unsigned int indexMinLeftSide = 0;
    unsigned int indexMaxLeftSide = 0;
    unsigned int indexMinRightSideReduced = 0;
    unsigned int indexMaxRightSideReduced = 0;

    if ( bboxSymbol.geom().isSet() &&
         ( bboxSymbol.geom().value() == BBoxSymbol::GEOM_BOX_ROUNDED ))
    {
        osg::Vec3 dump;
        float nbStep = 5.f;
        int sizeBeforePush = 0;
        float radius = ((box.yMax()-box.yMin())/2)+margin;
        osg::Vec3 center (box.xMax(),(box.yMax()+box.yMin())/2,0);

        // Right side full
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
        indexMaxRightSideFull = v->size() - 1;
        sizeBeforePush = v->size();

        // Left side
        indexMinLeftSide = indexMaxRightSideFull+1;
        for (int iterator = sizeBeforePush-1; iterator >= sizeBeforePush-nbStep*2-1; --iterator)
        {
            dump = v->at(iterator);
            v->push_back(osg::Vec3(2*center.x()-dump.x()-(box.xMax()-box.xMin()), dump.y(), dump.z()));
        }
        indexMaxLeftSide = v->size() - 1;

        // Right Side reduced
        indexMinRightSideReduced =  indexMaxLeftSide+1;
        for (int iterator = 0; iterator <= sizeBeforePush-1; ++iterator)
        {
            dump = v->at(iterator);
            v->push_back(osg::Vec3(dump.x() - (box.xMax()-box.xMin()) + reducedWidth, dump.y(), dump.z()));
        }
        indexMaxRightSideReduced = v->size() - 1;
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
        // Right side points with reduced width
        v->push_back( osg::Vec3(box.xMin()+reducedWidth+margin+shiftRight, box.yMax()+margin, 0) );
        v->push_back( osg::Vec3(box.xMin()+reducedWidth+margin+shiftRight, box.yMin()-margin, 0) );
    }

    setVertexArray(v);

    if ( v->getVertexBufferObject() )
        v->getVertexBufferObject()->setUsage(GL_STATIC_DRAW_ARB);

    osg::Vec4Array* c = new osg::Vec4Array(osg::Array::BIND_PER_PRIMITIVE_SET);
    if ( bboxSymbol.fill().isSet() )
    {
        c->push_back( bboxSymbol.fill()->color() );
        _deGlobal = new osg::DrawElementsUByte(GL_TRIANGLE_STRIP);
        _deReduced = new osg::DrawElementsUByte(GL_TRIANGLE_STRIP);
        if ( v->size() == 6 )
        {
            _deGlobal->addElement(0);
            _deGlobal->addElement(1);
            _deGlobal->addElement(3);
            _deGlobal->addElement(2);

            _deReduced->addElement(4);
            _deReduced->addElement(1);
            _deReduced->addElement(5);
            _deReduced->addElement(2);
        }
        else
        {
            unsigned int indexRight = indexMinRightSideFull;
            unsigned int indexLeft = indexMaxLeftSide;
            while (indexRight <= indexMaxRightSideFull && indexLeft >= indexMinLeftSide)
            {
                _deGlobal->addElement(indexRight);
                _deGlobal->addElement(indexLeft);
                indexRight++;
                indexLeft--;
            }

            indexRight = indexMinRightSideReduced;
            indexLeft = indexMaxLeftSide;
            while (indexRight <= indexMaxRightSideReduced && indexLeft >= indexMinLeftSide)
            {
                _deReduced->addElement(indexRight);
                _deReduced->addElement(indexLeft);
                indexRight++;
                indexLeft--;
            }
        }
        addPrimitiveSet(_deGlobal);
    }

    if ( bboxSymbol.border().isSet() )
    {
        c->push_back( bboxSymbol.border()->color() );
        if ( bboxSymbol.border()->width().isSet() )
            getOrCreateStateSet()->setAttribute( new osg::LineWidth( bboxSymbol.border()->width().value() ));
        if ( v->size() == 6 )
        {
            _daGlobal = new osg::DrawArrays(GL_LINE_LOOP, 0, 4);
            _daReduced = new osg::DrawArrays(GL_LINE_LOOP, 2, 4);
        }
        else
        {
            _daGlobal = new osg::DrawArrays(GL_LINE_LOOP, 0, static_cast<int>(indexMaxLeftSide+1));
            _daReduced = new osg::DrawArrays(GL_LINE_LOOP, static_cast<int>(indexMinLeftSide), static_cast<int>(indexMaxRightSideReduced-indexMinLeftSide+1));
        }
        addPrimitiveSet(_daGlobal);
    }

    setColorArray( c );

    // Disable culling since this bounding box will eventually be drawn in screen space.
    setCullingActive(false);
}

void BboxDrawable::setReducedSize(bool b)
{
    if (b)
    {
        setPrimitiveSet(0,_deReduced);
        setPrimitiveSet(1,_daReduced);
    }
    else
    {
        setPrimitiveSet(0,_deGlobal);
        setPrimitiveSet(1,_daGlobal);
    }
}
