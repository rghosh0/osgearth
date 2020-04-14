#ifndef OSGEARTHSYMBOLOGY_BUFFER_PARAMETERS_H
#define OSGEARTHSYMBOLOGY_BUFFER_PARAMETERS_H 1

namespace osgEarth { namespace Symbology 
{
    using namespace osgEarth;
    /** Options for the Geometry::buffer() operation. */
    class BufferParameters
    {
    public:

        enum CapStyle  { CAP_DEFAULT, CAP_SQUARE, CAP_ROUND, CAP_FLAT };
        enum JoinStyle { JOIN_ROUND, JOIN_MITRE, JOIN_BEVEL};
        BufferParameters( CapStyle capStyle =CAP_DEFAULT, JoinStyle joinStyle = JOIN_ROUND, int cornerSegs =0, bool singleSided=false, bool leftSide=false )
            : _capStyle(capStyle), _joinStyle(joinStyle),_cornerSegs(cornerSegs), _singleSided(singleSided), _leftSide(leftSide) { }
        CapStyle  _capStyle;
        JoinStyle _joinStyle;
        int       _cornerSegs; // # of line segment making up a rounded corner
        bool      _singleSided; //Whether or not to do a single sided buffer
        bool      _leftSide;    //If doing a single sided buffer are we buffering to the left?  If false, buffer to the right
    };
}}
#endif // OSGEARTHSYMBOLOGY_BUFFER_PARAMETERS_H
