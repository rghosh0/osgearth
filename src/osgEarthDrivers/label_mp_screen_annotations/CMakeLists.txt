SET(TARGET_SRC MPAnnotationLabelSource.cpp)
#SET(TARGET_H)
SET(TARGET_COMMON_LIBRARIES ${TARGET_COMMON_LIBRARIES} osgEarthFeatures osgEarthSymbology osgEarthAnnotation)
SETUP_PLUGIN(osgearth_label_mp_screen_annotations)

# to install public driver includes:
SET(LIB_NAME label_mp_screen_annotations)
#SET(LIB_PUBLIC_HEADERS AnnotationLabelOptions)
INCLUDE(ModuleInstallOsgEarthDriverIncludes OPTIONAL)
