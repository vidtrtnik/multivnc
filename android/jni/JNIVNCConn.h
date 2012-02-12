// -*- C++ -*- 
/* 
   JNIVNCConn.h: JNI VNC connection class API definition.

   This file is part of MultiVNC, a multicast-enabled crossplatform 
   VNC viewer.
 
   Copyright (C) 2009-2012 Christian Beier <dontmind@freeshell.org>
 
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

#ifndef VNCCONN_H
#define VNCCONN_H

#include <deque>
#include <string>
#include <vector>
#include "rfb/rfbclient.h"

using namespace std;


class VNCConn
{
public:
  VNCConn(void *parent);
  ~VNCConn(); 

  /*
    to make a connection, call
    Setup(), then
    Listen() (optional), then
    Init(), then
    Shutdown, then
    Cleanup()
    
    NB: If Init() fails, you have to call Setup() again!

    The semantic counterparts are:
       Setup() <-> Cleanup()
       Init()  <-> Shutdown()
  */

  bool Setup(char* (*getpasswdfunc)(rfbClient*));
  void Cleanup();
  bool Listen(int port);
  bool Init(const string& host, const string& encodings, int compresslevel = 1, int quality = 5,
	    bool multicast = true, int multicastSocketRecvBuf = 5120, int multicastRecvBuf = 5120);
  void Shutdown();


  /*
    This is for usage on high latency links: Keep asking for framebuffer 
    updates every 'interval' ms instead of asking after every received 
    server message.
    0 to disable. default: disabled
  */
  void setFastRequest(size_t interval);

  /*
    enables marking the DSCP/Traffic Class of outgoing IP/IPv6 packets
  */
  bool setDSCP(uint8_t dscp);

  // get kind of VNCConn
  bool isReverse() const { return cl ? cl->listenSpecified : false; };
  bool isMulticast() const;

  // send events
  void sendPointerEvent(int x, int y, int modifiers, int pointerMask);
  bool sendKeyEvent(int keycode, int metastate, bool down);
  

  // cuttext
  const string& getCuttext() const { const string& ref = cuttext; return ref; };
  void setCuttext(const string& text);

  // returns a wxBitmap (this uses COW, so is okay)
  int* getFrameBufferRegion(int x, int y, int w, int h) const;
  // writes requested region directly into dst bitmap which must have the same dimensions as the framebuffer
  bool getFrameBufferRegion(int x, int y, int w, int h, int* dst) const;
  int getFrameBufferWidth() const;
  int getFrameBufferHeight() const;
  int getFrameBufferDepth() const;

  string getDesktopName() const;
  string getServerHost() const;
  string getServerPort() const;

  /*
  // get current multicast receive buf state
  int getMCBufSize() const { if(cl) return cl->multicastRcvBufSize; else return 0; };
  int getMCBufFill() const { if(cl) return cl->multicastRcvBufLen; else return 0; };
  // returns average (over last few seconds) NACKed ratio or -1 if there was nothing to be measured
  double getMCNACKedRatio();
  // returns average (over last few seconds) loss ratio or -1 if there was nothing to be measured
  double getMCLossRatio();
  */

  // get error string
  const string& getErr() const { const string& ref = err; return ref; };
  // get global log string
  static const vector<string>& getLog() { const vector<string>& ref = log; return ref; };
  static void clearLog();

  static int getMaxSocketRecvBufSize();

private:
  void *parent;

  rfbClient* cl;

  // TODO port wxrect
  //wxRect updated_rect;

  int multicastStatus;
  deque<double> multicastNACKedRatios;
  deque<double> multicastLossRatios;
#define MULTICAST_RATIO_SAMPLES 10 // we are averaging over this many seconds
  pthread_mutex_t mutex_multicastratio; // the fifos above are read by both the VNC and the GUI thread
  //wxStopWatch  multicastratio_stopwatch;

  
#ifdef LIBVNCSERVER_WITH_CLIENT_TLS
  static bool TLS_threading_initialized;
#endif

  // this counts the ms since Init()
  //wxStopWatch conn_stopwatch;

  // fastrequest stuff
  size_t fastrequest_interval;
  //wxStopWatch fastrequest_stopwatch;

  // this contains cuttext we received or should send
  string cuttext;
  pthread_mutex_t mutex_cuttext;


  // per-connection error string
  string err;
  
  // global libvcnclient log stuff
  // there's no per-connection log since we cannot find out which client
  // called the logger function :-(
  static vector<string> log;
  static pthread_mutex_t mutex_log;
  static bool do_logfile;


  // messagequeues for posting events to the worker thread
  struct pointerEvent
  {
	  int x;
	  int y;
	  int pointerMask;
  };
  struct keyEvent
  {
    rfbKeySym keysym;
    bool down;
  };
  deque<pointerEvent> pointer_event_q;
  pthread_mutex_t mutex_pointer_event_q;
  deque<keyEvent> key_event_q;
  pthread_mutex_t mutex_key_event_q;


  bool thread_listenmode; 
  bool thread_send_pointer_event(pointerEvent &event);
  bool thread_send_key_event(keyEvent &event);
  bool thread_send_latency_probe();

  // event dispatchers
  void thread_post_incomingconnection_notify();
  void thread_post_disconnect_notify();
  void thread_post_update_notify(int x, int y, int w, int h);
  void thread_post_fbresize_notify();
  void thread_post_cuttext_notify();
  void thread_post_bell_notify();
  void thread_post_unimultichanged_notify();
  void thread_post_replayfinished_notify();

  // libvncclient callbacks
  static rfbBool alloc_framebuffer(rfbClient* client);
  static void thread_got_update(rfbClient* cl,int x,int y,int w,int h);
  static void thread_update_finished(rfbClient* client);
  static void thread_kbd_leds(rfbClient* cl, int value, int pad);
  static void thread_textchat(rfbClient* cl, int value, char *text);
  static void thread_got_cuttext(rfbClient *cl, const char *text, int len);
  static void thread_bell(rfbClient *cl);
  static void thread_handle_xvp(rfbClient *cl, uint8_t ver, uint8_t code);
  static void thread_logger(const char *format, ...);
};



#endif
