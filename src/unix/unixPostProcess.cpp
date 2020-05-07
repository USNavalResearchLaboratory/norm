/*********************************************************************
 *
 * AUTHORIZATION TO USE AND DISTRIBUTE
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: 
 *
 * (1) source code distributions retain this paragraph in its entirety, 
 *  
 * (2) distributions including binary code include this paragraph in
 *     its entirety in the documentation or other materials provided 
 *     with the distribution, and 
 *
 * (3) all advertising materials mentioning features or use of this 
 *     software display the following acknowledgment:
 * 
 *      "This product includes software written and developed 
 *       by Brian Adamson and Joe Macker of the Naval Research 
 *       Laboratory (NRL)." 
 *         
 *  The name of NRL, the name(s) of NRL  employee(s), or any entity
 *  of the United States Government may not be used to endorse or
 *  promote  products derived from this software, nor does the 
 *  inclusion of the NRL written and developed software  directly or
 *  indirectly suggest NRL or United States  Government endorsement
 *  of this product.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ********************************************************************/
 
// This file contains code for various UNIX MDP post processor options
        
        
/*******************************************************************
 * Netscape[tm] post processor routines
 */     
        
// Some portions of this code 
// Copyright � 1996 Netscape Communications Corporation, 
// all rights reserved.
        
#include "normPostProcess.h"
#include "protoDebug.h"

#include <errno.h>
#include <string.h>  // for strerror()
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>  // for exit()
#include <stdio.h>

#ifndef sighandler_t
#ifndef sig_t
typedef void (*sighandler_t)(int);
#else
typedef sig_t sighandler_t;
#endif // if/else !sig_t
#endif // !sighandler_t

#ifdef NETSCAPE_SUPPORT
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>	/* for XmuClientWindow() */

/* vroot.h is a header file which lets a client get along with `virtual root'
   window managers like swm, tvtwm, olvwm, etc.  If you don't have this header
   file, you can find it at "http://home.netscape.com/newsref/std/vroot.h".
   If you don't care about supporting virtual root window managers, you can
   comment this line out.
 */
#include "vroot.h"
#define MOZILLA_VERSION_PROP   "_MOZILLA_VERSION"
static Atom XA_MOZILLA_VERSION  = 0;
bool CheckForNetscape(const char* cmd);
bool NetscapeIsRunning(Window *result);
bool NetscapeCheckWindow(Window window);
#endif // NETSCAPE_SUPPORT

class UnixPostProcessor : public NormPostProcessor
{
    public:
        ~UnixPostProcessor();
        bool IsActive() {return (0 != process_id);}
        bool ProcessFile(const char* path);
        void Kill();
        
        void OnSIGCHLD();
        
    private:
        friend class NormPostProcessor;
        UnixPostProcessor();
        
        int     process_id;
#ifdef NETSCAPE_SUPPORT
        Window  window_id;
#endif // NETSCAPE_SUPPORT
};  // end class UnixPostProcessor 

UnixPostProcessor::UnixPostProcessor()
 : process_id(0)
#ifdef NETSCAPE_SUPPORT
   ,window_id(0)
#endif // NETSCAPE_SUPPORT
{
}

NormPostProcessor* NormPostProcessor::Create()
{
    return static_cast<NormPostProcessor*>(new UnixPostProcessor);   
}  // end NormPostProcessor::Create()

UnixPostProcessor::~UnixPostProcessor()
{
    if (IsActive()) Kill();   
}

bool UnixPostProcessor::ProcessFile(const char* path)
{
    const char** argv = (const char**)process_argv;
    int argc = process_argc;
#ifdef NETSCAPE_SUPPORT  
    // Special support for finding a Netscape window and
    // use the openURL remote command to display received file
#define USE_ACTIVE_WINDOW 0xffffffff  
    const char* myArgs[32];
    char wid_text[32], url_text[PATH_MAX+64];
    if (CheckForNetscape(argv[0]))
    {
        int i = 0;
        myArgs[i++] = process_argv[0];
        Window w = 0;
        // (TBD) We could put in a hack to look for the old
        // window if we made "w" static when "window_id" = 0xffffffff
        // This would help us capture the proper window for file display
        // It's still a hack but it might work a little better without
        // too much trouble
        if (NetscapeIsRunning(&w))
        {       
            // Use -remote command to display file in current
            // or new window
            if (window_id)
            {
                if (NetscapeCheckWindow(window_id))
                {
                    // Use the same window as last time
                    myArgs[i++] = "-id";
                    sprintf(wid_text, "0x%lx", window_id);
                    myArgs[i++] = wid_text;
                    myArgs[i++] = "-noraise";
                    myArgs[i++] = "-remote";
                    sprintf(url_text, "openURL(file://%s)", path);
                    myArgs[i++] = url_text;
                }
                else if (USE_ACTIVE_WINDOW == window_id)
                {
                    // Capture the open window we found
                    myArgs[i++] = "-id";
                    sprintf(wid_text, "0x%lx", w);
                    myArgs[i++] = wid_text;
                    window_id = w;
                    myArgs[i++] = "-noraise"; 
                    myArgs[i++] = "-remote";
                    sprintf(url_text, "openURL(file://%s)", path);
                    myArgs[i++] = url_text;         
                }
                else  // user must have closed old window so open another
                {
                    window_id = USE_ACTIVE_WINDOW;
                    //myArgs[i++] = "-raise";
                    myArgs[i++] = "-remote";
                    sprintf(url_text, "openURL(file://%s,new-window)", path);
                    myArgs[i++] = url_text;
                }                
                
            }
            else
            {
                // We're starting fresh, open a new window 
                window_id = USE_ACTIVE_WINDOW;
                //myArgs[i++] = "-raise";
                myArgs[i++] = "-remote";
                sprintf(url_text, "openURL(file://%s,new-window)", path);
                myArgs[i++] = url_text;
            }
            argv = myArgs;
     
        }
        else
        {
            if (IsActive()) Kill();
            window_id = USE_ACTIVE_WINDOW;
            myArgs[i++] = path;
        }  // end if/else (NetscapeIsRunning())
        myArgs[i] = NULL;
        argc = i - 1;
        argv = myArgs;
    }
    else
#endif // NETSCAPE_SUPPORT
    {
        if (IsActive()) Kill();
        argv[argc] = path;
    }
    
    // 1) temporarily disable signal handling
    sighandler_t sigtermHandler = signal(SIGTERM, SIG_DFL);
    sighandler_t sigintHandler = signal(SIGINT, SIG_DFL);
    sighandler_t sigchldHandler = signal(SIGCHLD, SIG_DFL);
      
    switch((process_id = fork()))
    {
        case -1:    // error
            DMSG(0, "UnixPostProcessor::ProcessFile fork() error: %s\n", strerror(errno));
            process_id = 0;
            process_argv[process_argc] = NULL;
            return false;

        case 0:     // child
            if (execvp((char*)argv[0], (char**)argv) < 0)
            {
		        DMSG(0, "UnixPostProcessor::ProcessFile execvp() error: %s\n", strerror(errno));
                exit(-1);
            }
            break;

        default:    // parent
            process_argv[process_argc] = NULL;
            // Restore signal handlers for parent
            signal(SIGTERM, sigtermHandler);
            signal(SIGINT, sigintHandler);
            // The use of "waitpid()" here is a work-around
            // for an IRIX SIGCHLD issue
            int status;
            while (waitpid(-1, &status, WNOHANG) > 0);
            signal(SIGCHLD, sigchldHandler);
            break;
    }
    return true;
}  // end UnixPostProcessor::ProcessFile()

void UnixPostProcessor::Kill()
{
    if (!IsActive()) return;
    int count = 0;
    while((kill(process_id, SIGTERM) != 0) && count < 10)
	{ 
	    if (errno == ESRCH) break;
	    count++;
	    DMSG(0, "UnixPostProcessor::Kill kill() error: %s\n", strerror(errno));
	}
	count = 0;
    int status;
	while((waitpid(process_id, &status, 0) != process_id) && count < 10)
	{
	    if (errno == ECHILD) break;
	    count++;
	    DMSG(0, "UnixPostProcessor::Kill waitpid() error: %s\n", strerror(errno));
	}
	process_id = 0;
}  // end UnixPostProcessor::Kill()

void UnixPostProcessor::OnSIGCHLD()
{
    // See if post processor exited itself
    int status;
    if (wait(&status) == process_id) process_id = 0;
}  // end UnixPostProcessor::HandleSIGCHLD()


#ifdef NETSCAPE_SUPPORT
bool CheckForNetscape(const char* cmd)
{
    // See if it's Netscape post processing 
    // for which we provide special support
    char txt[8];
    const char *ptr = strrchr(cmd, PROTO_PATH_DELIMITER);
    if (ptr)
        ptr++;
    else
        ptr = cmd;
    strncpy(txt, ptr, 8);
    for (int i=0; i<8; i++) txt[i] = toupper(txt[i]);
    if (!strncmp(txt, "NETSCAPE", 8)) 
        return true;
    else
        return false;
}  // end CheckForNetscape()

bool NetscapeIsRunning(Window *result)
{
    int i;
    
    Window root2, parent, *kids;
    unsigned int nkids;
    *result = 0;
    
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) 
    {
        fprintf(stderr, "mdp: XOpenDisplay() error\n");
        return false;
    }
    
    if (!XA_MOZILLA_VERSION) 
        XA_MOZILLA_VERSION = XInternAtom(dpy, MOZILLA_VERSION_PROP, False);
    
    Window root = RootWindowOfScreen(DefaultScreenOfDisplay(dpy));
    
    if (!XQueryTree (dpy, root, &root2, &parent, &kids, &nkids))
    {
        fprintf(stderr, "mdp: XQueryTree failed on display %s\n", DisplayString (dpy));
        XCloseDisplay(dpy);
        return false;
    }

    // Note: root != root2 is possible with virtual root WMs.
    if (!(kids && nkids))
    {
        fprintf(stderr, "mdp: root window has no children on display %s\n",
             DisplayString (dpy));
        XCloseDisplay(dpy);
        return false;
    }

    for (i = nkids-1; i >= 0; i--)
    {
        Atom type;
        int format;
        unsigned long nitems, bytesafter;
        unsigned char *version = 0;
        Window w = XmuClientWindow (dpy, kids[i]);
        int status = XGetWindowProperty (dpy, w, XA_MOZILLA_VERSION,
			           0, (65536 / sizeof (long)),
			           False, XA_STRING,
			           &type, &format, &nitems, &bytesafter,
			           &version);
        if (! version) continue;
        XFree (version);
        if (status == Success && type != None)
	    {
	        *result = w;
	        break;
	    }
    }
    XCloseDisplay(dpy);       
    return (*result ? true : false);
  
}  // end NetscapeIsRunning()

bool NetscapeCheckWindow(Window window)
{
    int i;   
    Window root2, parent, *kids;
    unsigned int nkids;
    
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) 
    {
        fprintf(stderr, "mdp: XOpenDisplay() error\n");
        return false;
    }
    
    if (!XA_MOZILLA_VERSION) 
        XA_MOZILLA_VERSION = XInternAtom(dpy, MOZILLA_VERSION_PROP, False);
    
    Window root = RootWindowOfScreen(DefaultScreenOfDisplay (dpy));
    
    if (!XQueryTree (dpy, root, &root2, &parent, &kids, &nkids))
    {
        fprintf(stderr, "mdp: XQueryTree failed on display %s\n", DisplayString (dpy));
        XCloseDisplay(dpy);
        return false;
    }

    // Note: root != root2 is possible with virtual root WMs.
    if (!(kids && nkids)) return false;

    for (i = nkids-1; i >= 0; i--)
    {
        Atom type;
        int format;
        unsigned long nitems, bytesafter;
        unsigned char *version = 0;
        Window w = XmuClientWindow (dpy, kids[i]);
        int status = XGetWindowProperty (dpy, w, XA_MOZILLA_VERSION,
			           0, (65536 / sizeof (long)),
			           False, XA_STRING,
			           &type, &format, &nitems, &bytesafter,
			           &version);
        if (!version) continue;
        XFree (version);
        if (status == Success && type != None)
	    {
	        if (w == window)
            {
                XCloseDisplay(dpy);
                return true;
            }
            else
            {
                continue;
            }
	    }
    }
    XCloseDisplay(dpy);
    return false;
    
}  // end NetscapeCheckWindow()
#endif // NETSCAPE_SUPPORT
