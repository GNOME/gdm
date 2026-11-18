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
#pragma once

#include <assert.h>

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

static inline void
gdm_pam_extension_choice_list_request_init (GdmPamExtensionChoiceListRequest *request,
                                            const char                       *title,
                                            int                               num_items)
{
        bool type_found;

        type_found = gdm_pam_extension_look_up_type (GDM_PAM_EXTENSION_CHOICE_LIST,
                                                     &request->header.type);
        assert (type_found);

        request->header.length = htobe32 (GDM_PAM_EXTENSION_CHOICE_LIST_REQUEST_SIZE (num_items));
        request->prompt_message = (char *) title;
        request->list.number_of_items = num_items;
}

#define GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_SIZE sizeof (GdmPamExtensionChoiceListResponse)

static inline void
gdm_pam_extension_choice_list_response_init (GdmPamExtensionChoiceListResponse *response)
{
        bool type_found;

        type_found = gdm_pam_extension_look_up_type (GDM_PAM_EXTENSION_CHOICE_LIST,
                                                     &response->header.type);
        assert (type_found);

        response->header.length = htobe32 (GDM_PAM_EXTENSION_CHOICE_LIST_RESPONSE_SIZE);
        response->key = NULL;
}

static inline GdmPamExtensionChoiceListResponse *
gdm_pam_extension_reply_to_choice_list_response (const struct pam_response *reply)
{
        return (GdmPamExtensionChoiceListResponse *) (void *) reply->resp;
}

static inline void
gdm_pam_extension_choice_list_response_free (GdmPamExtensionChoiceListResponse *response)
{
        if (response == NULL)
                return;

        if (response->key != NULL) {
               gdm_pam_extension_zero_buffer (response->key, strlen (response->key));
               free (response->key);
        }
        free (response);
}

#ifdef __G_LIB_H__
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GdmPamExtensionChoiceListResponse, gdm_pam_extension_choice_list_response_free)
#endif
