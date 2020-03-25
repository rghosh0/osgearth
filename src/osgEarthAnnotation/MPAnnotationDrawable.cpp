
#include <osgEarthAnnotation/MPAnnotationDrawable>
#include <osgEarthSymbology/InstanceSymbol>
#include <osgEarth/Registry>

#include <osgText/String>



#define LC "[MPStateSetFontAltas] "

using namespace osgEarth::Annotation;
using namespace osgEarth::Symbology;
using namespace osgEarth;


std::map<std::string, osg::observer_ptr<MPStateSetFontAltas>> MPAnnotationDrawable::s_atlasStateSetList;


LODAnno::LODAnno(double alt, const std::vector<GLushort>& pDrawElt) : altitudeMax{alt}, drawElts{pDrawElt}
{
}

MPAnnotationDrawable::MPAnnotationDrawable(const Style &style, const osgDB::Options* readOptions)
    : osg::Geometry()
{
    setUseVertexBufferObjects(true);
    setUseDisplayList(false);

    buildStateSet(style, readOptions);

    _v = new osg::Vec3Array();
    _c = new osg::Vec4Array();
    _t = new osg::Vec2Array();
    _typeArray = new osg::IntArray();
    _typeArray->setPreserveDataType(true);
    _d = new osg::DrawElementsUShort(GL_TRIANGLES);

    appendIcons( style, readOptions );
    appendTexts( style, readOptions );

    setVertexArray( _v.get() );
    setTexCoordArray( 0u, _t.get(), osg::Array::BIND_PER_VERTEX );
    setColorArray( _c.get(), osg::Array::BIND_PER_VERTEX );
    setVertexAttribArray( MPStateSetFontAltas::ATTRIB_DRAW_TYPE, _typeArray.get(), osg::Array::BIND_PER_VERTEX );
    addPrimitiveSet( _d.get() );

    if ( getVertexArray()->getVertexBufferObject() )
        getVertexArray()->getVertexBufferObject()->setUsage(GL_STATIC_DRAW_ARB);
}

void MPAnnotationDrawable::buildStateSet(const osgEarth::Symbology::Style& style, const osgDB::Options* readOptions)
{
    const TextSymbol* textSymbol = style.get<TextSymbol>();

    // shared font atlas stateset
    osg::ref_ptr<MPStateSetFontAltas> stateSet;
    if ( textSymbol && textSymbol->fontAltas().isSet() )
    {
        std::string atlasPath = textSymbol->fontAltas()->eval();
        osg::observer_ptr<MPStateSetFontAltas> &atlasStateSet = s_atlasStateSetList[atlasPath];
        if (! atlasStateSet.lock(stateSet))
        {
            static Threading::Mutex s_mutex;
            Threading::ScopedMutexLock lock(s_mutex);
            if (! atlasStateSet.lock(stateSet))
                atlasStateSet = stateSet = new MPStateSetFontAltas(atlasPath, textSymbol->fontAltas()->uriContext(), readOptions);
        }

        setStateSet(atlasStateSet.get());
    }
}

void MPAnnotationDrawable::appendIcons(const osgEarth::Symbology::Style& style, const osgDB::Options* readOptions)
{
    StringVector iconList;
    osg::ref_ptr<const InstanceSymbol> instance = style.get<InstanceSymbol>();
    const IconSymbol* iconSym = instance.valid() ? instance->asIcon() : nullptr;
    if (! iconSym || ! iconSym->url().isSet())
        return;

    // check if there is a list of icons
    std::string iconTxt = iconSym->url()->eval();
    if ( iconTxt.find(";") != std::string::npos )
    {
        StringTokenizer splitter( ";", "" );
        splitter.tokenize( iconTxt, iconList );
        if (! iconList.empty() )
            iconTxt = iconList[0];
    }
    if ( iconTxt.empty() )
        return;

    // load the image
    URI imageURI(iconTxt, iconSym->url().isSet() ? iconSym->url()->uriContext() : URIContext());
    osg::ref_ptr<osg::Image> image;
    if ( !imageURI.empty() )
        image = imageURI.getImage( readOptions );
    if (! image.get() )
        return;

    // position the icon. only center supported
    if ( ! iconSym->alignment().isSet() || ! iconSym->alignment().isSetTo(IconSymbol::ALIGN_CENTER_CENTER) )
        OE_WARN << LC << "The icon alignment " << iconSym->alignment().value() << " is not supported yet.\n";

    // scale the icon if necessary
    double scale = 1.0;
    if ( iconSym && iconSym->scale().isSet() )
        scale = iconSym->scale()->eval();

    // push vertices
    float width = Registry::instance()->getDevicePixelRatio() * scale * image->s();
    float height = Registry::instance()->getDevicePixelRatio() * scale * image->t();
    float x0 = -width/2.0;
    float y0 = -height/2.0;
    _v->push_back( osg::Vec3(x0, y0, 0) );
    _v->push_back( osg::Vec3(x0 + width, y0, 0 ) );
    _v->push_back( osg::Vec3(x0 + width, y0 + height, 0 ) );
    _v->push_back( osg::Vec3(x0, y0 + height, 0 ) );

    // push colors
    _c->push_back(osg::Vec4(1., 0., 0., 1.));
    _c->push_back(osg::Vec4(1., 0., 0., 1.));
    _c->push_back(osg::Vec4(1., 0., 0., 1.));
    _c->push_back(osg::Vec4(1., 0., 0., 1.));

    // push texture coords
    bool flip = image->getOrigin() == osg::Image::TOP_LEFT;
    _t->push_back( osg::Vec2(0.0, flip ? 1.0 : 0.0) );
    _t->push_back( osg::Vec2(1.0, flip ? 1.0 : 0.0) );
    _t->push_back( osg::Vec2(1.0, flip ? 0.0 : 1.0) );
    _t->push_back( osg::Vec2(0.0, flip ? 0.0 : 1.0) );

    // push the draw type
    _typeArray->push_back( MPStateSetFontAltas::TYPE_ICON );
    _typeArray->push_back( MPStateSetFontAltas::TYPE_ICON );
    _typeArray->push_back( MPStateSetFontAltas::TYPE_ICON );
    _typeArray->push_back( MPStateSetFontAltas::TYPE_ICON );

    // push the draw elements
    unsigned int last = _v->getNumElements() - 1;
    std::vector<GLushort> drawIndices { GLushort(last-3), GLushort(last-2), GLushort(last-1), GLushort(last-3), GLushort(last-1), GLushort(last)};
    pushDrawElements(DBL_MAX, drawIndices);
}

void MPAnnotationDrawable::appendTexts(const osgEarth::Symbology::Style &style, const osgDB::Options* readOptions)
{
    MPStateSetFontAltas* stateSet = static_cast<MPStateSetFontAltas*>(getStateSet());
    if ( ! stateSet )
        return;

    const TextSymbol* textSymbol = style.get<TextSymbol>();
    std::string font(textSymbol->font().get());
    osgText::String text( textSymbol->content()->eval() );
    osg::Vec3 advance(0., 0., 0.);
    std::vector<unsigned int> drawIndices;
    for ( osgText::String::iterator itr=text.begin() ; itr!=text.end() ; ++itr )
    {
        unsigned int charcode = *itr;
        GlyphInfo& glyphInfo = stateSet->_mapGlyphs[font + "/" + std::to_string(charcode)];

        _v->push_back( (glyphInfo.lb_v + advance) * 2. );
        _v->push_back( (glyphInfo.lt_v + advance) * 2. );
        _v->push_back( (glyphInfo.rt_v + advance) * 2. );
        _v->push_back( (glyphInfo.rb_v + advance) * 2. );

        _c->push_back(osg::Vec4(0., 1., 0., 1.));
        _c->push_back(osg::Vec4(0., 1., 0., 1.));
        _c->push_back(osg::Vec4(0., 1., 0., 1.));
        _c->push_back(osg::Vec4(0., 1., 0., 1.));

        _t->push_back(glyphInfo.lb_t);
        _t->push_back(glyphInfo.lt_t);
        _t->push_back(glyphInfo.rt_t);
        _t->push_back(glyphInfo.rb_t);

        // push the draw type
        _typeArray->push_back( MPStateSetFontAltas::TYPE_CHARACTER_MSDF );
        _typeArray->push_back( MPStateSetFontAltas::TYPE_CHARACTER_MSDF );
        _typeArray->push_back( MPStateSetFontAltas::TYPE_CHARACTER_MSDF );
        _typeArray->push_back( MPStateSetFontAltas::TYPE_CHARACTER_MSDF );

        unsigned int last = _v->getNumElements() - 1;
        std::vector<GLushort> drawIndices { GLushort(last-3), GLushort(last-2), GLushort(last-1), GLushort(last-3), GLushort(last-1), GLushort(last)};
        pushDrawElements(DBL_MAX, drawIndices);

        advance.x() += glyphInfo.advance;
    }
}

void MPAnnotationDrawable::pushDrawElements(double alt, const std::vector<GLushort> &pDrawElt)
{
    int nbEltBefore = 0;
    for ( auto level : _LODlist )
    {
        // insert a new level before this one
        if ( alt > level.altitudeMax )
        {
            _d->insert( _d->begin() + nbEltBefore, pDrawElt.begin(), pDrawElt.end() );
            _LODlist.insert( _LODlist.begin(), LODAnno(alt, pDrawElt) );
            return;
        }
        // insert into an existing level
        else if ( level.altitudeMax == alt )
        {
            _d->insert( _d->begin() + nbEltBefore + level.drawElts.size(), pDrawElt.begin(), pDrawElt.end() );
            level.drawElts.insert(level.drawElts.end(), pDrawElt.begin(), pDrawElt.end());
            return;
        }
        nbEltBefore += level.drawElts.size();
    }

    // insert at the end
    _d->insert( _d->begin(), pDrawElt.begin(), pDrawElt.end() );
    _LODlist.push_back( LODAnno(alt, pDrawElt) );
}

