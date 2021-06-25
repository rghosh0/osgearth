/* --*-c++-*-- */
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

#include <osgEarth/RasterToModelGraph>
#include <osgEarth/Registry>
#include <osgEarth/Utils>
#include <osgEarth/MultiBandsInterface>

#include <osg/Texture2D>


#define LC "[RasterToModelGraph] " << getName() << ": "

using namespace osgEarth;

#define USER_OBJECT_NAME "osgEarth.RasterToModelGraph"


//---------------------------------------------------------------------------

namespace {

    // callback to force features onto the high-latency queue.
    struct HighLatencyFileLocationCallback : public osgDB::FileLocationCallback
    {
        Location fileLocation(const std::string &/*filename*/, const osgDB::Options */*options*/) { return REMOTE_FILE; }
        bool useFileCache() const { return false; }
    };
} // namespace


//---------------------------------------------------------------------------

// pseudo-loader for paging in the tiles.

namespace {

static std::string s_makeURI(unsigned lod, unsigned x, unsigned y, unsigned minBand=0, unsigned maxBand=0)
{
    std::stringstream buf;
    buf << lod << "_" << x << "_" << y << "_b_" << minBand << "_" << maxBand  << ".osgearth_pseudo_rtmg";
    std::string str;
    str = buf.str();
    return str;
}

osg::Group *createPagedNode(const osg::BoundingSphered &bs,
                            const std::string &uri, float minRange,
                            float maxRange,
                            SceneGraphCallbacks *sgCallbacks,
                            osgDB::FileLocationCallback *flc,
                            const osgDB::Options *readOptions,
                            RasterToModelGraph *rtmg)
{
    osg::PagedLOD *p = sgCallbacks ? new PagedLODWithSceneGraphCallbacks(sgCallbacks) : new osg::PagedLOD();

    p->setCenter(bs.center());
    p->setRadius(bs.radius());
    p->setFileName(0, uri);
    p->setRange(0, minRange, maxRange);

    // force onto the high-latency thread pool.
    osgDB::Options *options = Registry::instance()->cloneOrCreateOptions(readOptions);
    options->setFileLocationCallback(flc);
    p->setDatabaseOptions(options);
    // so we can find the FMG instance in the pseudoloader.
    OptionsData<RasterToModelGraph>::set(options, USER_OBJECT_NAME, rtmg);

    return p;
}
} // namespace

/**
 * A pseudo-loader for paging the texture tiles.
 */
struct osgEarthRasterToModelPseudoLoader : public osgDB::ReaderWriter {
    osgEarthRasterToModelPseudoLoader() {
        supportsExtension("osgearth_pseudo_rtmg", "Raster to model pseudo-loader");
    }

    const char *className() const { // override
        return "osgEarth Raster to model Pseudo-Loader";
    }

    ReadResult readNode(const std::string &uri, const osgDB::Options *readOptions) const
    {
        if (!acceptsExtension(osgDB::getLowerCaseFileExtension(uri)))
            return ReadResult::FILE_NOT_HANDLED;

        unsigned lod, x, y, minb, maxb;
        lod = x = y = minb = maxb = 0;
        sscanf(uri.c_str(), "%d_%d_%d_b_%d_%d.%*s", &lod, &x, &y, &minb, &maxb);

        osg::ref_ptr<RasterToModelGraph> graph;
        if (!OptionsData<RasterToModelGraph>::lock(readOptions, USER_OBJECT_NAME,  graph))
        {
            OE_WARN << LC << "Internal error - no FeatureModelGraph object in OptionsData\n";
            return ReadResult::ERROR_IN_READING_FILE;
        }

        // provide some performance info
        osg::Timer_t t_start;
        if ( osgEarth::isNotifyEnabled( osg::DEBUG_INFO ) )
        {
            t_start = osg::Timer::instance()->tick();
            Registry::instance()->startActivity(uri);
        }

        // actually load the tile
        osg::Node *node = graph->load(lod, x, y, minb, maxb);

        // provide some performance info
        if ( osgEarth::isNotifyEnabled( osg::DEBUG_INFO ) )
        {
            osg::Timer_t t_end = osg::Timer::instance()->tick();
            double t = osg::Timer::instance()->delta_s(t_start, t_end);
            Registry::instance()->endActivity(uri);

            FindNodesVisitor<osg::Drawable> searchGeom;
            node->accept(searchGeom);

            graph->addProfilingLogs( Stringify() << graph->getName() <<
                                     "\t" << uri << "\t" << t_end <<
                                     "\t" << t << "\t" << searchGeom._results.size() );
        }
        return ReadResult(node);
    }
};

REGISTER_OSGPLUGIN(osgearth_pseudo_rtmg, osgEarthRasterToModelPseudoLoader);



//---------------------------------------------------------------------------

namespace {

    std::string makeCacheKey(const GeoExtent &extent, const TileKey *key)
    {
        if (key)
        {
            return Cache::makeCacheKey(key->str(), "fmg");
        }
        else
        {
            std::string b = Stringify() << extent.toString();
            return Cache::makeCacheKey(b, "fmg");
        }
    }

    // use this util method to trace the scenegraph starting from 'node'
    void traceNode(const osg::Node& node, const std::string tab = "")
    {
        OE_WARN << tab << "[" << node.className() << "/" << node.getName() << "]" << "\n";
        OE_WARN << tab << " |visible " << node.getNodeMask() << "\n";
        OE_WARN << tab << " |refCount " << node.referenceCount() << "\n";
        if (node.asGeometry())
            OE_WARN << tab << " |numVertices " << node.asGeometry()->getVertexArray()->getNumElements() << "\n";
        if (node.getStateSet())
        {
            for (auto uni : node.getStateSet()->getUniformList())
                OE_WARN << tab << " |uniform " << uni.first << "\n";
            const osg::Texture2D* tex = dynamic_cast<const osg::Texture2D*>( node.getStateSet()->getTextureAttribute(0, osg::StateAttribute::TEXTURE) );
            if ( tex )
            {
                const osg::Image* image = tex->getImage();
                if ( image )
                {
                    OE_WARN << tab << " |texture image with " << image->referenceCount() << " refCount\n";
                }
            }
        }
        if (node.asGroup() != nullptr)
        {
            OE_WARN << tab << " |numChildren " << node.asGroup()->getNumChildren() << "\n";
            for (unsigned int i = 0; i < node.asGroup()->getNumChildren(); ++i)
                traceNode(*node.asGroup()->getChild(i), tab + "    ");
        }
    }


    // sphere geometry when the feature layer embeds raster images
    static osg::ref_ptr<const Profile> sphereProfile;
    static osg::ref_ptr<osg::Geode> _ellipsoidGeom;

    // the uniform to change the selected band for multiband rasters
    const std::string _multiBand_uniform_name = "oe_u_channelRamp";
    const std::string _multiBand_2nd_level_uniform_name = "oe_u_channelRamp_2nd_level";

    // vertex shader for feature layer which embeds raster images
    const char* imageVS =
        "#version " GLSL_VERSION_STR "\n"
        GLSL_DEFAULT_PRECISION_FLOAT "\n"

        "out vec2 imageBinding_texcoord; \n"

        "void oe_ImageBinding_VS(inout vec4 vertex) { \n"
        "    imageBinding_texcoord = gl_MultiTexCoord0.st; \n"
        "} \n";

    // fragment shader for feature layer which embeds raster images
    const char* imageFS =
        "#version " GLSL_VERSION_STR "\n"
        GLSL_DEFAULT_PRECISION_FLOAT "\n"

        "in vec2 imageBinding_texcoord; \n"
        "__DECLARATION_CODE__"

        "void oe_ImageBinding_FS(inout vec4 color) { \n"
        "__BODY_CODE__"
        " \n "
        "    //uncomment to debug texture coordinates \n"
        "    //color = vec4(imageBinding_texcoord.st, 0., 1.); \n"
        "} \n";

    // stores information about the bands range that a given texture will handle and associated utils method
    struct BandsInformation : public osg::Referenced
    {
        BandsInformation( const TileKey& tileKey, unsigned int maxBandsPerTile = 4 ) :
            _maxBandsPerTile(maxBandsPerTile)
        {
            tileKey.getTileBands(_minBand, _maxBand);
            _maxBandsPerChannel = maxBandsPerTile / 4;
        }

        bool isBandInRange(unsigned int band) const
        {
            return band >= _minBand && band <= _maxBand;
        }

        // determine the color channel to use for a given band
        // use this method if the image holds one band per color channel
        int getUniformValForBand_1int(unsigned int band) const
        {
            int channel = band - _minBand;

            if (channel < 0 || channel >= static_cast<int>(_maxBandsPerTile))
                return -1;

            return channel / _maxBandsPerChannel;
        }

        // determine the color channel (i) and the offset (j) to use for a given band
        // use this method if the image holds two bands per color channel
        // in that case : the last digit encodes the band i and the previous one the band i+1
        // example : 051 means band i is 1 and band i+1 is 5
        void getUniformValForBand_2int(unsigned int band, int& i, int& j) const
        {
            int index = band - _minBand;

            if (index < 0 || index >= static_cast<int>(_maxBandsPerTile))
            {
                i = -1;
                j = -1;
            }

            else
            {
                i = index / _maxBandsPerChannel;
                j = index - i * _maxBandsPerChannel;
            }
        }

        unsigned int _minBand;
        unsigned int _maxBand;
        unsigned int _maxBandsPerTile;
        unsigned int _maxBandsPerChannel;
    };

    // Node holding one raster texture which may encode multiple rasters bands
    struct GroupMultiBands : public osg::Group, public MultiBandsInterface
    {
        explicit GroupMultiBands(unsigned band = 0) : Group(), MultiBandsInterface (band) {}

        // request to make visible a given band
        virtual void setBand(unsigned band) override
        {
            if (band == _band)
                return;

            for (unsigned i = 0 ; i < getNumChildren() ; i++)
            {
                osg::Group* group = getChild(i)->asGroup();
                osg::ref_ptr<BandsInformation> bandsInfo = static_cast<BandsInformation*>(group->getUserData());
                bool isValid = bandsInfo.valid() && bandsInfo->isBandInRange(band);
                group->setNodeMask( isValid ? ~0 : 0 );

                if ( isValid && group->getStateSet() )
                {
                    osg::Uniform* uniform = group->getStateSet()->getUniform(_multiBand_uniform_name.c_str());
                    if ( uniform )
                    {
                        if ( bandsInfo->_maxBandsPerChannel == 1u )
                        {
                            uniform->set(bandsInfo->getUniformValForBand_1int(band));
                        }
                        else if ( bandsInfo->_maxBandsPerChannel >= 2u )
                        {
                            bandsInfo->getUniformValForBand_2int(band, _tmpI, _tmpJ);
                            if (osg::Uniform* uniform2nd = group->getStateSet()->getUniform(_multiBand_2nd_level_uniform_name.c_str()))
                            {
                                uniform->set(_tmpI);
                                uniform2nd->set(_tmpJ);
                            }
                        }
                    }
                }
                else
                {
                    // \todo manage the memory of grib bands in a better way
                    //group->removeChildren(0, group->getNumChildren());
                }
            }
            _band = band;
        }
    };

    // use this struct to animate the bands (for test only)
    struct BandsAnimation : public osg::NodeCallback
    {
        osg::observer_ptr<osg::Group> _fmg;

        BandsAnimation(osg::Group* fmg) : _fmg(fmg) {}

        void operator()(osg::Node* node, osg::NodeVisitor* nv)
        {
            if (nv->getFrameStamp()->getFrameNumber() % 30 == 0)
            {
                static unsigned band = 1;
                GroupMultiBands* root = static_cast<GroupMultiBands*>( node );
                root->setBand(band);
                band++;
                if (band == 14) band = 1;

                //traceNode( *(_fmg->getParent(0)->getParent(0)) );
            }
        }
    };


    // build a part of sphere for a given extent
    osg::Geode* buildPartialEllipsoidGeometry(const osg::EllipsoidModel* ellipsoid, const osgEarth::Bounds& imageBounds)
    {
        double outerRadius = ellipsoid->getRadiusEquator();
        double hae = outerRadius - ellipsoid->getRadiusPolar();

        osg::Geometry* geom = new osg::Geometry();
        geom->setUseVertexBufferObjects(true);

        osgEarth::Bounds shapeBounds(imageBounds);
        if (shapeBounds.yMin() < -90.) shapeBounds.yMin() = -90.;
        if (shapeBounds.yMax() > 90.)  shapeBounds.yMax() =  90.;
        const double segmentSizeRefInDegree = 1.; // reference resolution of the sphere
        int latSegments = static_cast<int>(ceil(shapeBounds.height() / segmentSizeRefInDegree));
        int lonSegments = static_cast<int>(ceil(shapeBounds.width() / segmentSizeRefInDegree));
        int arraySize = ( (latSegments+1) * (lonSegments+1) );
        const double segmentSizeLat = shapeBounds.height() / latSegments;
        const double segmentSizeLong = shapeBounds.width() / lonSegments;
        const double scaleY = shapeBounds.height() / imageBounds.height();
        const double shiftY = (shapeBounds.yMin() - imageBounds.yMin()) / imageBounds.height();

        OE_DEBUG << "[RasterToModelGraph] Build partial sphere for image overlay. Extent " << imageBounds.toString() << ". "
                 << "Resolution " << (lonSegments+1) << "*" << (latSegments+1) << ". scaleY:" << scaleY << ". shiftY:" << shiftY << std::endl;

        osg::Vec3Array* verts = new osg::Vec3Array();
        verts->reserve( arraySize );

        osg::Vec2Array* texCoords  = new osg::Vec2Array();
        texCoords->reserve( arraySize );
        geom->setTexCoordArray( 0, texCoords );

        osg::Vec3Array* normals = new osg::Vec3Array(osg::Array::BIND_PER_VERTEX);
        normals->reserve( arraySize );
        geom->setNormalArray( normals );

        osg::DrawElementsUShort* el = new osg::DrawElementsUShort( GL_TRIANGLES );
        el->reserve( arraySize * 6 );

        for( int y = 0; y <= latSegments; ++y )
        {
            double lat = shapeBounds.yMin() + segmentSizeLat * (double)y;
            for( int x = 0; x <= lonSegments; ++x )
            {
                double lon = shapeBounds.xMin() + segmentSizeLong * (double)x;
                double gx, gy, gz;
                ellipsoid->convertLatLongHeightToXYZ( osg::DegreesToRadians(lat), osg::DegreesToRadians(lon), hae, gx, gy, gz );
                verts->push_back( osg::Vec3(gx, gy, gz) );

                double s = ((double) x) / lonSegments;
                double t = shiftY + (((double) y) / latSegments) * scaleY;
                texCoords->push_back( osg::Vec2(s, t ) );

                osg::Vec3d normal(gx, gy, gz);
                normal.normalize();
                normals->push_back( osg::Vec3f(normal) );

                if ( y < latSegments && x < lonSegments )
                {
                    int x_plus_1 = x+1;
                    int y_plus_1 = y+1;
                    el->push_back( y*(lonSegments+1) + x );
                    el->push_back( y*(lonSegments+1) + x_plus_1 );
                    el->push_back( y_plus_1*(lonSegments+1) + x );

                    el->push_back( y*(lonSegments+1) + x_plus_1 );
                    el->push_back( y_plus_1*(lonSegments+1) + x_plus_1 );
                    el->push_back( y_plus_1*(lonSegments+1) + x );
                }
            }
        }

        geom->setVertexArray( verts );
        geom->addPrimitiveSet( el );

        osg::Geode* geode = new osg::Geode();
        geode->addDrawable(geom);

        return geode;
    }
}

// build a new program which will handle the binding of a given texture with the sphere
VirtualProgram* createProgramForImageBinding( const TileSource* source )
{
    VirtualProgram* program = new VirtualProgram();

    // build the body of the shader code
    std::string imageFSupdated = imageFS;
    std::string bodyCode = "";
    std::string delcarationCode = "";

    // case no color ramp. just display texture value
    if (! source->getOptions().colorRamp().isSet())
    {
        bodyCode += "    color = texture(image_tex, imageBinding_texcoord); \n";
    }

    // case a color ramp is defined
    else
    {
        IndexedColorRampOptions::ChannelOptimizationTechnique technique
                = source->getOptions().colorRamp()->channelOptimizationTechnique()
                    .getOrUse(IndexedColorRampOptions::ChannelOptimizationTechnique::ONE_FLOAT_PER_BAND);

        delcarationCode += "    uniform lowp int " + _multiBand_uniform_name + "; \n";
        if (source->getOptions().colorRamp()->nbBandsPerChannel() >= 2u)
            delcarationCode += "    uniform lowp int " + _multiBand_2nd_level_uniform_name + "; \n";
        delcarationCode += technique == IndexedColorRampOptions::ChannelOptimizationTechnique::ONE_FLOAT_PER_BAND ?
                    "    uniform sampler2D image_tex; \n" : "    uniform lowp usampler2D image_tex; \n";
        delcarationCode += source->getOptions().colorRamp()->rampDeclCode();

        bodyCode += technique == IndexedColorRampOptions::ChannelOptimizationTechnique::ONE_FLOAT_PER_BAND ? "    float " : "    lowp uint ";
        bodyCode += "value = texture(image_tex, imageBinding_texcoord)[" + _multiBand_uniform_name + "]; \n";

        if (source->getOptions().colorRamp()->nbBandsPerChannel() >= 2u)
        {
            if (source->getOptions().colorRamp()->useDiscard())
                bodyCode += "    if (value == 0u) { discard; return; } \n";

            if (source->getOptions().colorRamp()->nbBandsPerChannel() == 2u)
            {
                bodyCode += "    if (" + _multiBand_2nd_level_uniform_name + " == 0) value = value - (value/10u)*10u; \n";
                bodyCode += "    else if (" + _multiBand_2nd_level_uniform_name + " == 1) value = value / 10u; \n";
            }
            else //if (source->getOptions().colorRamp()->nbBandsPerChannel() == 3u)
            {
                bodyCode += "    if (" + _multiBand_2nd_level_uniform_name + " == 0) value = value - (value/6u)*6u; \n";
                bodyCode += "    else if (" + _multiBand_2nd_level_uniform_name + " == 2) value = value / 36u; \n";
                bodyCode += "    else if (" + _multiBand_2nd_level_uniform_name + " == 1) value = (value - (value / 36u)) / 6u; \n";
            }
        }
        bodyCode += source->getOptions().colorRamp()->rampBodyCode("value");
    }

    osgEarth::replaceIn(imageFSupdated, "__DECLARATION_CODE__", delcarationCode);
    osgEarth::replaceIn(imageFSupdated, "__BODY_CODE__", bodyCode + "}");

    program->setFunction("oe_ImageBinding_VS", imageVS, ShaderComp::LOCATION_VERTEX_MODEL);
    program->setFunction("oe_ImageBinding_FS", imageFSupdated, ShaderComp::LOCATION_FRAGMENT_COLORING);

    return program;
}


// setup the root scenegraph
osg::Group* RasterToModelGraph::createRoot( const TileKey& key )
{
    // expected pre conditions
    int bandNumber = 0;
    if (! _imageLayer->getTileSource()->getBandsNumber(bandNumber)
            || ! _imageLayer->getProfile()->getSRS() || ! _imageLayer->getProfile()->getSRS()->getEllipsoid()
            || key.hasBandsDefined() )
        return nullptr;

    // build the sphere section which will be used to drape the image
    osg::ref_ptr<const osg::EllipsoidModel> ellipsoidModel = _imageLayer->getProfile()->getSRS()->getEllipsoid();
    _sphereForOverlay = buildPartialEllipsoidGeometry(ellipsoidModel.get(), _imageLayer->getProfile()->getLatLongExtent().originalBounds());

    // case multiple bands coded into one color channel
    int maxBandsPerTile = _imageLayer->getTileSource()->getOptions().colorRamp().isSet() ?
                _imageLayer->getTileSource()->getOptions().colorRamp()->nbBandsPerChannel() * 4 : 4;

    // check if wants a full load at once or many pagedLOD
    bool loadAllAtOnce = _options.loadAllAtOnce().isSetTo(true);

    // then build one pagedLOD or grounp per possible texture (each texture holds many bands defined in 'maxBandsPerTile')
    GroupMultiBands* groupMultiBands = new GroupMultiBands();
    unsigned defaultBand = _options.imageBand().getOrUse(0);
    TileKey keyTmp(key);
    keyTmp.setupNextAvailableBands(bandNumber, maxBandsPerTile);
    while (keyTmp.hasBandsDefined())
    {
        unsigned int minBand, maxBand;
        keyTmp.getTileBands(minBand, maxBand);
        std::string uri = s_makeURI(keyTmp.getLOD(), keyTmp.getTileX(), keyTmp.getTileY(), minBand, maxBand);

        osg::Node* node = nullptr;
        if (loadAllAtOnce)
            node = load(keyTmp.getLOD(), keyTmp.getTileX(), keyTmp.getTileY(), minBand, maxBand);
        else
            node = createPagedNode( _rootBs, uri, 0.0f, _rootMaxRange, _sgCallbacks.get(),
                                    _defaultFileLocationCallback.get(), getDBOptions(), this );

        OE_WARN << "JD Build raster " << keyTmp.full_str() << "\n";

        groupMultiBands->addChild( node );

        osg::ref_ptr<BandsInformation> bandsInfo = new BandsInformation(keyTmp, maxBandsPerTile);
        node->setUserData( bandsInfo );

        // this uniform holds the color channel to use to access a given band
        osg::Uniform* uniform = new osg::Uniform(osg::Uniform::INT, _multiBand_uniform_name.c_str());
        node->getOrCreateStateSet()->addUniform(uniform, osg::StateAttribute::OVERRIDE);

        // case the texture holds one band per color channel
        // then only one uniform is used to define the color channel to use
        if (maxBandsPerTile == 4)
        {
            int channel = bandsInfo->getUniformValForBand_1int(defaultBand);
            if ( channel == -1 )
            {
                uniform->set(0);
                node->setNodeMask(0);
            }
            else
            {
                uniform->set(channel);
                node->setNodeMask(~0);
            }
        }

        // case the texture holds two bands per color channel
        // then a second uniform is necessary to define the offset to use in the given color channel
        else if (maxBandsPerTile >= 8)
        {
            osg::Uniform* uniform2nd = new osg::Uniform(osg::Uniform::INT, _multiBand_2nd_level_uniform_name.c_str());
            node->getOrCreateStateSet()->addUniform(uniform2nd, osg::StateAttribute::OVERRIDE);
            int indexI = -1;
            int indexJ = -1;
            bandsInfo->getUniformValForBand_2int(defaultBand, indexI, indexJ);
            if ( indexI == -1 || indexJ == -1 )
            {
                uniform->set(0);
                uniform2nd->set(0);
                node->setNodeMask(0);
            }
            else
            {
                uniform->set(indexI);
                uniform2nd->set(indexJ);
                node->setNodeMask(~0);
            }
        }

        // go to the next available bands
        keyTmp.setupNextAvailableBands(bandNumber, maxBandsPerTile);
    }

    // setup the shader
    VirtualProgram* program = createProgramForImageBinding(_imageLayer->getTileSource());
    groupMultiBands->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);

    // test the animation of bands
    //groupMultiBands->addUpdateCallback( new BandsAnimation(this) );

    OE_DEBUG << LC << "Setup " << groupMultiBands->getNumChildren() << " " << (loadAllAtOnce ? "pagedLODs" : "nodes")
             << " for managing " << bandNumber << " raster bands for layer " << *_imageLayer->options().name() << std::endl;

    return groupMultiBands;
}

// setup the image as a texture and bind it the to sphere
osg::Group* RasterToModelGraph::bindGeomWithImage(const TileKey& key, ProgressCallback* progress )
{
    if (! _imageLayer.valid() || ! _imageLayer->getProfile() || ! _imageLayer->getTileSource())
        return nullptr;

    GeoImage image = _imageLayer->createImage(key, progress);
    if (image.valid())
    {
        osg::Group* bandsGroup = new osg::Group();

        osg::Texture2D* tex = new osg::Texture2D( image.getImage() );
        tex->setWrap( osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE );
        tex->setWrap( osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE );
        tex->setResizeNonPowerOfTwoHint(false);
        tex->setFilter( osg::Texture::MAG_FILTER, _imageLayer->options().magFilter().getOrUse(osg::Texture::NEAREST) );
        tex->setFilter( osg::Texture::MIN_FILTER, _imageLayer->options().minFilter().getOrUse(osg::Texture::NEAREST) );
        tex->setUnRefImageDataAfterApply(true);
        tex->setMaxAnisotropy( 1.f );
        tex->setInternalFormatMode( osg::Texture::USE_IMAGE_DATA_FORMAT );

        bandsGroup->getOrCreateStateSet()->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);
        bandsGroup->getOrCreateStateSet()->addUniform( new osg::Uniform("image_tex", 0) );

        bandsGroup->addChild(_sphereForOverlay.get());
        return bandsGroup;
    }

    return nullptr;
}


//---------------------------------------------------------------------------

RasterToModelGraph::RasterToModelGraph( const RasterToModelSourceOptions &options,
                                        const osgDB::Options*            dbOptions,
                                        SceneGraphCallbacks *            callbacks,
                                        ImageLayer*                      imageLayer)
    :_options(options), _sgCallbacks(callbacks), _dbOptions(dbOptions), _imageLayer(imageLayer)
{
    // check first the validity of the image layer
    if (! _imageLayer.valid() || ! _imageLayer->getProfile() || ! imageLayer->getTileSource())
    {
        OE_WARN << LC << "The provider ImageLayer is invalid." << std::endl;
        return;
    }

    // build the graph
    ctor();
}


void RasterToModelGraph::ctor()
{
    ADJUST_EVENT_TRAV_COUNT(this, 1);

    // So we can pass it to the pseudoloader
    setName(USER_OBJECT_NAME);

    // an FLC that queues feature data on the high-latency thread.
    _defaultFileLocationCallback = new HighLatencyFileLocationCallback();

    _usableMapExtent = _imageLayer->getProfile()->getExtent();

    osg::Node *node = setupPaging();

    addChild( node );
}


const osgDB::Options*
RasterToModelGraph::getDBOptions() const
{
    return _dbOptions.get();

//    // local options if they were set:
//    if (_dbOptions.valid())
//        return _dbOptions.get();

//    // otherwise get them from the map if possible:
//    osg::ref_ptr<const Map> map;
//    if (_map.lock(map))
//        return map->getReadOptions();

//    return 0L;
}


void RasterToModelGraph::addProfilingLogs(const std::string& trace)
{
    OpenThreads::ScopedLock< OpenThreads::Mutex > lock ( _profilingLogsMutex );
    _profilingLogs.push_back(trace);
}

RasterToModelGraph::~RasterToModelGraph()
{
    if ( osgEarth::isNotifyEnabled( osg::DEBUG_INFO ) )
        for ( auto& s : _profilingLogs )
            OE_DEBUG << LC << "[Profiling] " << s << std::endl;
}



osg::BoundingSphered
RasterToModelGraph::getBoundInWorldCoords(const GeoExtent &extent) const
{
    osg::Vec3d center, corner;
    GeoExtent workingExtent;

    if (!extent.isValid())
        return osg::BoundingSphered();

    if (extent.getSRS()->isEquivalentTo(_usableMapExtent.getSRS()))
        workingExtent = extent;
    else
        workingExtent = extent.transform(_usableMapExtent.getSRS());

    return workingExtent.createWorldBoundingSphere(-11000, 9000); // lowest and highest points on earth
}

osg::Node* RasterToModelGraph::setupPaging()
{
    // calculate the bounds of the full data extent:
    _rootBs = getBoundInWorldCoords(_usableMapExtent);

    // calculate the max range for the top-level PLOD:
    // TODO: a user-specified maxRange is actually an altitude, so this is not strictly correct anymore!
    _rootMaxRange = _options.maxRange().isSet() ? *_options.maxRange() : FLT_MAX;

    // build the URI for the top-level paged LOD:
    std::string uri = s_makeURI(0, 0, 0);

    // bulid the top level node:
    osg::Node *topNode;

    // if a full asynchronous load is required then create a PageLOD at root level
    if (_options.loadAllAtOnce().isSetTo(true))
    {
        topNode = createPagedNode(_rootBs, uri, 0.0f, _rootMaxRange, _sgCallbacks.get(),
                    _defaultFileLocationCallback.get(), getDBOptions(), this);
    }

    // if it is not a full asynchronous load, then build the root graph now
    else
    {
        topNode = load(0, 0, 0);
    }

    return topNode;
}

/**
 * Called by the pseudo-loader, this method attempts to load a single tile of several bands.
 */
osg::Node *RasterToModelGraph::load(unsigned lod, unsigned tileX, unsigned tileY,
                                   unsigned minBand, unsigned maxBand)
{
    if ( lod != 0u )
    {
        OE_WARN << LC << "Request for a LOD different from 0 is not allowed when embeding an image layer." << std::endl;
        return new osg::Group();
    }

    OE_WARN << "JD START LOAD " << minBand << " -> " << maxBand << "\n";

    osg::Group *result = nullptr;
    bool isRootLoad    = false;
    osg::Group *geometry = nullptr;
    TileKey key(0, tileX, tileY, _imageLayer->getProfile());
    key.setBands(minBand, maxBand);

    // case build of the top root node
    if (! key.hasBandsDefined() )
    {
        geometry = createRoot( key );
        isRootLoad = true;
    }

    // case build of one image to bind on the sphere
    else
    {
        geometry = bindGeomWithImage( key, nullptr );
        if ( ! geometry )
            OE_WARN << LC << "Error while binding image layer with geometry for key " << key.full_str() << std::endl;
    }

    result = geometry;

    if (!result)
    {
        // If the read resulting in nothing, create an empty group so that the read
        // (technically) succeeds and the pager won't try to load the null child
        // over and over.
        result = new osg::Group();
    }
    else
    {
        // For some unknown reason, this breaks when I insert an LOD. -gw
        // RemoveEmptyGroupsVisitor::run( result );
    }

    OE_WARN << "JD END LOAD " << minBand << " -> " << maxBand << " isRootLoad=" << isRootLoad << "\n";

    // Done - run the pre-merge operations.
    if (! _options.loadAllAtOnce().isSetTo(true) || isRootLoad)
        runPreMergeOperations(result);

    return result;
}


void RasterToModelGraph::runPreMergeOperations(osg::Node *node)
{
    // apply the render order to the incomming node
    if ( _options.renderOrder().isSet() )
    {
        node->getOrCreateStateSet()->setRenderBinDetails(
            _options.renderOrder().value(), "DepthSortedBin", osg::StateSet::PROTECTED_RENDERBIN_DETAILS );
    }

    OE_WARN << "JD runPreMergeOperations \n";

    if (_sgCallbacks.valid())
        _sgCallbacks->firePreMergeNode(node);
}

void RasterToModelGraph::runPostMergeOperations(osg::Node *node)
{
    if (_sgCallbacks.valid())
        _sgCallbacks->firePostMergeNode(node);
}
