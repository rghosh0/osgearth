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
#include <osgEarth/RasterToModelLayer>
#include <osgEarth/RasterToModelSource>

#define LC "[RasterToModelLayer] Layer \"" << getName() << "\" "

using namespace osgEarth;

namespace osgEarth {
    REGISTER_OSGEARTH_LAYER(raster_to_model, RasterToModelLayer);
}


//------------------------------------------------------------------------

RasterToModelLayerOptions::RasterToModelLayerOptions() :
ModelLayerOptions()
{
    fromConfig(_conf);
}

RasterToModelLayerOptions::RasterToModelLayerOptions(const ConfigOptions& options) :
ModelLayerOptions( options )
{
    fromConfig( _conf );
}

Config
RasterToModelLayerOptions::getConfig() const
{
    Config conf = ModelLayerOptions::getConfig();

    if ( sourceOptions().isSet() )
        conf.merge(sourceOptions()->getConfig());

    return conf;
}

void
RasterToModelLayerOptions::fromConfig( const Config& conf )
{
   sourceOptions() = ModelSourceOptions(conf);
}

void
RasterToModelLayerOptions::mergeConfig( const Config& conf )
{
    ModelLayerOptions::mergeConfig( conf );
    fromConfig( conf );
}

//------------------------------------------------------------------------

// callback to catch the end of the loading process
struct RasterToModelLayer::LoadingInProgressCB : public SceneGraphCallback
{
    LoadingInProgressCB (RasterToModelLayer* layer) : _layer(layer) {}

    void onPreMergeNode(osg::Node* node) override
    {
        if (node == _layer.get()->getNode())
            return;

        _layer->_isFullyLoaded = true;

        // refresh the visibility of the layer
        _layer->setVisible(_layer->getVisible());

        // notify listeners that this layer is now fully loaded
        _layer->fireCallback(&RasterToModelLayerCallback::onInitializationFinished);
    }

private:
    osg::ref_ptr<RasterToModelLayer> _layer;
};




//------------------------------------------------------------------------

RasterToModelLayer::RasterToModelLayer() :
ModelLayer(),
_options(&_optionsConcrete)
{
}

RasterToModelLayer::RasterToModelLayer(const RasterToModelLayerOptions &options) :
ModelLayer(options),
_options(&_optionsConcrete),
_optionsConcrete(options)
{
}

RasterToModelLayer::~RasterToModelLayer()
{
    //nop
}

const Status&
RasterToModelLayer::open()
{
    if ( ModelLayer::open().isOK() && !_modelSource.valid() && options().sourceOptions().isSet() )
    {
        // Try to create the model source:
        _modelSource = new RasterToModelSource( options().sourceOptions().get() );
        if ( _modelSource.valid() )
        {
            _modelSource->setName( this->getName() );

            const Status& modelStatus = _modelSource->open( _readOptions.get() );
            if (! modelStatus.isOK())
            {
                // propagate the model source's error status
                setStatus(modelStatus);
            }
        }
        else
        {
            setStatus(Status::Error(Status::ServiceUnavailable, Stringify() << "Failed to create source for \"" << getName() << "\""));
        }
    }

    return getStatus();
}

void
RasterToModelLayer::addedToMap(const Map* map)
{
    ModelLayer::addedToMap(map);

    if (options().sourceOptions().get().loadAllAtOnce().isSetTo(true))
        getSceneGraphCallbacks()->add( new LoadingInProgressCB(this) );
}

void
RasterToModelLayer::setVisible(bool value)
{
    if (! options().sourceOptions().get().loadAllAtOnce().isSetTo(true) || _isFullyLoaded)
        VisibleLayer::setVisible(value);

    // in case we want to load everything at once we cannot hide the node
    // because we want the pagedLOD to be triggered
    else
        VisibleLayer::options().visible() = value;
}

void
RasterToModelLayer::fireCallback(RasterToModelLayerCallback::MethodPtr method)
{
    for(CallbackVector::const_iterator i = _callbacks.begin(); i != _callbacks.end(); ++i )
    {
        RasterToModelLayerCallback* cb = dynamic_cast<RasterToModelLayerCallback*>(i->get());
        if (cb) (cb->*method)( this );
    }
}

