/*  Videochat - Sample videochat server based on libmoment
    Copyright (C) 2011 Dmitry Shatrov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include <libmary/libmary.h>
#include <mycpp/mycpp.h>
#include <mycpp/cmdline.h>
#include <moment/libmoment.h>
#include <mconfig/mconfig.h>


#define MOMENT_VIDEOCHATD__ENABLE_AUDIO


using namespace M;
using namespace Moment;

namespace {

namespace {
LogGroup libMary_logGroup_msg ("msg", LogLevel::N);
}

struct Options {
    bool help;
    Ref<String> config_filename;

    Options ()
	: help (false)
    {
    }
};

Options options;
MConfig::Config config;

ServerApp server_app (NULL /* coderef_container */);

PagePool page_pool (4096 /* page_size */, 128 /* min_pages */);

RtmpService rtmp_service   (NULL /* coderef_container */);
RtmptService rtmpt_service (NULL /* coderef_container */);

class ClientQueue_name;

class ClientSession : public Object,
		      public IntrusiveListElement<ClientQueue_name>
{
public:
    enum ClientType {
	Unknown,
	Subscriber,
	Broadcaster
    };

    ClientType client_type;

    RtmpConnection *rtmp_conn;
    RtmpServer rtmp_server;
    VideoStream::FrameSaver frame_saver;

    bool queued;

    ClientSession *peer_session;

    ClientSession ()
	: client_type (Unknown),
	  rtmp_conn (NULL),
	  queued (false),
	  peer_session (NULL)
    {
    }
};

IntrusiveList<ClientSession, ClientQueue_name> broadcaster_queue;
Count broadcasters_waiting = 0;

IntrusiveList<ClientSession, ClientQueue_name> subscriber_queue;
Count subscribers_waiting = 0;

static void
printUsage ()
{
    outs->print ("Usage: videochatd [options]\n"
		  "Options:\n"
		  "  -c --config <config_file>\n");
    outs->flush ();
}

static bool
cmdline_help (char const * /* short_name */,
	      char const * /* long_name */,
	      char const * /* value */,
	      void       * /* opt_data */,
	      void       * /* callback_data */)
{
    options.help = true;
    return true;
}

static bool
cmdline_config (char const * /* short_name */,
		char const * /* long_name */,
		char const *value,
		void       * /* opt_data */,
		void       * /* callback_data */)
{
    options.config_filename = grab (new String (value));
    return true;
}

static void
dumpStats ()
{
    logD_ (_func, "broadcasters: ", broadcasters_waiting, ", subscribers: ", subscribers_waiting);
}

Result startStreaming (ConstMemory const & /* stream_name */,
		       void * const _client_session)
{
    logD_ (_func_);
    dumpStats ();

    ClientSession * const client_session = static_cast <ClientSession*> (_client_session);
    client_session->client_type = ClientSession::Broadcaster;

    assert (!client_session->queued &&
	    !client_session->peer_session);

    if (!subscriber_queue.isEmpty ()) {
	ClientSession * const peer_session = subscriber_queue.getFirst ();

	// TODO logD_ (Hex (peer_session));
	logD_ (_func, "peer session: 0x", fmt_hex, (UintPtr) peer_session);

	subscriber_queue.remove (peer_session);
	peer_session->queued = false;

	client_session->peer_session = peer_session;
	peer_session->peer_session = client_session;

	--subscribers_waiting;
    } else {
	logD_ (_func, "queueing");

	broadcaster_queue.append (client_session);
	client_session->queued = true;

	++broadcasters_waiting;
    }

    return Result::Success;
}

Result startWatching (ConstMemory const & /* stream_name */,
		      void * const _client_session)
{
    logD_ (_func_);
    dumpStats ();

    ClientSession * const client_session = static_cast <ClientSession*> (_client_session);
    client_session->client_type = ClientSession::Subscriber;

    assert (!client_session->queued &&
	    !client_session->peer_session);

    if (!broadcaster_queue.isEmpty ()) {
	ClientSession * const peer_session = broadcaster_queue.getFirst ();

	logD_ (_func, "peer session: 0x", fmt_hex, (UintPtr) peer_session, ", ",
	       "frame_saver 0x", fmt_hex, (UintPtr) &peer_session->frame_saver);

	broadcaster_queue.remove (peer_session);
	peer_session->queued = false;

	client_session->peer_session = peer_session;
	peer_session->peer_session = client_session;

	client_session->rtmp_server.sendInitialMessages_unlocked (&peer_session->frame_saver);

	--broadcasters_waiting;
    } else {
	logD_ (_func, "queueing");

	subscriber_queue.append (client_session);
	client_session->queued = true;

	++subscribers_waiting;
    }

    return Result::Success;
}

RtmpServer::Frontend const rtmp_server_frontend = {
    NULL /* connect */,
    startStreaming,
    startWatching,
    NULL /* commandMessage */
};

Result handshakeComplete (void * const /* _client_session */)
{
    logD_ (_func_);
    return Result::Success;
}

Result commandMessage (VideoStream::Message   * const mt_nonnull msg,
		       Uint32                   const msg_stream_id,
		       AmfEncoding              const amf_encoding,
		       void                   * const _client_session)
{
    logD_ (_func_);

    ClientSession * const client_session = static_cast <ClientSession*> (_client_session);

    return client_session->rtmp_server.commandMessage (msg, msg_stream_id, amf_encoding);
}

Result audioMessage (VideoStream::AudioMessage * const mt_nonnull msg,
		     void                      * const _client_session)
{
    logD (msg, _func, "timestamp: ", msg->timestamp);

    ClientSession * const client_session = static_cast <ClientSession*> (_client_session);

    if (client_session->peer_session) {
#ifdef MOMENT_VIDEOCHATD__ENABLE_AUDIO
	client_session->peer_session->rtmp_server.sendAudioMessage (msg);
#endif
    } else
	logD (msg, _func, "no peer");

    return Result::Success;
}

Result videoMessage (VideoStream::VideoMessage * const mt_nonnull msg,
		     void                      * const _client_session)
{
    logD (msg, _func, "timestamp: ", msg->timestamp);

    ClientSession * const client_session = static_cast <ClientSession*> (_client_session);

    client_session->frame_saver.processVideoFrame (msg);

    if (client_session->peer_session) {
	logD (msg, _func, "sending, ts ", msg->timestamp, ", ", toString (msg->codec_id), ", ", toString (msg->frame_type));
	client_session->peer_session->rtmp_server.sendVideoMessage (msg);
    } else
	logD (msg, _func, "no peer");

    return Result::Success;
}

void closed (Exception * const /* exc_ */,
	     void      * const _client_session)
{
    logD_ (_func, "client_session 0x", fmt_hex, (UintPtr) _client_session);

    ClientSession * const client_session = static_cast <ClientSession*> (_client_session);
    if (client_session->queued) {
	switch (client_session->client_type) {
	    case ClientSession::Subscriber:
		subscriber_queue.remove (client_session);
		--subscribers_waiting;
		break;
	    case ClientSession::Broadcaster:
		broadcaster_queue.remove (client_session);
		--broadcasters_waiting;
		break;
	    default:
		unreachable ();
	}
	client_session->queued = false;
    }

    if (client_session->peer_session) {
	client_session->peer_session->peer_session = NULL;
	client_session->peer_session->rtmp_conn->closeAfterFlush ();
    }

    client_session->unref ();
}

RtmpConnection::Frontend const rtmp_frontend = {
    handshakeComplete,
    commandMessage,
    audioMessage,
    videoMessage,
    NULL /* sendStateChanged */,
    closed
};

Result clientConnected (RtmpConnection  * mt_nonnull const rtmp_conn,
			IpAddress const & /* client_addr */,
			void            * const /* cb_data */)
{
    logD_ (_func_);

    Ref<ClientSession> const client_session = grab (new ClientSession);
    client_session->rtmp_conn = rtmp_conn;

    client_session->rtmp_server.setFrontend (Cb<RtmpServer::Frontend> (
	    &rtmp_server_frontend,
	    static_cast <ClientSession*> (client_session),
	    client_session));
    client_session->rtmp_server.setRtmpConnection (rtmp_conn);

    rtmp_conn->setFrontend (Cb<RtmpConnection::Frontend> (
	    &rtmp_frontend,
	    (void*) (ClientSession*) client_session,
	    client_session));
    rtmp_conn->startServer ();

    client_session->ref ();

    return Result::Success;
}

RtmpVideoService::Frontend const rtmp_video_service_frontend = {
    clientConnected
};

// TODO print exceptions.
Result
runVideoChat ()
{
    {
	ConstMemory const config_filename = options.config_filename ?
						    options.config_filename->mem() :
						    ConstMemory ("videochatd.conf");
	if (!MConfig::parseConfig (config_filename, &config)) {
	    logE_ (_func, "Failed to parse config file ", config_filename);
	    return Result::Failure;
	}
    }
    config.dump (logs);

    if (!server_app.init ()) {
	logE_ (_func, "ServerApp::init() failed: ", exc->toString());
	return Result::Failure;
    }

    {
	rtmp_service.setFrontend (Cb<RtmpVideoService::Frontend> (&rtmp_video_service_frontend, NULL, NULL));

	rtmp_service.setServerContext (server_app.getServerContext());
	rtmp_service.setPagePool (&page_pool);

	if (!rtmp_service.init ())
	    return Result::Failure;

	IpAddress addr;
	{
	    ConstMemory rtmp_bind = config.getString_default ("rtmp/bind", ":1935");
	    logD_ (_func, "rtmp_bind: ", rtmp_bind);
	    if (!rtmp_bind.isNull ()) {
		if (!setIpAddress_default (rtmp_bind,
					   ConstMemory() /* default_host */,
					   1935          /* default_port */,
					   true          /* allow_any_host */,
					   &addr))
		{
		    logE_ (_func, "setIpAddress_default() failed (rtmp)");
		    return Result::Failure;
		}

		if (!rtmp_service.bind (addr))
		    return Result::Failure;

		if (!rtmp_service.start ())
		    return Result::Failure;
	    }
	}
    }

    {
	rtmpt_service.setFrontend (Cb<RtmpVideoService::Frontend> (&rtmp_video_service_frontend, NULL, NULL));

	rtmpt_service.setTimers (server_app.getTimers());
	rtmpt_service.setPollGroup (server_app.getMainPollGroup());
	rtmpt_service.setPagePool (&page_pool);

	if (!rtmpt_service.init (30 /* session_keepalive_timeout */, false /* no_keepalive_conns */))
	    return Result::Failure;

	IpAddress addr;
	{
	    ConstMemory rtmpt_bind = config.getString_default ("rtmpt/bind", ":8081");
	    logD_ (_func, "rtmpt_bind: ", rtmpt_bind);
	    if (!rtmpt_bind.isNull ()) {
		logD_ (_func, "rtmpt_bind is non-null", rtmpt_bind);

		if (!setIpAddress_default (rtmpt_bind,
					   ConstMemory() /* default_host */,
					   8081          /* default_port */,
					   true          /* allow_any_host */,
					   &addr))
		{
		    logE_ (_func, "setIpAddress_default() failed (rtmpt)");
		    return Result::Failure;
		}

		if (!rtmpt_service.bind (addr))
		    return Result::Failure;

		if (!rtmpt_service.start ())
		    return Result::Failure;
	    }
	}
    }

    if (!server_app.run ()) {
	logE_ (_func, "ServerApp::run() failed: ", exc->toString());
	return Result::Failure;
    }

    return Result::Success;
}

}

int main (int argc, char **argv)
{
    MyCpp::myCppInit ();
    libMaryInit ();

    {
	unsigned const num_opts = 2;
	MyCpp::CmdlineOption opts [num_opts];

	opts [0].short_name = "h";
	opts [0].long_name  = "help";
	opts [0].with_value = false;
	opts [0].opt_data   = NULL;
	opts [0].opt_callback = cmdline_help;

	opts [1].short_name = "c";
	opts [1].long_name  = "config";
	opts [1].with_value = true;
	opts [1].opt_data   = NULL;
	opts [1].opt_callback = cmdline_config;

	MyCpp::ArrayIterator<MyCpp::CmdlineOption> opts_iter (opts, num_opts);
	MyCpp::parseCmdline (&argc, &argv, opts_iter, NULL /* callback */, NULL /* callbackData */);
    }

    if (options.help) {
	printUsage ();
	return 0;
    }

    if (!runVideoChat ())
	return EXIT_FAILURE;

    return 0;
}

