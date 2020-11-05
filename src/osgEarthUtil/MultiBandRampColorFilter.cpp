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
*
*/
#include <osgEarthUtil/MultiBandRampColorFilter>
#include <osgEarth/VirtualProgram>
#include <osgEarth/StringUtils>
#include <osgEarth/ThreadingUtils>
#include <osg/Program>
#include <OpenThreads/Atomic>

using namespace osgEarth;
using namespace osgEarth::Util;

namespace
{
    static OpenThreads::Atomic s_uniformNameGen;

    static const char* s_localShaderSourceFS =
        "#version " GLSL_VERSION_STR "\n"
        GLSL_DEFAULT_PRECISION_FLOAT "\n"

        "uniform int __UNIFORM_NAME__;\n"

        "__CONST_DEF__"

        "void __ENTRY_POINT_FS__(inout vec4 color)\n"
        "{\n"
        "    float value = color[__UNIFORM_NAME__];\n"
        "    __RAMP_CODE__"
        "} \n";
}

//---------------------------------------------------------------------------

#define FUNCTION_PREFIX_FS "osgearthutil_rampColorFilter_fs_"
#define UNIFORM_PREFIX  "osgearthutil_u_channelRamp_"

//---------------------------------------------------------------------------

MultiBandRampColorFilter::MultiBandRampColorFilter(void)
{
    init();
}

void MultiBandRampColorFilter::init()
{
    // Generate a unique name for this filter's uniform. This is necessary
    // so that each layer can have a unique uniform and entry point.
    m_instanceId = (++s_uniformNameGen) - 1;
    m_colorComponent = new osg::Uniform(osg::Uniform::INT, (osgEarth::Stringify() << UNIFORM_PREFIX << m_instanceId));
    m_colorComponent->set(0);//red
}

void MultiBandRampColorFilter::setColorComponent(int i)
{
    if ( m_colorComponent.valid() )
        m_colorComponent->set(i);
}

int MultiBandRampColorFilter::getColorComponent(void) const
{
    int i = 0;
    if ( m_colorComponent.valid() )
        m_colorComponent->get(i);
    return i;
}

std::string MultiBandRampColorFilter::getEntryPointFunctionName(void) const
{
    return (osgEarth::Stringify() << FUNCTION_PREFIX_FS << m_instanceId);
}


void MultiBandRampColorFilter::install(osg::StateSet* stateSet) const
{
    std::string entryPoint, code;
    installCodeAndUniforms( stateSet, entryPoint, code );
    osgEarth::VirtualProgram* vp = dynamic_cast<osgEarth::VirtualProgram*>(stateSet->getAttribute(VirtualProgram::SA_TYPE));
    installVP( vp, entryPoint, code, false );
}


void MultiBandRampColorFilter::installAsFunction( osg::StateSet* stateSet ) const
{
    std::string entryPoint, code;
    installCodeAndUniforms( stateSet, entryPoint, code, true );
    osgEarth::VirtualProgram* vp = dynamic_cast<osgEarth::VirtualProgram*>(stateSet->getAttribute(VirtualProgram::SA_TYPE));
    installVP( vp, entryPoint, code, true );
}


void MultiBandRampColorFilter::installCodeAndUniforms(osg::StateSet* stateSet, std::string& entryPoint, std::string& code, bool installAsFunction) const
{
    unsigned instanceId = m_instanceId;
    osg::ref_ptr<osg::Uniform> colorComponent;
    if (installAsFunction)
    {
        instanceId = (++s_uniformNameGen) - 1;
        colorComponent = new osg::Uniform(osg::Uniform::INT, (osgEarth::Stringify() << UNIFORM_PREFIX << instanceId)) ;
        colorComponent->set(0);//red
    }
    else
    {
        colorComponent = m_colorComponent.get();
    }

    stateSet->addUniform(colorComponent.get());

    osgEarth::VirtualProgram* vp = dynamic_cast<osgEarth::VirtualProgram*>(stateSet->getAttribute(VirtualProgram::SA_TYPE));
    if (vp)
    {
        // build the local shader (unique per instance). We will
        // use a template with search and replace for this one.
        entryPoint = osgEarth::Stringify() << FUNCTION_PREFIX_FS << instanceId;
        code = s_localShaderSourceFS;
        osgEarth::replaceIn(code, "__UNIFORM_NAME__", colorComponent->getName());
        osgEarth::replaceIn(code, "__ENTRY_POINT_FS__", entryPoint);

        std::string rampCode;
        std::string constDefCode;
        int iConst = 0;
        for (auto ramp : m_ramps)
        {
            // add color to the constant block
            std::string colorConstName = "color"+std::to_string(iConst);
            if (ramp._color.isSet())
            {
                Color color = *ramp._color;
                constDefCode += "const vec4 " + colorConstName + " = vec4("
                        + std::to_string(color.r()) + "f, "
                        + std::to_string(color.g()) + "f, "
                        + std::to_string(color.b()) + "f, "
                        + std::to_string(color.a()) + "f);\n";
                iConst++;
            }

            // first ramp -> discard below a threshold
            if (ramp._from.isSet())
                rampCode += "    if (value < " + std::to_string(*ramp._from) + "f) color = vec4(0.f, 0.f, 0.f, 0.f);\n";

            // other cases
            if (ramp._to.isSet())
                rampCode += "    else if (value < " + std::to_string(*ramp._to) + "f) color = " + colorConstName + ";\n";

            // last ramp
            if (! ramp._from.isSet() && ! ramp._to.isSet())
                rampCode += "    else color = " + colorConstName + ";\n";
        }
        osgEarth::replaceIn(code, "__CONST_DEF__", constDefCode);
        osgEarth::replaceIn(code, "__RAMP_CODE__", rampCode);
    }
}


//---------------------------------------------------------------------------

OSGEARTH_REGISTER_COLORFILTER( multiband_color_ramp, osgEarth::Util::MultiBandRampColorFilter );


MultiBandRampColorFilter::MultiBandRampColorFilter(const Config& conf)
{
    init();

    std::string color = conf.value( "color" );
    if ( color == "red" )        setColorComponent(0);
    else if ( color == "green" ) setColorComponent(1);
    else if ( color == "blue" )  setColorComponent(2);
    else if ( color == "alpha")  setColorComponent(3);

    ConfigSet children = conf.children("ramp");
    for (ConfigSet::const_iterator i = children.begin(); i != children.end(); ++i)
        m_ramps.push_back(RampOptions(*i));
}

Config
MultiBandRampColorFilter::getConfig() const
{
    Config conf("multiband_color_ramp");

    int colorComp = getColorComponent();
    if ( colorComp == 0 )  conf.set( "color", "red" );
    else if ( colorComp == 1 ) conf.set( "color", "green" );
    else if ( colorComp == 2 )  conf.set( "color", "blue" );
    else if ( colorComp == 3 ) conf.set( "color", "alpha" );

    for (auto ramp : m_ramps)
        conf.add(ramp.getConfig());

    return conf;
}
