#version $GLSL_VERSION_STR
$GLSL_DEFAULT_PRECISION_FLOAT

#pragma vp_entryPoint oe_anno_VS
#pragma vp_location   vertex_model


in int oe_anno_attr_type;
out vec2 oe_anno_texcoord;

flat out int oe_anno_type;


void oe_anno_VS(inout vec4 vertex)
{
    oe_anno_texcoord = gl_MultiTexCoord0.st;
    oe_anno_type = oe_anno_attr_type;
}

