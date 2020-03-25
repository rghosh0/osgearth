IF   (DYNAMIC_OSGEARTH)
    ADD_DEFINITIONS(-DOSGEARTHANNO_LIBRARY)
ELSE (DYNAMIC_OSGEARTH)
    ADD_DEFINITIONS(-DOSGEARTHANNO_LIBRARY_STATIC)
ENDIF(DYNAMIC_OSGEARTH)

set(LIB_NAME osgEarthAnnotation)

set(HEADER_PATH ${OSGEARTH_SOURCE_DIR}/include/${LIB_NAME})

set(LIB_PUBLIC_HEADERS
    AnnotationSettings
    AnnotationEditing
    AnnotationData
    AnnotationLayer
    AnnotationNode
    AnnotationRegistry
    AnnotationUtils
    BboxDrawable
    AttachedLabelNode
    CircleNode
    Common
    Draggers
    EllipseNode
    Export
    FeatureEditing
    FeatureNode
    GeoPositionNode
    GeoPositionNodeAutoScaler
    LocalGeometryNode
    ImageOverlay
    ImageOverlayEditor
    LabelNode
    ModelNode
    PlaceNode
    RectangleNode
    TrackNode
    Shaders

    MPStateSetFontAltas
    MPAnnotationDrawable
    MPAnnotationGroup
)

set(TARGET_GLSL
    MPAnno.vert.glsl
    MPAnno.frag.glsl )

set(SHADERS_CPP "${CMAKE_CURRENT_BINARY_DIR}/AutoGenShaders.cpp")

set(TARGET_IN
    Shaders.cpp.in)

configure_shaders(
    Shaders.cpp.in
    ${SHADERS_CPP}
    ${TARGET_GLSL} )

set(LIB_COMMON_FILES
    AnnotationEditing.cpp
    AnnotationSettings.cpp
    AnnotationData.cpp
    AnnotationLayer.cpp
    AnnotationNode.cpp
    AnnotationRegistry.cpp
    AnnotationUtils.cpp
    BboxDrawable.cpp
    AttachedLabelNode.cpp
    CircleNode.cpp
    Draggers.cpp
    EllipseNode.cpp
    FeatureEditing.cpp
    FeatureNode.cpp
    GeoPositionNode.cpp
    GeoPositionNodeAutoScaler.cpp
    LocalGeometryNode.cpp
    ImageOverlay.cpp
    ImageOverlayEditor.cpp
    LabelNode.cpp
    RectangleNode.cpp
    ModelNode.cpp
    PlaceNode.cpp
    TrackNode.cpp

    MPStateSetFontAltas.cpp
    MPAnnotationDrawable.cpp
    MPAnnotationGroup.cpp
    ${SHADERS_CPP}
)

IF (GEOS_FOUND)
    ADD_DEFINITIONS(-DOSGEARTH_HAVE_GEOS)
ENDIF(GEOS_FOUND)

ADD_LIBRARY( ${LIB_NAME} ${OSGEARTH_USER_DEFINED_DYNAMIC_OR_STATIC}
    ${LIB_PUBLIC_HEADERS}
    ${LIB_COMMON_FILES}
    ${TARGET_GLSL}
    ${TARGET_IN}
)



INCLUDE_DIRECTORIES(${OSG_INCLUDE_DIR} ${OSGEARTH_SOURCE_DIR} ${GDAL_INCLUDE_DIR})

IF (WIN32)
  LINK_EXTERNAL(${LIB_NAME} ${TARGET_EXTERNAL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${MATH_LIBRARY})
ELSE(WIN32)
  LINK_EXTERNAL(${LIB_NAME} ${TARGET_EXTERNAL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${MATH_LIBRARY})
ENDIF(WIN32)

LINK_INTERNAL(${LIB_NAME}
    osgEarth
    osgEarthFeatures
    osgEarthSymbology
)

LINK_WITH_VARIABLES(${LIB_NAME} OSG_LIBRARY OSGUTIL_LIBRARY OSGSIM_LIBRARY OSGTERRAIN_LIBRARY OSGDB_LIBRARY OSGFX_LIBRARY OSGMANIPULATOR_LIBRARY OSGVIEWER_LIBRARY OSGTEXT_LIBRARY OSGGA_LIBRARY OPENTHREADS_LIBRARY GDAL_LIBRARY)
LINK_CORELIB_DEFAULT(${LIB_NAME} ${CMAKE_THREAD_LIBS_INIT} ${MATH_LIBRARY})

INCLUDE(ModuleInstall OPTIONAL)
