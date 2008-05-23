/**
 * Copyright (C) 2007-2008 Felipe Contreras
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

#include <glib.h>

#include "msn.h"
#include "page.h"
#include "session.h"
#include "pecan_util.h"
#include "pecan_status.h"
#include "pecan_log.h"

#include "switchboard.h"
#include "notification.h"
#include "sync.h"

#include "session_private.h"

#include "cmd/msg.h"

#include "ab/pecan_contact_priv.h"

#include "cvr/slplink.h"

/* libpurple stuff. */
#include "fix_purple_win32.h"
#include <debug.h>
#include <privacy.h>
#include <request.h>
#include <accountopt.h>
#include <pluginpref.h>
#include <cmds.h>
#include <version.h>
#include <core.h>
#include <prpl.h>
#include <util.h>
#include <prefs.h>
#include "internal.h"

typedef struct
{
	PurpleConnection *gc;
	const char *passport;

} MsnMobileData;

typedef struct
{
	PurpleConnection *gc;
	char *name;

} MsnGetInfoData;

typedef struct
{
	MsnGetInfoData *info_data;
	char *stripped;
	char *url_buffer;
	PurpleNotifyUserInfo *user_info;
	char *photo_url_text;

} MsnGetInfoStepTwoData;

typedef struct
{
	PurpleConnection *gc;
	const char *who;
	char *msg;
	PurpleMessageFlags flags;
	time_t when;
} MsnIMData;

static void msn_set_prp(PurpleConnection *gc, const char *type, const char *entry);

/** @todo remove this crap */
static const char *
msn_normalize(const PurpleAccount *account, const char *str)
{
	static char buf[BUF_LEN];
	char *tmp;

	g_return_val_if_fail(str != NULL, NULL);

	g_snprintf(buf, sizeof(buf), "%s%s", str,
			   (strchr(str, '@') ? "" : "@hotmail.com"));

	tmp = g_utf8_strdown(buf, -1);
	strncpy(buf, tmp, sizeof(buf));
	g_free(tmp);

	return buf;
}

static gboolean
msn_send_attention(PurpleConnection *gc, const char *username, guint type)
{
	MsnMessage *msg;
	MsnSession *session;
	MsnSwitchBoard *swboard;

	msg = msn_message_new_nudge();
	session = gc->proto_data;
	swboard = msn_session_get_swboard(session, username, MSN_SB_FLAG_IM);

	if (swboard == NULL)
		return FALSE;

	msn_switchboard_send_msg(swboard, msg, TRUE);

	return TRUE;
}

static GList *
msn_attention_types(PurpleAccount *account)
{
	PurpleAttentionType *attn;
	static GList *list = NULL;

	if (!list) {
		attn = g_new0(PurpleAttentionType, 1);
		attn->name = _("Nudge");
		attn->incoming_description = _("%s has nudged you!");
		attn->outgoing_description = _("Nudging %s...");
		list = g_list_append(list, attn);
	}

	return list;
}


static PurpleCmdRet
msn_cmd_nudge(PurpleConversation *conv, const gchar *cmd, gchar **args, gchar **error, void *data)
{
	PurpleAccount *account = purple_conversation_get_account(conv);
	PurpleConnection *gc = purple_account_get_connection(account);
	const gchar *username;

	username = purple_conversation_get_name(conv);

	serv_send_attention(gc, username, MSN_NUDGE);

	return PURPLE_CMD_RET_OK;
}

static void
msn_act_id(PurpleConnection *gc, const char *entry)
{
	MsnCmdProc *cmdproc;
	MsnSession *session;
	PurpleAccount *account;

	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;
	account = purple_connection_get_account(gc);

        msn_set_prp(gc, "MFN", entry ? entry : "");
}

static void
msn_set_prp(PurpleConnection *gc, const char *type, const char *entry)
{
	MsnCmdProc *cmdproc;
	MsnSession *session;

	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;

	if (entry == NULL || *entry == '\0')
	{
		msn_cmdproc_send(cmdproc, "PRP", "%s", type);
	}
	else
	{
		msn_cmdproc_send(cmdproc, "PRP", "%s %s", type,
						 purple_url_encode(entry));
	}
}

static void
msn_set_home_phone_cb(PurpleConnection *gc, const char *entry)
{
	msn_set_prp(gc, "PHH", entry);
}

static void
msn_set_work_phone_cb(PurpleConnection *gc, const char *entry)
{
	msn_set_prp(gc, "PHW", entry);
}

static void
msn_set_mobile_phone_cb(PurpleConnection *gc, const char *entry)
{
	msn_set_prp(gc, "PHM", entry);
}

static void
enable_msn_pages_cb(PurpleConnection *gc)
{
	msn_set_prp(gc, "MOB", "Y");
}

static void
disable_msn_pages_cb(PurpleConnection *gc)
{
	msn_set_prp(gc, "MOB", "N");
}

static void
send_to_mobile(PurpleConnection *gc, const char *who, const char *entry)
{
	MsnTransaction *trans;
	MsnSession *session;
	MsnCmdProc *cmdproc;
	MsnPage *page;
	char *payload;
	size_t payload_len;

	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;

	page = msn_page_new();
	msn_page_set_body(page, entry);

	payload = msn_page_gen_payload(page, &payload_len);

	trans = msn_transaction_new(cmdproc, "PGD", "%s 1 %d", who, payload_len);

	msn_transaction_set_payload(trans, payload, payload_len);

	msn_page_destroy(page);

	msn_cmdproc_send_trans(cmdproc, trans);
}

static void
send_to_mobile_cb(MsnMobileData *data, const char *entry)
{
	send_to_mobile(data->gc, data->passport, entry);
	g_free(data);
}

static void
close_mobile_page_cb(MsnMobileData *data, const char *entry)
{
	g_free(data);
}

/* -- */

static void
msn_show_set_friendly_name(PurplePluginAction *action)
{
	PurpleConnection *gc;

	gc = (PurpleConnection *) action->context;

	purple_request_input(gc, NULL, _("Set your friendly name."),
					   _("This is the name that other MSN buddies will "
						 "see you as."),
					   purple_connection_get_display_name(gc), FALSE, FALSE, NULL,
					   _("OK"), G_CALLBACK(msn_act_id),
					   _("Cancel"), NULL,
					   purple_connection_get_account(gc), NULL, NULL,
					   gc);
}

static void
msn_show_set_home_phone(PurplePluginAction *action)
{
	PurpleConnection *gc;
	MsnSession *session;

	gc = (PurpleConnection *) action->context;
	session = gc->proto_data;

	purple_request_input(gc, NULL, _("Set your home phone number."), NULL,
					   pecan_contact_get_home_phone(session->user), FALSE, FALSE, NULL,
					   _("OK"), G_CALLBACK(msn_set_home_phone_cb),
					   _("Cancel"), NULL,
					   purple_connection_get_account(gc), NULL, NULL,
					   gc);
}

static void
msn_show_set_work_phone(PurplePluginAction *action)
{
	PurpleConnection *gc;
	MsnSession *session;

	gc = (PurpleConnection *) action->context;
	session = gc->proto_data;

	purple_request_input(gc, NULL, _("Set your work phone number."), NULL,
					   pecan_contact_get_work_phone(session->user), FALSE, FALSE, NULL,
					   _("OK"), G_CALLBACK(msn_set_work_phone_cb),
					   _("Cancel"), NULL,
					   purple_connection_get_account(gc), NULL, NULL,
					   gc);
}

static void
msn_show_set_mobile_phone(PurplePluginAction *action)
{
	PurpleConnection *gc;
	MsnSession *session;

	gc = (PurpleConnection *) action->context;
	session = gc->proto_data;

	purple_request_input(gc, NULL, _("Set your mobile phone number."), NULL,
					   pecan_contact_get_mobile_phone(session->user), FALSE, FALSE, NULL,
					   _("OK"), G_CALLBACK(msn_set_mobile_phone_cb),
					   _("Cancel"), NULL,
					   purple_connection_get_account(gc), NULL, NULL,
					   gc);
}

static void
msn_show_set_mobile_pages(PurplePluginAction *action)
{
	PurpleConnection *gc;

	gc = (PurpleConnection *) action->context;

	purple_request_action(gc, NULL, _("Allow MSN Mobile pages?"),
						_("Do you want to allow or disallow people on "
						  "your buddy list to send you MSN Mobile pages "
						  "to your cell phone or other mobile device?"),
						-1,
						purple_connection_get_account(gc), NULL, NULL,
						gc, 3,
						_("Allow"), G_CALLBACK(enable_msn_pages_cb),
						_("Disallow"), G_CALLBACK(disable_msn_pages_cb),
						_("Cancel"), NULL);
}

static void
show_hotmail_inbox (PurplePluginAction *action)
{
    PurpleConnection *gc;
    MsnSession *session;

    gc = (PurpleConnection *) action->context;
    session = gc->proto_data;

    /** @todo what about people who don't want notifications but still check
     * the email? */
    if (!session->passport_info.mail_url)
    {
        purple_notify_error (gc, NULL,  _("This Hotmail account may not be active."), NULL);
        return;
    }

    /** apparently the correct value is 777 */
    if (time (NULL) - session->passport_info.mail_url_timestamp >= 750)
    {
        MsnTransaction *trans;
        MsnCmdProc *cmdproc;

        cmdproc = session->notification->cmdproc;

        trans = msn_transaction_new (cmdproc, "URL", "%s", "INBOX");
        msn_transaction_set_data (trans, GUINT_TO_POINTER (TRUE));

        msn_cmdproc_send_trans (cmdproc, trans);

        pecan_debug ("mail_url update");

        return;
    }

    purple_notify_uri (gc, session->passport_info.mail_url);
}

static void
show_send_to_mobile_cb(PurpleBlistNode *node, gpointer ignored)
{
	PurpleBuddy *buddy;
	PurpleConnection *gc;
	MsnSession *session;
	MsnMobileData *data;

	g_return_if_fail(PURPLE_BLIST_NODE_IS_BUDDY(node));

	buddy = (PurpleBuddy *) node;
	gc = purple_account_get_connection(buddy->account);

	session = gc->proto_data;

	data = g_new0(MsnMobileData, 1);
	data->gc = gc;
	data->passport = buddy->name;

	purple_request_input(gc, NULL, _("Send a mobile message."), NULL,
					   NULL, TRUE, FALSE, NULL,
					   _("Page"), G_CALLBACK(send_to_mobile_cb),
					   _("Close"), G_CALLBACK(close_mobile_page_cb),
					   purple_connection_get_account(gc), purple_buddy_get_name(buddy), NULL,
					   data);
}

static gboolean
msn_offline_message(const PurpleBuddy *buddy) {
	PecanContact *user;
	if (buddy == NULL)
		return FALSE;
	user = buddy->proto_data;
	return user && user->mobile;
}

static void
initiate_chat_cb(PurpleBlistNode *node, gpointer data)
{
	PurpleBuddy *buddy;
	PurpleConnection *gc;

	MsnSession *session;
	MsnSwitchBoard *swboard;

	g_return_if_fail(PURPLE_BLIST_NODE_IS_BUDDY(node));

	buddy = (PurpleBuddy *) node;
	gc = purple_account_get_connection(buddy->account);

	session = gc->proto_data;

	swboard = msn_switchboard_new(session);
	msn_switchboard_request(swboard);
	msn_switchboard_request_add_user(swboard, buddy->name);

	/* TODO: This might move somewhere else, after USR might be */
	swboard->chat_id = session->conv_seq++;
	swboard->conv = serv_got_joined_chat(gc, swboard->chat_id, "MSN Chat");
	swboard->flag = MSN_SB_FLAG_IM;

	purple_conv_chat_add_user(PURPLE_CONV_CHAT(swboard->conv),
							purple_account_get_username(buddy->account), NULL, PURPLE_CBFLAGS_NONE, TRUE);
}

static void
t_msn_xfer_init(PurpleXfer *xfer)
{
	MsnSlpLink *slplink = xfer->data;
	msn_slplink_request_ft(slplink, xfer);
}

static PurpleXfer*
msn_new_xfer(PurpleConnection *gc, const char *who)
{
	MsnSession *session;
	MsnSlpLink *slplink;
	PurpleXfer *xfer;

	session = gc->proto_data;

	xfer = purple_xfer_new(gc->account, PURPLE_XFER_SEND, who);
	if (xfer)
	{
		slplink = msn_session_get_slplink(session, who);

		xfer->data = slplink;

		purple_xfer_set_init_fnc(xfer, t_msn_xfer_init);
	}

	return xfer;
}

static void
msn_send_file(PurpleConnection *gc, const char *who, const char *file)
{
	PurpleXfer *xfer = msn_new_xfer(gc, who);

	if (file)
		purple_xfer_request_accepted(xfer, file);
	else
		purple_xfer_request(xfer);
}

static gboolean
msn_can_receive_file(PurpleConnection *gc, const char *who)
{
	PurpleAccount *account;
	char *normal;
	gboolean ret;

	account = purple_connection_get_account(gc);

	normal = g_strdup(msn_normalize(account, purple_account_get_username(account)));

	ret = strcmp(normal, msn_normalize(account, who));

	g_free(normal);

	return ret;
}

/**************************************************************************
 * Protocol Plugin ops
 **************************************************************************/

static const gchar *
list_icon (PurpleAccount *a,
           PurpleBuddy *b)
{
    return "msn";
}

static gchar *
status_text (PurpleBuddy *buddy)
{
    PurplePresence *presence;
    PurpleStatus *status;
    PecanContact *contact;

    presence = purple_buddy_get_presence (buddy);
    status = purple_presence_get_active_status (presence);
    contact = buddy->proto_data;

    {
        const gchar *personal_message;
        personal_message = pecan_contact_get_personal_message (contact);
        if (personal_message)
            return g_strdup (personal_message);
    }

    if (!purple_presence_is_available (presence) &&
        !purple_presence_is_idle (presence))
    {
        return g_strdup (purple_status_get_name (status));
    }

    return NULL;
}

static void
tooltip_text (PurpleBuddy *buddy,
              PurpleNotifyUserInfo *user_info,
              gboolean full)
{
    PecanContact *user;
    PurplePresence *presence;
    PurpleStatus *status;

    presence = purple_buddy_get_presence (buddy);
    status = purple_presence_get_active_status (presence);
    user = buddy->proto_data;

    if (purple_presence_is_online (presence))
    {
        purple_notify_user_info_add_pair (user_info, _("Status"),
                                          (purple_presence_is_idle (presence) ? _("Idle") : purple_status_get_name (status)));
    }

    if (user)
    {
        if (full)
        {
            if (pecan_contact_get_personal_message (user))
            {
                purple_notify_user_info_add_pair (user_info, _("Personal Message"),
                                                  pecan_contact_get_personal_message (user));
            }

            purple_notify_user_info_add_pair (user_info, _("Has you"),
                                              ((user->list_op & (1 << MSN_LIST_RL)) ? _("Yes") : _("No")));
        }

        /** @todo what's the issue here? */
        /* XXX: This is being shown in non-full tooltips because the
         * XXX: blocked icon overlay isn't always accurate for MSN.
         * XXX: This can die as soon as purple_privacy_check() knows that
         * XXX: this prpl always honors both the allow and deny lists. */
        purple_notify_user_info_add_pair (user_info, _("Blocked"),
                                          ((user->list_op & (1 << MSN_LIST_BL)) ? _("Yes") : _("No")));
    }
}

static inline PurpleStatusType *
util_gen_state (PurpleStatusPrimitive primitive,
                const gchar *id,
                const gchar *name)
{
    return purple_status_type_new_with_attrs (primitive,
                                              id, name, TRUE, TRUE, FALSE,
                                              "message", _("Message"), purple_value_new (PURPLE_TYPE_STRING),
                                              NULL);
}

static GList *
status_types (PurpleAccount *account)
{
    GList *types = NULL;

    /* visible states */
    types = g_list_append (types, util_gen_state (PURPLE_STATUS_AVAILABLE, NULL, NULL));
    types = g_list_append (types, util_gen_state (PURPLE_STATUS_AWAY, NULL, NULL));
    types = g_list_append (types, util_gen_state (PURPLE_STATUS_AWAY, "brb", _("Be Right Back")));
    types = g_list_append (types, util_gen_state (PURPLE_STATUS_UNAVAILABLE, "busy", _("Busy")));
    types = g_list_append (types, util_gen_state (PURPLE_STATUS_UNAVAILABLE, "phone", _("On the Phone")));
    types = g_list_append (types, util_gen_state (PURPLE_STATUS_AWAY, "lunch", _("Out to Lunch")));

    {
        PurpleStatusType *status;

        /* non-visible states */

        status = purple_status_type_new_full (PURPLE_STATUS_INVISIBLE, NULL, NULL, FALSE, TRUE, FALSE);
        types = g_list_append (types, status);

        status = purple_status_type_new_full (PURPLE_STATUS_OFFLINE, NULL, NULL, FALSE, TRUE, FALSE);
        types = g_list_append (types, status);

        /** @todo when do we use this? */
        status = purple_status_type_new_full (PURPLE_STATUS_MOBILE, "mobile", NULL, FALSE, FALSE, TRUE);
        types = g_list_append (types, status);
    }

    return types;
}

static GList *
msn_actions(PurplePlugin *plugin, gpointer context)
{
	PurpleConnection *gc = (PurpleConnection *)context;
	PurpleAccount *account;
	const char *user;

	GList *m = NULL;
	PurplePluginAction *act;

	act = purple_plugin_action_new(_("Set Friendly Name..."),
								 msn_show_set_friendly_name);
	m = g_list_append(m, act);
	m = g_list_append(m, NULL);

	act = purple_plugin_action_new(_("Set Home Phone Number..."),
								 msn_show_set_home_phone);
	m = g_list_append(m, act);

	act = purple_plugin_action_new(_("Set Work Phone Number..."),
			msn_show_set_work_phone);
	m = g_list_append(m, act);

	act = purple_plugin_action_new(_("Set Mobile Phone Number..."),
			msn_show_set_mobile_phone);
	m = g_list_append(m, act);
	m = g_list_append(m, NULL);

#if 0
	act = purple_plugin_action_new(_("Enable/Disable Mobile Devices..."),
			msn_show_set_mobile_support);
	m = g_list_append(m, act);
#endif

	act = purple_plugin_action_new(_("Allow/Disallow Mobile Pages..."),
			msn_show_set_mobile_pages);
	m = g_list_append(m, act);

	account = purple_connection_get_account(gc);
	user = msn_normalize(account, purple_account_get_username(account));

	if ((strstr(user, "@hotmail.") != NULL) ||
		(strstr(user, "@msn.com") != NULL))
	{
		m = g_list_append(m, NULL);
		act = purple_plugin_action_new (_("Open Hotmail Inbox"), show_hotmail_inbox);
		m = g_list_append(m, act);
	}

	return m;
}

static GList *
blist_node_menu (PurpleBlistNode *node)
{
    if (!PURPLE_BLIST_NODE_IS_BUDDY (node))
        return NULL;

    {
        PurpleBuddy *buddy;
        GList *m = NULL;
        PurpleMenuAction *act;

        buddy = (PurpleBuddy *) node;

        {
            PecanContact *user;

            user = buddy->proto_data;

            if (user && user->mobile)
            {
                /** @todo why is there a special way to do this? */
                act = purple_menu_action_new (_("Send to Mobile"),
                                              PURPLE_CALLBACK (show_send_to_mobile_cb),
                                              NULL, NULL);
                m = g_list_append (m, act);
            }
        }

        if (g_ascii_strcasecmp (buddy->name, purple_account_get_username (buddy->account)) != 0)
        {
            act = purple_menu_action_new (_("Initiate _Chat"),
                                          PURPLE_CALLBACK (initiate_chat_cb),
                                          NULL, NULL);
            m = g_list_append(m, act);
        }

        return m;
    }
}

static void
login (PurpleAccount *account)
{
    PurpleConnection *gc;
    MsnSession *session;
    const char *username;
    const char *host;
    int port;

    gc = purple_account_get_connection (account);

    if (!purple_ssl_is_supported ())
    {
        gc->wants_to_die = TRUE;
        purple_connection_error (gc,
                                 _("SSL support is needed for MSN. Please install a supported "
                                   "SSL library."));
        return;
    }

    host = purple_account_get_string (account, "server", MSN_SERVER);
    port = purple_account_get_int (account, "port", MSN_PORT);

    session = msn_session_new (account);

    gc->proto_data = session;
    gc->flags |= PURPLE_CONNECTION_HTML | \
                 PURPLE_CONNECTION_FORMATTING_WBFO | \
                 PURPLE_CONNECTION_NO_BGCOLOR | \
                 PURPLE_CONNECTION_NO_FONTSIZE | \
                 PURPLE_CONNECTION_NO_URLDESC;

    msn_session_set_login_step (session, PECAN_LOGIN_STEP_START);

    /** @todo remove normalization */
    username = msn_normalize (account, purple_account_get_username (account));

    if (strcmp (username, purple_account_get_username (account)))
        purple_account_set_username (account, username);

    if (!msn_session_connect (session, host, port))
        purple_connection_error (gc, _("Failed to connect to server."));
}

static void
logout (PurpleConnection *gc)
{
    MsnSession *session;

    session = gc->proto_data;

    g_return_if_fail (!session);

    msn_session_destroy (session);

    gc->proto_data = NULL;
}

static gint
send_im (PurpleConnection *gc,
         const gchar *who,
         const gchar *message,
         PurpleMessageFlags flags)
{
    PurpleAccount *account;
    gchar *msgformat;
    gchar *msgtext;

    account = purple_connection_get_account (gc);

    /** @todo use internal status instead */
    {
        PurpleBuddy *buddy;
        buddy = purple_find_buddy (account, who);
        if (buddy)
        {
            PurplePresence *p;
            p = purple_buddy_get_presence (buddy);
            if (purple_presence_is_status_primitive_active (p, PURPLE_STATUS_MOBILE))
            {
                gchar *text;
                text = purple_markup_strip_html (message);
                send_to_mobile (gc, who, text);
                g_free (text);
                return 1;
            }
        }
    }

    msn_import_html (message, &msgformat, &msgtext);

    /** @todo use a constant */
    /** @todo don't call strlen all the time */
    if (strlen (msgtext) + strlen (msgformat) + strlen (VERSION) > 1564)
    {
        g_free (msgformat);
        g_free (msgtext);

        return -E2BIG;
    }

    {
        MsnMessage *msg;
        msg = msn_message_new_plain (msgtext);
        msn_message_set_attr (msg, "X-MMS-IM-Format", msgformat);

        g_free (msgformat);
        g_free (msgtext);

        /* a message to ourselves? */
        if (g_ascii_strcasecmp (who, purple_account_get_username (account)) == 0)
            return -1;

        {
            MsnSession *session;
            MsnSwitchBoard *swboard;

            session = gc->proto_data;
            swboard = msn_session_get_swboard (session, who, MSN_SB_FLAG_IM);

            msn_switchboard_send_msg (swboard, msg, TRUE);
        }

        msn_message_destroy (msg);
    }

    return 1;
}

static guint
send_typing (PurpleConnection *gc,
             const gchar *who,
             PurpleTypingState state)
{
    PurpleAccount *account;
    MsnSession *session;
    MsnSwitchBoard *swboard;

    account = purple_connection_get_account (gc);
    session = gc->proto_data;

    if (state != PURPLE_TYPING)
        return 0;

    /* a message to ourselves? */
    if (g_ascii_strcasecmp (who, purple_account_get_username (account)) == 0)
        return MSN_TYPING_SEND_TIMEOUT;

    swboard = msn_session_find_swboard (session, who);

    if (!swboard || !msn_switchboard_can_send (swboard))
        return 0;

    swboard->flag |= MSN_SB_FLAG_IM;

    {
        MsnMessage *msg;

        msg = msn_message_new (MSN_MSG_TYPING);
        msn_message_set_content_type (msg, "text/x-msmsgscontrol");
        msn_message_set_flag (msg, 'U');
        msn_message_set_attr (msg, "TypingUser", purple_account_get_username (account));
        msn_message_set_bin_data (msg, "\r\n", 2);

        msn_switchboard_send_msg (swboard, msg, FALSE);

        msn_message_destroy (msg);
    }

    return MSN_TYPING_SEND_TIMEOUT;
}

static void
set_status (PurpleAccount *account,
            PurpleStatus *status)
{
    PurpleConnection *gc;
    MsnSession *session;

    gc = purple_account_get_connection (account);

    if (gc)
    {
        session = gc->proto_data;
        pecan_update_status (session);
        pecan_update_personal_message (session);
    }
}

static void
set_idle (PurpleConnection *gc,
          gint idle)
{
    MsnSession *session;

    session = gc->proto_data;

    pecan_update_status (session);
}

static void
msn_add_buddy(PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
	MsnSession *session;
	PecanContactList *contactlist;

	session = gc->proto_data;
	contactlist = session->contactlist;

	if (!session->logged_in)
	{
		purple_debug_error("msn", "msn_add_buddy called before connected\n");

		return;
	}

	pecan_contactlist_add_buddy_helper(contactlist, buddy, group);
}

static void
msn_rem_buddy(PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group)
{
	MsnSession *session;
	PecanContactList *contactlist;
	const gchar *group_name;

	session = gc->proto_data;
	contactlist = session->contactlist;
	group_name = group->name;

	if (!session->logged_in)
		return;

	/* Are we going to remove him completely? */
	if (group_name)
	{
	    PecanContact *user;

	    user = pecan_contactlist_find_contact (contactlist, buddy->name);

	    if (user && pecan_contact_get_group_count (user) <= 1)
		group_name = NULL;
	}

	pecan_contactlist_rem_buddy(contactlist, buddy->name, MSN_LIST_FL, group_name);
}

static void
msn_add_permit(PurpleConnection *gc, const char *who)
{
	MsnSession *session;
	PecanContactList *contactlist;
	PecanContact *user;

	session = gc->proto_data;
	contactlist = session->contactlist;
	user = pecan_contactlist_find_contact(contactlist, who);

	if (!session->logged_in)
		return;

	if (user != NULL && user->list_op & MSN_LIST_BL_OP)
		pecan_contactlist_rem_buddy(contactlist, who, MSN_LIST_BL, NULL);

	pecan_contactlist_add_buddy(contactlist, who, MSN_LIST_AL, NULL);
}

static void
msn_add_deny(PurpleConnection *gc, const char *who)
{
	MsnSession *session;
	PecanContactList *contactlist;
	PecanContact *user;

	session = gc->proto_data;
	contactlist = session->contactlist;
	user = pecan_contactlist_find_contact(contactlist, who);

	if (!session->logged_in)
		return;

	if (user != NULL && user->list_op & MSN_LIST_AL_OP)
		pecan_contactlist_rem_buddy(contactlist, who, MSN_LIST_AL, NULL);

	pecan_contactlist_add_buddy(contactlist, who, MSN_LIST_BL, NULL);
}

static void
msn_rem_permit(PurpleConnection *gc, const char *who)
{
	MsnSession *session;
	PecanContactList *contactlist;
	PecanContact *user;

	session = gc->proto_data;
	contactlist = session->contactlist;

	if (!session->logged_in)
		return;

	user = pecan_contactlist_find_contact(contactlist, who);

	pecan_contactlist_rem_buddy(contactlist, who, MSN_LIST_AL, NULL);

	if (user != NULL && user->list_op & MSN_LIST_RL_OP)
		pecan_contactlist_add_buddy(contactlist, who, MSN_LIST_BL, NULL);
}

static void
msn_rem_deny(PurpleConnection *gc, const char *who)
{
	MsnSession *session;
	PecanContactList *contactlist;
	PecanContact *user;

	session = gc->proto_data;
	contactlist = session->contactlist;

	if (!session->logged_in)
		return;

	user = pecan_contactlist_find_contact(contactlist, who);

	pecan_contactlist_rem_buddy(contactlist, who, MSN_LIST_BL, NULL);

	if (user != NULL && user->list_op & MSN_LIST_RL_OP)
		pecan_contactlist_add_buddy(contactlist, who, MSN_LIST_AL, NULL);
}

static void
msn_set_permit_deny(PurpleConnection *gc)
{
	PurpleAccount *account;
	MsnSession *session;
	MsnCmdProc *cmdproc;

	account = purple_connection_get_account(gc);
	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;

	if (account->perm_deny == PURPLE_PRIVACY_ALLOW_ALL ||
		account->perm_deny == PURPLE_PRIVACY_DENY_USERS)
	{
		msn_cmdproc_send(cmdproc, "BLP", "%s", "AL");
	}
	else
	{
		msn_cmdproc_send(cmdproc, "BLP", "%s", "BL");
	}
}

static void
msn_chat_invite(PurpleConnection *gc, int id, const char *msg,
				const char *who)
{
	MsnSession *session;
	MsnSwitchBoard *swboard;

	session = gc->proto_data;

	swboard = msn_session_find_swboard_with_id(session, id);

	if (swboard == NULL)
	{
		/* if we have no switchboard, everyone else left the chat already */
		swboard = msn_switchboard_new(session);
		msn_switchboard_request(swboard);
		swboard->chat_id = id;
		swboard->conv = purple_find_chat(gc, id);
	}

	swboard->flag |= MSN_SB_FLAG_IM;

	msn_switchboard_request_add_user(swboard, who);
}

static void
msn_chat_leave(PurpleConnection *gc, int id)
{
	MsnSession *session;
	MsnSwitchBoard *swboard;
	PurpleConversation *conv;

	session = gc->proto_data;

	swboard = msn_session_find_swboard_with_id(session, id);

	/* if swboard is NULL we were the only person left anyway */
	if (swboard == NULL)
		return;

	conv = swboard->conv;

	msn_switchboard_release(swboard, MSN_SB_FLAG_IM);

	/* If other switchboards managed to associate themselves with this
	 * conv, make sure they know it's gone! */
	if (conv != NULL)
	{
		while ((swboard = msn_session_find_swboard_with_conv(session, conv)) != NULL)
			swboard->conv = NULL;
	}
}

static int
msn_chat_send(PurpleConnection *gc, int id, const char *message, PurpleMessageFlags flags)
{
	PurpleAccount *account;
	MsnSession *session;
	MsnSwitchBoard *swboard;
	MsnMessage *msg;
	char *msgformat;
	char *msgtext;

	account = purple_connection_get_account(gc);
	session = gc->proto_data;
	swboard = msn_session_find_swboard_with_id(session, id);

	if (swboard == NULL)
		return -EINVAL;

	if (!swboard->ready)
		return 0;

	swboard->flag |= MSN_SB_FLAG_IM;

	msn_import_html(message, &msgformat, &msgtext);

	if (strlen(msgtext) + strlen(msgformat) + strlen(VERSION) > 1564)
	{
		g_free(msgformat);
		g_free(msgtext);

		return -E2BIG;
	}

	msg = msn_message_new_plain(msgtext);
	msn_message_set_attr(msg, "X-MMS-IM-Format", msgformat);
	msn_switchboard_send_msg(swboard, msg, FALSE);
	msn_message_destroy(msg);

	g_free(msgformat);
	g_free(msgtext);

	serv_got_chat_in(gc, id, purple_account_get_username(account), 0,
					 message, time(NULL));

	return 0;
}

static void
msn_keepalive(PurpleConnection *gc)
{
	MsnSession *session;

	session = gc->proto_data;

	if (!session->http_method)
	{
		MsnCmdProc *cmdproc;
		cmdproc = session->notification->cmdproc;
		msn_cmdproc_send_quick(cmdproc, "PNG", NULL, NULL);
	}
}

static void
msn_alias_buddy (PurpleConnection *gc, const char *name, const char *alias)
{
	MsnSession *session;
	MsnCmdProc *cmdproc;
	PecanContact *contact;

	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;
	contact = pecan_contactlist_find_contact (session->contactlist, name);

        if (alias && strlen (alias))
            alias = purple_url_encode (alias);
        else
            alias = pecan_contact_get_passport (contact);

        msn_cmdproc_send(cmdproc, "SBP", "%s %s %s", pecan_contact_get_guid (contact), "MFN", alias);
}

static void
pecan_group_buddy(PurpleConnection *gc, const char *who,
				const char *old_group_name, const char *new_group_name)
{
	MsnSession *session;
	PecanContactList *contactlist;

	session = gc->proto_data;
	contactlist = session->contactlist;

	pecan_contactlist_move_buddy(contactlist, who, old_group_name, new_group_name);
}

static void
msn_rename_group(PurpleConnection *gc, const char *old_name,
				 PurpleGroup *group, GList *moved_buddies)
{
	MsnSession *session;
	MsnCmdProc *cmdproc;
	const gchar *old_group_guid;
	const char *enc_new_group_name;

	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;
	enc_new_group_name = purple_url_encode(group->name);

	old_group_guid = pecan_contactlist_find_group_id(session->contactlist, old_name);

        g_return_if_fail (old_group_guid);
        msn_cmdproc_send (cmdproc, "REG", "%s %s", old_group_guid,
                          enc_new_group_name);
}

static void
msn_convo_closed(PurpleConnection *gc, const char *who)
{
	MsnSession *session;
	MsnSwitchBoard *swboard;
	PurpleConversation *conv;

	session = gc->proto_data;

	swboard = msn_session_find_swboard(session, who);

	/*
	 * Don't perform an assertion here. If swboard is NULL, then the
	 * switchboard was either closed by the other party, or the person
	 * is talking to himself.
	 */
	if (swboard == NULL)
		return;

	conv = swboard->conv;

	/* If we release the switchboard here, it may still have messages
	   pending ACK which would result in incorrect unsent message errors.
	   Just let it timeout... This is *so* going to screw with people who
	   use dumb clients that report "User has closed the conversation window" */
	/* msn_switchboard_release(swboard, MSN_SB_FLAG_IM); */
	swboard->conv = NULL;

	/* If other switchboards managed to associate themselves with this
	 * conv, make sure they know it's gone! */
	if (conv != NULL)
	{
		while ((swboard = msn_session_find_swboard_with_conv(session, conv)) != NULL)
			swboard->conv = NULL;
	}
}

static void
set_buddy_icon (PurpleConnection *gc,
                PurpleStoredImage *img)
{
    MsnSession *session;
    PecanContact *user;

    session = gc->proto_data;
    user = session->user;

    {
        PecanBuffer *image;
        image = pecan_buffer_new_memdup (purple_imgstore_get_data (img),
                                         purple_imgstore_get_size (img));
        pecan_contact_set_buddy_icon (session->user, image);
    }

    pecan_update_status (session);
}

static void
msn_remove_group(PurpleConnection *gc, PurpleGroup *group)
{
	MsnSession *session;
	MsnCmdProc *cmdproc;
	const gchar *group_guid;

	session = gc->proto_data;
	cmdproc = session->notification->cmdproc;

	/* The server automatically removes the contacts and sends
	 * notifications back. */
	if ((group_guid = pecan_contactlist_find_group_id(session->contactlist, group->name)))
	{
		msn_cmdproc_send(cmdproc, "RMG", "%s", group_guid);
	}
}

static gboolean
load (PurplePlugin *plugin)
{
    msn_notification_init ();
    msn_switchboard_init ();
    msn_sync_init ();

    return TRUE;
}

static gboolean
unload (PurplePlugin *plugin)
{
    msn_notification_end ();
    msn_switchboard_end ();
    msn_sync_end ();

    return TRUE;
}

/*
 * Plugin information
 */

static PurplePluginProtocolInfo prpl_info =
{
    OPT_PROTO_MAIL_CHECK,
    NULL, /* user_splits */
    NULL, /* protocol_options */
    {"png", 0, 0, 96, 96, 0, PURPLE_ICON_SCALE_SEND}, /* icon_spec */
    list_icon, /* list_icon */
    NULL, /* list_emblems */
    status_text, /* status_text */
    tooltip_text, /* tooltip_text */
    status_types, /* away_states */
    blist_node_menu, /* blist_node_menu */
    NULL, /* chat_info */
    NULL, /* chat_info_defaults */
    login, /* login */
    logout, /* close */
    send_im, /* send_im */
    NULL, /* set_info */
    send_typing, /* send_typing */
    NULL, /* get_info */
    set_status, /* set_away */
    set_idle, /* set_idle */
    NULL, /* change_passwd */
    msn_add_buddy, /* add_buddy */
    NULL, /* add_buddies */
    msn_rem_buddy, /* remove_buddy */
    NULL, /* remove_buddies */
    msn_add_permit, /* add_permit */
    msn_add_deny, /* add_deny */
    msn_rem_permit, /* rem_permit */
    msn_rem_deny, /* rem_deny */
    msn_set_permit_deny, /* set_permit_deny */
    NULL, /* join_chat */
    NULL, /* reject chat invite */
    NULL, /* get_chat_name */
    msn_chat_invite, /* chat_invite */
    msn_chat_leave, /* chat_leave */
    NULL, /* chat_whisper */
    msn_chat_send, /* chat_send */
    msn_keepalive, /* keepalive */
    NULL, /* register_user */
    NULL, /* get_cb_info */
    NULL, /* get_cb_away */
    msn_alias_buddy, /* alias_buddy */
    pecan_group_buddy, /* group_buddy */
    msn_rename_group, /* rename_group */
    NULL, /* buddy_free */
    msn_convo_closed, /* convo_closed */
    msn_normalize, /* normalize */
    set_buddy_icon, /* set_buddy_icon */
    msn_remove_group, /* remove_group */
    NULL, /* get_cb_real_name */
    NULL, /* set_chat_topic */
    NULL, /* find_blist_chat */
    NULL, /* roomlist_get_list */
    NULL, /* roomlist_cancel */
    NULL, /* roomlist_expand_category */
    msn_can_receive_file, /* can_receive_file */
    msn_send_file, /* send_file */
    msn_new_xfer, /* new_xfer */
    msn_offline_message, /* offline_message */
    NULL, /* whiteboard_prpl_ops */
    NULL, /* send_raw */
    NULL, /* roomlist_room_serialize */
    NULL, /* unregister_user */
    msn_send_attention, /* send_attention */
    msn_attention_types, /* attention_types */

    /* padding */
    NULL
};

static PurplePluginInfo info =
{
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_PROTOCOL, /**< type */
    NULL, /**< ui_requirement */
    0, /**< flags */
    NULL, /**< dependencies */
    PURPLE_PRIORITY_DEFAULT, /**< priority */

    "prpl-msn-pecan", /**< id */
    "WLM", /**< name */
    VERSION, /**< version */
    N_("WLM Protocol Plugin"), /**< summary */
    N_("WLM Protocol Plugin"), /**< description */
    "Felipe Contreras <felipe.contreras@gmail.com>", /**< author */
    PURPLE_WEBSITE, /**< homepage */

    load, /**< load */
    unload, /**< unload */
    NULL, /**< destroy */

    NULL, /**< ui_info */
    &prpl_info, /**< extra_info */
    NULL, /**< prefs_info */
    msn_actions,

    /* padding */
    NULL,
    NULL,
    NULL,
    NULL
};

static void
init_plugin (PurplePlugin *plugin)
{
    {
        PurpleAccountOption *option;

        option = purple_account_option_string_new (_("Server"), "server", MSN_SERVER);
        prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, option);

        option = purple_account_option_int_new (_("Port"), "port", 1863);
        prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, option);

        option = purple_account_option_bool_new (_("Use HTTP Method"), "http_method", FALSE);
        prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, option);

        option = purple_account_option_bool_new (_("Show custom smileys"), "custom_smileys", TRUE);
        prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, option);

        option = purple_account_option_bool_new (_("Use server-side alias"), "server_alias", FALSE);
        prpl_info.protocol_options = g_list_append (prpl_info.protocol_options, option);
    }

    purple_cmd_register ("nudge", "", PURPLE_CMD_P_PRPL,
                         PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_PRPL_ONLY,
                         "prpl-msn-pecan", msn_cmd_nudge,
                         _("nudge: nudge a user to get their attention"), NULL);

    purple_prefs_remove ("/plugins/prpl/msn");
}

PURPLE_INIT_PLUGIN (msn_pecan, init_plugin, info)
