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
    static const char* s_localShaderSourceFS =
        "#version " GLSL_VERSION_STR "\n"
        GLSL_DEFAULT_PRECISION_FLOAT "\n"

        "__VAR_DEF__"

        "void __ENTRY_POINT_FS__(inout vec4 color)\n"
        "{\n"
        "    __BODY_CODE__"
        "} \n";
    
    static const char* s_bicubicFcts =
            
           " float CubicHermite (float A, float B, float C, float D, float t)\n"
            "{"
                "float t2 = t*t;\n"
                "float t3 = t*t*t;\n"
                "float a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;\n"
                "float b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;\n"
                "float c = -A/2.0 + C/2.0;\n"
               " float d = B;\n"
                
               " return a*t3 + b*t2 + c*t + d;\n"
           " }\n"
            
"            float inigo(sampler2D sampler, vec2 uv,int col){"
            "vec2 textureResolution  = textureSize(sampler, 0);\n"
            "uv = uv*textureResolution + 0.5;\n"
            "vec2 iuv = floor( uv );\n"
            "vec2 fuv = fract( uv );\n"
            "uv=iuv + clamp(smoothstep(0.0f,1.0f,fuv)*2.0f-1.0f,0.5f,0.5f)+0.5f;\n"
            ""
            //"uv = iuv + fuv*fuv*(3.0-2.0*fuv);\n" // fuv*fuv*fuv*(fuv*(fuv*6.0-15.0)+10.0);;
            "uv = (uv - 0.5)/textureResolution;\n"
            "return max(texture( sampler, uv )[col],0.1);\n}"
            
            
             
            "vec2 nearestcoordSample(vec2 P,vec2 textureSize){\n"
                "vec2 pixel = P * textureSize;\n"      
                "vec2 onePixel=1/textureSize;\n"
                "vec2 frac = fract(pixel);\n"
                "pixel = (floor(pixel) / textureSize);\n"
                "return (pixel + vec2(onePixel/2.0));\n"
                "}\n"
         
            "float circle(sampler2D sampler, vec2 uv,int col){"
            "   float thres =0.25;"
            "   vec2 textureResolution  = textureSize(sampler, 0);\n"
            "   vec2 c_onePixel=1.0/textureResolution;\n"
            "   vec2 uv2= (fract(uv*textureResolution)-c_onePixel/2.0)/textureResolution -c_onePixel/2.0;\n" 
            "   float an= atan(abs(uv2.y),abs(uv2.x));\n"
            "   float l1=min(1.0/sin(an),1.0/cos(an));\n"              
            "   float luv2=length(uv2*2.*textureResolution);\n"
            "   float d1=(luv2-thres)/(1.0-thres);\n"
            "   d1=(luv2>thres)?d1:0.0;\n"
            "   d1=min(1.0,d1);\n"
            "   vec2 nuv=nearestcoordSample(uv,textureResolution);\n"
            "   return texture( sampler, nuv+uv2*d1 )[col];\n"
            "}"
            
            
           " float SampleBicubic(sampler2D sampler, vec2 adjustedFragCoord,int col)"
           " {"
           "vec2 iResolution = textureSize(sampler, 0);\n"
             " adjustedFragCoord*= iResolution; \n"
               " adjustedFragCoord-= 0.5; \n"
               " vec2 fragFract = fract(adjustedFragCoord);\n"
            
                
                // get the 16 data points
                "float dataNN = max(texture(sampler, (adjustedFragCoord + vec2(-1.0, -1.0)) / iResolution.xy)[col],0.001);\n"
                "float data0N = max(texture(sampler, (adjustedFragCoord + vec2( 0.0, -1.0)) / iResolution.xy)[col],0.001);\n"
                "float data1N = max(texture(sampler, (adjustedFragCoord + vec2( 1.0, -1.0)) / iResolution.xy)[col],0.001);\n"
                "float data2N = max(texture(sampler, (adjustedFragCoord + vec2( 2.0, -1.0)) / iResolution.xy)[col],0.001);\n"
                
                "float dataN0 = max(texture(sampler, (adjustedFragCoord + vec2(-1.0,  0.0)) / iResolution.xy)[col],0.001);\n"
                "float data00 = max(texture(sampler, (adjustedFragCoord + vec2( 0.0,  0.0)) / iResolution.xy)[col],0.001);\n"
                "float data10 = max(texture(sampler, (adjustedFragCoord + vec2( 1.0,  0.0)) / iResolution.xy)[col],0.001);\n"
                "float data20 = max(texture(sampler, (adjustedFragCoord + vec2( 2.0,  0.0)) / iResolution.xy)[col],0.001);\n"   
                
                "float dataN1 = max(texture(sampler, (adjustedFragCoord + vec2(-1.0,  1.0)) / iResolution.xy)[col],0.001);\n"
                "float data01 = max(texture(sampler, (adjustedFragCoord + vec2( 0.0,  1.0)) / iResolution.xy)[col],0.001);\n"
                "float data11 = max(texture(sampler, (adjustedFragCoord + vec2( 1.0,  1.0)) / iResolution.xy)[col],0.001);\n"
                "float data21 = max(texture(sampler, (adjustedFragCoord + vec2( 2.0,  1.0)) / iResolution.xy)[col],0.001);\n"     
                
                "float dataN2 = max(texture(sampler, (adjustedFragCoord + vec2(-1.0,  2.0)) / iResolution.xy)[col],0.001);\n"
                "float data02 = max(texture(sampler, (adjustedFragCoord + vec2( 0.0,  2.0)) / iResolution.xy)[col],0.001);\n"
                "float data12 = max(texture(sampler, (adjustedFragCoord + vec2( 1.0,  2.0)) / iResolution.xy)[col],0.001);\n"
                "float data22 = max(texture(sampler, (adjustedFragCoord + vec2( 2.0,  2.0)) / iResolution.xy)[col],0.001);\n "   
                
                // bicubic interpolate
                "float dataxN = CubicHermite(dataNN, data0N, data1N, data2N, fragFract.x);\n"
                "float datax0 = CubicHermite(dataN0, data00, data10, data20, fragFract.x);\n"
                "float datax1 = CubicHermite(dataN1, data01, data11, data21, fragFract.x);\n"
                "float datax2 = CubicHermite(dataN2, data02, data12, data22, fragFract.x);\n"
                "return CubicHermite(dataxN, datax0, datax1, datax2, fragFract.y);\n"
            "}\n"
            
            
            "// from http://www.java-gaming.org/index.php?topic=35123.0\n"
                                       "vec4 cubic(float v){\n"
                                       "    vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;\n"
                                       "    vec4 s = n * n * n;\n"
                                       "    float x = s.x;\n"
                                       "    float y = s.y - 4.0 * s.x;\n"
                                       "    float z = s.z - 4.0 * s.y + 6.0 * s.x;\n"
                                       "    float w = 6.0 - x - y - z;\n"
                                       "    return vec4(x, y, z, w) * (1.0/6.0);\n"
                                       "}\n"
                                       "\n"
                                        "float biLerp(float a, float b, float c, float d, float s, float t)\n"
                                        "{\n"
                                          "  float x = mix(a, b, t);\n"
                                          "  float y = mix(c, d, t);\n"
                                         " return mix(x, y, s);\n"
                                       " }\n"
            
            "float biSmooth(float a, float b, float c, float d, float s, float t)\n"
            "{\n"
            "  float x = mix(a, b, smoothstep(0.0f,1.0f,t));\n"
            "  float y = mix(c, d, smoothstep(0.0f,1.0f,t));\n"
           " return mix(x, y, smoothstep(0.0f,1.0f,s));\n"
           " }\n"
            "float biSmoothC(float a, float b, float c, float d, float s, float t)\n"
            "{\n"
            "  float x = mix(a, b, smoothstep(0.25f,0.75f,t));\n"
            "  float y = mix(c, d, smoothstep(0.25f,0.75f,t));\n"
           " return mix(x, y, smoothstep(0.25f,0.75f,s));\n"
           " }\n"
            
            "float biSmoothC2(float a, float b, float c, float d, float s, float t)\n"
            "{\n"
            "if(a==b && a==c && a==d)return a;\n"
            "  float x = mix(a, b, smoothstep(0.25f,0.75f,t));\n"
            "  float y = mix(c, d, smoothstep(0.25f,0.75f,t));\n"
           " return mix(x, y, smoothstep(0.25f,0.75f,s));\n"
           " }\n"
            
            "float biSmoothW(float a, float b, float c, float d, float s, float t)\n"
            "{\n"
            
            
            "  float x = mix(a, b, smoothstep((a>b)?0.0f:0.5f,(a>b)?0.5f:1.0f,t));\n"
            "  float y = mix(c, d, smoothstep((c>d)?0.0f:0.5f,(c>d)?0.5f:1.0f,t));\n"
           " return mix(x, y, smoothstep((x>y)?0.0f:0.5f,(x>y)?0.5f:1.0f,s));\n"
           " }\n"
            
                                       "float textureBicubic(sampler2D sampler, vec2 texCoords,int col){\n"
                                       "\n"
                                       "   vec2 texSize = textureSize(sampler, 0);\n"
                                       "   vec2 invTexSize = 1.0 / texSize;\n"
                                       "\n"
                                       "   texCoords = texCoords * texSize - 0.5;\n"
                                       "\n"
                                       "\n"
                                       "    vec2 fxy = fract(texCoords);\n"
                                       "    texCoords -= fxy;\n"
                                       "\n"
                                       "    vec4 xcubic = cubic(fxy.x);\n"
                                       "    vec4 ycubic = cubic(fxy.y);\n"
                                       "\n"
                                       "    vec4 c = texCoords.xxyy + vec2 (-0.5, +1.5).xyxy;\n"
                                       "\n"
                                       "    vec4 s = vec4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);\n"
                                       "    vec4 offset = c + vec4 (xcubic.yw, ycubic.yw) / s;\n"
                                       "\n"
                                       "    offset *= invTexSize.xxyy;\n"
                                       "\n"
                                       "    float sample0 = max(texture(sampler, offset.xz)[col],0.001);\n"
                                       "    float sample1 = max(texture(sampler, offset.yz)[col],0.001);\n"
                                       "    float sample2 = max(texture(sampler, offset.xw)[col],0.001);\n"
                                       "    float sample3 = max(texture(sampler, offset.yw)[col],0.001);\n"
                                       "\n"
                                       "    float sx = s.x / (s.x + s.y);\n"
                                       "    float sy = s.z / (s.z + s.w);\n"
                                       "\n"
                                       "    return mix(\n"
                                       "       mix(sample3, sample2, sx), mix(sample1, sample0, sx)\n"
                                       "    , sy);\n"
                                       "}\n"
            
            
                ;
}

//---------------------------------------------------------------------------

#define FUNCTION_PREFIX_FS "osgearthutil_rampColorFilter_fs_"
#define UNIFORM_PREFIX  "osgearthutil_u_channelRamp_"

static OpenThreads::Atomic s_uniformNameGen;
const std::string MultiBandRampColorFilter::uniform_name = UNIFORM_PREFIX;


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


osg::Uniform* MultiBandRampColorFilter::installAsFunction(osg::StateSet* stateSet, bool uniqueUniform) const
{
    std::string entryPoint, code;
    osg::Uniform* uniform = installCodeAndUniforms( stateSet, entryPoint, code, true, uniqueUniform );
    osgEarth::VirtualProgram* vp = dynamic_cast<osgEarth::VirtualProgram*>(stateSet->getAttribute(VirtualProgram::SA_TYPE));
    installVP( vp, entryPoint, code, true );
    return uniform;
}

void MultiBandRampColorFilter::mergeInShader(std::string& shaderCode, const std::string& uniformName, const std::string& extractValueCode) const
{
    

    
    std::string extract = extractValueCode.empty() ? "color" : extractValueCode;
    std::string rampCode="highp float value;\n";
            rampCode += "    value = max(" + extract + "[" + uniformName + "],0.001);\n";
    
    
  
    
    std::string varDefCode = "uniform int " + uniformName + ";\n";

   
    varDefCode+=s_bicubicFcts;
    
    
    
 rampCode="float value=circle(image_tex, imageBinding_texcoord," + uniformName + ");\n";
 rampCode+="value=max(0.0f,value);\n";
 
 
       /* rampCode+="   vec2 texSize =textureSize(image_tex, 0);\n";
        rampCode+="   vec2 invTexSize = 1.0 / texSize;\n";
        rampCode+="   vec2 texCoords = imageBinding_texcoord * texSize -0.5;\n";
        
        rampCode+="    vec2 fxy = fract(texCoords);\n";
        rampCode+="    texCoords -= fxy;\n";
        rampCode+="vec4 offset=texCoords.xxyy+ vec2 (0.5, +1.5).xyxy;\n";
        rampCode+= "    offset *= invTexSize.xxyy;\n";
        
     
        rampCode+="float sample0 = texture(image_tex, offset.xz)[" + uniformName + "];\n";   //0.0
        rampCode+= "    float sample1 = texture(image_tex,offset.xw)[" + uniformName + "];\n";//0,1
        rampCode+=  "    float sample2 = texture(image_tex,offset.yz)[" + uniformName + "];\n";//1,0
        rampCode+=  "    float sample3 = texture(image_tex, offset.yw)[" + uniformName + "];\n";//1,1
        
        //rampCode+=" value=biLerp(max(sample0,0.001),max(sample1,0.001),max(sample2,0.001),max(sample3,0.001),fxy.x,fxy.y);\n";
           rampCode+=" value=biSmoothC(max(sample0,0.001),max(sample1,0.001),max(sample2,0.001),max(sample3,0.001),fxy.x,fxy.y);\n";
     */
        
       // rampCode+="float ori = texture(image_tex, imageBinding_texcoord)[" + uniformName + "];\n";
 
    int iConst = 0;
    for (auto ramp : m_ramps)
    {
        // add color to the constant block
        std::string colorConstName = "color"+std::to_string(iConst);
        if (ramp._color.isSet())
        {
            Color color = *ramp._color;
            varDefCode += "const vec4 " + colorConstName + " = vec4("
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
            rampCode += "    else if (value <= " + std::to_string(*ramp._to) + "f) color = " + colorConstName + ";\n";

        // last ramp
        if (! ramp._from.isSet() && ! ramp._to.isSet())
            rampCode += "    else color = " + colorConstName + ";\n";
    }

    
    //rampCode+="color.rgba=vec4(value/10.0f,0.0f,1.0-value/10.0f,1.0f);";
    
    osgEarth::replaceIn(shaderCode, "__VAR_DEF__", varDefCode);
    osgEarth::replaceIn(shaderCode, "__BODY_CODE__", rampCode);
}

osg::Uniform* MultiBandRampColorFilter::installCodeAndUniforms(osg::StateSet* stateSet, std::string& entryPoint, std::string& code,
                                                               bool installAsFunction, bool uniqueUniform) const
{
    unsigned instanceId = m_instanceId;
    osg::ref_ptr<osg::Uniform> colorComponent;
    if (installAsFunction)
    {
        if (! uniqueUniform)
        {
            colorComponent = new osg::Uniform(osg::Uniform::INT, (osgEarth::Stringify() << UNIFORM_PREFIX));
            colorComponent->set(0);//red
        }
        else
        {
            instanceId = (++s_uniformNameGen) - 1;
            colorComponent = new osg::Uniform(osg::Uniform::INT, (osgEarth::Stringify() << UNIFORM_PREFIX << instanceId));
            colorComponent->set(0);//red
        }
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

        mergeInShader(code, colorComponent->getName());
    }

    return colorComponent.get();
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
