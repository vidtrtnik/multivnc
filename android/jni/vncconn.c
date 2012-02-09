/*
   vncconn.c: VNCConn.java native backend.

   This file is part of MultiVNC, a multicast-enabled crossplatform
   VNC viewer.

   Copyright (C) 2009 - 2012 Christian Beier <dontmind@freeshell.org>

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
#include <errno.h>
#include <android/log.h>
#include "rfb/rfbclient.h"

#define TAG "VNCConn-native"
#define VNCCONN_OBJ_ID (void*)thread_logger


static void setErr(jobject conn, const char* msg)
{




}



static rfbClient* getRfbClient(JNIEnv *env, jobject conn)
{
	rfbClient* cl = NULL;
	jclass cls = (*env)->GetObjectClass(env, conn);
	jfieldID fid = (*env)->GetFieldID(env, cls, "rfbClient", "J");
	if (fid == 0)
		return NULL;

	cl = (rfbClient*)(long)(*env)->GetLongField(env, conn, fid);

	return cl;
}



static jboolean setRfbClient(JNIEnv *env, jobject conn, rfbClient* cl)
{
	jclass cls = (*env)->GetObjectClass(env, conn);
	jfieldID fid = (*env)->GetFieldID(env, cls, "rfbClient", "J");
	if (fid == 0)
		return JNI_FALSE;

	(*env)->SetLongField(env, conn, fid, (long)cl);

	return JNI_TRUE;
}




/*
 * there's no per-connection log since we cannot find out which client
 * called the logger function :-(
 */
static void thread_logger(const char *format, ...)
{
	if(!rfbEnableClientLogging)
		return;

	// since we're accessing some global things here from different threads
	//wxCriticalSectionLocker lock(mutex_log);

	va_list args;

	/*wxChar timebuf[256];
	time_t log_clock;
	time(&log_clock);
	wxStrftime(timebuf, WXSIZEOF(timebuf), _T("%d/%m/%Y %X "), localtime(&log_clock));

	// global log string array
	wxString wx_format(format, wxConvUTF8);
	va_start(args, format);
	char msg[1024];
	vsnprintf(msg, 1024, format, args);
	log.Add( wxString(timebuf) + wxString(msg, wxConvUTF8));
	va_end(args);
*/


	// and stderr
	va_start(args, format);
	//fprintf(stderr, wxString(timebuf).mb_str());
	__android_log_vprint(ANDROID_LOG_INFO, TAG, format, args);
	va_end(args);
}


static rfbBool alloc_framebuffer(rfbClient* client)
{
  // get VNCConn object belonging to this client
  jobject conn = rfbClientGetClientData(client, VNCCONN_OBJ_ID);

  __android_log_print(ANDROID_LOG_DEBUG, ("VNCConn %p: alloc'ing framebuffer w:%i, h:%i"), conn, client->width, client->height);

  // assert 32bpp, as requested with GetClient() in Init()
  if(client->format.bitsPerPixel != 32) {
      setErr(conn, "Failure setting up framebuffer: wrong BPP!");
      return FALSE;
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
  //thread_post_fbresize_notify(conn);

  return client->frameBuffer ? TRUE : FALSE;
}







jint Java_com_coboltforge_dontmind_multivnc_VNCConn_construct(JNIEnv *env, jobject obj)
{
	__android_log_print(ANDROID_LOG_DEBUG, TAG, "construct %p\n", obj);

	rfbClientLog = rfbClientErr = thread_logger;

}






jboolean Java_com_coboltforge_dontmind_multivnc_VNCConn_setup(JNIEnv *env, jobject obj, int bitsPerSample,int samplesPerPixel, int bytesPerPixel)
{
	__android_log_print(ANDROID_LOG_DEBUG, TAG, "setup %p\n", obj);

	if(getRfbClient(env, obj)) // already set up
	{
		__android_log_print(ANDROID_LOG_DEBUG, TAG, "VNCConn %p: Setup already done. Call Cleanup() first!", obj);
		return JNI_FALSE;
	}

	/* 5,3,2 and 8,3,4 seem possible */
	rfbClient* cl=rfbGetClient(bitsPerSample, samplesPerPixel, bytesPerPixel);

	rfbClientSetClientData(cl, VNCCONN_OBJ_ID, obj);

	/* callbacks */
	cl->MallocFrameBuffer = alloc_framebuffer;
/*	cl->GotFrameBufferUpdate = thread_got_update;
	cl->FinishedFrameBufferUpdate = thread_update_finished;
	cl->GetPassword = getpasswdfunc;
	cl->HandleKeyboardLedState = thread_kbd_leds;
	cl->HandleTextChat = thread_textchat;
	cl->GotXCutText = thread_got_cuttext;
	cl->Bell = thread_bell;
	cl->HandleXvpMsg = thread_handle_xvp;*/

	cl->canHandleNewFBSize = TRUE;

	setRfbClient(env, obj, cl);

	return JNI_TRUE;
}


void Java_com_coboltforge_dontmind_multivnc_VNCConn_cleanup(JNIEnv *env,  jobject obj)
{
	__android_log_print(ANDROID_LOG_DEBUG, TAG, "cleanup %p\n", obj);

	rfbClient* cl = getRfbClient(env, obj);

	if(cl) {
		__android_log_print(ANDROID_LOG_DEBUG, TAG, "VNCConn %p: cleanup() before client cleanup", obj);
		rfbClientCleanup(cl);
		setRfbClient(env, obj, 0);
		__android_log_print(ANDROID_LOG_DEBUG, TAG, "VNCConn %p: cleanup() after client cleanup", obj);
	}
}


