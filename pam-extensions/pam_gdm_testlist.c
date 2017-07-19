/*
 * Copyright  (C) 2017 Red Hat, Inc.
 *
 * compile with gcc -rdynamic -fPIC -shared -lpam $(pkg-config --cflags gdm-pam-extensions) pam_testlist.c -o pam_testlist.so
 */

#include <unistd.h>

#include <security/_pam_macros.h>
#include <security/pam_ext.h>
#include <security/pam_misc.h>
#include <security/pam_modules.h>
#include <security/pam_modutil.h>

#ifndef GDM_IN_TREE_BUILD
#include <gdm/gdm-pam-extensions.h>
#else
#include "gdm-pam-extensions.h"
#endif

int
pam_sm_authenticate (pam_handle_t  *pamh,
                     int            flags,
                     int            argc,
                     const char   **argv)
{
        int retval = PAM_ABORT;

        struct pam_conv *conversation_closure;
        struct pam_message prompt_message;
        const struct pam_message *prompt_messages[1];
        struct pam_response *reply = NULL;

        GdmPamExtensionChoiceListRequest *request = NULL;
        GdmPamExtensionChoiceListResponse *response = NULL;
        uint32_t number_of_items = 4;
        int i = 0;

        if (!GDM_PAM_EXTENSION_SUPPORTED (GDM_PAM_EXTENSION_CHOICE_LIST))
                goto out;

        pam_info (pamh, "Please select a certificate");

        retval = pam_get_item (pamh, PAM_CONV, (const void **) &conversation_closure);

        if (retval != PAM_SUCCESS)
                goto out;

        request = malloc (GDM_PAM_EXTENSION_CHOICE_LIST_REQUEST_SIZE (number_of_items));
        GDM_PAM_EXTENSION_CHOICE_LIST_REQUEST_INIT (request, number_of_items);

        request->list.items[i].key = "john-smith-edipi0";
        request->list.items[i++].text = "EDIPI Cert 0";
        request->list.items[i].key = "john-smith-piv-sig0";
        request->list.items[i++].text = "PIV Signature Cert 0";
        request->list.items[i].key = "john-smith-secret0";
        request->list.items[i++].text = "Class Secret Cert 0";
        request->list.items[i].key = "john-smith-mess-hall0";
        request->list.items[i++].text = "Mess Hall Cert 0";

        GDM_PAM_EXTENSION_MESSAGE_TO_BINARY_PROMPT_MESSAGE (request, &prompt_message);
        prompt_messages[0] = &prompt_message;

        retval = conversation_closure->conv (1, prompt_messages, &reply, conversation_closure->appdata_ptr);

        if (retval != PAM_SUCCESS)
                goto out;

        retval = PAM_ABORT;
        response = GDM_PAM_EXTENSION_REPLY_TO_CHOICE_LIST_RESPONSE (reply);

        if (response->key == NULL)
                goto out;

        for (i = 0; i < number_of_items; i++) {
                if (strcmp (response->key, request->list.items[i].key) != 0)
                        continue;
                pam_info (pamh, "Certificate ‘%s’ selected", request->list.items[i].text);
                retval = PAM_SUCCESS;
        }

out:
        free (request);
        free (response);

        return retval;
}

int
pam_sm_setcred (pam_handle_t *pamh,
                int           flags,
                int           argc,
                const char  **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_acct_mgmt (pam_handle_t  *pamh,
                  int            flags,
                  int            argc,
                  const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_chauthtok (pam_handle_t  *pamh,
                  int            flags,
                  int            argc,
                  const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_open_session (pam_handle_t  *pamh,
                     int            flags,
                     int            argc,
                     const char   **argv)
{
        return PAM_SUCCESS;
}

int
pam_sm_close_session (pam_handle_t  *pamh,
                      int            flags,
                      int            argc,
                      const char   **argv)
{
        return PAM_SUCCESS;
}
