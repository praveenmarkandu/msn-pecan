/**
 * Copyright (C) 2008 Felipe Contreras.
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#ifndef MSN_OBJECT_H
#define MSN_OBJECT_H

#include <glib.h>

typedef struct MsnObject MsnObject;

#include "io/pecan_buffer.h"

typedef enum
{
    MSN_OBJECT_UNKNOWN = -1, /**< Unknown object */
    MSN_OBJECT_RESERVED1 = 1, /**< Reserved */
    MSN_OBJECT_EMOTICON = 2, /**< Custom Emoticon */
    MSN_OBJECT_USERTILE = 3, /**< UserTile (buddy icon) */
    MSN_OBJECT_RESERVED2 = 4, /**< Reserved */
    MSN_OBJECT_BACKGROUND = 5  /**< Background */
} MsnObjectType;

/**
 * Creates a MsnObject structure.
 *
 * @return A new MsnObject structure.
 */
MsnObject *msn_object_new(void);

/**
 * Creates a MsnObject structure from a string.
 *
 * @param str The string.
 *
 * @return The new MsnObject structure.
 */
MsnObject *msn_object_new_from_string(const gchar *str);

/**
 * Destroys an MsnObject structure.
 *
 * @param obj The object structure.
 */
void msn_object_destroy(MsnObject *obj);

/**
 * Outputs a string representation of an MsnObject.
 *
 * @param obj The object.
 *
 * @return The string representation. This must be freed.
 */
gchar *msn_object_to_string(const MsnObject *obj);

/**
 * Sets the creator field in a MsnObject.
 *
 * @param creator The creator value.
 */
void msn_object_set_creator(MsnObject *obj, const gchar *creator);

/**
 * Sets the size field in a MsnObject.
 *
 * @param size The size value.
 */
void msn_object_set_size(MsnObject *obj, gint size);

/**
 * Sets the type field in a MsnObject.
 *
 * @param type The type value.
 */
void msn_object_set_type(MsnObject *obj, MsnObjectType type);

/**
 * Sets the location field in a MsnObject.
 *
 * @param location The location value.
 */
void msn_object_set_location(MsnObject *obj, const gchar *location);

/**
 * Sets the friendly name field in a MsnObject.
 *
 * @param friendly The friendly name value.
 */
void msn_object_set_friendly(MsnObject *obj, const gchar *friendly);

/**
 * Sets the SHA1D field in a MsnObject.
 *
 * @param sha1d The sha1d value.
 */
void msn_object_set_sha1d(MsnObject *obj, const gchar *sha1d);

/**
 * Sets the SHA1C field in a MsnObject.
 *
 * @param sha1c The sha1c value.
 */
void msn_object_set_sha1c(MsnObject *obj, const gchar *sha1c);

/**
 * Associates an image with an MsnObject.
 *
 * @param obj The object.
 * @param buffer The image to associate.
 */
void msn_object_set_image (MsnObject *obj, PecanBuffer *buffer);

/**
 * Returns a MsnObject's creator value.
 *
 * @param obj The object.
 *
 * @return The creator value.
 */
const gchar *msn_object_get_creator(const MsnObject *obj);

/**
 * Returns a MsnObject's size value.
 *
 * @param obj The object.
 *
 * @return The size value.
 */
gint msn_object_get_size(const MsnObject *obj);

/**
 * Returns a MsnObject's type.
 *
 * @param obj The object.
 *
 * @return The object type.
 */
MsnObjectType msn_object_get_type(const MsnObject *obj);

/**
 * Returns a MsnObject's location value.
 *
 * @param obj The object.
 *
 * @return The location value.
 */
const gchar *msn_object_get_location(const MsnObject *obj);

/**
 * Returns a MsnObject's friendly name value.
 *
 * @param obj The object.
 *
 * @return The friendly name value.
 */
const gchar *msn_object_get_friendly(const MsnObject *obj);

/**
 * Returns a MsnObject's SHA1D value.
 *
 * @param obj The object.
 *
 * @return The SHA1D value.
 */
const gchar *msn_object_get_sha1d(const MsnObject *obj);

/**
 * Returns a MsnObject's SHA1C value.
 *
 * @param obj The object.
 *
 * @return The SHA1C value.
 */
const gchar *msn_object_get_sha1c(const MsnObject *obj);

/**
 * Returns a MsnObject's SHA1C value if it exists, otherwise SHA1D.
 *
 * @param obj The object.
 *
 * @return The SHA1C value.
 */
const gchar *msn_object_get_sha1(const MsnObject *obj);

/**
 * Returns the image associated with the MsnObject.
 *
 * @param obj The object.
 *
 * @return The associated image.
 */
PecanBuffer *msn_object_get_image (const MsnObject *obj);

void msn_object_set_local(MsnObject *obj);

#endif /* MSN_OBJECT_H */
