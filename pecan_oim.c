#include "pecan_oim_private.h"
#include "io/pecan_ssl_conn.h"
#include "io/pecan_parser.h"

#include "io/pecan_node_priv.h"
#include "session_private.h"
#include <string.h> /* for strlen */
#include <stdlib.h> /* for atoi */

#include "pecan_log.h"

/* libpurple stuff. */
#include <util.h> /* for base64_dec */
#include <conversation.h> /* for conversation_new */

typedef struct OimRequest OimRequest;

struct OimRequest
{
    PecanOimSession *oim_session;
    gchar *passport;
    gchar *message_id;
    PecanParser *parser;
    guint parser_state;
    gsize payload;
};

static inline OimRequest *
oim_request_new (PecanOimSession *oim_session,
                 const gchar *passport,
                 const gchar *message_id)
{
    OimRequest *oim_request;

    oim_request = g_new0 (OimRequest, 1);
    oim_request->oim_session = oim_session;
    oim_request->passport = g_strdup (passport);
    oim_request->message_id = g_strdup (message_id);

    return oim_request;
}

static inline void
oim_request_free (OimRequest *oim_request)
{
    pecan_parser_free (oim_request->parser);
    g_free (oim_request->passport);
    g_free (oim_request->message_id);
    g_free (oim_request);
}

PecanOimSession *
pecan_oim_session_new (MsnSession *session)
{
    PecanOimSession *oim_session;

    oim_session = g_new0 (PecanOimSession, 1);
    oim_session->session = session;
    oim_session->request_queue = g_queue_new ();

    return oim_session;
}

void
pecan_oim_session_free (PecanOimSession *oim_session)
{
    if (!oim_session)
        return;

    {
        OimRequest *oim_request;
        while ((oim_request = g_queue_pop_tail (oim_session->request_queue)))
        {
            oim_request_free (oim_request);
        }
    }
    g_queue_free (oim_session->request_queue);

    g_free (oim_session);
}

static inline void
oim_send_request (PecanNode *conn,
                  const gchar *message_id)
{
    gchar *body;
    gchar *header;
    gsize body_len;

    pecan_log ("begin");

    body = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                            "<soap:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                            "<soap:Header>"
                            "<PassportCookie xmlns=\"http://www.hotmail.msn.com/ws/2004/09/oim/rsi\">"
                            "<t>%s</t>"
                            "<p>%s</p>"
                            "</PassportCookie>"
                            "</soap:Header>"
                            "<soap:Body>"
                            "<GetMessage xmlns=\"http://www.hotmail.msn.com/ws/2004/09/oim/rsi\">"
                            "<messageId>%s</messageId>"
                            "<alsoMarkAsRead>false</alsoMarkAsRead>"
                            "</GetMessage>"
                            "</soap:Body>"
                            "</soap:Envelope>",
                            conn->session->passport_cookie.t,
                            conn->session->passport_cookie.p,
                            message_id);

    body_len = strlen (body);

    header = g_strdup_printf ("POST /rsi/rsi.asmx HTTP/1.1\r\n"
                              "Accept: */*\r\n"
                              "SOAPAction: \"http://www.hotmail.msn.com/ws/2004/09/oim/rsi/GetMessage\"\r\n"
                              "Content-Type: text/xml; charset=utf-8\r\n"
                              "Content-Length: %d\r\n"
                              "User-Agent: Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)\r\n"
                              "Host: %s\r\n"
                              "Connection: Keep-Alive\r\n"
                              "Cache-Control: no-cache\r\n"
                              /* "Cookie: MSPAuth=%s\r\n" */
                              "\r\n%s",
                              body_len,
                              "rsi.hotmail.com",
                              /* session->passport_info.mspauth, */
                              body);

    pecan_debug ("header=[%s]", header);
    /* pecan_debug ("body=[%s]", body); */

    {
        gsize len;
        pecan_node_write (conn, header, strlen (header), &len, NULL);
        pecan_debug ("write_len=%d", len);
    }

    pecan_log ("end");
}

static void
open_cb (PecanNode *conn,
         OimRequest *oim_request)
{
    g_return_if_fail (conn);

    pecan_log ("begin");

    oim_send_request (conn, oim_request->message_id);

    pecan_log ("end");
}

static inline void oim_process_requests (PecanOimSession *oim_session);

static inline void
next_request (PecanOimSession *oim_session)
{
    g_queue_pop_head (oim_session->request_queue);
    oim_process_requests (oim_session);
}

static void
read_cb (PecanNode *conn,
         gpointer data)
{
    OimRequest *oim_request;
    GIOStatus status = G_IO_STATUS_NORMAL;
    gchar *str = NULL;

    oim_request = data;

    if (oim_request->parser_state < 2)
    {
        while (TRUE)
        {
            gsize terminator_pos;

            status = pecan_parser_read_line (oim_request->parser, &str, NULL, &terminator_pos, NULL);

            if (status == G_IO_STATUS_AGAIN)
                return;

            if (status != G_IO_STATUS_NORMAL)
            {
                pecan_node_close (conn);
                pecan_node_free (conn);
                goto leave;
            }

            if (str)
            {
                str[terminator_pos] = '\0';

                /* now comes the content */
                if (str[0] == '\0')
                {
                    g_free (str);
                    break;
                }

                g_free (str);
            }
        }

        oim_request->parser_state++;
    }

    /** @todo can we really be sure it's just one line? */
    if (oim_request->parser_state == 2)
    {
        gsize terminator_pos;

        status = pecan_parser_read_line (oim_request->parser, &str, NULL, &terminator_pos, NULL);

        if (status == G_IO_STATUS_AGAIN)
            return;

        if (status != G_IO_STATUS_NORMAL)
        {
            pecan_node_close (conn);
            pecan_node_free (conn);
            goto leave;
        }

        if (str)
        {
            PurpleConversation *conv;
            gchar *tmp;

            str[terminator_pos] = '\0';

            tmp = (gchar *) purple_base64_decode (str, NULL);
            pecan_debug ("oim: passport=[%s],msg=[%s]", oim_request->passport, tmp);
            conv = purple_conversation_new (PURPLE_CONV_TYPE_IM,
                                            msn_session_get_account (oim_request->oim_session->session), 
                                            oim_request->passport);
            /** @todo get the right time. */
            /** @todo remove the no_log before release */
            purple_conversation_write (conv, NULL, tmp,
                                       PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_DELAYED | PURPLE_MESSAGE_NO_LOG, time (NULL));

            g_free (tmp);
            g_free (str);
        }

        pecan_node_close (conn);
        pecan_node_free (conn);
    }

leave:
    next_request (oim_request->oim_session);
}

static inline void
oim_process_requests (PecanOimSession *oim_session)
{
    OimRequest *oim_request;

    oim_request = g_queue_peek_head (oim_session->request_queue);

    if (!oim_request)
        return;

    {
        PecanSslConn *ssl_conn;
        PecanNode *conn;

        ssl_conn = pecan_ssl_conn_new ("oim", PECAN_NODE_NULL);
        conn = PECAN_NODE (ssl_conn);
        conn->session = oim_session->session;

        oim_request->parser = pecan_parser_new (conn);
        pecan_ssl_conn_set_read_cb (ssl_conn, read_cb, oim_request);

        pecan_node_connect (conn, "rsi.hotmail.com", 443);

        g_signal_connect (conn, "open", G_CALLBACK (open_cb), oim_request);
    }
}

void
pecan_oim_session_request (PecanOimSession *oim_session,
                           const gchar *passport,
                           const gchar *message_id)
{
    gboolean initial;

    initial = g_queue_is_empty (oim_session->request_queue);

    g_queue_push_tail (oim_session->request_queue,
                       oim_request_new (oim_session, passport, message_id));

    if (initial)
        oim_process_requests (oim_session);
}
