/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#ifndef GDM_CHOICE_LIST_PAM_EXTENSION_H
#define GDM_CHOICE_LIST_PAM_EXTENSION_H

#include "gdm-pam-extensions-common.h"

typedef struct {
        const char *key;
        const char *text;
} GdmChoiceListItems;

typedef struct {
        size_t number_of_items;
        GdmChoiceListItems items[];
} GdmChoiceList;

typedef struct {
        GdmPamExtensionMessage header;

        char *prompt_message;
        GdmChoiceList list;
} GdmPamExtensionChoiceListRequest;

typedef struct {
        GdmPamExtensionMessage header;

        char *key;
} GdmPamExtensionChoiceListResponse;

#define GDM_PAM_EXTENSION_CHOICE_LIST "org.gnome.DisplayManager.UserVerifier.ChoiceList"

#define GDM_CHOICE_LIST_SIZE(num_items) (offsetof(GdmChoiceList, items) + (num_items) * sizeof (GdmChoiceListItems))
#define GDM_PAM_EXTENSION_CHOICE_LIST_REQUEST_SIZE(num_items) (offsetof(GdmPamExtensionChoiceListRequest, list) + GDM_CHOICE_LIST_SIZE((num_items)))
#define GDM_PAM_EXTENSION_CHOICE_LIST_REQUEST_INIT(request, title, num_items) \
{ \
        int _n = num_items; \
        GDM_PAM_EXTENSION_LOOK_UP_TYPE (GDM_PAM_EXTENSION_CHOICE_LIST, &request->header.type); \
        request->header.length = htobe32 (GDM_PAM_EXTENSION_CHOICE_LIST_REQUEST_SIZE(_n)); \
        request->prompt_message = title; \
        request->list.number_of_items = _n; \
}

#define GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_SIZE sizeof (GdmPamExtensionChoiceListResponse)
#define GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_INIT(response) \
{ \
        GDM_PAM_EXTENSION_LOOK_UP_TYPE (GDM_PAM_EXTENSION_CHOICE_LIST, &response->header.type); \
        response->header.length = htobe32 (GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_SIZE); \
        response->key = NULL; \
}
#define GDM_PAM_EXTENSION_REPLY_TO_CHOICE_LIST_RESPONSE(reply) ((GdmPamExtensionChoiceListResponse *) (void *) reply->resp)

#endif
