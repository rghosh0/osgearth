#version $GLSL_VERSION_STR
$GLSL_DEFAULT_PRECISION_FLOAT

#pragma vp_entryPoint oe_anno_FS
#pragma vp_location   fragment_coloring

#pragma import_defines(TYPE_CHARACTER_MSDF, TYPE_ICON, TYPE_BBOX, TYPE_BBOX_ROUNDED, TYPE_BBOX_ONEARROW, TYPE_BBOX_TWOARROWS, TYPE_BBOX_STAIR, TYPE_BBOX_ROUNDED_ORIENTED)


uniform sampler2D oe_anno_icon_tex;
uniform vec4 oe_anno_highlightFillColor;

in vec2 oe_anno_texcoord;

flat in vec4 oe_anno_info;
flat in vec4 oe_anno_color2;
flat in float oe_anno_stroke_width;
flat in float oe_anno_fill_white_threshold;
flat in float widthBy2;
flat in float heightBy2;
flat in int selected;

const float twoRootSquare = 1.4142135;
const float sinusHalfArrow = (3./2.) / sqrt(1. + 9./4.);
const float pixelSmooth = 1.5;


float getHeightBorderDistance()
{
    float dist = abs( (oe_anno_texcoord.t - 0.5) * oe_anno_info.y);
    return heightBy2 - dist;
}

float getWidthBorderDistance()
{
    float dist = abs( (oe_anno_texcoord.s - 0.5) * oe_anno_info.x);
    return widthBy2 - dist;
}

float getBorderDistanceForRoundedBox(float distX, float distY)
{
    // rectangle part
    if ( distX >= heightBy2 )
    {
        return distY;
    }
    // rounded part
    else
    {
        // check what computation is faster...
        //minDist = heightBy2 - length( vec2(heightBy2 - distX, heightBy2 - distY) );
        float r_by_2 = 2. * heightBy2;
        return heightBy2 - sqrt( r_by_2 * heightBy2 - r_by_2 * distX - r_by_2 * distY + distX*distX + distY*distY );
    }
}


void oe_anno_FS(inout vec4 color)
{
    // A CHARACTER using multichannel distance field
    if ( oe_anno_info.z == TYPE_CHARACTER_MSDF )
    {
        float halfMSDFUnit = oe_anno_info.w/2.;
        vec3 sample = texture(oe_anno_icon_tex, oe_anno_texcoord).rgb;
        float sigDist = max(min(sample.r, sample.g), min(max(sample.r, sample.g), sample.b));
        color.a = smoothstep(0.5 - halfMSDFUnit, 0.5 + halfMSDFUnit, sigDist );
    }

    // An ICON
    else if ( oe_anno_info.z == TYPE_ICON )
    {
        color *= texture(oe_anno_icon_tex, oe_anno_texcoord);
    }

    // Symbol STAIR
    else if ( oe_anno_info.z == TYPE_BBOX_STAIR )
    {
        float midBorder = (oe_anno_stroke_width+pixelSmooth) * 0.5;
        vec2 distVec = (oe_anno_texcoord - 0.5) * oe_anno_info.xy;
        if ( distVec.x < -midBorder && distVec.y > 0. || distVec.x > midBorder && distVec.y < 0. )
        {
            discard;
        }
        else
        {
            if ( distVec.y < 0.)
                distVec = -distVec;
            distVec.x += midBorder;
            float distToBorder = min( distVec.x, heightBy2 - distVec.y );
            float alpha = smoothstep(midBorder - pixelSmooth*0.5, midBorder, abs(distToBorder-midBorder));
            color = vec4(oe_anno_color2.rgb, 1. - alpha);
        }
    }


    // Other cases : bounding boxes
    else
    {
        // arbitrary distance to draw plain color in case of non defined shape type
        float minDist = 10.;
        float distX = getWidthBorderDistance();
        float distY = getHeightBorderDistance();

        // Rectange BBOX
        if ( oe_anno_info.z == TYPE_BBOX )
        {
            minDist = min( distX, distY );
        }

        // A ROUNDED BBOX
        else if ( oe_anno_info.z == TYPE_BBOX_ROUNDED )
        {
            minDist = getBorderDistanceForRoundedBox( distX, distY );
        }

        // A BBOX with ARROW(S)
        else
        {
            float arrowX = oe_anno_info.y*1./3.;
            float arrowXstrokeWdith = (oe_anno_stroke_width + pixelSmooth) * 2./3.;

            if ( oe_anno_info.z != TYPE_BBOX_TWOARROWS && ( oe_anno_texcoord.s <= 0.5 || distX > (2.*arrowX) ) )
            {
                if ( oe_anno_info.z == TYPE_BBOX_ONEARROW )
                    minDist = min( distX, distY );
                else if ( oe_anno_info.z == TYPE_BBOX_ROUNDED_ORIENTED )
                    minDist = getBorderDistanceForRoundedBox( distX, distY );
            }

            else if ( oe_anno_info.z == TYPE_BBOX_TWOARROWS && distX >= (2.*arrowX) )
            {
                minDist = distY;
            }

            else
            {
                minDist = min (distY, - sinusHalfArrow * 2./3. * (3./2. * (arrowX+arrowXstrokeWdith-distX) - distY));
            }
        }

        // THEN COLOR ACCORING TO THE DISTANCE

        // case "reverse video" at the end of the bbox
        if ( oe_anno_fill_white_threshold > 0. && (oe_anno_texcoord.s * oe_anno_info.x) > oe_anno_fill_white_threshold )
        {
            color.rgb = vec3(1., 1., 1.);
        }

        // case highlight
        else if ( selected == 1 )
        {
            color = oe_anno_highlightFillColor;
        }

        float alpha = smoothstep(0., pixelSmooth, minDist);

        // no border
        if ( oe_anno_stroke_width == 0. )
        {
            color.a *= alpha;
        }

        // with border
        else
        {
            if ( alpha < 1. )
            {
                color = oe_anno_color2;
                color.a *= alpha;
            }
            else
            {
                color = mix(oe_anno_color2, color, smoothstep(oe_anno_stroke_width, oe_anno_stroke_width+pixelSmooth, minDist));
            }
        }
    }

} 
