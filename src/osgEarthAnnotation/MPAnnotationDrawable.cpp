
#include <osgEarthAnnotation/MPAnnotationDrawable>
#include <osgEarthSymbology/InstanceSymbol>
#include <osgEarth/Registry>
#include <osgEarthAnnotation/AnnotationUtils>

#include <osgText/String>



#define LC "[MPAnnotationDrawable] "

using namespace osgEarth::Annotation;
using namespace osgEarth::Symbology;
using namespace osgEarth;


std::map<std::string, osg::observer_ptr<MPStateSetFontAltas>> MPAnnotationDrawable::s_atlasStateSetList;

namespace  {
    const std::string undef = "-32765";
    const Color magenta(1., 135./255., 195./255.);
}

LODAnno::LODAnno(double alt, const std::vector<GLushort>& pDrawElt) : altitudeMax{alt}, drawElts{pDrawElt}
{
}

MPAnnotationDrawable::MPAnnotationDrawable(const Style &style, const osgDB::Options* readOptions)
    : _readOptions(readOptions)
{
    setUseVertexBufferObjects(true);
    setUseDisplayList(false);

    osg::ref_ptr<const InstanceSymbol> instance = style.get<InstanceSymbol>();
    const IconSymbol* iconSym = instance.valid() ? instance->asIcon() : nullptr;
    _context = iconSym && iconSym->url().isSet() ? iconSym->url()->uriContext() : URIContext();
    _scale = iconSym && iconSym->scale().isSet() ? iconSym->scale()->eval() : 1.;
    osg::ref_ptr<const BBoxSymbol> bboxSym = style.get<BBoxSymbol>();
    _bbox_margin = bboxSym && bboxSym->margin().isSet() ? bboxSym->margin().value() : 2.f;
    _icon_margin = iconSym && iconSym->margin().isSet() ? iconSym->margin().value() : 5.f;
    const TextSymbol* textSymbol = style.get<TextSymbol>();
    _mainFont = textSymbol && textSymbol->font().isSet() ? textSymbol->font().get() : "";
    _mainFontSize = textSymbol && textSymbol->size().isSet() ? textSymbol->size()->eval() : 16.;
    _multi_text_margin = textSymbol && textSymbol->predefinedOrganisationMargin().isSet() ? textSymbol->predefinedOrganisationMargin().value() : 4.f;
    _altFirstLevel = textSymbol && textSymbol->minRange().isSet() ? textSymbol->minRange().get() : DBL_MAX;

    const TextSymbol* textSym = style.get<TextSymbol>();
    if ( textSym && textSym->encoding().isSet() )
        _text_encoding = AnnotationUtils::convertTextSymbolEncoding(textSym->encoding().value());

    buildStateSet(style);
    buildGeometry(style);
}

void MPAnnotationDrawable::buildStateSet(const osgEarth::Symbology::Style& style)
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
                atlasStateSet = stateSet = new MPStateSetFontAltas(atlasPath, textSymbol->fontAltas()->uriContext(), _readOptions);
        }

        setStateSet(atlasStateSet.get());
    }
}

void MPAnnotationDrawable::buildGeometry(const osgEarth::Symbology::Style& style)
{
    _v = new osg::Vec3Array();
    _c = new osg::Vec4Array();
    _t = new osg::Vec2Array();
    _infoArray = new osg::Vec4Array();
    _infoArray->setPreserveDataType(true);
    _d = new osg::DrawElementsUShort(GL_TRIANGLES);
    osg::BoundingBox refBBox(0., 0., 0., 0., 0., 0.);

    //-----------------------
    // Search for all icons

    StringVector iconList;
    osg::ref_ptr<const InstanceSymbol> instance = style.get<InstanceSymbol>();
    const IconSymbol* iconSym = instance.valid() ? instance->asIcon() : nullptr;
    if ( iconSym && iconSym->url().isSet() )
    {
        if ( ! iconSym->alignment().isSet() || ! iconSym->alignment().isSetTo(IconSymbol::ALIGN_CENTER_CENTER) )
            OE_WARN << LC << "The icon alignment " << iconSym->alignment().value() << " is not supported yet.\n";

        std::string iconTxt = iconSym->url()->eval();

        // check if there is a list of icons
        if ( iconTxt.find(";") != std::string::npos )
        {
            StringTokenizer splitter( ";", "" );
            splitter.tokenize( iconTxt, iconList );
            if (! iconList.empty() && ! iconList[0].empty() )
                iconTxt = iconList[0];
        }

        // push the main centered icon
        if (! iconTxt.empty() )
        {
            appendIcon(iconTxt);
            refBBox.init();
            for ( unsigned int i = 0 ; i < _v->getNumElements() ; i++ )
                refBBox.expandBy( _v->at(i) );
            refBBox.xMax() += _icon_margin;
            refBBox.yMax() += _icon_margin;
            refBBox.xMin() -= _icon_margin;
            refBBox.yMin() -= _icon_margin;
        }
    }

    //-----------------------
    // Search for all texts

    StringVector textList;
    const TextSymbol* textSymbol = style.get<TextSymbol>();
    bool predefinedOrganisation = textSymbol && textSymbol->predefinedOrganisation().isSet();
    if ( textSymbol )
    {
        std::string mainText = textSymbol->content()->eval();
        if ( predefinedOrganisation )
        {
            StringTokenizer splitter( ";", "" );
            splitter.tokenize( mainText, textList );
            if (! textList.empty() )
                mainText = textList[0];
        }

        // push the main centered text
        if ( ! mainText.empty() )
        {
            osg::Vec4 color(textSymbol->fill().isSet() ? textSymbol->fill()->color() : Color::White);
            int nbVert = appendText(mainText, _mainFont, color, _mainFontSize, _altFirstLevel);
            if (nbVert > 0)
            {
                TextSymbol::Alignment align = textSymbol->alignment().isSet() ?
                            textSymbol->alignment().get() : TextSymbol::Alignment::ALIGN_CENTER_CENTER;
                moveTextPosition(nbVert, refBBox, align);
                for ( unsigned int i = 0 ; i < _v->getNumElements() ; i++ )
                    refBBox.expandBy( _v->at(i) );
            }
        }
    }


    //-----------------------
    // Append the secondary icons to the right

    if ( iconList.size() > 1 && _v->size() > 0 )
    {
        float xMax = refBBox.xMax();
        for ( unsigned int i=1 ; i<iconList.size() ; ++i )
            xMax = appendIcon(iconList[i], _altFirstLevel, xMax);

        refBBox.init();
        for ( unsigned int i = 0 ; i < _v->getNumElements() ; ++i )
            refBBox.expandBy( _v->at(i) );
    }


    // ----------------------
    // Append the secondary texts
    // only airway organisation treated for now

    if ( textSymbol && textSymbol->predefinedOrganisation().isSetTo("airway") && textList.size() == 7 )
    {
        double margin = textSymbol->predefinedOrganisationMargin().isSet() ? textSymbol->predefinedOrganisationMargin().get() : 4.;
        const osg::Vec3 marginVec(margin, margin, 0.);
        refBBox.set( refBBox._min - marginVec, refBBox._max + marginVec);

        TextSymbol::Alignment alignList[] = { TextSymbol::ALIGN_LEFT_BOTTOM, TextSymbol::ALIGN_LEFT_BOTTOM_BASE_LINE, TextSymbol::ALIGN_LEFT_TOP,
                                             TextSymbol::ALIGN_RIGHT_TOP, TextSymbol::ALIGN_RIGHT_BOTTOM_BASE_LINE, TextSymbol::ALIGN_RIGHT_BOTTOM };
        float fontSizeSmaller = _mainFontSize / 18.f * 15.f;
        float fontSizeList[] = {fontSizeSmaller, fontSizeSmaller, fontSizeSmaller, fontSizeSmaller, fontSizeSmaller, fontSizeSmaller};
        Color colorOrg = textSymbol->fill().isSet() ? textSymbol->fill().get().color() : Color::White;
        Color colorList[] = {colorOrg, colorOrg, colorOrg, magenta, colorOrg, colorOrg};
        bool textBBox[] = {true, false, false, false, false, false};
        double alt2ndLevel = textSymbol->minRange2ndlevel().isSet() ? textSymbol->minRange2ndlevel().value() : _altFirstLevel;
        // make the font regular for the secondary texts
        std::string subFont(_mainFont);
        replaceIn(subFont, "Bold", "Regular");

        // for each text (except the first one)
        for ( unsigned int i=1 ; i<textList.size() ; ++i )
        {
            int nbVert = appendText(textList[i], subFont, colorList[i-1], fontSizeList[i-1], alt2ndLevel);
            if (nbVert > 0)
                moveTextPosition(nbVert, refBBox, alignList[i-1]);
        }
    }

    setVertexArray( _v.get() );
    setTexCoordArray( 0u, _t.get(), osg::Array::BIND_PER_VERTEX );
    setColorArray( _c.get(), osg::Array::BIND_PER_VERTEX );
    setVertexAttribArray( MPStateSetFontAltas::ATTRIB_ANNO_INFO, _infoArray.get(), osg::Array::BIND_PER_VERTEX );
    addPrimitiveSet( _d.get() );

    if ( getVertexArray()->getVertexBufferObject() )
        getVertexArray()->getVertexBufferObject()->setUsage(GL_STATIC_DRAW_ARB);

    _camAlt = 0.;
    _LOD = _LODlist.size()-1;
    const osg::BoundingSphere &bSphere = getBound();
    osg::Vec3 radius(bSphere.radius(), bSphere.radius(), 0.);
    _bboxSymetric.set( bSphere.center() - radius, bSphere.center() + radius);
}

float MPAnnotationDrawable::appendIcon(const std::string& urlPath, double alt, float xMax)
{
    // load the image
    URI imageURI(urlPath, _context);
    osg::ref_ptr<osg::Image> image;
    if ( ! imageURI.empty() )
        image = imageURI.getImage( _readOptions );
    if ( ! image.get() )
        return xMax;

    float width = Registry::instance()->getDevicePixelRatio() * _scale * image->s();
    float height = Registry::instance()->getDevicePixelRatio() * _scale * image->t();
    float x0 = -width/2.0;
    float y0 = -height/2.0;

    // compute the x translation if necessary
    float rightShift = 0.f;
    if ( xMax != FLT_MAX )
        rightShift += xMax + width / 2.f + _icon_margin;

    // push vertices
    _v->push_back( osg::Vec3(x0 + rightShift, y0, 0) );
    _v->push_back( osg::Vec3(x0 + width + rightShift, y0, 0 ) );
    _v->push_back( osg::Vec3(x0 + width + rightShift, y0 + height, 0 ) );
    _v->push_back( osg::Vec3(x0 + rightShift, y0 + height, 0 ) );

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
    _infoArray->push_back( osg::Vec4(width, height, MPStateSetFontAltas::TYPE_ICON, 0.f) );
    _infoArray->push_back( osg::Vec4(width, height, MPStateSetFontAltas::TYPE_ICON, 0.f) );
    _infoArray->push_back( osg::Vec4(width, height, MPStateSetFontAltas::TYPE_ICON, 0.f) );
    _infoArray->push_back( osg::Vec4(width, height, MPStateSetFontAltas::TYPE_ICON, 0.f) );

    // push the draw elements
    unsigned int last = _v->getNumElements() - 1;
    std::vector<GLushort> drawIndices { GLushort(last-3), GLushort(last-2), GLushort(last-1), GLushort(last-3), GLushort(last-1), GLushort(last)};
    pushDrawElements(alt, drawIndices);

    return x0 + width + rightShift;
}

int MPAnnotationDrawable::appendText(const std::string& text, const std::string& font, const osg::Vec4& color, double fontSize, double alt)
{
    MPStateSetFontAltas* stateSet = static_cast<MPStateSetFontAltas*>(getStateSet());
    if ( ! stateSet )
        return 0;

    // filter the text according to known rules
    if ( text.empty() || text == undef)
        return 0;

    std::string tmpText(text);
    if ( text.find(":") == 1 )
    {
        // F: means that the value must be display with format FLxxx
        // D: means that the value must be display with format xxx°
        // R: means that the value must be display with format Rxxx
        StringVector textSplit;
        StringTokenizer splitter( ":", "" );
        splitter.tokenize( text, textSplit );

        if (textSplit.size() == 1)
            return 0;

        if (textSplit.size() == 2)
        {
            if (textSplit[1].empty() || textSplit[1] == undef)
                return 0;

            // convert to int
            int val;
            std::istringstream(textSplit[1]) >> val;

            // add the FL symbol if required
            if ( textSplit[0] == "F" )
            {
                tmpText = "FL" + std::to_string(val/100);
            }
            // add the R symbol if required
            else if ( textSplit[0] == "R" )
            {
                tmpText = "R" + std::to_string(val);
            }
            // add the ° symbol if required and make sure it has always three digits
            else if ( textSplit[0] == "D" )
            {
                if ( val < 10 ) tmpText = "00" + textSplit[1] + "°";
                else if ( val < 100 ) tmpText = "0" + textSplit[1] + "°";
                else tmpText = textSplit[1] + "°";
            }
            else
            {
                tmpText = textSplit[1];
            }
        }
    }

//    OE_WARN << "BUILD TEXT " << text << " " << font << " " <<  color.r() << " " <<  fontSize << " " << alt << "\n";

    // append each character one by one
    osg::Vec3 advance(0., 0., 0.);
    std::vector<unsigned int> drawIndices;
    int nbVertices = 0;
    osgText::String textFormated(tmpText, _text_encoding);
    MPStateSetFontAltas::GlyphMap::const_iterator itGlyph;
    for ( osgText::String::const_iterator itrChar=textFormated.begin() ; itrChar!=textFormated.end() ; ++itrChar )
    {
        unsigned int charcode = *itrChar;
        itGlyph = stateSet->mapGlyphs.find(font + "/" + std::to_string(charcode));
        if ( itGlyph == stateSet->mapGlyphs.end() )
        {
            OE_WARN << LC << "The font atlas does not contain the character code " << std::to_string(charcode) << "\n";
            continue;
        }

        const GlyphInfo& glyphInfo = itGlyph->second;

        double scale = fontSize / stateSet->refFontSize;

        _v->push_back( glyphInfo.lb_v * scale + advance );
        _v->push_back( glyphInfo.lt_v * scale + advance );
        _v->push_back( glyphInfo.rt_v * scale + advance );
        _v->push_back( glyphInfo.rb_v * scale + advance );

        _c->push_back( color );
        _c->push_back( color );
        _c->push_back( color );
        _c->push_back( color );

        _t->push_back( glyphInfo.lb_t );
        _t->push_back( glyphInfo.lt_t );
        _t->push_back( glyphInfo.rt_t );
        _t->push_back( glyphInfo.rb_t );

        // push the draw type
        _infoArray->push_back( osg::Vec4(glyphInfo.size.x(), glyphInfo.size.y(), MPStateSetFontAltas::TYPE_CHARACTER_MSDF, 0.f) );
        _infoArray->push_back( osg::Vec4(glyphInfo.size.x(), glyphInfo.size.y(), MPStateSetFontAltas::TYPE_CHARACTER_MSDF, 0.f) );
        _infoArray->push_back( osg::Vec4(glyphInfo.size.x(), glyphInfo.size.y(), MPStateSetFontAltas::TYPE_CHARACTER_MSDF, 0.f) );
        _infoArray->push_back( osg::Vec4(glyphInfo.size.x(), glyphInfo.size.y(), MPStateSetFontAltas::TYPE_CHARACTER_MSDF, 0.f) );

        unsigned int last = _v->getNumElements() - 1;
        std::vector<GLushort> drawIndices { GLushort(last-3), GLushort(last-2), GLushort(last-1), GLushort(last-3), GLushort(last-1), GLushort(last)};
        pushDrawElements(alt, drawIndices);

        advance.x() += glyphInfo.advance * scale;
        nbVertices += 4;
    }

    return nbVertices;
}

void MPAnnotationDrawable::pushDrawElements(double alt, const std::vector<GLushort> &pDrawElt)
{
    int nbEltBefore = 0;
    int indexBefore = 0;
    for ( auto& level : _LODlist )
    {
        // insert a new level before this one
        if ( alt > level.altitudeMax )
        {
            _d->insert( _d->begin() + nbEltBefore, pDrawElt.begin(), pDrawElt.end() );
            _LODlist.insert( _LODlist.begin() + indexBefore, LODAnno(alt, pDrawElt) );
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
        indexBefore++;
    }

    // insert at the end
    _d->insert( _d->end(), pDrawElt.begin(), pDrawElt.end() );
    _LODlist.push_back( LODAnno(alt, pDrawElt) );
}

void MPAnnotationDrawable::moveTextPosition(int nbVertices, const osg::BoundingBox& refBBox, TextSymbol::Alignment alignment)
{
    osg::Vec3 translate;
    bool doTranslation = false;

    // bbox of the text
    osg::BoundingBox textBBox;
    for ( unsigned int i = _v->getNumElements()-nbVertices ; i < _v->getNumElements() ; ++i )
        textBBox.expandBy( _v->at(i) );

    // horizontal : center of the ref
    // vertical : center of the ref
    if ( alignment == TextSymbol::Alignment::ALIGN_CENTER_CENTER )
    {
        osg::Vec3 textCenter(textBBox.center());
        osg::Vec3 refCenter(refBBox.center());
        translate = - textCenter + refCenter;
        doTranslation = true;
    }

    // horizontal : right of the ref
    // vertical : center of the ref
    else if ( alignment == TextSymbol::Alignment::ALIGN_LEFT_CENTER )
    {
        osg::Vec3 textCenter(textBBox.center());
        osg::Vec3 refCenter(refBBox.center());
        translate.set( refBBox.xMax() - textBBox.xMin(), - textCenter.y() + refCenter.y(), 0. );
        doTranslation = true;
    }

    // horizontal : center of the ref = left of the text
    // vertical : top of the ref = bottom of the text
    else if ( alignment == TextSymbol::Alignment::ALIGN_LEFT_BOTTOM )
    {
        osg::Vec3 refCenter(refBBox.center());
        translate.set( refCenter.x() - textBBox.xMin() + _multi_text_margin, refBBox.yMax() - textBBox.yMin() + _multi_text_margin, 0. );

//        translate.set( refBBox.xMax() - textBBox.xMin(), - textBBox.yMax() - refBBox.yMin(), 0. );
        doTranslation = true;
    }

    // horizontal : center of the ref = left of the text
    // vertical : bottom of the ref = top of the text
    else if ( alignment == TextSymbol::Alignment::ALIGN_LEFT_TOP )
    {
        osg::Vec3 refCenter(refBBox.center());
        translate.set( refCenter.x() - textBBox.xMin() + _multi_text_margin, - textBBox.yMax() + refBBox.yMin() - _multi_text_margin,  0. );

//        translate.set( refBBox.xMax() - textBBox.xMin(), - textBBox.yMax() - refBBox.yMin(), 0. );
        doTranslation = true;
    }

    // horizontal : center of the ref = right of the text
    // vertical : bottom of the ref = top of the text
    else if ( alignment == TextSymbol::Alignment::ALIGN_RIGHT_TOP )
    {
        osg::Vec3 refCenter(refBBox.center());
        translate.set( refCenter.x() - textBBox.xMax() - _multi_text_margin, -textBBox.yMax() + refBBox.yMin() - _multi_text_margin, 0. );
        doTranslation = true;
    }

    // horizontal : center of the ref = right of the text
    // vertical : top of the ref = bottom of the text
    else if ( alignment == TextSymbol::Alignment::ALIGN_RIGHT_BOTTOM )
    {
        osg::Vec3 refCenter(refBBox.center());
        //translate.set( refCenter.x() - (textBBox.xMax()-textBBox.xMin()) - _multi_text_margin, refBBox.yMax() + _multi_text_margin, 0. );
        translate.set( refCenter.x() - textBBox.xMax() - _multi_text_margin, refBBox.yMax() - textBBox.yMin() + _multi_text_margin, 0. );
        doTranslation = true;
    }

    // horizontal : right of the ref
    // vertical : center of the ref = bottom of the text
    else if ( alignment == TextSymbol::Alignment::ALIGN_LEFT_BOTTOM_BASE_LINE )
    {
        osg::Vec3 textCenter(textBBox.center());
        osg::Vec3 refCenter(refBBox.center());
        translate.set( refBBox.xMax() - textBBox.xMin() + _multi_text_margin, - textCenter.y() + refCenter.y() + _multi_text_margin, 0. );
        doTranslation = true;
    }

    // horizontal : left of the ref
    // vertical : center of the ref = bottom of the text
    else if ( alignment == TextSymbol::Alignment::ALIGN_RIGHT_BOTTOM_BASE_LINE )
    {
        osg::Vec3 textCenter(textBBox.center());
        osg::Vec3 refCenter(refBBox.center());
        translate.set( - textBBox.xMax() + refBBox.xMin() - _multi_text_margin, - textCenter.y() + refCenter.y() + _multi_text_margin, 0. );
        doTranslation = true;
    }



    // other cases are not treated
    else
    {
        OE_WARN << LC << "The text alignement " << alignment << " is not supported yet.";
    }

    // then translate
    if ( doTranslation )
        for ( unsigned int i = _v->getNumElements()-nbVertices ; i < _v->getNumElements() ; ++i )
            _v->at(i) += translate;
}

void MPAnnotationDrawable::setAltitude(double alt)
{
    if ( _camAlt == alt )
        return;

//    OE_WARN << LC << "CAM MOVE\n";

    // case zoom out
    if ( alt > _camAlt )
    {
//        OE_WARN << LC << "  zoom out\n";
        for ( int i = _LOD ; i >= 0 ; i-- )
        {
            if ( alt > _LODlist[i].altitudeMax )
            {
                // hide elements
//                OE_WARN << LC << "    zoom out NEW LOD " << i-1 << "\n";
                _d->resizeElements(_d->size() - _LODlist[i].drawElts.size());
                _LOD = i-1;
                dirty();
            }
        }
    }

    // case zoom in
    else// if ( alt < _camAlt )
    {
//        OE_WARN << LC << "  zoom in\n";
        for ( unsigned int i = static_cast<unsigned int>(_LOD+1) ; i < _LODlist.size() ; ++i )
        {
            if ( alt <= _LODlist[i].altitudeMax )
            {
                // push elements
                for ( auto elt : _LODlist[i].drawElts )
                    _d->addElement(elt);
//                OE_WARN << LC << "    zoom in NEW LOD " << i << "\n";
                _LOD = i;
                dirty();
            }
        }
    }

    if ( _LOD == -1 && _d->size() > 0 )
    {
        OE_WARN << LC << "    full reset \n";
        _d->resizeElements(0);
        dirty();
    }

    _camAlt = alt;
}

void MPAnnotationDrawable::dirty()
{
    _d->dirty();
    dirtyBound();
}
