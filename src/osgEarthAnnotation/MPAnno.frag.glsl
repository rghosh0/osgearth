#version $GLSL_VERSION_STR

#pragma vp_entryPoint oe_anno_FS
#pragma vp_location   fragment_coloring

#pragma import_defines(TYPE_CHARACTER_MSDF, TYPE_ICON, TYPE_BBOX)


uniform sampler2D oe_anno_font_tex;

in vec2 oe_anno_texcoord;

flat in int oe_anno_type;

void oe_anno_FS(inout vec4 color)
{
    if ( oe_anno_type == TYPE_CHARACTER_MSDF )
    {
        vec2 msdfUnit = vec2(2., 2.)/vec2(32., 32.);
        vec3 sample = texture(oe_anno_font_tex, oe_anno_texcoord).rgb;
        float sigDist = max(min(sample.r, sample.g), min(max(sample.r, sample.g), sample.b)) - 0.5;
        sigDist *= dot(msdfUnit, 0.5/fwidth(oe_anno_texcoord));
        //float opacity = clamp(sigDist + 0.5, 0.0, 1.0);
        float opacity = clamp(sigDist + 0.5, 0.0, 1.0);
        color.a = opacity;
    }
    else if ( oe_anno_type == TYPE_ICON )
    {
        float dist = 1. - length (oe_anno_texcoord - vec2(0.5, 0.5));
        color.rgb = dist * color.rgb;
    }
    else if ( oe_anno_type == TYPE_BBOX )
        color = vec4(0., 0., 1., 1.);
    else
        color = vec4(0.5, 0.5, 0.5, 1.);
} 
