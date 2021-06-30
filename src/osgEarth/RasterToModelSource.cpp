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
#include <osgEarth/RasterToModelSource>
#include <osgEarth/Registry>
#include <osgEarth/DrapeableNode>
#include <osgEarth/RasterToModelGraph>

using namespace osgEarth;


#define LC "[RasterToModelSource] "


RasterToModelSourceOptions::RasterToModelSourceOptions(const ConfigOptions& co)
    : ModelSourceOptions( co )
{
    fromConfig(co.getConfig());
}

void
RasterToModelSourceOptions::fromConfig(const Config& conf)
{
    conf.get( "image",            _imageOptions );
    conf.get( "image_band",       _imageBand );
    conf.get( "load_all_at_once", _loadAllAtOnce );
    conf.get( "draped",           _draped );
    conf.get( "depth_test",       _depthTest );
    conf.get( "backface_culling", _backfaceCulling );
    conf.get( "alpha_blending",   _alphaBlending );
}

Config
RasterToModelSourceOptions::getConfig() const
{
    Config conf = ModelSourceOptions::getConfig();

    conf.set( "image",            _imageOptions );
    conf.set( "image_band",       _imageBand );
    conf.set( "load_all_at_once", _loadAllAtOnce );
    conf.set( "draped",           _draped );
    conf.set( "depth_test",       _depthTest );
    conf.set( "backface_culling", _backfaceCulling );
    conf.set( "alpha_blending",   _alphaBlending );

    return conf;
}

//------------------------------------------------------------------------

RasterToModelSource::RasterToModelSource(const ModelSourceOptions &options) :
ModelSource( options ),
_options   ( options )
{
    //nop
}

void
RasterToModelSource::setReadOptions(const osgDB::Options* readOptions)
{
    _readOptions = Registry::cloneOrCreateOptions(readOptions);

    // for texture atlas support
    _readOptions->setObjectCacheHint(osgDB::Options::CACHE_IMAGES);
}

Status
RasterToModelSource::initialize(const osgDB::Options* readOptions)
{
    if (readOptions)
        setReadOptions(readOptions);

    if ( _options.imageOptions().isSet() )
    {
        _imageLayer = new ImageLayer(_options.imageOptions().value());
        _imageLayer->setReadOptions(_readOptions);
        Status status = _imageLayer->open();
        if (status.isOK())
        {
            OE_DEBUG << LC << "Successfull open of an embeded Image Layer" << std::endl;
        }
        else
        {
            return Status::Error(Status::ResourceUnavailable, "Failed to open an embeded Image Layer");
        }
    }

    if (! _imageLayer.valid())
        return Status::Error(Status::ResourceUnavailable, "Failed to create an embeded Image Layer");

    return Status::OK();
}

osg::Node*
RasterToModelSource::createNodeImplementation(const Map*        map,
                                             ProgressCallback* progress )
{
    // pre conditions
    if ( ! getStatus().isOK() || ! _imageLayer.valid() )
        return 0L;

    // user must provide a valid map.
    if ( ! map )
    {
        OE_WARN << LC << "NULL Map is illegal when building feature data." << std::endl;
        return 0L;
    }

    // Graph that will render feature models. May included paged data.
    osg::Group* graph = new RasterToModelGraph(_options, _readOptions, getSceneGraphCallbacks(), _imageLayer.get());

    // Handle the draped technique if required
    osg::Group* root = nullptr;
    if (_options.draped().isSetTo(true))
    {
        root = new DrapeableNode();
        root->addChild( graph );
    }
    else
    {
        root = graph;
    }

    // Apply stateset customization

    if ( _options.depthTest().isSet() )
    {
        root->getOrCreateStateSet()->setMode(
            GL_DEPTH_TEST,
            (_options.depthTest().isSetTo(true) ? osg::StateAttribute::ON : osg::StateAttribute::OFF) | osg::StateAttribute::OVERRIDE );
    }

    if ( _options.backfaceCulling().isSet() )
    {
        root->getOrCreateStateSet()->setMode(
            GL_CULL_FACE,
            (_options.backfaceCulling().isSetTo(true) ? osg::StateAttribute::ON : osg::StateAttribute::OFF) | osg::StateAttribute::OVERRIDE );
    }

    if ( _options.alphaBlending().isSet() )
    {
        root->getOrCreateStateSet()->setMode(
            GL_BLEND,
            (_options.alphaBlending().isSetTo(true) ? osg::StateAttribute::ON : osg::StateAttribute::OFF) | osg::StateAttribute::OVERRIDE );
    }

    return root;
}

