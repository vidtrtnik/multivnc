/* 
   JNIVNCConn.cpp: JNI VNC connection class implemtation.

   This file is part of MultiVNC, a multicast-enabled crossplatform 
   VNC viewer.
 
   Copyright (C) 2009 - 2011 Christian Beier <dontmind@freeshell.org>
 
   MultiVNC is free software; you can redistribute it and/or modify 
   it under the terms of the GNU General Public License as published by 
   the Free Software Foundation; either version 2 of the License, or 
   (at your option) any later version. 
 
   MultiVNC is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of 
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
   GNU General Public License for more details. 
 
   You should have received a copy of the GNU General Public License 
   along with this program; if not, write to the Free Software 
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
*/



#include <jni.h>
#include <stdarg.h>
#include <cerrno>
#include <sstream>
#include <android/log.h>

#include "JNIVNCConn.h"


#define TAG "JNIVNCConn"

// use some global address
#define VNCCONN_OBJ_ID (void*)VNCConn::thread_got_update

// logfile name
#define LOGFILE _T("MultiVNC.log")


// pixelformat defaults
// seems 8,3,4 and 5,3,2 are possible with rfbGetClient()
#define BITSPERSAMPLE 8
#define SAMPLESPERPIXEL 3
#define BYTESPERPIXEL 4 



#ifdef LIBVNCSERVER_WITH_CLIENT_TLS
bool VNCConn::TLS_threading_initialized;
#endif




/*
  constructor/destructor
*/


VNCConn::VNCConn(void* p)
{
  // save our caller
  parent = p;
  
  cl = 0;
  multicastStatus = 0;

  rfbClientLog = rfbClientErr = thread_logger;

#ifdef LIBVNCSERVER_WITH_CLIENT_TLS
  /* we're using threads in here, tell libgcrypt before TLS 
     gets initialized by libvncclient! */
  if(! TLS_threading_initialized)
    {
      wxLogDebug(wxT("Initialized libgcrypt threading."));
      gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_wx);
      gcry_check_version (NULL);
      gcry_control (GCRYCTL_DISABLE_SECMEM, 0);
      gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
      TLS_threading_initialized = true;
    }
#endif

  // fastrequest stuff
  fastrequest_interval = 0;

}




VNCConn::~VNCConn()
{
  Shutdown();
  Cleanup();
  __android_log_print(ANDROID_LOG_DEBUG, TAG, "VNCConn %p: I'm dead!", this);
}




/*
  private members
*/



rfbBool VNCConn::alloc_framebuffer(rfbClient* client)
{
  // get VNCConn object belonging to this client
  VNCConn* conn = (VNCConn*) rfbClientGetClientData(client, VNCCONN_OBJ_ID); 

  __android_log_print(ANDROID_LOG_DEBUG, TAG, "VNCConn %p: alloc'ing framebuffer w:%i, h:%i", conn, client->width, client->height);

  // assert 32bpp, as requested with GetClient() in Init()
  if(client->format.bitsPerPixel != 32)
    {
      conn->err = "Failure setting up framebuffer: wrong BPP!";
      return false;
    }

  // ensure that we get the whole framebuffer in case of a resize!
  client->updateRect.x = client->updateRect.y = 0;
  client->updateRect.w = client->width; client->updateRect.h = client->height;

  // free
  if(client->frameBuffer)
    free(client->frameBuffer);

  // alloc, zeroed
  client->frameBuffer = (uint8_t*)calloc(1, client->width*client->height*client->format.bitsPerPixel/8);

  // notify our parent
 // FIXME conn->thread_post_fbresize_notify();
 
  return client->frameBuffer ? TRUE : FALSE;
}


#if 0

wxThread::ExitCode VNCConn::Entry()
{
  int i=0;

  pointerEvent pe;
  keyEvent ke = {0, 0};

  while(! GetThread()->TestDestroy()) 
    {
      if(thread_listenmode)
	{
	  i=listenForIncomingConnectionsNoFork(cl, 100000); // 100 ms
	  if(i<0)
	    {
	      if(errno==EINTR)
		continue;
	      wxLogDebug(wxT("VNCConn %p: vncthread listen() failed"), this);
	      thread_post_disconnect_notify();
	      break;
	    }
	  if(i)
	    {
	      thread_post_incomingconnection_notify();
	      break;
	    }
	}
      else
	{
	  // userinput replay here
	  {
	    wxCriticalSectionLocker lock(mutex_recordreplay);

	    if(replaying)
	      {
		if(userinput_pos < userinput.GetCount()) // still recorded input there
		  {
		    wxString ui_now = userinput[userinput_pos];
		    // get timestamp and strip it from string
		    long ts = wxAtol(ui_now.BeforeFirst(','));
		    ui_now = ui_now.AfterFirst(',');

		    if(ts <= recordreplay_stopwatch.Time()) // past or now, process it
		      {
			// get type
			wxString type = ui_now.BeforeFirst(',');
			ui_now = ui_now.AfterFirst(',');

			if(type == wxT("p"))
			  {
			    // get pointer x,y, buttmask
			    int x = wxAtoi(ui_now.BeforeFirst(','));
			    ui_now = ui_now.AfterFirst(',');
			    int y = wxAtoi(ui_now.BeforeFirst(','));
			    ui_now = ui_now.AfterFirst(',');
			    int bmask = wxAtoi(ui_now);

			    // and send
			    SendPointerEvent(cl, x, y, bmask);
			  }

			if(type == wxT("k"))
			  {
			    // get keysym
			    rfbKeySym keysym = wxAtoi(ui_now.BeforeFirst(','));
			    ui_now = ui_now.AfterFirst(',');
			    bool down = wxAtoi(ui_now); 

			    // and send 
			    SendKeyEvent(cl, keysym, down);
			  }

			// advance to next input
			++userinput_pos;
		      }
		  }
		else if (replay_loop)
		  {
		    userinput_pos = 0; // rewind
		    recordreplay_stopwatch.Start(); // restart
		  }
		else
		  {
		    replaying = false; // all done
		    thread_post_replayfinished_notify();
		  }

	      }
	  }

	  // send everything that's inside the input queues
	  while(pointer_event_q.ReceiveTimeout(0, pe) != wxMSGQUEUE_TIMEOUT) // timeout == empty
	    thread_send_pointer_event(pe);
	  while(key_event_q.ReceiveTimeout(0, ke) != wxMSGQUEUE_TIMEOUT) // timeout == empty
	    thread_send_key_event(ke);

	  {
	    wxCriticalSectionLocker lock(mutex_stats);
	    if(latency_test_trigger)
	      {
		latency_test_trigger = false;
		thread_send_latency_probe();
	      }
	  }
	 
	  if(fastrequest_interval && (size_t)fastrequest_stopwatch.Time() > fastrequest_interval)
	    {
	      if(isMulticast())
		SendMulticastFramebufferUpdateRequest(cl, TRUE);
	      else
		SendFramebufferUpdateRequest(cl, 0, 0, cl->width, cl->height, TRUE);

	      fastrequest_stopwatch.Start(); // restart
	    }


	  // request update and handle response 
	  if(!rfbProcessServerMessage(cl, 500))
	    {
	      if(errno == EINTR)
		continue;
	      wxLogDebug(wxT("VNCConn %p: vncthread rfbProcessServerMessage() failed"), this);
	      thread_post_disconnect_notify();
	      break;
	    }

	  
	  /*
	    Compute nacked/loss ratio: We take a ratio sample every second and put it into a sample queue
	    of size N. Action is taken when the average sample value of the whole buffer exceeds a per-action
	    limit. This has advantages over taking a sample every N seconds: First, it's able to catch say a 5sec burst
	    that could be missed by two adjacent 10sec samples (one catches 2sec, the next one 3sec - no action triggered
	    although condition present). Second, this way we're able to show a value to the user every second independent
	    of the sample time frame.
	  */
   	  if(isMulticast() && multicastratio_stopwatch.Time() >= 1000)
	    {
	      // restart
	      multicastratio_stopwatch.Start();
  
	      /*
		take sample
	      */
	      {
		// the fifos are read by the GUI thread as well!
		wxCriticalSectionLocker lock(mutex_multicastratio);
		
		if(multicastNACKedRatios.size() >= MULTICAST_RATIO_SAMPLES) // make room if size exceeded
		  multicastNACKedRatios.pop_front();
		if(cl->multicastPktsRcvd + cl->multicastPktsNACKed > 0)
		  multicastNACKedRatios.push_back(cl->multicastPktsNACKed/(double)(cl->multicastPktsRcvd + cl->multicastPktsNACKed));
		else
		  multicastNACKedRatios.push_back(-1); // nothing to measure, add invalid marker

		if(multicastLossRatios.size() >= MULTICAST_RATIO_SAMPLES) // make room if size exceeded
		  multicastLossRatios.pop_front();
		if(cl->multicastPktsRcvd + cl->multicastPktsLost > 0)
		  multicastLossRatios.push_back(cl->multicastPktsLost/(double)(cl->multicastPktsRcvd + cl->multicastPktsLost));
		else
		  multicastLossRatios.push_back(-1); // nothing to measure, add invalid marker

		// reset the values we sample
		cl->multicastPktsRcvd = cl->multicastPktsNACKed = cl->multicastPktsLost = 0;
	      }

	      /*
		And act accordingly, but only after the ratio deques are at least half full.
		When a client joins a multicast group with heavy traffic going on, it will lose
		a lot of packets in the very beginning because there is a considerable time
		amount between it's multicast socket creation and the first read. Thus, the socket
		buffer is likely to overflow in this start situation, resulting in packet loss.
	      */
	      if(multicastLossRatios.size() >= MULTICAST_RATIO_SAMPLES/2)
		{
		  if(getMCLossRatio() > 0.5)
		    {
		      rfbClientLog("MultiVNC: loss ratio > 0.5, falling back to unicast\n");
		      wxLogDebug(wxT("VNCConn %p: multicast loss ratio > 0.5, falling back to unicast"), this);
		      cl->multicastDisabled = TRUE;
		      SendFramebufferUpdateRequest(cl, 0, 0, cl->width, cl->height, FALSE);
		    }
		  else if(getMCLossRatio() > 0.2)
		    {
		      rfbClientLog("MultiVNC: loss ratio > 0.2, requesting a full multicast framebuffer update\n");
		      SendMulticastFramebufferUpdateRequest(cl, FALSE);
		      cl->multicastPktsLost /= 2;
		    }
		}
	    }



	  int now = isMulticast();
	  if(now != multicastStatus)
	    {
	      multicastStatus = now;
	      thread_post_unimultichanged_notify();
	    }
	}
    }
  
  return 0;
}





bool VNCConn::thread_send_pointer_event(pointerEvent &event)
{
  int buttonmask = 0;

  if(event.LeftIsDown())
    buttonmask |= rfbButton1Mask;

  if(event.MiddleIsDown())
    buttonmask |= rfbButton2Mask;
  
  if(event.RightIsDown())
    buttonmask |= rfbButton3Mask;

  if(event.GetWheelRotation() > 0)
    buttonmask |= rfbWheelUpMask;

  if(event.GetWheelRotation() < 0)
    buttonmask |= rfbWheelDownMask;

  if(event.Entering() && ! cuttext.IsEmpty())
    {
      wxCriticalSectionLocker lock(mutex_cuttext); // since cuttext can be set from the main thread
      // if encoding fails, a NULL pointer is returned!
      if(cuttext.mb_str(wxCSConv(wxT("iso-8859-1"))))
	{
	  wxLogDebug(wxT("VNCConn %p: sending cuttext: '%s'"), this, cuttext.c_str());
	  char* encoded_text = strdup(cuttext.mb_str(wxCSConv(wxT("iso-8859-1"))));
	  SendClientCutText(cl, encoded_text, strlen(encoded_text));
	  free(encoded_text);	  
	}
      else
	wxLogDebug(wxT("VNCConn %p: sending cuttext FAILED, could not convert '%s' to ISO-8859-1"), this, cuttext.c_str());
    }

  // record here
  {
    wxCriticalSectionLocker lock(mutex_recordreplay);

    if(recording)
      {
	wxString ui_now;
	ui_now += (wxString() << (int)recordreplay_stopwatch.Time()); 
	ui_now += wxT(",");
	ui_now += wxT("p"); // is pointer event
	ui_now += wxT(",");
	ui_now += (wxString() << event.m_x);
	ui_now += wxT(",");
	ui_now += (wxString() << event.m_y);
	ui_now += wxT(",");
	ui_now += (wxString() << buttonmask);

	userinput.Add(ui_now);
      }
  }

  wxLogDebug(wxT("VNCConn %p: sending pointer event at (%d,%d), buttonmask %d"), this, event.m_x, event.m_y, buttonmask);
  return SendPointerEvent(cl, event.m_x, event.m_y, buttonmask);
}



bool VNCConn::thread_send_key_event(keyEvent &event)
{
  // record here
  {
    wxCriticalSectionLocker lock(mutex_recordreplay);

    if(recording)
      {
	wxString ui_now;
	ui_now += (wxString() << (int)recordreplay_stopwatch.Time()); 
	ui_now += wxT(",");
	ui_now += wxT("k"); // is key event
	ui_now += wxT(",");
	ui_now += (wxString() << event.keysym);
	ui_now += wxT(",");
	ui_now += (wxString() << event.down);

	userinput.Add(ui_now);
      }
  }

  return SendKeyEvent(cl, event.keysym, event.down);
}




void VNCConn::thread_post_incomingconnection_notify() 
{
  wxLogDebug(wxT("VNCConn %p: post_incomingconnection_notify()"), this);

  // new NOTIFY event, we got no window id
  wxCommandEvent event(VNCConnIncomingConnectionNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender

  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}


void VNCConn::thread_post_disconnect_notify() 
{
  wxLogDebug(wxT("VNCConn %p: post_disconnect_notify()"), this);

  // new NOTIFY event, we got no window id
  wxCommandEvent event(VNCConnDisconnectNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender

  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}


void VNCConn::thread_post_update_notify(int x, int y, int w, int h)
{
  VNCConnUpdateNotifyEvent event(VNCConnUpdateNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender

  // set info about what was updated
  event.rect = wxRect(x, y, w, h);
  wxLogDebug(wxT("VNCConn %p: post_update_notify(%i,%i,%i,%i)"), this,
	     event.rect.x,
	     event.rect.y,
	     event.rect.width,
	     event.rect.height);
  
  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}


void VNCConn::thread_post_fbresize_notify() 
{
  wxLogDebug(wxT("VNCConn %p: post_fbresize_notify() (%i, %i)"), 
	     this,
	     getFrameBufferWidth(),
	     getFrameBufferHeight());

  // new NOTIFY event, we got no window id
  wxCommandEvent event(VNCConnFBResizeNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender

  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}



void VNCConn::thread_post_cuttext_notify()
{
  // new NOTIFY event, we got no window id
  wxCommandEvent event(VNCConnCuttextNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender

  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}



void VNCConn::thread_post_bell_notify()
{
  // new NOTIFY event, we got no window id
  wxCommandEvent event(VNCConnBellNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender

  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}



void VNCConn::thread_post_unimultichanged_notify()
{
  wxCommandEvent event(VNCConnUniMultiChangedNOTIFY, wxID_ANY);
  event.SetEventObject(this); // set sender
  
  // Send it
  wxPostEvent((wxEvtHandler*)parent, event);
}


void VNCConn::thread_got_update(rfbClient* client,int x,int y,int w,int h)
{
	VNCConn* conn = (VNCConn*) rfbClientGetClientData(client, VNCCONN_OBJ_ID);
	if(! conn->GetThread()->TestDestroy())
	{
		conn->updated_rect.Union(wxRect(x, y, w, h));

		// single (partial) multicast updates are small, so when a big region is updated,
		// the update notify receiver gets flooded, resulting in way too much cpu load.
		// thus, when multicasting, we only notify for logic (whole) framebuffer updates.
		if(!conn->isMulticast())
			conn->thread_post_update_notify(x, y, w, h);


	}
}




void VNCConn::thread_update_finished(rfbClient* client)
{
  VNCConn* conn = (VNCConn*) rfbClientGetClientData(client, VNCCONN_OBJ_ID);
  if(! conn->GetThread()->TestDestroy())
    {
      // single (partial) multicast updates are small, so when a big region is updated,
      // the update notify receiver gets flooded, resulting in way too much cpu load.
      // thus, when multicasting, we only notify for logic (whole) framebuffer updates.
      if(conn->isMulticast() && !conn->updated_rect.IsEmpty())
	conn->thread_post_update_notify(conn->updated_rect.x, conn->updated_rect.y, conn->updated_rect.width, conn->updated_rect.height);

      conn->updated_rect = wxRect();

    }
}



void VNCConn::thread_kbd_leds(rfbClient* cl, int value, int pad)
{
  VNCConn* conn = (VNCConn*) rfbClientGetClientData(cl, VNCCONN_OBJ_ID); 
  wxLogDebug(wxT("VNCConn %p: Led State= 0x%02X"), conn, value);
}


void VNCConn::thread_textchat(rfbClient* cl, int value, char *text)
{
  VNCConn* conn = (VNCConn*) rfbClientGetClientData(cl, VNCCONN_OBJ_ID); 
  switch(value)
    {
    case rfbTextChatOpen:
      wxLogDebug(wxT("VNCConn %p: got textchat open\n"), conn);
      break;
    case rfbTextChatClose:
      wxLogDebug(wxT("VNCConn %p: got textchat close\n"), conn);
      break;
    case rfbTextChatFinished:
      wxLogDebug(wxT("VNCConn %p: got textchat finish\n"), conn);
      break;
    default:
      wxLogDebug(wxT("VNCConn %p: got textchat text: '%s'\n"), conn, text);
    }
}


void VNCConn::thread_got_cuttext(rfbClient *cl, const char *text, int len)
{
  VNCConn* conn = (VNCConn*) rfbClientGetClientData(cl, VNCCONN_OBJ_ID);

  wxLogDebug(wxT("VNCConn %p: got cuttext: '%s'"), conn, wxString(text, wxCSConv(wxT("iso-8859-1"))).c_str());

  wxCriticalSectionLocker lock(conn->mutex_cuttext); // since cuttext can also be set from the main thread
  conn->cuttext = wxString(text, wxCSConv(wxT("iso-8859-1")));
  conn->thread_post_cuttext_notify();
}


void VNCConn::thread_bell(rfbClient *cl)
{
  VNCConn* conn = (VNCConn*) rfbClientGetClientData(cl, VNCCONN_OBJ_ID);
  wxLogDebug(wxT("VNCConn %p: bell"), conn);
  conn->thread_post_bell_notify();
}

#endif


// there's no per-connection log since we cannot find out which client
// called the logger function :-(
vector<string> VNCConn::log;
pthread_mutex_t VNCConn::mutex_log;

void VNCConn::thread_logger(const char *format, ...)
{
  if(!rfbEnableClientLogging)
    return;

  // since we're accessing some global things here from different threads
  pthread_mutex_lock(&mutex_log);

  va_list args;

  // global log string array
  va_start(args, format);
  char msg[1024];
  vsnprintf(msg, 1024, format, args);
  log.push_back(msg);
  va_end(args);

 
  // and stderr
  va_start(args, format);
  __android_log_vprint(ANDROID_LOG_INFO, TAG, format, args);
  va_end(args);

  pthread_mutex_unlock(&mutex_log);
}

#if 0



/*
  public members
*/

bool VNCConn::Setup(char* (*getpasswdfunc)(rfbClient*))
{
  wxLogDebug(wxT("VNCConn %p: Setup()"), this);

  if(cl) // already set up
    {
      wxLogDebug(wxT("VNCConn %p: Setup already done. Call Cleanup() first!"), this);
      return false;
    }

  // this takes (int bitsPerSample,int samplesPerPixel, int bytesPerPixel) 
  // 5,3,2 and 8,3,4 seem possible
  cl=rfbGetClient(BITSPERSAMPLE, SAMPLESPERPIXEL, BYTESPERPIXEL);
 
  rfbClientSetClientData(cl, VNCCONN_OBJ_ID, this); 

  // callbacks
  cl->MallocFrameBuffer = alloc_framebuffer;
  cl->GotFrameBufferUpdate = thread_got_update;
  cl->FinishedFrameBufferUpdate = thread_update_finished;
  cl->GetPassword = getpasswdfunc;
  cl->HandleKeyboardLedState = thread_kbd_leds;
  cl->HandleTextChat = thread_textchat;
  cl->GotXCutText = thread_got_cuttext;
  cl->Bell = thread_bell;
  cl->HandleXvpMsg = thread_handle_xvp;

  cl->canHandleNewFBSize = TRUE;

  return true;
}


void VNCConn::Cleanup()
{
  wxLogDebug(wxT( "VNCConn %p: Cleanup()"), this);

  if(cl)
    {
      wxLogDebug(wxT( "VNCConn %p: Cleanup() before client cleanup"), this);
      rfbClientCleanup(cl);
      cl = 0;
      wxLogDebug(wxT( "VNCConn %p: Cleanup() after client cleanup"), this);
    }
}


bool VNCConn::Listen(int port)
{
  wxLogDebug(wxT("VNCConn %p: Listen() port %d"), this, port);

  cl->listenPort = port;

  thread_listenmode = true;

  if( Create() != wxTHREAD_NO_ERROR )
    {
      err.Printf(_("Could not create VNC listener thread!"));
      Shutdown();
      return false;
    }

  if( GetThread()->Run() != wxTHREAD_NO_ERROR )
    {
      err.Printf(_("Could not start VNC listener thread!")); 
      Shutdown();
      return false;
    }

  return true;
}


bool VNCConn::Init(const wxString& host, const wxString& encodings, int compresslevel, int quality, 
		   bool multicast, int multicast_socketrecvbuf, int multicast_recvbuf)
{
  wxLogDebug(wxT("VNCConn %p: Init()"), this);

  if(cl->frameBuffer || (GetThread() && GetThread()->IsRunning()))
    {
      wxLogDebug(wxT("VNCConn %p: Init() already done. Call Shutdown() first!"), this);
      return false;
    }

  // reset stats before doing new connection
  resetStats();

  cl->programName = "VNCConn";
  cl->serverHost = strdup(host.BeforeFirst(':').mb_str()); // this one gets freed by the library
  cl->serverPort = wxAtoi(host.AfterFirst(':'));
  if(cl->serverPort < 5900)
    cl->serverPort += 5900;
  cl->appData.compressLevel = compresslevel;
  cl->appData.qualityLevel = quality;
  cl->appData.encodingsString = strdup(encodings.mb_str());
  if(multicast)
    {
      cl->canHandleMulticastVNC = TRUE;
      cl->multicastSocketRcvBufSize = multicast_socketrecvbuf*1024;
      cl->multicastRcvBufSize = multicast_recvbuf*1024;
      multicastratio_stopwatch.Start();
    }
  else
    cl->canHandleMulticastVNC = FALSE;


  if(! rfbInitClient(cl, 0, NULL))
    {
      cl = 0; //  rfbInitClient() calls rfbClientCleanup() on failure, but this does not zero the ptr
      err.Printf(_("Failure connecting to server at %s!"),  host.c_str());
      wxLogDebug(wxT("VNCConn %p: Init() failed. Cleanup by library."), this);
      Shutdown();
      return false;
    }

  // set the client sock to blocking again until libvncclient is fixed
#ifdef WIN32
  unsigned long block=0;
  if(ioctlsocket(cl->sock, FIONBIO, &block) == SOCKET_ERROR) {
    errno=WSAGetLastError();
#else
  int flags = fcntl(cl->sock, F_GETFL);
  if(flags < 0 || fcntl(cl->sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
#endif
      rfbClientErr("Setting socket to blocking failed: %s\n",strerror(errno));
    }

  
  // if there was an error in alloc_framebuffer(), catch that here
  // err is set by alloc_framebuffer()
  if(! cl->frameBuffer)
    {
      Shutdown();
      return false;
    }
  
  // this is like our main loop
  thread_listenmode = false;
  if( Create() != wxTHREAD_NO_ERROR )
    {
      err.Printf(_("Could not create VNC thread!"));
      Shutdown();
      return false;
    }

  if( GetThread()->Run() != wxTHREAD_NO_ERROR )
    {
      err.Printf(_("Could not start VNC thread!")); 
      Shutdown();
      return false;
    }

  conn_stopwatch.Start();

  return true;
}




void VNCConn::Shutdown()
{
  wxLogDebug(wxT("VNCConn %p: Shutdown()"), this);

  conn_stopwatch.Pause();

  if(GetThread() && GetThread()->IsRunning())
    {
      wxLogDebug(wxT( "VNCConn %p: Shutdown() before vncthread delete"), this);

      GetThread()->Delete(); // this blocks if thread is joinable, i.e. on stack
      wxLogDebug(wxT("VNCConn %p: Shutdown() after vncthread delete"), this);
    }
  
  if(cl)
    {
      wxLogDebug(wxT( "VNCConn %p: Shutdown() closing connection"), this);
#ifdef __WIN32__
      closesocket(cl->sock);
#else
      close(cl->sock);
#endif
      // in case we called listen, canceled that, and now want to connect to some
      // host via Init()
      cl->listenSpecified = FALSE;

      if(cl->frameBuffer)
	{
	  free(cl->frameBuffer);
	  cl->frameBuffer = 0;
	}
      // this one was strdup'ed before
      free((void*)cl->appData.encodingsString);
    }
}


void VNCConn::setFastRequest(size_t interval)
{
  fastrequest_interval = interval;
}

bool VNCConn::setDSCP(uint8_t dscp)
{
  if(cl && cl->sock >= 0)
    return SetDSCP(cl->sock, dscp);
  else
    return false;
}


// this simply posts the mouse event into the worker thread's input queue
void VNCConn::sendPointerEvent(wxMouseEvent &event)
{
  if(replaying)
    return;

  if(GetThread() && GetThread()->IsRunning())
    pointer_event_q.Post(event);
}


// because of the possible wxKeyEvent.Skip(), this posts the found keysym + down
bool VNCConn::sendKeyEvent(wxKeyEvent &event, bool down, bool isChar)
{
  if(replaying)
    return false;

  keyEvent kev = {0,0};

  if(isChar)
    {
      wxLogDebug(wxT("VNCConn %p: got CHAR key event"), this);
      wxLogDebug(wxT("VNCConn %p:     wxkeycode:      %d"), this, event.GetKeyCode());
      wxLogDebug(wxT("VNCConn %p:     wxkeycode char: %c"), this, event.GetKeyCode());
      wxLogDebug(wxT("VNCConn %p:     unicode:        %d"), this, event.GetUnicodeKey());
      wxLogDebug(wxT("VNCConn %p:     unicode char:   %c"), this, event.GetUnicodeKey()); 

      // get tranlated char
      kev.keysym = event.GetKeyCode();
      // if we got ne keycode, try unicodekey
      if(kev.keysym==0) 
	kev.keysym = event.GetUnicodeKey();

      // if wxwidgets translates a key combination into a
      // value below 32, revert this here.
      // we dont't send ASCII 0x03, but ctrl and then a 'c'!
      if(kev.keysym <= 32)
	{
	  kev.keysym += 96;
	  wxLogDebug(wxT("VNCConn %p: translating key to: %d"), this, kev.keysym);
	}

      wxLogDebug(wxT("VNCConn %p: sending rfbkeysym: 0x%.3x down"), this, kev.keysym);
      wxLogDebug(wxT("VNCConn %p: sending rfbkeysym: 0x%.3x  up"), this, kev.keysym);
      
      // down, then up
      if(GetThread() && GetThread()->IsRunning())
	{
	  kev.down = true;
	  key_event_q.Post(kev);
	  kev.down = false;
	  key_event_q.Post(kev);
	}
      return true;
    }
  else
    {
      // lookup keysym  
      switch(event.GetKeyCode()) 
	{
	case WXK_BACK: kev.keysym = XK_BackSpace; break;
	case WXK_TAB: kev.keysym = XK_Tab; break;
	case WXK_CLEAR: kev.keysym = XK_Clear; break;
	case WXK_RETURN: kev.keysym = XK_Return; break;
	case WXK_PAUSE: kev.keysym = XK_Pause; break;
	case WXK_ESCAPE: kev.keysym = XK_Escape; break;
	case WXK_SPACE: kev.keysym = XK_space; break;
	case WXK_DELETE: kev.keysym = XK_Delete; break;
	case WXK_NUMPAD0: kev.keysym = XK_KP_0; break;
	case WXK_NUMPAD1: kev.keysym = XK_KP_1; break;
	case WXK_NUMPAD2: kev.keysym = XK_KP_2; break;
	case WXK_NUMPAD3: kev.keysym = XK_KP_3; break;
	case WXK_NUMPAD4: kev.keysym = XK_KP_4; break;
	case WXK_NUMPAD5: kev.keysym = XK_KP_5; break;
	case WXK_NUMPAD6: kev.keysym = XK_KP_6; break;
	case WXK_NUMPAD7: kev.keysym = XK_KP_7; break;
	case WXK_NUMPAD8: kev.keysym = XK_KP_8; break;
	case WXK_NUMPAD9: kev.keysym = XK_KP_9; break;
	case WXK_NUMPAD_DECIMAL: kev.keysym = XK_KP_Decimal; break;
	case WXK_NUMPAD_DIVIDE: kev.keysym = XK_KP_Divide; break;
	case WXK_NUMPAD_MULTIPLY: kev.keysym = XK_KP_Multiply; break;
	case WXK_NUMPAD_SUBTRACT: kev.keysym = XK_KP_Subtract; break;
	case WXK_NUMPAD_ADD: kev.keysym = XK_KP_Add; break;
	case WXK_NUMPAD_ENTER: kev.keysym = XK_KP_Enter; break;
	case WXK_NUMPAD_EQUAL: kev.keysym = XK_KP_Equal; break;
	case WXK_UP: kev.keysym = XK_Up; break;
	case WXK_DOWN: kev.keysym = XK_Down; break;
	case WXK_RIGHT: kev.keysym = XK_Right; break;
	case WXK_LEFT: kev.keysym = XK_Left; break;
	case WXK_INSERT: kev.keysym = XK_Insert; break;
	case WXK_HOME: kev.keysym = XK_Home; break;
	case WXK_END: kev.keysym = XK_End; break;
	case WXK_PAGEUP: kev.keysym = XK_Page_Up; break;
	case WXK_PAGEDOWN: kev.keysym = XK_Page_Down; break;
	case WXK_F1: kev.keysym = XK_F1; break;
	case WXK_F2: kev.keysym = XK_F2; break;
	case WXK_F3: kev.keysym = XK_F3; break;
	case WXK_F4: kev.keysym = XK_F4; break;
	case WXK_F5: kev.keysym = XK_F5; break;
	case WXK_F6: kev.keysym = XK_F6; break;
	case WXK_F7: kev.keysym = XK_F7; break;
	case WXK_F8: kev.keysym = XK_F8; break;
	case WXK_F9: kev.keysym = XK_F9; break;
	case WXK_F10: kev.keysym = XK_F10; break;
	case WXK_F11: kev.keysym = XK_F11; break;
	case WXK_F12: kev.keysym = XK_F12; break;
	case WXK_F13: kev.keysym = XK_F13; break;
	case WXK_F14: kev.keysym = XK_F14; break;
	case WXK_F15: kev.keysym = XK_F15; break;
	case WXK_NUMLOCK: kev.keysym = XK_Num_Lock; break;
	case WXK_CAPITAL: kev.keysym = XK_Caps_Lock; break;
	case WXK_SCROLL: kev.keysym = XK_Scroll_Lock; break;
	  //case WXK_RSHIFT: kev.keysym = XK_Shift_R; break;
	case WXK_SHIFT: kev.keysym = XK_Shift_L; break;
	  //case WXK_RCTRL: kev.keysym = XK_Control_R; break;
	case WXK_CONTROL: kev.keysym = XK_Control_L; break;
	  //    case WXK_RALT: kev.keysym = XK_Alt_R; break;
	case WXK_ALT: kev.keysym = XK_Alt_L; break;
	  // case WXK_RMETA: kev.keysym = XK_Meta_R; break;
	  // case WXK_META: kev.keysym = XK_Meta_L; break;
	case WXK_WINDOWS_LEFT: kev.keysym = XK_Super_L; break;	
	case WXK_WINDOWS_RIGHT: kev.keysym = XK_Super_R; break;	
	  //case WXK_COMPOSE: kev.keysym = XK_Compose; break;
	  //case WXK_MODE: kev.keysym = XK_Mode_switch; break;
	case WXK_HELP: kev.keysym = XK_Help; break;
	case WXK_PRINT: kev.keysym = XK_Print; break;
	  //case WXK_SYSREQ: kev.keysym = XK_Sys_Req; break;
	case WXK_CANCEL: kev.keysym = XK_Break; break;
	default: break;
	}

      if(kev.keysym)
	{
	  wxLogDebug(wxT("VNCConn %p: got key %s event:"), this, down ? wxT("down") : wxT("up"));
	  wxLogDebug(wxT("VNCConn %p:     wxkeycode:      %d"), this, event.GetKeyCode());
	  wxLogDebug(wxT("VNCConn %p:     wxkeycode char: %c"), this, event.GetKeyCode());
	  wxLogDebug(wxT("VNCConn %p:     unicode:        %d"), this, event.GetUnicodeKey());
	  wxLogDebug(wxT("VNCConn %p:     unicode char:   %c"), this, event.GetUnicodeKey()); 
	  wxLogDebug(wxT("VNCConn %p: sending rfbkeysym:  0x%.3x %s"), this, kev.keysym, down ? wxT("down") : wxT("up"));

	  kev.down = down;
	  if(GetThread() && GetThread()->IsRunning())
	    key_event_q.Post(kev);
	  return true;
	}
      else
	{
	  // nothing of the above?
	  // then propagate this event through the EVT_CHAR handler
	  event.Skip();
	  return false;
	}
    }

  wxLogDebug(wxT("VNCConn %p: no matching keysym found"), this);
  return false; 

}




/*
  we could use a wxBitmap directly as the framebuffer, thus being more efficient
  BUT:
  - win32 expects DIB data in BGRA, not RGBA (can be solved by requesting another pixel format)
  - win32 DIBs expect data from bottom to top (this means we HAVE to use a self made
    framebuffer -> wxBitmap function instead of GetSubBitmap().)
  - direct data access of a wxBitmap on Mac OS X does not seem to work
  INSTEAD:
  - we have an ordinary char array as framebuffer and copy the requested content into a
    wxBitmap (the return of its copy is cheap cause wxBitmaps use copy-on-write)
 */
wxBitmap VNCConn::getFrameBufferRegion(const wxRect& rect) const
{
  // sanity check requested region
  if(rect.x < 0 || rect.x + rect.width > getFrameBufferWidth()
     || rect.y < 0 || rect.y + rect.height > getFrameBufferHeight())
       return wxBitmap();

  /*
    copy directly from framebuffer into a new bitmap
  */

  wxBitmap region(rect.width, rect.height, cl->format.bitsPerPixel);
  wxAlphaPixelData region_data(region);
  wxAlphaPixelData::Iterator region_it(region_data);

  int bytesPerPixel = cl->format.bitsPerPixel/8;
  uint8_t *fbsub_it = cl->frameBuffer + rect.y*cl->width*bytesPerPixel + rect.x*bytesPerPixel;

  for( int y = 0; y < rect.height; ++y )
    {
      wxAlphaPixelData::Iterator region_it_rowStart = region_it;
      uint8_t *fbsub_it_rowStart = fbsub_it;

      for( int x = 0; x < rect.width; ++x, ++region_it, fbsub_it += bytesPerPixel)
	{
	  region_it.Red() = *(fbsub_it+0);	  
	  region_it.Green() = *(fbsub_it+1);	  
	  region_it.Blue() = *(fbsub_it+2);	  
	  region_it.Alpha() = 255; // 255 is opaque, libvncclient always sets this byte to 0
	}
      
      // CR
      region_it = region_it_rowStart;
      fbsub_it = fbsub_it_rowStart;
      
      // LF
      region_it.OffsetY(region_data, 1);
      fbsub_it += cl->width * bytesPerPixel;
    }

  return region;
}




bool VNCConn::getFrameBufferRegion(const wxRect& rect, wxBitmap& dst) const
{
  // sanity check requested region against framebuffer
  if(rect.x < 0 || rect.x + rect.width > getFrameBufferWidth()
     || rect.y < 0 || rect.y + rect.height > getFrameBufferHeight())
       return false;

  // check dst against framebuffer
  if(dst.GetWidth() != getFrameBufferWidth() || dst.GetHeight() != getFrameBufferHeight())
    return false;

  /*
    copy directly from framebuffer into the destination bitmap
  */
  wxAlphaPixelData dst_data(dst);
  wxAlphaPixelData::Iterator dst_it(dst_data);
  dst_it.Offset(dst_data, rect.x, rect.y);

  int bytesPerPixel = cl->format.bitsPerPixel/8;
  uint8_t *fbsub_it = cl->frameBuffer + rect.y*cl->width*bytesPerPixel + rect.x*bytesPerPixel;

  for( int y = 0; y < rect.height; ++y )
    {
      wxAlphaPixelData::Iterator dst_it_rowStart = dst_it;
      uint8_t *fbsub_it_rowStart = fbsub_it;

      for( int x = 0; x < rect.width; ++x, ++dst_it, fbsub_it += bytesPerPixel)
	{
	  dst_it.Red() = *(fbsub_it+0);	  
	  dst_it.Green() = *(fbsub_it+1);	  
	  dst_it.Blue() = *(fbsub_it+2);	  
	  dst_it.Alpha() = 255; // 255 is opaque, libvncclient always sets this byte to 0
	}
      
      // CR
      dst_it = dst_it_rowStart;
      fbsub_it = fbsub_it_rowStart;
      
      // LF
      dst_it.OffsetY(dst_data, 1);
      fbsub_it += cl->width * bytesPerPixel;
    }

  return true;
}

#endif


int VNCConn::getFrameBufferWidth() const
{
  if(cl)
    return cl->width;
  else 
    return 0;
}


int VNCConn::getFrameBufferHeight() const
{
  if(cl)
    return cl->height;
  else 
    return 0;
}


int VNCConn::getFrameBufferDepth() const
{
  if(cl)
    return cl->format.bitsPerPixel;
  else 
    return 0;
}




string VNCConn::getDesktopName() const
{
  if(cl)
    return string(cl->desktopName);
  else
    return "";
}


string VNCConn::getServerHost() const
{
  if(cl)
    {
      if(cl->listenSpecified)
    	  return "";
      else
    	  return string(cl->serverHost);
    }
  else
    return "";
}



string VNCConn::getServerPort() const
{
  stringstream ss;
  if(cl)
    if(cl->listenSpecified)
      ss << cl->listenPort;
    else
      ss << cl->serverPort;

  return ss.str();
}

# if 0
void VNCConn::clearLog()
{
  // since we're accessing some global things here from different threads
  wxCriticalSectionLocker lock(mutex_log);
  log.Clear();
}
#endif

#if 0

bool VNCConn::isMulticast() const
{
  if(cl && cl->multicastSock >= 0 && !cl->multicastDisabled) 
    return true;
  else
    return false;
}


double VNCConn::getMCNACKedRatio()
{
  wxCriticalSectionLocker lock(mutex_multicastratio);
  double retval = 0;
  int samples = 0;
  std::deque<double>::const_iterator it;
  for(it = multicastNACKedRatios.begin(); it != multicastNACKedRatios.end(); ++it)
    if(*it >= 0) // valid value
      {
	retval += *it;
	++samples;
      }

  if(samples)
    return retval/samples;
  else
    return -1;
}


double VNCConn::getMCLossRatio()
{
  wxCriticalSectionLocker lock(mutex_multicastratio);
  double retval = 0;
  int samples = 0;
  std::deque<double>::const_iterator it;
  for(it = multicastLossRatios.begin(); it != multicastLossRatios.end(); ++it)
    if(*it >= 0) // valid value
      {
	retval += *it;
	++samples;
      }
    
  if(samples)
    return retval/samples;
  else
    return -1;
}




// get the OS-dependent socket receive buffer size in KByte.
// max returned value is 32MB, -1 on error
int VNCConn::getMaxSocketRecvBufSize()
{
  int sock; 

#ifdef WIN32
  WSADATA trash;
  WSAStartup(MAKEWORD(2,0),&trash);
#endif

  // create the test socket
  if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    return -1;

  // try with some high value and see what we get
  int recv_buf_try = 33554432; // 32 MB
  if(setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&recv_buf_try, sizeof(recv_buf_try)) < 0) 
    {
      close(sock);
      return -1;
    } 
  int recv_buf_got = -1;
  socklen_t recv_buf_got_len = sizeof(recv_buf_got);
  if(getsockopt(sock, SOL_SOCKET, SO_RCVBUF,(char*)&recv_buf_got, &recv_buf_got_len) <0)
    {
      close(sock);
      return -1;
    } 

  close(sock);

  return recv_buf_got/1024;
}

#endif
