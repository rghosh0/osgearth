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
#include <osgEarth/MPScreenSpaceLayout>
#include <osgEarth/Utils>
#include <osgEarth/VirtualProgram>
#include <osgEarth/Extension>
#include <osgEarthAnnotation/BboxDrawable>
#include <osgText/Text>
#include <osg/ValueObject>
#include <osgEarth/EllipsoidIntersector>
#include <osgEarth/MapNode>
#include <osg/LineSegment>

// -----------------------------------------------------------
// This class is mainly copied from ScreenSpaceLayout.cpp
// but with performance improvements
// -----------------------------------------------------------


#define LC "[MPScreenSpaceLayout] "

using namespace osgEarth;
using namespace osgEarth::Util;

//----------------------------------------------------------------------------

namespace
{
// Sort wrapper to satisfy the template processor.
struct MPSortContainer
{
    MPSortContainer( DeclutterSortFunctor& f ) : _f(f) { }
    const DeclutterSortFunctor& _f;
    bool operator()( const osgUtil::RenderLeaf* lhs, const osgUtil::RenderLeaf* rhs ) const
    {
        return _f(lhs, rhs);
    }
};

// Group drawables that are attached to the same feature, then sort them by priority
struct SortByPriority : public DeclutterSortFunctor
{
    bool operator()( const osgUtil::RenderLeaf* lhs, const osgUtil::RenderLeaf* rhs ) const
    {
        const ScreenSpaceLayoutData* lhsdata = static_cast<const ScreenSpaceLayoutData*>(lhs->_drawable->getUserData());
        const ScreenSpaceLayoutData* rhsdata = static_cast<const ScreenSpaceLayoutData*>(rhs->_drawable->getUserData());
        
        float lhsPriority = lhsdata->_priority ;
        float rhsPriority = rhsdata->_priority;
        if ( lhsPriority != rhsPriority )
            return lhsPriority > rhsPriority;
        
        // then fallback on traversal order.
#if OSG_VERSION_GREATER_THAN(3,6,0)
        return lhs->_traversalOrderNumber < rhs->_traversalOrderNumber;
#else
        int diff = lhs->_traversalNumber - rhs->_traversalNumber;
        return diff < 0.0f;
#endif
    }
};

// Data structure shared across entire layout system.
struct MPScreenSpaceLayoutContext : public osg::Referenced
{
    ScreenSpaceLayoutOptions _options;
};

// records information about each drawable.
// TODO: a way to clear out this list when drawables go away
struct DrawableInfo
{
    DrawableInfo() : _lastScale(1.), _frame(0u), _visible(true) { }
    double _lastScale;
    unsigned _frame;
    bool _visible;
};

typedef std::map<const osg::Drawable*, DrawableInfo> DrawableMemory;

typedef std::pair<int, osg::BoundingBox> RenderLeafBox;

// Data structure stored one-per-View.
struct PerCamInfo
{
    PerCamInfo() : _lastTimeStamp(0), _firstFrame(true) { }
    
    // remembers the state of each drawable from the previous pass
    DrawableMemory _memory;
    
    // re-usable structures (to avoid unnecessary re-allocation)
    osgUtil::RenderBin::RenderLeafList _passed;
    osgUtil::RenderBin::RenderLeafList _failed;
    std::vector<RenderLeafBox>         _used;
    
    // time stamp of the previous pass, for calculating animation speed
    osg::Timer_t _lastTimeStamp;
    bool _firstFrame;
    osg::Matrix _lastCamVPW;
};

// Data structure for storing common data for all drawables of one given annotation
struct AnnotationInfo
{
    long id{-1};
    bool isInitialized{false};
    bool isOutOfView{false};
    double angle{0.};
    
};

typedef std::map<long, AnnotationInfo> AnnotationInfoList;

static bool s_mp_declutteringEnabledGlobally = true;

static bool s_mp_extension_loaded = false;

}

//----------------------------------------------------------------------------

template<typename T>
struct LCGIterator
{
    T& _vec;
    unsigned _seed;
    unsigned _n;
    unsigned _index;
    unsigned _a, _c;
    LCGIterator(T& vec) : _vec(vec), _seed(0u), _index(0u) {
        _n = vec.size();
        _a = _n+1;
        _c = 15487457u; // a very large prime
    }
    bool hasMore() const {
        return _index < _n;
    }
    const typename T::value_type& next() {
        _seed = (_a*_seed + _c) % _n;
        _index++;
        return _vec[_seed];
    }
};

/**
 * A custom RenderLeaf sorting algorithm for decluttering objects.
 *
 * First we sort the leaves front-to-back so that objects closer to the camera
 * get higher priority. If you have installed a custom sorting functor,
 * this is used instead.
 *
 * Next, we go though all the drawables and remove any that try to occupy
 * already-occupied real estate in the 2D viewport. Objects that fail the test
 * go on a "failed" list and are either completely removed from the display
 * or transitioned to a secondary visual state (scaled down, alpha'd down)
 * dependeing on the options setup.
 *
 * Drawables with the same parent (i.e., Geode) are treated as a group. As
 * soon as one passes the occlusion test, all its siblings will automatically
 * pass as well.
 */
struct /*internal*/ MPDeclutterSort : public osgUtil::RenderBin::SortCallback
{
    DeclutterSortFunctor* _customSortFunctor;
    MPScreenSpaceLayoutContext* _context;
    
    PerObjectFastMap<osg::Camera*, PerCamInfo> _perCam;
    
    /**
     * Constructs the new sorter.
     * @param f Custom declutter sorting predicate. Pass NULL to use the
     *          default sorter (sort by distance-to-camera).
     */
    MPDeclutterSort( MPScreenSpaceLayoutContext* context, DeclutterSortFunctor* f = nullptr )
        : _context(context), _customSortFunctor(f)
    {
        //nop
    }
    
    // Update the offset so that the drawable is always visible and constraint on a line
    void updateOffsetForAutoLabelOnLine(const osg::BoundingBox& box, const osg::Viewport* vp,
                                        const osg::Vec3f& loc, const ScreenSpaceLayoutData* layoutData,
                                        const osg::Matrix& camVPW, osg::Vec3f& offset, const osg::Vec3f& to)
    {
        // impossible to work when z greater then 1
        // TODO improve
        if (/*loc.z() < -1 ||*/ loc.z() > 1)
            return;
        
        //        OE_WARN << "------------------------------------------\n";
        //        OE_WARN << "loc " << loc.x() << " " << loc.y() << "\n";
        //        OE_WARN << "to " << to.x() << " " << to.y() << "\n";
        
        float vpX = static_cast<float>(vp->x());
        float vpY = static_cast<float>(vp->y());
        float vpWidth = static_cast<float>(vp->width());
        float vpHeight = static_cast<float>(vp->height());
        
        // inits
        float leftMin = vpX - box.xMin() + offset.x();
        float rightMax = vpX + vpWidth - box.xMax() + offset.x();
        float bottomMin = vpY - box.yMin() + offset.y();
        float topMax = vpY + vpHeight - box.yMax() + offset.y();
        bool isResolved = false;
        bool maxPointIsDef = false;
        osg::Vec3f linePt;
        bool toIsDef = to.x() != 0.f && to.y() != 0.f && to.z() != 0.f;
        
        // must go to the right
        if (loc.x() < leftMin)
        {
            if (toIsDef)
            {
                linePt = to;
            }
            else
            {
                linePt = layoutData->getLineEndPoint() * camVPW;
                if (linePt.x() < loc.x() || linePt.z() < -1 || linePt.z() > 1)
                    linePt = layoutData->getLineStartPoint() * camVPW;
            }
            maxPointIsDef = true;
            
            if (linePt.x() >= (leftMin - (box.xMax() - box.xMin())))
            {
                float ratio = (leftMin - loc.x()) / (linePt.x() - loc.x());
                if (ratio < 1)
                    offset.set(leftMin - loc.x(), ratio * (linePt.y() - loc.y()), 0.f);
                else
                    offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved = ratio >= 1.f || ((loc.y() + offset.y()) > bottomMin && (loc.y() + offset.y()) < topMax);
            }
            else
            {
                // out of screen : used closest point
                offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved = true;
            }
        }
        
        // must go up
        if (!isResolved && loc.y() < bottomMin)
        {
            if (!maxPointIsDef)
            {
                if (toIsDef)
                {
                    linePt = to;
                }
                else
                {
                    linePt = layoutData->getLineEndPoint() * camVPW;
                    if (linePt.y() < loc.y() || linePt.z() < -1 || linePt.z() > 1)
                        linePt = layoutData->getLineStartPoint() * camVPW;
                }
                maxPointIsDef = true;
            }
            
            if (linePt.y() >= (bottomMin - (box.yMax() - box.yMin())))
            {
                float ratio = (bottomMin - loc.y()) / (linePt.y() - loc.y());
                if (ratio < 1)
                    offset.set(ratio * (linePt.x() - loc.x()), bottomMin - loc.y(), 0.f);
                else
                    offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved =
                        ratio >= 1.f || ((loc.x() + offset.x()) > leftMin && (loc.x() + offset.x()) < rightMax);
            }
            else
            {
                // out of screen : used closest point
                offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved = true;
            }
        }
        
        // must go to the left
        if (!isResolved && loc.x() > rightMax)
        {
            if (!maxPointIsDef)
            {
                if (toIsDef)
                {
                    linePt = to;
                }
                else
                {
                    linePt = layoutData->getLineEndPoint() * camVPW;
                    if (linePt.x() > loc.x() || linePt.z() < -1 || linePt.z() > 1)
                        linePt = layoutData->getLineStartPoint() * camVPW;
                }
                maxPointIsDef = true;
            }
            
            if (linePt.x() <= (rightMax + (box.xMax() - box.xMin())))
            {
                float ratio = (rightMax - loc.x()) / (linePt.x() - loc.x());
                if (ratio < 1)
                    offset.set(rightMax - loc.x(), ratio * (linePt.y() - loc.y()), 0.f);
                else
                    offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved =
                        ratio >= 1.f || ((loc.y() + offset.y()) > bottomMin && (loc.y() + offset.y()) < topMax);
            }
            else
            {
                // out of screen : used closest point
                offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved = true;
            }
        }
        
        // must go down
        if (!isResolved && loc.y() > topMax)
        {
            if (!maxPointIsDef)
            {
                if (toIsDef)
                {
                    linePt = to;
                }
                else
                {
                    linePt = layoutData->getLineEndPoint() * camVPW;
                    if (linePt.y() > loc.y() || linePt.z() < -1 || linePt.z() > 1)
                        linePt = layoutData->getLineStartPoint() * camVPW;
                }
                maxPointIsDef = true;
            }
            
            if (linePt.y() <= (topMax + (box.yMax() - box.yMin())))
            {
                float ratio = (topMax - loc.y()) / (linePt.y() - loc.y());
                if (ratio < 1)
                    offset.set(ratio * (linePt.x() - loc.x()), topMax - loc.y(), 0.f);
                else
                    offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved =
                        ratio >= 1.f || ((loc.x() + offset.x()) > leftMin && (loc.x() + offset.x()) < rightMax);
            }
            else
            {
                // out of screen : used closest point
                offset.set(linePt.x() - loc.x(), linePt.y() - loc.y(), 0.f);
                isResolved = true;
            }
        }
    }
    
    // override.
    // Sorts the bin. This runs in the CULL thread after the CULL traversal has completed.
    void sortImplementation(osgUtil::RenderBin* bin)
    {
        const ScreenSpaceLayoutOptions& options = _context->_options;
        
        osgUtil::RenderBin::RenderLeafList& leaves = bin->getRenderLeafList();
        
        bin->copyLeavesFromStateGraphListToRenderLeafList();
        
        // nothing to sort? bail out
        if ( leaves.size() == 0 )
            return;
        
        // if there's a custom sorting function installed
        if ( _customSortFunctor && s_mp_declutteringEnabledGlobally )
            std::sort( leaves.begin(), leaves.end(), MPSortContainer( *_customSortFunctor ) );
        
        // access the view-specific persistent data:
        osg::Camera* cam = bin->getStage()->getCamera();
        
        // bail out if this camera is a master camera with no GC
        // (e.g., in a multi-screen layout)
        if (cam == nullptr || (cam->getGraphicsContext() == nullptr && !cam->isRenderToTextureCamera()))
            return;
        
        PerCamInfo& local = _perCam.get( cam );
        
        osg::Timer_t now = osg::Timer::instance()->tick();
        if (local._firstFrame)
        {
            local._firstFrame = false;
            local._lastTimeStamp = now;
        }
        
        // calculate the elapsed time since the previous pass; we'll use this for
        // the animations
        double elapsedSeconds = osg::Timer::instance()->delta_s(local._lastTimeStamp, now);
        local._lastTimeStamp = now;
        
        // Reset the local re-usable containers
        local._passed.clear();          // drawables that pass occlusion test
        local._failed.clear();          // drawables that fail occlusion test
        local._used.clear();            // list of occupied bounding boxes in screen space
        
        // compute a window matrix so we can do window-space culling.
        const osg::Viewport* vp = cam->getViewport();
        double vpXMin = vp->x();
        double vpYMin = vp->y();
        double vpWidth = vp->width();
        double vpHeight = vp->height();
        double vpXMax = vpXMin + vp->width();
        double vpYMax = vpYMin + vp->height();
        osg::Matrix windowMatrix = vp->computeWindowMatrix();
        osg::Vec3d eye, center, up, look;
        cam->getViewMatrixAsLookAt(eye, center, up);
        look = center - eye;
        look.normalize();
        
        int screenMapNbCol = options.screenGridNbCol().get();
        int screenMapNbRow = options.screenGridNbRow().get();
        const bool useScreenGrid = options.useScreenGrid().get();
        double mapSizeX = vp->width() / screenMapNbCol;
        double mapSizeY = vp->height() / screenMapNbRow;
        std::vector<std::vector<std::vector<RenderLeafBox>>> _usedMap(screenMapNbCol, std::vector<std::vector<RenderLeafBox>>(screenMapNbRow));
        
        // Track the features that are obscured (and culled). Drawables
        // with the same feature id are considered to be grouped and
        // will be culled as a group.
        std::set<int> culledParents;
        
        unsigned limit = *options.maxObjects();
        
        double minAnimationScale = static_cast<double>(*options.minAnimationScale());
        
        //        bool snapToPixel = options.snapToPixel() == true;
        
        osg::Matrix camVPW;
        camVPW.postMult(cam->getViewMatrix());
        camVPW.postMult(cam->getProjectionMatrix());
        camVPW.postMult(windowMatrix);
        
        // has the camera moved?
        //        bool camChanged = camVPW != local._lastCamVPW;
        local._lastCamVPW = camVPW;
        
        osg::Vec3f offset;
        static const osg::Vec3d offset1px(1., 1., 0.);
        static const osg::Vec3d offset3px(3., 3., 0.);
        
        // Go through each leaf and test for visibility.
        // Enforce the "max objects" limit along the way.
        
        
        for(osgUtil::RenderBin::RenderLeafList::iterator i = leaves.begin();
            i != leaves.end() && local._passed.size() < limit;
            ++i )
        {
            osgUtil::RenderLeaf* leaf = *i;
            const osg::Drawable* drawable = leaf->_drawable;
            const ScreenSpaceLayoutData* layoutData = static_cast<const ScreenSpaceLayoutData*>(drawable->getUserData());
            
            // transform the bounding box of the drawable into window-space.
            // (use parent bbox for line following algorithm)
            osg::BoundingBox box = layoutData->isAutoFollowLine() ? layoutData->getBBox() : drawable->getBoundingBox();
            
            const osgText::Text* asText = dynamic_cast<const osgText::Text*>(drawable);
            bool isText = asText != nullptr;
            long drawableFid = layoutData->getId();
            double angle = 0;
            osg::Quat rot;
            osg::Vec3d to;
            osg::Vec3f pos(layoutData->_cull_anchorOnScreen);
            bool visible = true;
            
            // local transformation data
            // and management of the label orientation (must be always readable)
            if (layoutData->isAutoRotate())
            {
                angle = layoutData->_cull_rotationRadOnScreen;
            }
            
            if ( isText && (angle < -osg::PI_2 || angle > osg::PI_2) )
            {
                // avoid the label characters to be inverted:
                // use a symetric translation and adapt the rotation to be in the desired angles
                if ( asText->getAlignment() == osgText::TextBase::AlignmentType::LEFT_BOTTOM_BASE_LINE
                     || asText->getAlignment() == osgText::TextBase::AlignmentType::RIGHT_BOTTOM_BASE_LINE )
                    offset.set( layoutData->_pixelOffset.x() - (box.xMax()+box.xMin()),
                                layoutData->_pixelOffset.y(),
                                0. );
                else if ( layoutData->_simpleCharacterInvert )
                    offset.set( layoutData->_pixelOffset.x() - (box.xMax()+box.xMin()),
                                layoutData->_pixelOffset.y() - (box.yMax()+box.yMin()),
                                0. );
                else
                    offset.set( layoutData->_pixelOffset.x(),
                                layoutData->_pixelOffset.y(),
                                0. );
                angle += angle < -osg::PI_2 ? osg::PI : -osg::PI; // JD #1029
            }
            else
            {
                offset.set( layoutData->_pixelOffset.x(), layoutData->_pixelOffset.y(), 0. );
            }
            
            // handle the local translation
            box.xMin() += offset.x();
            box.xMax() += offset.x();
            box.yMin() += offset.y();
            box.yMax() += offset.y();
            
            // handle the local rotation
            if ( angle != 0. )
            {
                rot.makeRotate ( angle, osg::Vec3d(0, 0, 1) );
                osg::Vec3f ld = rot * ( osg::Vec3f(box.xMin(), box.yMin(), 0.) );
                osg::Vec3f lu = rot * ( osg::Vec3f(box.xMin(), box.yMax(), 0.) );
                osg::Vec3f ru = rot * ( osg::Vec3f(box.xMax(), box.yMax(), 0.) );
                osg::Vec3f rd = rot * ( osg::Vec3f(box.xMax(), box.yMin(), 0.) );
                if ( angle > - osg::PI / 2. && angle < osg::PI / 2. )
                    box.set( osg::minimum(ld.x(), lu.x()), osg::minimum(ld.y(), rd.y()), 0,
                             osg::maximum(rd.x(), ru.x()), osg::maximum(lu.y(), ru.y()), 0 );
                else
                    box.set(osg::minimum(rd.x(), ru.x()), osg::minimum(lu.y(), ru.y()), 0,
                            osg::maximum(ld.x(), lu.x()), osg::maximum(ld.y(), rd.y()), 0);
            }
            
            // adapt the offset for auto sliding label
            if (layoutData->isAutoFollowLine())
            {
                osg::Vec3f slidingOffset;
                updateOffsetForAutoLabelOnLine(box, vp, pos, layoutData, camVPW, slidingOffset, to);
                pos += slidingOffset;
            }
            if(layoutData->screenClamping()){
                
                
                
                // Calculate the "clip to world" matrix = MVPinv.
                osg::Matrix MVP = (cam->getViewMatrix()) * cam->getProjectionMatrix();
                osg::Matrix MVPinv;
                MVPinv.invert(MVP);
                
                osg::ref_ptr<MapNode> mapNode = MapNode::findMapNode(leaf->_drawable->asNode());
//                EllipsoidIntersector ellipsoid(mapNode->getMapSRS()->getEllipsoid());
                
//                // For each corner, transform the clip coordinates at the near and far
//                // planes into world space and intersect that line with the ellipsoid:
//                osg::Vec3d p0, p1;
                
//                bool is_intesect=true;
//                // find the lower-left corner of the frustum:
//                osg::Vec3d LL_world;
//                p0 = osg::Vec3d(-1, -1, -1) * MVPinv;
//                p1 = osg::Vec3d(-1, -1, +1) * MVPinv;
//                bool LL_ok = ellipsoid.intersectLine(p0, p1, LL_world);
//                if (!LL_ok)
//                    is_intesect= false;
                
//                // find the upper-left corner of the frustum:
//                osg::Vec3d UL_world;
//                p0 = osg::Vec3d(-1, +1, -1) * MVPinv;
//                p1 = osg::Vec3d(-1, +1, +1) * MVPinv;
//                bool UL_ok = ellipsoid.intersectLine(p0, p1, UL_world);
//                if (!UL_ok)
//                    is_intesect= false;
                
//                // find the lower-right corner of the frustum:
//                osg::Vec3d LR_world;
//                p0 = osg::Vec3d(+1, -1, -1) * MVPinv;
//                p1 = osg::Vec3d(+1, -1, +1) * MVPinv;
//                bool LR_ok = ellipsoid.intersectLine(p0, p1, LR_world);
//                if (!LR_ok)
//                    is_intesect= false;
                
                osg::Vec3d pw1=layoutData->getLineStartPoint();
                osg::Vec3d pw2=layoutData->getLineEndPoint();
                
                osg::Vec3d pc1=layoutData->getLineStartPoint()*MVP;
                osg::Vec3d pc2=layoutData->getLineEndPoint()*MVP;
                osg::Vec3d centerEarth=osg::Vec3d(0.0,0.0,0.0)*MVP;
                
                
                bool p1_in_width=pc1.x()<1.0 && pc1.x()>-1.0;
                bool p1_in_height=pc1.y()<1.0 && pc1.y()>-1.0;
                bool p1_in_depth=(pc1.length() -centerEarth.length())<-1.0;
                bool p1_inside_screen = p1_in_width && p1_in_height;
                
                bool p2_in_width=pc2.x()<1.0 && pc2.x()>-1.0;
                bool p2_in_height=pc2.y()<1.0 && pc2.y()>-1.0;
                bool p2_in_depth=(pc2.length() -centerEarth.length())<-1.0;
                bool p2_inside_screen = p2_in_width && p2_in_height;                
                
                
                bool line_inside_screen=p1_inside_screen && p2_inside_screen;  
                
                bool line_cross_screen=false;    
                
                if(!p1_inside_screen && !p2_inside_screen){
                    //in this case , line may crosses the screen
                    line_cross_screen = (p1_in_width && p2_in_height) || (p2_in_width && p1_in_height) ;
                    
                }
                
                
                
              visible = ( !line_inside_screen) ;
                
                if(visible){ 
                    osg::BoundingBox bb (-1.0,-1.0,-1.0,1.0,1.0,1.0);
                    //osg::BoundingBox bb (-0.0,-0.0,-0.0,0.8,0.8,0.8);
                    osg::ref_ptr<osg::LineSegment> seg = new osg::LineSegment (pc1,pc2);
                    float r1=0,r2=0;
                    visible=seg->intersectAndComputeRatios(bb,r1,r2);
                    
                    if( layoutData->getInstanceIndex() ==0)
                        pos= (pc1 + (pc2-pc1)*r1);
                    else
                        pos= (pc1 + (pc2-pc1)*r2);
                    
                    float x_bbox=0;
                    float y_bbox=0;
                    
                    if(pos.x() >=0.99){
                        x_bbox=box.xMax() ;
                    }else if(pos.x() <=-0.99){
                        x_bbox=box.xMin() ;
                    }
                    
                    if(pos.y() >=0.99){
                        y_bbox=box.yMax() ;
                    }else if(pos.y() <=-0.99){
                        y_bbox=box.yMin() ;
                    }
                    
                    pos=pos*windowMatrix;
                    pos.x()-=x_bbox;
                     pos.y()-=y_bbox;
                    OE_DEBUG<<" geomLineString "<<layoutData->getId()<<" "<<drawable<<" p1"<<pw1.x()<<" "<<pw1.y()<<" "<<pw1.z()<<"  p2"<<pw2.x()<<" "<<pw2.y()<<" "<<pw2.z()<<std::endl;
                    OE_DEBUG<<" p2="<<pc2.x()<<" "<<pc2.y()<<" "<<pc2.z()
                           <<" p1="<<pc1.x()<<" "<<pc1.y()<<" "<<pc1.z()
                          <<" i="<<pos.x()<<" "<<pos.y()<<" "<<pos.z()<<std::endl;
                }
                
            }
            
            //            // Expand the box if this object is currently not visible, so that it takes a little
            //            // more room for it to before visible once again.
            //            DrawableInfo& info = local._memory[drawable];
            //            float buffer = info._visible ? 1.0f : 3.0f;
            DrawableInfo& info = local._memory[drawable];
            const osg::Vec3d &buffer = info._visible ? offset1px : offset3px;
            
            //            // The "declutter" box is the box we use to reserve screen space.
            //            // This must be unquantized regardless of whether snapToPixel is set.
            //            box.set(
            //                        floor(layoutData->_cull_anchorOnScreen.x() + box.xMin())-buffer,
            //                        floor(layoutData->_cull_anchorOnScreen.y() + box.yMin())-buffer,
            //                        layoutData->_cull_anchorOnScreen.z(),
            //                        ceil(layoutData->_cull_anchorOnScreen.x() + box.xMax())+buffer,
            //                        ceil(layoutData->_cull_anchorOnScreen.y() + box.yMax())+buffer,
            //                        layoutData->_cull_anchorOnScreen.z() );
            
            //            // if snapping is enabled, only snap when the camera stops moving.
            //            bool quantize = snapToPixel;
            //            if ( quantize && !camChanged )
            //            {
            //                // Quanitize the window draw coordinates to mitigate text rendering filtering anomalies.
            //                // Drawing text glyphs on pixel boundaries mitigates aliasing.
            //                // Adding 0.5 will cause the GPU to sample the glyph texels exactly on center.
            //                layoutData->_cull_anchorOnScreen.x() = floor(layoutData->_cull_anchorOnScreen.x()) + 0.5;
            //                layoutData->_cull_anchorOnScreen.y() = floor(layoutData->_cull_anchorOnScreen.y()) + 0.5;
            //            }
            
            box.set( box._min + pos - buffer, box._max + pos + buffer);
            
            int mapStartX,  mapStartY, mapEndX, mapEndY;
            
            if ( s_mp_declutteringEnabledGlobally )
            {
                // if this leaf is already in a culled group, skip it.
                if ( culledParents.find(drawableFid) != culledParents.end() )
                {
                    visible = false;
                }
                
                else
                {
                    // A max priority => never occlude.
                    float priority = layoutData ? layoutData->_priority : 0.0f;
                    
                    if ( useScreenGrid )
                    {
                        mapStartX = osg::clampTo(static_cast<int>(floor((box.xMin() - vpXMin) / mapSizeX)), 0, screenMapNbCol-1);
                        mapStartY = osg::clampTo(static_cast<int>(floor((box.yMin() - vpYMin) / mapSizeY)), 0, screenMapNbRow-1);
                        mapEndX = osg::clampTo(static_cast<int>(floor((box.xMax() - vpXMin) / mapSizeX)), 0, screenMapNbCol-1);
                        mapEndY = osg::clampTo(static_cast<int>(floor((box.yMax() - vpYMin) / mapSizeY)), 0, screenMapNbRow-1);
                    }
                    
                    if ( priority >= FLT_MAX )
                    {
                        visible = true;
                    }
                    
                    else
                    {
                        
                        // declutter only on screen cells that intersects the current bbox cells
                        if ( useScreenGrid )
                        {
                            for ( int mapX = mapStartX ; mapX <= mapEndX && visible ; ++mapX )
                                for ( int mapY = mapStartY ; mapY <= mapEndY && visible ; ++mapY )
                                    for( std::vector<RenderLeafBox>::const_iterator j = _usedMap[mapX][mapY].begin(); j != _usedMap[mapX][mapY].end(); ++j )
                                    {
                                        // only need a 2D test since we're in clip space
                                        bool isClear = osg::maximum(box.xMin(), j->second.xMin()) > osg::minimum(box.xMax(), j->second.xMax()) ||
                                                osg::maximum(box.yMin(), j->second.yMin()) > osg::minimum(box.yMax(), j->second.yMax());
                                        
                                        // if there's an overlap (and the conflict isn't from the same drawable
                                        // parent, which is acceptable), then the leaf is culled.
                                        if ( ! isClear && drawableFid != j->first )
                                        {
                                            visible = false;
                                            break;
                                        }
                                    }
                        }
                        
                        // declutter in full screen
                        else
                        {
                            // weed out any drawables that are obscured by closer drawables.
                            // TODO: think about a more efficient algorithm - right now we are just using
                            // brute force to compare all bbox's
                            for( std::vector<RenderLeafBox>::const_iterator j = local._used.begin(); j != local._used.end(); ++j )
                            {
                                // only need a 2D test since we're in clip space
                                bool isClear = osg::maximum(box.xMin(),j->second.xMin()) > osg::minimum(box.xMax(),j->second.xMax()) ||
                                        osg::maximum(box.yMin(),j->second.yMin()) > osg::minimum(box.yMax(),j->second.yMax());
                                
                                // if there's an overlap (and the conflict isn't from the same drawable
                                // parent, which is acceptable), then the leaf is culled.
                                if ( ! isClear && drawableFid != j->first )
                                {
                                    visible = false;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if ( visible )
                    {
                        // passed the test, so add the leaf's bbox to the "used" list, and add the leaf
                        // to the final draw list.
                        local._used.push_back( std::make_pair(drawableFid, box) );
                        local._passed.push_back( leaf );
                        
                        if ( useScreenGrid )
                            for ( int mapX = mapStartX ; mapX <= mapEndX ; ++mapX )
                                for ( int mapY = mapStartY ; mapY <= mapEndY ; ++mapY )
                                    _usedMap[mapX][mapY].push_back( local._used.back() );
                    }
                }
                
                if ( ! visible )
                {
                    // culled, so put the feature id in the culled list so that any future leaves
                    // with the same feature id will be trivially rejected
                    culledParents.insert(drawableFid);
                    local._failed.push_back( leaf );
                }
            }
            
            // no declutter
            else
            {
                // passed the test, so add the leaf's bbox to the "used" list, and add the leaf
                // to the final draw list.
                local._used.push_back( std::make_pair(drawableFid, box) );
                local._passed.push_back( leaf );
            }
            
            osg::Matrix newModelView;
            newModelView.makeTranslate(static_cast<double>(pos.x()/* + offset.x()*/), static_cast<double>(pos.y()/* + offset.y()*/), 0);
            if (! rot.zeroRotation())
                newModelView.preMultRotate(rot);
            newModelView.preMultTranslate(offset);
            
            // Leaf modelview matrixes are shared (by objects in the traversal stack) so we
            // cannot just replace it unfortunately. Have to make a new one. Perhaps a nice
            // allocation pool is in order here
            leaf->_modelview = new osg::RefMatrix(newModelView);
            
        } // end for each leaf
        
        // copy the final draw list back into the bin, rejecting any leaves whose parents
        // are in the cull list.
        if ( s_mp_declutteringEnabledGlobally )
        {
            leaves.clear();
            for( osgUtil::RenderBin::RenderLeafList::const_iterator i=local._passed.begin(); i != local._passed.end(); ++i )
            {
                osgUtil::RenderLeaf* leaf     = *i;
                const osg::Drawable* drawable = leaf->_drawable;
                const ScreenSpaceLayoutData* layoutData = static_cast<const ScreenSpaceLayoutData*>(drawable->getUserData());
                long drawableFid = layoutData->getId();
                
                if ( culledParents.find(drawableFid) == culledParents.end() )
                {
                    DrawableInfo& info = local._memory[drawable];
                    
                    bool fullyIn = true;
                    
                    // scale in until at full scale:
                    if ( info._lastScale != 1. )
                    {
                        fullyIn = false;
                        if ( info._lastScale == 0. )
                            info._lastScale = minAnimationScale;
                        info._lastScale += elapsedSeconds / osg::maximum(*options.inAnimationTime(), 0.001f);
                        if ( info._lastScale > 1. )
                            info._lastScale = 1.;
                    }
                    
                    if ( info._lastScale != 1. )
                        leaf->_modelview->preMult( osg::Matrix::scale(info._lastScale, info._lastScale, 1) );
                    
                    leaves.push_back( leaf );
                    
                    info._frame++;
                    info._visible = true;
                }
                else
                {
                    local._failed.push_back(leaf);
                }
            }
            
            // next, go through the FAILED list and sort them into failure bins so we can draw
            // them using a different technique if necessary.
            for( osgUtil::RenderBin::RenderLeafList::const_iterator i=local._failed.begin(); i != local._failed.end(); ++i )
            {
                osgUtil::RenderLeaf* leaf =     *i;
                const osg::Drawable* drawable = leaf->_drawable;
                
                DrawableInfo& info = local._memory[drawable];
                
                if (info._frame > 0u)
                {
                    // the drawable is failed but has not reached its out animation
                    if ( info._lastScale > minAnimationScale )
                    {
                        bool isText = dynamic_cast<const osgText::Text*>(drawable) != nullptr;
                        bool isBbox = dynamic_cast<const osgEarth::Annotation::BboxDrawable*>(drawable) != nullptr;
                        
                        // case out animation exists
                        if ( *options.outAnimationTime() <= 0.001f)
                        {
                            if ( isText || isBbox )
                                info._lastScale = 0.;
                            else
                                info._lastScale = minAnimationScale;
                        }
                        
                        // case no out animation exists
                        else
                        {
                            info._lastScale -= elapsedSeconds / osg::maximum(*options.outAnimationTime(), 0.001f);
                            if ( info._lastScale <= minAnimationScale )
                            {
                                if ( isText || isBbox )
                                    info._lastScale = 0.;
                                else
                                    info._lastScale = minAnimationScale;
                            }
                        }
                    }
                }
                else
                {
                    bool isText = dynamic_cast<const osgText::Text*>(drawable) != nullptr;
                    bool isBbox = dynamic_cast<const osgEarth::Annotation::BboxDrawable*>(drawable) != nullptr;
                    
                    // prevent first-frame "pop out"
                    if ( isText || isBbox )
                        info._lastScale = 0.;
                    else
                        info._lastScale = minAnimationScale;
                }
                
                if ( info._lastScale > 0. )
                {
                    leaves.push_back( leaf );
                    
                    // scale it:
                    if ( info._lastScale != 1. )
                        leaf->_modelview->preMult( osg::Matrix::scale(info._lastScale, info._lastScale, 1) );
                }
                
                info._frame++;
                info._visible = false;
            }
        }
    }
};

namespace
{
/**
 * Custom draw routine for our declutter render bin.
 */
struct MPDeclutterDraw : public osgUtil::RenderBin::DrawCallback
{
    MPScreenSpaceLayoutContext*                 _context;
    PerThread< osg::ref_ptr<osg::RefMatrix> > _ortho2D;
    
    /**
     * Constructs the decluttering draw callback.
     * @param context A shared context among all decluttering objects.
     */
    MPDeclutterDraw( MPScreenSpaceLayoutContext* context )
        : _context( context )
    {
    }
    
    /**
     * Draws a bin. Most of this code is copied from osgUtil::RenderBin::drawImplementation.
     * The modifications are (a) skipping code to render child bins, (b) setting a bin-global
     * projection matrix in orthographic space, and (c) calling our custom "renderLeaf()" method
     * instead of RenderLeaf::render()
     */
    void drawImplementation( osgUtil::RenderBin* bin, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous )
    {
        osg::State& state = *renderInfo.getState();
        
        unsigned int numToPop = (previous ? osgUtil::StateGraph::numToPop(previous->_parent) : 0);
        if (numToPop>1) --numToPop;
        unsigned int insertStateSetPosition = state.getStateSetStackSize() - numToPop;
        
        if (bin->getStateSet())
        {
            state.insertStateSet(insertStateSetPosition, bin->getStateSet());
        }
        
        // apply a window-space projection matrix.
        const osg::Viewport* vp = renderInfo.getCurrentCamera()->getViewport();
        if ( vp )
        {
            //TODO see which is faster
            
            osg::ref_ptr<osg::RefMatrix>& m = _ortho2D.get();
            if ( !m.valid() )
                m = new osg::RefMatrix();
            
            //m->makeOrtho2D( vp->x(), vp->x()+vp->width()-1, vp->y(), vp->y()+vp->height()-1 );
            m->makeOrtho( vp->x(), vp->x()+vp->width()-1, vp->y(), vp->y()+vp->height()-1, -1000, 1000);
            state.applyProjectionMatrix( m.get() );
        }
        
        // render the list
        osgUtil::RenderBin::RenderLeafList& leaves = bin->getRenderLeafList();
        
        for(osgUtil::RenderBin::RenderLeafList::reverse_iterator rlitr = leaves.rbegin();
            rlitr!= leaves.rend();
            ++rlitr)
        {
            osgUtil::RenderLeaf* rl = *rlitr;
            renderLeaf( rl, renderInfo, previous );
            previous = rl;
        }
        
        if ( bin->getStateSet() )
        {
            state.removeStateSet(insertStateSetPosition);
        }
    }
    
    /**
     * Renders a single leaf. We already applied the projection matrix, so here we only
     * need to apply a modelview matrix that specifies the ortho offset of the drawable.
     *
     * Most of this code is copied from RenderLeaf::draw() -- but I removed all the code
     * dealing with nested bins, since decluttering does not support them.
     */
    void renderLeaf( osgUtil::RenderLeaf* leaf, osg::RenderInfo& renderInfo, osgUtil::RenderLeaf*& previous )
    {
        osg::State& state = *renderInfo.getState();
        
        // don't draw this leaf if the abort rendering flag has been set.
        if (state.getAbortRendering())
        {
            //cout << "early abort"<<endl;
            return;
        }
        
        state.applyModelViewMatrix( leaf->_modelview.get() );
        
        if (previous)
        {
            // apply state if required.
            osgUtil::StateGraph* prev_rg = previous->_parent;
            osgUtil::StateGraph* prev_rg_parent = prev_rg->_parent;
            osgUtil::StateGraph* rg = leaf->_parent;
            if (prev_rg_parent!=rg->_parent)
            {
                osgUtil::StateGraph::moveStateGraph(state,prev_rg_parent,rg->_parent);
                
                // send state changes and matrix changes to OpenGL.
                state.apply(rg->getStateSet());
                
            }
            else if (rg!=prev_rg)
            {
                // send state changes and matrix changes to OpenGL.
                state.apply(rg->getStateSet());
            }
        }
        else
        {
            // apply state if required.
            osgUtil::StateGraph::moveStateGraph(state, nullptr, leaf->_parent->_parent);
            
            state.apply(leaf->_parent->getStateSet());
        }
        
        // if we are using osg::Program which requires OSG's generated uniforms to track
        // modelview and projection matrices then apply them now.
        if (state.getUseModelViewAndProjectionUniforms())
            state.applyModelViewAndProjectionUniformsIfRequired();
        
        // draw the drawable
        leaf->_drawable->draw(renderInfo);
        
        if (leaf->_dynamic)
            state.decrementDynamicObjectCount();
    }
};
}

//----------------------------------------------------------------------------

/**
 * The actual custom render bin
 * This wants to be in the global scope for the dynamic registration to work,
 * hence the annoyinging long class name
 */
class osgEarthMPScreenSpaceLayoutRenderBin : public osgUtil::RenderBin
{
public:
    osgEarthMPScreenSpaceLayoutRenderBin()
    {
        this->setName( OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN );
        _context = new MPScreenSpaceLayoutContext();
        clearSortingFunctor();
        setDrawCallback( new MPDeclutterDraw(_context.get()) );
        
        // needs its own state set for special magic.
        osg::StateSet* stateSet = new osg::StateSet();
        this->setStateSet( stateSet );
    }
    
    osgEarthMPScreenSpaceLayoutRenderBin(const osgEarthMPScreenSpaceLayoutRenderBin& rhs, const osg::CopyOp& copy)
        : osgUtil::RenderBin(rhs, copy),
          _f(rhs._f.get()),
          _context(rhs._context.get())
    {
    }
    
    virtual osg::Object* clone(const osg::CopyOp& copyop) const
    {
        return new osgEarthMPScreenSpaceLayoutRenderBin(*this, copyop);
    }
    
    void setSortingFunctor( DeclutterSortFunctor* f )
    {
        _f = f;
        setSortCallback( new MPDeclutterSort(_context.get(), f) );
    }
    
    void clearSortingFunctor()
    {
        setSortCallback( new MPDeclutterSort(_context.get()) );
    }
    
    osg::ref_ptr<DeclutterSortFunctor> _f;
    osg::ref_ptr<MPScreenSpaceLayoutContext> _context;
};


//----------------------------------------------------------------------------

void
MPScreenSpaceLayout::activate(osg::StateSet* stateSet) //, int binNum)
{
    if ( stateSet )
    {
        int binNum = getOptions().renderOrder().get();
        
        // the OVERRIDE prevents subsequent statesets from disabling the layout bin
        stateSet->setRenderBinDetails(
                    binNum,
                    OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN,
                    osg::StateSet::OVERRIDE_PROTECTED_RENDERBIN_DETAILS);
        
        // Force a single shared layout bin per render stage
        stateSet->setNestRenderBins( false );
        
        // Range opacity is not supported for screen-space rendering
        stateSet->setDefine("OE_DISABLE_RANGE_OPACITY");
    }
}

void
MPScreenSpaceLayout::deactivate(osg::StateSet* stateSet)
{
    if (stateSet)
    {
        stateSet->setRenderBinToInherit();
        stateSet->setNestRenderBins(true);
    }
}

void
MPScreenSpaceLayout::setDeclutteringEnabled(bool enabled)
{
    s_mp_declutteringEnabledGlobally = enabled;
}

void
MPScreenSpaceLayout::setSortFunctor( DeclutterSortFunctor* functor )
{
    // pull our prototype
    osgEarthMPScreenSpaceLayoutRenderBin* bin = dynamic_cast<osgEarthMPScreenSpaceLayoutRenderBin*>(
                osgUtil::RenderBin::getRenderBinPrototype( OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN ) );
    
    if ( bin )
    {
        bin->setSortingFunctor( functor );
    }
}

void
MPScreenSpaceLayout::clearSortFunctor()
{
    // pull our prototype
    osgEarthMPScreenSpaceLayoutRenderBin* bin = dynamic_cast<osgEarthMPScreenSpaceLayoutRenderBin*>(
                osgUtil::RenderBin::getRenderBinPrototype( OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN ) );
    
    if ( bin )
    {
        bin->clearSortingFunctor();
    }
}

void
MPScreenSpaceLayout::setOptions( const ScreenSpaceLayoutOptions& options )
{
    // pull our prototype
    osgEarthMPScreenSpaceLayoutRenderBin* bin = dynamic_cast<osgEarthMPScreenSpaceLayoutRenderBin*>(
                osgUtil::RenderBin::getRenderBinPrototype( OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN ) );
    
    if ( bin )
    {
        MPScreenSpaceLayout::setSortFunctor(new SortByPriority());
        
        if ( options.useScreenGrid().get() )
        {
            OE_INFO << LC << "Decluttering will use a screen grid of " <<
                       options.screenGridNbCol().get() << " * " <<options.screenGridNbRow().get() << "\n";
        }
        
        s_mp_extension_loaded = true;
        OE_INFO << LC << "Use mp_screen_annotations as default text provider\n";
        
        // communicate the new options on the shared context.
        bin->_context->_options = options;
    }
}

const ScreenSpaceLayoutOptions&
MPScreenSpaceLayout::getOptions()
{
    static ScreenSpaceLayoutOptions s_defaultOptions;
    
    // pull our prototype
    osgEarthMPScreenSpaceLayoutRenderBin* bin = dynamic_cast<osgEarthMPScreenSpaceLayoutRenderBin*>(
                osgUtil::RenderBin::getRenderBinPrototype( OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN ) );
    
    if ( bin )
        return bin->_context->_options;
    else
        return s_defaultOptions;
}

bool
MPScreenSpaceLayout::isExtensionLoaded()
{
    return s_mp_extension_loaded;
}


//----------------------------------------------------------------------------

/** the actual registration. */
extern "C" void osgEarth_mp_declutter(void) {}
static osgEarthRegisterRenderBinProxy<osgEarthMPScreenSpaceLayoutRenderBin> s_regbin(OSGEARTH_MP_SCREEN_SPACE_LAYOUT_BIN);


//----------------------------------------------------------------------------

// Extension for configuring the decluterring/SSL options from an Earth file.
namespace osgEarth
{
class MPScreenSpaceLayoutExtension : public Extension,
        public ScreenSpaceLayoutOptions
{
public:
    META_OE_Extension(osgEarth, MPScreenSpaceLayoutExtension, mp_screen_space_layout)
    //META_OE_Extension(osgEarth, MPScreenSpaceLayoutExtension, screen_space_layout);
    
    MPScreenSpaceLayoutExtension() { }
    
    MPScreenSpaceLayoutExtension(const ConfigOptions& co) : ScreenSpaceLayoutOptions(co)
    {
        // sets the global default options.
        MPScreenSpaceLayout::setOptions(*this);
    }
    
    const ConfigOptions& getConfigOptions() const { return *this; }
};

REGISTER_OSGEARTH_EXTENSION(osgearth_mp_screen_space_layout, MPScreenSpaceLayoutExtension);
REGISTER_OSGEARTH_EXTENSION(osgearth_mp_decluttering,        MPScreenSpaceLayoutExtension);
}

