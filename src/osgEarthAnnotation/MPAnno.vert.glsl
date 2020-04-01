#version $GLSL_VERSION_STR
$GLSL_DEFAULT_PRECISION_FLOAT

#pragma vp_entryPoint oe_anno_VS
#pragma vp_location   vertex_model


in vec4 oe_anno_attr_info;
out vec2 oe_anno_texcoord;

flat out vec4 oe_anno_info;


void oe_anno_VS(inout vec4 vertex)
{
    oe_anno_texcoord = gl_MultiTexCoord0.st;
    oe_anno_info = oe_anno_attr_info;
}

