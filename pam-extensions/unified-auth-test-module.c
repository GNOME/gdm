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

#define JSON                                                                   \
"{\"auth-selection\":                                                        " \
"  {                                                                         " \
"    \"mechanisms\":                                                         " \
"      {                                                                     " \
"        \"eidp\":                                                           " \
"          {                                                                 " \
"            \"name\": \"Web Login\",                                        " \
"            \"role\": \"eidp\",                                             " \
"            \"init_prompt\": \"Log In\",                                    " \
"            \"link_prompt\": \"Log in online with another device\",         " \
"            \"uri\": \"https://short.url.com/1234\",                        " \
"            \"code\": \"1234\"                                              " \
"          },                                                                " \
"        \"password\":                                                       " \
"          {                                                                 " \
"            \"name\": \"Password\",                                         " \
"            \"role\": \"password\",                                         " \
"            \"prompt\": \"Password:\"                                       " \
"          },                                                                " \
"        \"smartcard:honescu@BIZ.COM\":                                      " \
"          {                                                                 " \
"            \"name\": \"Smartcard honescu@BIZ.COM\",                        " \
"            \"role\": \"smartcard\",                                        " \
"            \"prompt\": \"Enter PIN:\",                                     " \
"            \"slot_id\": \"0\",                                             " \
"            \"token_name\": \"honescu@BIZ.COM\"                             " \
"          },                                                                " \
"        \"smartcard:honescu@NAVY.MIL\":                                     " \
"          {                                                                 " \
"            \"name\": \"Smartcard honescu@NAVY.MIL\",                       " \
"            \"role\": \"smartcard\",                                        " \
"            \"prompt\": \"Enter PIN:\",                                     " \
"            \"slot_id\": \"1\",                                             " \
"            \"token_name\": \"honescu@BIZ.COM\"                             " \
"          }                                                                 " \
"      },                                                                    " \
"    \"priority\":                                                           " \
"      [                                                                     " \
"        \"eidp\",                                                           " \
"        \"password\",                                                       " \
"        \"smartcard:honescu@NAVY.MIL\",                                     " \
"        \"smartcard:honescu@BIZ.COM\"                                       " \
"      ]                                                                     " \
"  }                                                                         " \
"}                                                                           "

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

        GdmPamExtensionJSONProtocol *json_request = NULL, *json_response = NULL;

        if (!GDM_PAM_EXTENSION_SUPPORTED (GDM_PAM_EXTENSION_CUSTOM_JSON))
                goto out;

        retval = pam_get_item (pamh, PAM_CONV, (const void **) &conversation_closure);

        if (retval != PAM_SUCCESS)
                goto out;

        json_request = malloc (GDM_PAM_EXTENSION_CUSTOM_JSON_SIZE);
        GDM_PAM_EXTENSION_CUSTOM_JSON_REQUEST_INIT (json_request, "auth-mechanisms", 1, JSON);
        g_printerr ("request: %s\n", json_request->json);

        GDM_PAM_EXTENSION_MESSAGE_TO_BINARY_PROMPT_MESSAGE (json_request, &prompt_message);
        prompt_messages[0] = &prompt_message;

        retval = conversation_closure->conv (1, prompt_messages, &reply, conversation_closure->appdata_ptr);

        if (retval != PAM_SUCCESS)
                goto out;

        json_response = GDM_PAM_EXTENSION_REPLY_TO_CUSTOM_JSON_RESPONSE (reply);

        g_printerr ("response: %s\n", json_response->json);

        sleep (2);

out:
        free (json_request);
        free (json_response);

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
