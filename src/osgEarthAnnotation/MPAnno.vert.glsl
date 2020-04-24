#version $GLSL_VERSION_STR
$GLSL_DEFAULT_PRECISION_FLOAT

#pragma vp_entryPoint oe_anno_VS
#pragma vp_location   vertex_model

#pragma import_defines(TYPE_CHARACTER_MSDF, TYPE_ICON)


in vec4 oe_anno_attr_info;
in vec4 oe_anno_attr_color2;

uniform float oe_anno_texicon_factor;

out vec2 oe_anno_texcoord;

flat out vec4 oe_anno_info;
flat out vec4 oe_anno_color2;
flat out float oe_anno_stroke_width;
flat out float oe_anno_fill_white_threshold;
flat out float widthBy2;
flat out float heightBy2;
flat out float msdfUnit;

// --------- Highlight management ---------
uniform uint objectid_to_highlight;
uniform float oe_anno_highlightStrokeWidth;
uniform vec4 oe_anno_highlightStrokeColor;
// Stage global containing object id
uint oe_index_objectid;
flat out int selected;


void oe_anno_VS(inout vec4 vertex)
{
    oe_anno_info = oe_anno_attr_info;
    widthBy2 = oe_anno_info.x * 0.5;
    heightBy2 = oe_anno_info.y * 0.5;

    if ( oe_anno_info.z == TYPE_ICON )
        oe_anno_texcoord = gl_MultiTexCoord0.st * oe_anno_texicon_factor;
    else
        oe_anno_texcoord = gl_MultiTexCoord0.st;

    if ( oe_anno_info.z != TYPE_CHARACTER_MSDF && oe_anno_info.z != TYPE_ICON )
        oe_anno_fill_white_threshold = int(oe_anno_info.w);

    // is this item selected ?
    selected = (objectid_to_highlight > 1u && objectid_to_highlight == oe_index_objectid) ? 1 : 0;
    if ( selected == 1 )
    {
        oe_anno_stroke_width = oe_anno_highlightStrokeWidth;
        oe_anno_color2 = oe_anno_highlightStrokeColor;
    }
    else
    {
        oe_anno_stroke_width = (oe_anno_info.w - oe_anno_fill_white_threshold) * 10.;
        oe_anno_color2 = oe_anno_attr_color2;
    }
}

