#version $GLSL_VERSION_STR
$GLSL_DEFAULT_PRECISION_FLOAT

#pragma vp_entryPoint oe_anno_FS
#pragma vp_location   fragment_coloring

#pragma import_defines(TYPE_CHARACTER_MSDF, TYPE_ICON, TYPE_BBOX, TYPE_BBOX_ROUNDED, TYPE_BBOX_ONEARROW, TYPE_BBOX_TWOARROWS, TYPE_BBOX_STAIR, TYPE_BBOX_ROUNDED_ORIENTED)


uniform sampler2D oe_anno_font_tex;
uniform sampler2D oe_anno_icon_tex;

in vec2 oe_anno_texcoord;

flat in vec4 oe_anno_info;
flat in vec4 oe_anno_color2;

flat in float oe_anno_stroke_width;
flat in float oe_anno_fill_white_threshold;

flat in float widthBy2;
flat in float heightBy2;


const float twoRootSquare = 1.4142135;

const float sinusHalfArrow = (3./2.) / sqrt(1. + 9./4.);

const float smoothing = 6./32.0;
const float pixelSmooth = 1.5;
const float pixelSmoothBy2 = pixelSmooth * 0.5;

void oe_anno_FS(inout vec4 color)
{
    // A CHARACTER using multichannel distance field
    if ( oe_anno_info.z == TYPE_CHARACTER_MSDF )
    {
        float halfMSDFUnit = oe_anno_info.w/2.;
        vec3 sample = texture(oe_anno_font_tex, oe_anno_texcoord).rgb;
        float sigDist = max(min(sample.r, sample.g), min(max(sample.r, sample.g), sample.b));
        color.a = smoothstep(0.5 - halfMSDFUnit, 0.5 + halfMSDFUnit, sigDist );
    }

    // An ICON
    else if ( oe_anno_info.z == TYPE_ICON )
    {
        color *= texture(oe_anno_icon_tex, oe_anno_texcoord);
    }

    // Rectange BBOX
    else if ( oe_anno_info.z == TYPE_BBOX )
    {
        // no border (filled bbox)
        //if ( oe_anno_info.w == 0. )
        if ( oe_anno_stroke_width == 0. )
        {
            vec2 distVec = abs( (oe_anno_texcoord - 0.5) * oe_anno_info.xy);
            float distToBorder = min( widthBy2 - distVec.x, heightBy2 - distVec.y );
            float alpha = smoothstep(0., pixelSmooth, distToBorder);
            color.a *= alpha;
        }
        // with border
        else
        {
            vec2 distVec = abs( (oe_anno_texcoord - 0.5) * oe_anno_info.xy);
            float distToBorder = min( widthBy2 - distVec.x, heightBy2 - distVec.y );
            float alpha = smoothstep(0., pixelSmooth, distToBorder);
            if ( alpha < 1. )
            {
                color = oe_anno_color2;
                color.a *= alpha;
            }
            else
            {
                color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
            }
        }
    }

    // A ROUNDED BBOX
    else if ( oe_anno_info.z == TYPE_BBOX_ROUNDED )
    {
        vec2 distVec = (oe_anno_texcoord - 0.5) * oe_anno_info.xy;
        vec2 distVecAbs = abs( distVec );
        if ( oe_anno_fill_white_threshold > 0. && distVec.x + widthBy2 > oe_anno_fill_white_threshold )
            color.rgb = vec3(1., 1., 1.);

        // no border (filled rounded bbox)
        if ( oe_anno_stroke_width == 0. )
        {
            float distX = widthBy2 - distVecAbs.x;
            if ( distX >= heightBy2 )
            {
                float alpha = smoothstep(heightBy2-pixelSmooth, heightBy2, distVecAbs.y);
                color.a *= 1. - alpha;
            }
            else
            {
                vec2 center = vec2(widthBy2 - heightBy2, 0.);
                float alpha = smoothstep(heightBy2-pixelSmooth, heightBy2, length(distVecAbs - center));
                color.a *= 1. - alpha;
            }
        }
        // with border
        else
        {
            float distX = widthBy2 - distVecAbs.x;
            if ( distX >= heightBy2 )
            {
                float distToBorder = heightBy2 - distVecAbs.y;
                float alpha = smoothstep(0., pixelSmooth, distToBorder);
                if ( alpha < 1. )
                {
                    color = oe_anno_color2;
                    color.a *= alpha;
                }
                else
                {
                    color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
                }
            }
            else
            {
                vec2 center = vec2(widthBy2 - heightBy2, 0.);
                float distToBorder = heightBy2 - length(distVecAbs - center);
                float alpha = smoothstep(0., pixelSmooth, distToBorder);
                if ( alpha < 1. )
                {
                    color = oe_anno_color2;
                    color.a *= alpha;
                }
                else
                {
                    color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
                }
            }
        }
    }

    // A BBOX with ARROWS
    else if ( oe_anno_info.z == TYPE_BBOX_ONEARROW || oe_anno_info.z == TYPE_BBOX_TWOARROWS )
    {
        vec2 textToScreen = oe_anno_info.z == TYPE_BBOX_ONEARROW ?
                    oe_anno_texcoord * oe_anno_info.xy : (abs(oe_anno_texcoord-0.5)+0.5)*oe_anno_info.xy;
        float startArrowX = oe_anno_info.x -  oe_anno_info.y/3.;

        // no border (filled bbox)
        if ( oe_anno_stroke_width == 0. )
        {
            if ( textToScreen.x <= startArrowX )
            {
                float distToBorder = min( textToScreen.x, heightBy2 - abs(textToScreen.y - heightBy2) );
                float alpha = smoothstep(0., pixelSmooth, distToBorder);
                color.a *= alpha;
            }
            else
            {
                textToScreen = vec2( oe_anno_info.x - textToScreen.x, abs(textToScreen.y - heightBy2) );
                if (textToScreen.x <= textToScreen.y*2./3.)
                {
                    discard;
                }
                else
                {
                    float distToBorder = sinusHalfArrow * 2./3. * (3./2. * textToScreen.x - textToScreen.y);
                    float alpha = smoothstep(0., pixelSmooth, distToBorder);
                    color.a *= alpha;
                }
            }
        }
        // with border
        else
        {
            float fullBorderSize = oe_anno_stroke_width + pixelSmooth;
            float yAbs = abs(textToScreen.y - heightBy2);
            float yBorderDist = heightBy2 - yAbs;
            if ( textToScreen.x <= startArrowX - fullBorderSize * 2./3.
                 || textToScreen.x <= startArrowX && yBorderDist <= (startArrowX - textToScreen.x) * 3./2. )
            {
                float distToBorder = min( textToScreen.x, yBorderDist );
                float alpha = smoothstep(0., pixelSmooth, distToBorder);
                if ( alpha < 1. )
                {
                    color = oe_anno_color2;
                    color.a *= alpha;
                }
                else
                {
                    color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
                }
            }
            else
            {
                textToScreen = vec2( oe_anno_info.x - textToScreen.x, yAbs );
                if (textToScreen.x <= textToScreen.y*2./3.)
                {
                    discard;
                }
                else
                {
                    float distToBorder = sinusHalfArrow * 2./3. * (3./2. * textToScreen.x - textToScreen.y);
                    float alpha = smoothstep(0., pixelSmooth, distToBorder);
                    if ( alpha < 1. )
                    {
                        color = oe_anno_color2;
                        color.a *= alpha;
                    }
                    else
                    {
                        color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, fullBorderSize, distToBorder));
                    }
                }
            }
        }
    }

    // Rectange STAIR
    else if ( oe_anno_info.z == TYPE_BBOX_STAIR )
    {
        float midBorder = (oe_anno_stroke_width+pixelSmooth) * 0.5;
        vec2 distVec = (oe_anno_texcoord - 0.5) * oe_anno_info.xy;
        if ( distVec.x < -midBorder && distVec.y > 0 || distVec.x > midBorder && distVec.y < 0 )
        {
            discard;
        }
        else
        {
            if ( distVec.y < 0)
                distVec = -distVec;
            distVec.x += midBorder;
            float distToBorder = min( distVec.x, heightBy2 - distVec.y );
            float alpha = smoothstep(midBorder-pixelSmoothBy2, midBorder, abs(distToBorder-midBorder));
            color = vec4(oe_anno_color2.rgb, 1. - alpha);
        }
    }

    // BBOX rounded on left and arrow on right
    else if ( oe_anno_info.z == TYPE_BBOX_ROUNDED_ORIENTED )
    {
        vec2 distVec = (oe_anno_texcoord - 0.5) * oe_anno_info.xy;
        vec2 distVecAbs = abs( distVec );

        // part rounded
        if ( distVec.x < 0. )
        {
            // no border (filled)
            if ( oe_anno_stroke_width == 0. )
            {
                float distX = widthBy2 - distVecAbs.x;
                if ( distX >= heightBy2 )
                {
                    float alpha = smoothstep(heightBy2-pixelSmooth, heightBy2, distVecAbs.y);
                    color.a *= 1. - alpha;
                }
                else
                {
                    vec2 center = vec2(widthBy2 - heightBy2, 0.);
                    float alpha = smoothstep(heightBy2-pixelSmooth, heightBy2, length(distVecAbs - center));
                    color.a *= 1. - alpha;
                }
            }
            // with border
            else
            {
                float distX = widthBy2 - distVecAbs.x;
                if ( distX >= heightBy2 )
                {
                    float distToBorder = heightBy2 - distVecAbs.y;
                    float alpha = smoothstep(0., pixelSmooth, distToBorder);
                    if ( alpha < 1. )
                    {
                        color = oe_anno_color2;
                        color.a *= alpha;
                    }
                    else
                    {
                        color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
                    }
                }
                else
                {
                    vec2 center = vec2(widthBy2 - heightBy2, 0.);
                    float distToBorder = heightBy2 - length(distVecAbs - center);
                    float alpha = smoothstep(0., pixelSmooth, distToBorder);
                    if ( alpha < 1. )
                    {
                        color = oe_anno_color2;
                        color.a *= alpha;
                    }
                    else
                    {
                        color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
                    }
                }
            }
        }

        // part arrow
        else
        {
            vec2 textToScreen = oe_anno_texcoord * oe_anno_info.xy ;
            float startArrowX = oe_anno_info.x -  oe_anno_info.y/3.;

            // no border (filled bbox)
            if ( oe_anno_stroke_width == 0. )
            {
                if ( textToScreen.x <= startArrowX )
                {
                    float distToBorder = min( textToScreen.x, heightBy2 - abs(textToScreen.y - heightBy2) );
                    float alpha = smoothstep(0., pixelSmooth, distToBorder);
                    color.a *= alpha;
                }
                else
                {
                    textToScreen = vec2( oe_anno_info.x - textToScreen.x, abs(textToScreen.y - heightBy2) );
                    if (textToScreen.x <= textToScreen.y*2./3.)
                    {
                        discard;
                    }
                    else
                    {
                        float distToBorder = sinusHalfArrow * 2./3. * (3./2. * textToScreen.x - textToScreen.y);
                        float alpha = smoothstep(0., pixelSmooth, distToBorder);
                        color.a *= alpha;
                    }
                }
            }
            // with border
            else
            {
                float fullBorderSize = oe_anno_stroke_width + pixelSmooth;
                float yAbs = abs(textToScreen.y - heightBy2);
                float yBorderDist = heightBy2 - yAbs;
                if ( textToScreen.x <= startArrowX - fullBorderSize * 2./3.
                     || textToScreen.x <= startArrowX && yBorderDist <= (startArrowX - textToScreen.x) * 3./2. )
                {
                    float distToBorder = min( textToScreen.x, yBorderDist );
                    float alpha = smoothstep(0., pixelSmooth, distToBorder);
                    if ( alpha < 1. )
                    {
                        color = oe_anno_color2;
                        color.a *= alpha;
                    }
                    else
                    {
                        color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, distToBorder));
                    }
                }
                else
                {
                    textToScreen = vec2( oe_anno_info.x - textToScreen.x, yAbs );
                    if (textToScreen.x <= textToScreen.y*2./3.)
                    {
                        discard;
                    }
                    else
                    {
                        float distToBorder = sinusHalfArrow * 2./3. * (3./2. * textToScreen.x - textToScreen.y);
                        float alpha = smoothstep(0., pixelSmooth, distToBorder);
                        if ( alpha < 1. )
                        {
                            color = oe_anno_color2;
                            color.a *= alpha;
                        }
                        else
                        {
                            color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, fullBorderSize, distToBorder));
                        }
                    }
                }
            }
        }
    }

    // Undefined shape, draw a red box
    else
    {
        color = vec4(1., 0., 0., 1.);
    }
} 
