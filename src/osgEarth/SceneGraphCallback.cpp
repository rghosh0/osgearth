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
#include <osgEarth/CullingUtils>
#include <osgEarth/SceneGraphCallback>
#include <osgUtil/CullVisitor>

using namespace osgEarth;

PagedLODwithVisibilityAltitude::PagedLODwithVisibilityAltitude()
    : _visibilityMaxAltitude(FLT_MAX), _currentAltitude(FLT_MAX),
      _isVisible(true) {
  // nop
}

void PagedLODwithVisibilityAltitude::setVisibilityMaxAltitude(
    float visibilityMaxAltitude) {
  _visibilityMaxAltitude = visibilityMaxAltitude;

  // dirty the camera altitude so as to force a new cull computation
  _currentAltitude = FLT_MAX;
}

bool PagedLODwithVisibilityAltitude::addChild(osg::Node *child) {
  bool ok = false;
  if (child) {
    ok = osg::PagedLOD::addChild(child);
    if (ok) {
      // dirty the camera altitude so as to force a new cull computation
      _currentAltitude = FLT_MAX;
    }
  }
  return ok;
}

bool PagedLODwithVisibilityAltitude::insertChild(unsigned index,
                                                 osg::Node *child) {
  bool ok = false;
  if (child) {
    ok = osg::PagedLOD::insertChild(index, child);
    if (ok) {
      // dirty the camera altitude so as to force a new cull computation
      _currentAltitude = FLT_MAX;
    }
  }
  return ok;
}

bool PagedLODwithVisibilityAltitude::replaceChild(osg::Node *oldChild,
                                                  osg::Node *newChild) {
  bool ok = false;
  if (oldChild && newChild) {
    ok = osg::PagedLOD::replaceChild(oldChild, newChild);
    if (ok) {
      // dirty the camera altitude so as to force a new cull computation
      _currentAltitude = FLT_MAX;
    }
  }
  return ok;
}

void PagedLODwithVisibilityAltitude::traverse(osg::NodeVisitor &nv) {
  if (_visibilityMaxAltitude != FLT_MAX &&
      nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR) {
    osgUtil::CullVisitor *cv = Culling::asCullVisitor(nv);
    osg::Camera *camera = cv->getCurrentCamera();

    // work only on reference camera
    if (camera && !camera->isRenderToTextureCamera()) {
      double alt;
      if (camera->getUserValue("altitude", alt) && _currentAltitude != alt) {
        _currentAltitude = alt;
        if (_currentAltitude > _visibilityMaxAltitude) {
          if (_isVisible && getNumChildren() > 0) {
            getChild(0)->setNodeMask(0);
            _isVisible = false;
          }
        } else {
          if (!_isVisible && getNumChildren() > 0) {
            getChild(0)->setNodeMask(~0);
            _isVisible = true;
          }
        }
      }
    }
  }

  osg::PagedLOD::traverse(nv);
}

//...................................................................

SceneGraphCallbacks::SceneGraphCallbacks(osg::Object *sender)
    : _sender(sender) {
  // nop
}

void SceneGraphCallbacks::add(SceneGraphCallback *cb) {
  if (cb) {
    Threading::ScopedMutexLock lock(_mutex);
    _callbacks.push_back(cb);
  }
}

void SceneGraphCallbacks::remove(SceneGraphCallback *cb) {
  if (cb) {
    Threading::ScopedMutexLock lock(_mutex);
    for (SceneGraphCallbackVector::iterator i = _callbacks.begin();
         i != _callbacks.end(); ++i) {
      if (i->get() == cb) {
        _callbacks.erase(i);
        break;
      }
    }
  }
}

void SceneGraphCallbacks::firePreMergeNode(osg::Node *node) {
  Threading::ScopedMutexLock lock(_mutex);
  osg::ref_ptr<osg::Object> sender;
  _sender.lock(sender);
  for (SceneGraphCallbackVector::iterator i = _callbacks.begin();
       i != _callbacks.end(); ++i)
    i->get()->onPreMergeNode(node, sender.get());
}

void SceneGraphCallbacks::firePostMergeNode(osg::Node *node) {
  osg::ref_ptr<osg::Object> sender;
  _sender.lock(sender);
  for (SceneGraphCallbackVector::iterator i = _callbacks.begin();
       i != _callbacks.end(); ++i)
    i->get()->onPostMergeNode(node, sender.get());
}

void SceneGraphCallbacks::fireRemoveNode(osg::Node *node) {
  osg::ref_ptr<osg::Object> sender;
  _sender.lock(sender);
  for (SceneGraphCallbackVector::iterator i = _callbacks.begin();
       i != _callbacks.end(); ++i)
    i->get()->onRemoveNode(node, sender.get());
}

//...................................................................

PagedLODWithSceneGraphCallbacks::PagedLODWithSceneGraphCallbacks(
    SceneGraphCallbacks *host)
    : PagedLODwithVisibilityAltitude(), _host(host) {
  // nop
}

SceneGraphCallbacks *
PagedLODWithSceneGraphCallbacks::getSceneGraphCallbacks() const {
  return _host.get();
}

void PagedLODWithSceneGraphCallbacks::setSceneGraphCallbacks(
    SceneGraphCallbacks *host) {
  _host = host;
}

bool PagedLODWithSceneGraphCallbacks::addChild(osg::Node *child) {
  bool ok = false;
  if (child) {
    ok = PagedLODwithVisibilityAltitude::addChild(child);
    osg::ref_ptr<SceneGraphCallbacks> host;
    if (_host.lock(host))
      host->firePostMergeNode(child);
  }
  return ok;
}

bool PagedLODWithSceneGraphCallbacks::insertChild(unsigned index,
                                                  osg::Node *child) {
  bool ok = false;
  if (child) {
    ok = PagedLODwithVisibilityAltitude::insertChild(index, child);
    osg::ref_ptr<SceneGraphCallbacks> host;
    if (_host.lock(host))
      host->firePostMergeNode(child);
  }
  return ok;
}

bool PagedLODWithSceneGraphCallbacks::replaceChild(osg::Node *oldChild,
                                                   osg::Node *newChild) {
  bool ok = false;
  if (oldChild && newChild) {
    ok = PagedLODwithVisibilityAltitude::replaceChild(oldChild, newChild);
    osg::ref_ptr<SceneGraphCallbacks> host;
    if (_host.lock(host))
      host->firePostMergeNode(newChild);
  }
  return ok;
}

void PagedLODWithSceneGraphCallbacks::childRemoved(unsigned pos, unsigned num) {
  osg::ref_ptr<SceneGraphCallbacks> host;
  if (_host.lock(host)) {
    for (unsigned i = pos; i < pos + num; ++i) {
      host->fireRemoveNode(getChild(i));
    }
  }
}
