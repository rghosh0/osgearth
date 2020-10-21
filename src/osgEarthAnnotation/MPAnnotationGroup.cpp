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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <osgEarthAnnotation/MPAnnotationGroup>


using namespace osgEarth::Annotation;


// Highlight an Annotation
void MPAnnotationGroup::setHighlight( long id, bool highlight ) {}
void MPAnnotationGroup::clearHighlight() {}

// change icon color
void MPAnnotationGroup::setIconColor(long id, const Color &color ) {}

// update icon
void MPAnnotationGroup::updateIcon( long id, const std::string &icon ) {}

// change the visibility status
void MPAnnotationGroup::setVisible( long id, bool visible ) {}

