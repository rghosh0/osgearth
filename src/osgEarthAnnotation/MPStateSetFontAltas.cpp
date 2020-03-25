/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/


#include <osgEarthAnnotation/MPStateSetFontAltas>
#include <osgEarth/GLUtils>
#include <osgEarth/VirtualProgram>
#include <osgEarthAnnotation/Shaders>

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osg/Texture2D>


#define LC "[MPStateSetFontAltas] "


using namespace osgEarth::Annotation;

const int MPStateSetFontAltas::ATTRIB_DRAW_TYPE = osg::Drawable::ATTRIBUTE_7;


GlyphInfo::GlyphInfo(double x, double y, double scale, double w, double h, double cursorX, double cursorY, double pAdvance, double textureSize) :
    advance(pAdvance/scale)
{
    // quad vertexes considering y=0 as baseline and scaled in font native unit
    lb_v.set(-cursorX / scale, -cursorY / scale, 0.);
    lt_v.set(-cursorX / scale, -cursorY / scale + h / scale, 0.);
    rt_v.set(-cursorX / scale + w / scale, -cursorY / scale + h / scale, 0.);
    rb_v.set(-cursorX / scale + w / scale, -cursorY / scale, 0.);

    // corresponding texture coordinates
    lb_t.set(x / textureSize, (y+h) / textureSize);
    lt_t.set(x / textureSize, y / textureSize);
    rt_t.set( (x+w) / textureSize, y / textureSize);
    rb_t.set( (x+w) / textureSize, (y+h) / textureSize);
}

MPStateSetFontAltas::MPStateSetFontAltas(const std::string& fontAtlasPath, const osgEarth::URIContext& context, const osgDB::Options *readOptions) : StateSet()
{
    // load the image atlas
    OE_WARN << "LOAD ATLAS IMAGE " << fontAtlasPath << "\n";
    URI imageURI(fontAtlasPath, context);
    if (! osgDB::fileExists(imageURI.full()))
        return;
    osg::ref_ptr<osg::Image> image;
    if ( !imageURI.empty() )
        image = imageURI.getImage(readOptions);
    if (! image->valid() )
    {
        OE_WARN << LC << "Unable to load the atlas " << fontAtlasPath << "\n";
        return;
    }

    // build the Glyph map
    double textureSize = image.get()->s();
    URI atlasConfURI(osgDB::getNameLessExtension(fontAtlasPath) + ".txt", URIContext());
    if (! osgDB::fileExists(atlasConfURI.full()))
        return;

    std::ifstream in(atlasConfURI.full().c_str());
    std::string key;
    double x, y, scale, w, h, advance, cursorX, cursorY;
    while(in >> key >> x >> y >> scale >> w >> h >> cursorX >> cursorY >> advance)
    {
        _mapGlyphs[key] = GlyphInfo(x, y, scale, w, h, cursorX, cursorY, advance, textureSize);
    }

    // set it as a texture
    osg::Texture2D* texture = new osg::Texture2D();
    texture->setWrap(osg::Texture2D::WRAP_S, osg::Texture2D::CLAMP_TO_EDGE);
    texture->setWrap(osg::Texture2D::WRAP_T, osg::Texture2D::CLAMP_TO_EDGE);
//    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
//    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    texture->setResizeNonPowerOfTwoHint(false);
    texture->setImage( image );

    // stateset stuff
    setDataVariance(osg::Object::DYNAMIC);
    setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    setMode(GL_BLEND, osg::StateAttribute::ON);
    GLUtils::setLighting(this, osg::StateAttribute::OFF);
    setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);

    // shaders
    VirtualProgram* vp = VirtualProgram::getOrCreate(this);
    vp->setName("MPAnnotation::stateSet");
    vp->addBindAttribLocation( "oe_anno_attr_type", ATTRIB_DRAW_TYPE );
    Shaders pkg;
    pkg.load( vp, pkg.MPAnno_Vertex );
    pkg.load( vp, pkg.MPAnno_Fragment );
    addUniform(new osg::Uniform("oe_anno_font_tex", 0));
    DefineList defineList;
    defineList["TYPE_CHARACTER_MSDF"] = osg::StateSet::DefinePair(std::to_string(TYPE_CHARACTER_MSDF), osg::StateAttribute::ON);
    defineList["TYPE_ICON"] = osg::StateSet::DefinePair(std::to_string(TYPE_ICON), osg::StateAttribute::ON);
    defineList["TYPE_BBOX"] = osg::StateSet::DefinePair(std::to_string(TYPE_BBOX), osg::StateAttribute::ON);
    setDefineList(defineList);
}
