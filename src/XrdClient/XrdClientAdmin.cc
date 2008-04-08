//////////////////////////////////////////////////////////////////////////
//                                                                      //
// XrdClientAdmin                                                       //
//                                                                      //
// Author: Fabrizio Furano (INFN Padova, 2004)                          //
// Adapted from XTNetAdmin (root.cern.ch) originally done by            //
//  Alvise Dorigo, Fabrizio Furano                                      //
//          INFN Padova, 2003                                           //
//                                                                      //
// A UNIX reference admin client for xrootd.                            //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

//       $Id$

#include "XrdClient/XrdClientAdmin.hh"
#include "XrdClient/XrdClientDebug.hh"
#include "XrdClient/XrdClientUrlSet.hh"
#include "XrdClient/XrdClientConn.hh"
#include "XrdClient/XrdClientEnv.hh"
#include "XrdClient/XrdClientConnMgr.hh"
#include "XrdOuc/XrdOucTokenizer.hh"


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef WIN32
#include <unistd.h>
#include <strings.h>
#include <netinet/in.h>
#endif

//_____________________________________________________________________________
void joinStrings(XrdOucString &buf, vecString &vs,
		 int startidx, int endidx)
{

  if (endidx < 0) endidx = vs.GetSize()-1;

  if (!vs.GetSize() || (vs.GetSize() <= startidx) ||
      (endidx < startidx) ){
    buf = "";
    return;
  }

  int lastidx = xrdmin(vs.GetSize()-1, endidx);
  
  for(int j=startidx; j <= lastidx; j++) {
    buf += vs[j];
    if (j < lastidx) buf += "\n";
  }

}

//_____________________________________________________________________________
XrdClientAdmin::XrdClientAdmin(const char *url) {


  // Pick-up the latest setting of the debug level
  DebugSetLevel(EnvGetLong(NAME_DEBUG));

  if (!ConnectionManager)
    Info(XrdClientDebug::kNODEBUG,
	 "",
	 "(C) 2004 SLAC XrdClientAdmin " << XRD_CLIENT_VERSION);

   fInitialUrl = url;

   fConnModule = new XrdClientConn();

   if (!fConnModule) {
      Error("XrdClientAdmin",
	    "Object creation failed.");
      abort();
   }

   // Set this instance as a handler for handling the consequences of a redirection
   fConnModule->SetRedirHandler(this);

}

//_____________________________________________________________________________
XrdClientAdmin::~XrdClientAdmin()
{
   delete fConnModule;
}



//_____________________________________________________________________________
bool XrdClientAdmin::Connect()
{
   // Connect to the server

   // Nothing to do if already connected
   if (fConnModule && fConnModule->IsConnected()) {
      return TRUE;
   }

   short locallogid;
  
   // Now we try to set up the first connection
   // We cycle through the list of urls given in fInitialUrl
  
   // Max number of tries
   int connectMaxTry = EnvGetLong(NAME_FIRSTCONNECTMAXCNT);
  
   // Construction of the url set coming from the resolution of the hosts given
   XrdClientUrlSet urlArray(fInitialUrl);
   if (!urlArray.IsValid()) {
      Error("Connect", "The URL provided is incorrect.");
      return FALSE;
   }

   //
   // Now start the connection phase, picking randomly from UrlArray
   //
   urlArray.Rewind();
   locallogid = -1;
   int urlstried = 0;
   for (int connectTry = 0;
	(connectTry < connectMaxTry) && (!fConnModule->IsConnected()); 
	connectTry++) {

      XrdClientUrlInfo *thisUrl = 0;
      urlstried = (urlstried == urlArray.Size()) ? 0 : urlstried;

      bool nogoodurl = TRUE;
      while (urlArray.Size() > 0) {
     
         // Get an url from the available set
         if ((thisUrl = urlArray.GetARandomUrl())) {

            if (fConnModule->CheckHostDomain(thisUrl->Host)) {
               nogoodurl = FALSE;
               Info(XrdClientDebug::kHIDEBUG, "Connect", "Trying to connect to " <<
                                              thisUrl->Host << ":" << thisUrl->Port <<
                                              ". Connect try " << connectTry+1);
               locallogid = fConnModule->Connect(*thisUrl, this);
               // To find out if we have tried the whole URLs set
               urlstried++;
               break;
            } else {
               // Invalid domain: drop the url and move to next, if any
               urlArray.EraseUrl(thisUrl);
               continue;
            }

	 }
      }
      if (nogoodurl) {
         Error("Connect", "Access denied to all URL domains requested");
         break;
      }
     
      // We are connected to a host. Let's handshake with it.
      if (fConnModule->IsConnected()) {

	 // Now the have the logical Connection ID, that we can use as streamid for 
	 // communications with the server

	 Info(XrdClientDebug::kHIDEBUG, "Connect",
	      "The logical connection id is " << fConnModule->GetLogConnID() <<
	      ". This will be the streamid for this client");

	 fConnModule->SetUrl(*thisUrl);
        
	 Info(XrdClientDebug::kHIDEBUG, "Connect",
	      "Working url is " << thisUrl->GetUrl());
        
	 // after connection deal with server
	 if (!fConnModule->GetAccessToSrv()) {
            if (fConnModule->LastServerError.errnum == kXR_NotAuthorized) {
               if (urlstried == urlArray.Size()) {
                  // Authentication error: we tried all the indicated URLs:
                  // does not make much sense to retry
                  fConnModule->Disconnect(TRUE);
                  XrdOucString msg(fConnModule->LastServerError.errmsg);
                  msg.erasefromend(1);
                  Error("Connect", "Authentication failure: " << msg);
                  break;
               } else {
                  XrdOucString msg(fConnModule->LastServerError.errmsg);
                  msg.erasefromend(1);
                  Info(XrdClientDebug::kHIDEBUG, "Connect",
                                                 "Authentication failure: " << msg);
               }
            } else {
               Error("Connect", "Access to server failed: error: " <<
                                fConnModule->LastServerError.errnum << " (" << 
                                fConnModule->LastServerError.errmsg << ") - retrying.");
            }
         } else {
            Info(XrdClientDebug::kUSERDEBUG, "Connect", "Access to server granted.");
            break;
	 }
      }
     
      // The server denied access. We have to disconnect.
      Info(XrdClientDebug::kHIDEBUG, "Connect", "Disconnecting.");
     
      fConnModule->Disconnect(FALSE);
     
      if (connectTry < connectMaxTry-1) {

	 if (DebugLevel() >= XrdClientDebug::kUSERDEBUG)
	    Info(XrdClientDebug::kUSERDEBUG, "Connect",
	         "Connection attempt failed. Sleeping " <<
	         EnvGetLong(NAME_RECONNECTTIMEOUT) << " seconds.");
     
         sleep(EnvGetLong(NAME_RECONNECTTIMEOUT));

      }

   } //for connect try


   if (!fConnModule->IsConnected()) {
      return FALSE;
   }

  
   //
   // Variable initialization
   // If the server is a new xrootd ( load balancer or data server)
   //
   if ((fConnModule->GetServerType() != kSTRootd) && 
       (fConnModule->GetServerType() != kSTNone)) {
      // Now we are connected to a server that didn't redirect us after the 
      // login/auth phase

      Info(XrdClientDebug::kUSERDEBUG, "Connect", "Connected.");

     
   } else {
      // We close the connection only if we do not know the server type.
      // In the rootd case the connection may be re-used later.
      if (fConnModule->GetServerType() == kSTNone)
	 fConnModule->Disconnect(TRUE);

      return FALSE;
   }

   return TRUE;

}



//_____________________________________________________________________________
bool XrdClientAdmin::Stat(const char *fname, long &id, long long &size, long &flags, long &modtime)
{
   // Return file stat information. The interface and return value is
   // identical to TSystem::GetPathInfo().

   bool ok;

   // asks the server for stat file informations
   ClientRequest statFileRequest;

   memset( &statFileRequest, 0, sizeof(ClientRequest) );

   fConnModule->SetSID(statFileRequest.header.streamid);

   statFileRequest.stat.requestid = kXR_stat;

   memset(statFileRequest.stat.reserved, 0,
	  sizeof(statFileRequest.stat.reserved));

   statFileRequest.header.dlen = strlen(fname);

   char fStats[2048];
   id = 0;
   size = 0;
   flags = 0;
   modtime = 0;

   ok = fConnModule->SendGenCommand(&statFileRequest, (const char*)fname,
				    NULL, fStats , FALSE, (char *)"Stat");


   if (ok && (fConnModule->LastServerResp.status == 0)) {
      if (fConnModule->LastServerResp.dlen >= 0)
         fStats[fConnModule->LastServerResp.dlen] = 0;
      else
         fStats[0] = 0;
      Info(XrdClientDebug::kHIDEBUG,
	   "Stat", "Returned stats=" << fStats);
      sscanf(fStats, "%ld %lld %ld %ld", &id, &size, &flags, &modtime);
   }

   return ok;
}



//_____________________________________________________________________________
bool XrdClientAdmin::Stat_vfs(const char *fname,
			      int &rwservers,
			      long long &rwfree,
			      int &rwutil,
			      int &stagingservers,
			      long long &stagingfree,
			      int &stagingutil)
{
   // Return information for a virtual file system

   bool ok;

   // asks the server for stat file informations
   ClientRequest statFileRequest;

   memset( &statFileRequest, 0, sizeof(ClientRequest) );

   fConnModule->SetSID(statFileRequest.header.streamid);

   statFileRequest.stat.requestid = kXR_stat;

   memset(statFileRequest.stat.reserved, 0,
	  sizeof(statFileRequest.stat.reserved));

   statFileRequest.stat.options = kXR_vfs;

   statFileRequest.header.dlen = strlen(fname);

   char fStats[2048];
   rwservers = 0;
   rwfree = 0;
   rwutil = 0;
   stagingservers = 0;
   stagingfree = 0;
   stagingutil = 0;


   ok = fConnModule->SendGenCommand(&statFileRequest, (const char*)fname,
				    NULL, fStats , FALSE, (char *)"Stat_vfs");


   if (ok && (fConnModule->LastServerResp.status == 0)) {
      if (fConnModule->LastServerResp.dlen >= 0)
         fStats[fConnModule->LastServerResp.dlen] = 0;
      else
         fStats[0] = 0;
      Info(XrdClientDebug::kHIDEBUG,
	   "Stat_vfs", "Returned stats=" << fStats);

      sscanf(fStats, "%d %lld %d %d %lld %d", &rwservers, &rwfree, &rwutil,
	     &stagingservers, &stagingfree, &stagingutil);

   }

   return ok;
}


//_____________________________________________________________________________
bool XrdClientAdmin::SysStatX(const char *paths_list, kXR_char *binInfo)
{
   XrdOucString pl(paths_list);
   bool ret;
   // asks the server for stat file informations
   ClientRequest statXFileRequest;
  
   memset( &statXFileRequest, 0, sizeof(ClientRequest) );
   fConnModule->SetSID(statXFileRequest.header.streamid);
   statXFileRequest.header.requestid = kXR_statx;

   statXFileRequest.stat.dlen = pl.length();
  
   ret = fConnModule->SendGenCommand(&statXFileRequest, pl.c_str(),
				     NULL, binInfo , FALSE, (char *)"SysStatX");
  
   return(ret);
}

//_____________________________________________________________________________
bool XrdClientAdmin::ExistFiles(vecString &vs, vecBool &vb)
{
   bool ret;
   XrdOucString buf;
   joinStrings(buf, vs);

   kXR_char *Info;
   Info = (kXR_char*) malloc(vs.GetSize()+10);
   memset((void *)Info, 0, vs.GetSize()+10);
  
   ret = this->SysStatX(buf.c_str(), Info);

   if (ret) for(int j=0; j <= vs.GetSize()-1; j++) {
         bool tmp = TRUE;

         if ( (*(Info+j) & kXR_isDir) || (*(Info+j) & kXR_other) ||
              (*(Info+j) & kXR_offline) )
                 tmp = FALSE;

         vb.Push_back(tmp);
      }


   free(Info);

   return ret;
}

//_____________________________________________________________________________
bool XrdClientAdmin::ExistDirs(vecString &vs, vecBool &vb)
{
   bool ret;
   XrdOucString buf;
   joinStrings(buf, vs);

   kXR_char *Info;
   Info = (kXR_char*) malloc(vs.GetSize()+10);
   memset((void *)Info, 0, vs.GetSize()+10);
  
   ret = this->SysStatX(buf.c_str(), Info);
  
   if (ret) for(int j=0; j <= vs.GetSize()-1; j++) {
      bool tmp;

      if( (*(Info+j) & kXR_isDir) ) {
	 tmp = TRUE;
	 vb.Push_back(tmp);
      } else {
	 tmp = FALSE;
	 vb.Push_back(tmp);
      }

   }

   free(Info);

   return ret;
}

//_____________________________________________________________________________
bool XrdClientAdmin::IsFileOnline(vecString &vs, vecBool &vb)
{
   bool ret;
   XrdOucString buf;
   joinStrings(buf, vs);

   kXR_char *Info;
   Info = (kXR_char*) malloc(vs.GetSize()+10);
   memset((void *)Info, 0, vs.GetSize()+10);
  
   ret = this->SysStatX(buf.c_str(), Info);
  
   if (ret) for(int j=0; j <= vs.GetSize()-1; j++) {
      bool tmp;

      if( !(*(Info+j) & kXR_offline) ) {
	 tmp = TRUE;
	 vb.Push_back(tmp);
      } else {
	 tmp = FALSE;
	 vb.Push_back(tmp);
      }
      
   }

   free(Info);

   return ret;
}


// Called by the conn module after a redirection has been succesfully handled
//_____________________________________________________________________________
bool XrdClientAdmin::OpenFileWhenRedirected(char *newfhandle, bool &wasopen) {
   // We simply do nothing...
   wasopen = FALSE;
   return TRUE;
}

//_____________________________________________________________________________
bool XrdClientAdmin::Rmdir(const char *path) 
{
   // Remove an empty remote directory
   ClientRequest rmdirFileRequest;

   memset( &rmdirFileRequest, 0, sizeof(rmdirFileRequest) );
   fConnModule->SetSID(rmdirFileRequest.header.streamid);
   rmdirFileRequest.header.requestid = kXR_rmdir;
   rmdirFileRequest.header.dlen = strlen(path);
  
   return (fConnModule->SendGenCommand(&rmdirFileRequest, path, 
				       NULL, NULL, FALSE, (char *)"Rmdir"));

}

//_____________________________________________________________________________
bool XrdClientAdmin::Rm(const char *file) 
{
   // Remove a remote file
   ClientRequest rmFileRequest;

   memset( &rmFileRequest, 0, sizeof(rmFileRequest) );
   fConnModule->SetSID(rmFileRequest.header.streamid);
   rmFileRequest.header.requestid = kXR_rm;
   rmFileRequest.header.dlen = strlen(file);
  
   return (fConnModule->SendGenCommand(&rmFileRequest, file,
				       NULL, NULL, FALSE, (char *)"Rm"));
}

//_____________________________________________________________________________
bool XrdClientAdmin::Chmod(const char *file, int user, int group, int other)
{
   // Change the permission of a remote file
   ClientRequest chmodFileRequest;

   memset( &chmodFileRequest, 0, sizeof(chmodFileRequest) );

   fConnModule->SetSID(chmodFileRequest.header.streamid);
   chmodFileRequest.header.requestid = kXR_chmod;

   if(user  & 4) 
      chmodFileRequest.chmod.mode |= kXR_ur;
   if(user  & 2) 
      chmodFileRequest.chmod.mode |= kXR_uw;
   if(user  & 1) 
      chmodFileRequest.chmod.mode |= kXR_ux;

   if(group & 4) 
      chmodFileRequest.chmod.mode |= kXR_gr;
   if(group & 2)
      chmodFileRequest.chmod.mode |= kXR_gw;
   if(group & 1)
      chmodFileRequest.chmod.mode |= kXR_gx;

   if(other & 4)
      chmodFileRequest.chmod.mode |= kXR_or;
   if(other & 2)
      chmodFileRequest.chmod.mode |= kXR_ow;
   if(other & 1)
      chmodFileRequest.chmod.mode |= kXR_ox;

   chmodFileRequest.header.dlen = strlen(file);
  
  
   return (fConnModule->SendGenCommand(&chmodFileRequest, file,
				       NULL, NULL, FALSE, (char *)"Chmod")); 

}

//_____________________________________________________________________________
bool XrdClientAdmin::Mkdir(const char *dir, int user, int group, int other)
{
   // Create a remote directory
   ClientRequest mkdirRequest;

   memset( &mkdirRequest, 0, sizeof(mkdirRequest) );

   fConnModule->SetSID(mkdirRequest.header.streamid);

   mkdirRequest.header.requestid = kXR_mkdir;

   memset(mkdirRequest.mkdir.reserved, 0, 
	  sizeof(mkdirRequest.mkdir.reserved));

   if(user  & 4) 
      mkdirRequest.mkdir.mode |= kXR_ur;
   if(user  & 2) 
      mkdirRequest.mkdir.mode |= kXR_uw;
   if(user  & 1) 
      mkdirRequest.mkdir.mode |= kXR_ux;

   if(group & 4) 
      mkdirRequest.mkdir.mode |= kXR_gr;
   if(group & 2)
      mkdirRequest.mkdir.mode |= kXR_gw;
   if(group & 1)
      mkdirRequest.mkdir.mode |= kXR_gx;

   if(other & 4)
      mkdirRequest.mkdir.mode |= kXR_or;
   if(other & 2)
      mkdirRequest.mkdir.mode |= kXR_ow;
   if(other & 1)
      mkdirRequest.mkdir.mode |= kXR_ox;

   mkdirRequest.mkdir.options[0] = kXR_mkdirpath;

   mkdirRequest.header.dlen = strlen(dir);
  
   return (fConnModule->SendGenCommand(&mkdirRequest, dir,
				       NULL, NULL, FALSE, (char *)"Mkdir"));

}

//_____________________________________________________________________________
bool XrdClientAdmin::Mv(const char *fileSrc, const char *fileDest)
{
   bool ret;

   // Rename a remote file
   ClientRequest mvFileRequest;

   memset( &mvFileRequest, 0, sizeof(mvFileRequest) );

   fConnModule->SetSID(mvFileRequest.header.streamid);
   mvFileRequest.header.requestid = kXR_mv;

   mvFileRequest.header.dlen = strlen( fileDest ) + strlen( fileSrc ) + 1; // len + len + string terminator \0

   char *data = new char[mvFileRequest.header.dlen+2]; // + 1 for space separator + 1 for \0
   memset(data, 0, mvFileRequest.header.dlen+2);
   strcpy( data, fileSrc );
   strcat( data, " " );
   strcat( data, fileDest );
  
   ret = fConnModule->SendGenCommand(&mvFileRequest, data,
				     NULL, NULL, FALSE, (char *)"Mv");

   delete(data);

   return ret;
}

//_____________________________________________________________________________
UnsolRespProcResult XrdClientAdmin::ProcessUnsolicitedMsg(XrdClientUnsolMsgSender *sender,
							  XrdClientMessage *unsolmsg)
{
   // We are here if an unsolicited response comes from a logical conn
   // The response comes in the form of an TXMessage *, that must NOT be
   // destroyed after processing. It is destroyed by the first sender.
   // Remember that we are in a separate thread, since unsolicited 
   // responses are asynchronous by nature.

   if (unsolmsg->GetStatusCode() != XrdClientMessage::kXrdMSC_ok) {
	Info(XrdClientDebug::kHIDEBUG,
	     "ProcessUnsolicitedMsg", "Incoming unsolicited communication error message." );
    }
    else {
	Info(XrdClientDebug::kHIDEBUG,
	     "ProcessUnsolicitedMsg", "Incoming unsolicited response from streamid " <<
	     unsolmsg->HeaderSID() );
    }

   // Local processing ....
   if (unsolmsg->IsAttn()) {
      struct ServerResponseBody_Attn *attnbody;

      attnbody = (struct ServerResponseBody_Attn *)unsolmsg->GetData();

      int actnum = (attnbody) ? (attnbody->actnum) : 0;

      // "True" async resp is processed here
      switch (actnum) {

      case kXR_asyncdi:
	 // Disconnection + delayed reconnection request

	 struct ServerResponseBody_Attn_asyncdi *di;
	 di = (struct ServerResponseBody_Attn_asyncdi *)unsolmsg->GetData();

	 // Explicit redirection request
	 if (di) {
	    Info(XrdClientDebug::kUSERDEBUG,
		 "ProcessUnsolicitedMsg", "Requested Disconnection + Reconnect in " <<
		 ntohl(di->wsec) << " seconds.");

	    fConnModule->SetRequestedDestHost((char *)(fConnModule->GetCurrentUrl().Host.c_str()),
					      fConnModule->GetCurrentUrl().Port);
	    fConnModule->SetREQDelayedConnectState(ntohl(di->wsec));
	 }

	 // Other objects may be interested in this async resp
	 return kUNSOL_CONTINUE;
	 break;
	 
      case kXR_asyncrd:
	 // Redirection request

	 struct ServerResponseBody_Attn_asyncrd *rd;
	 rd = (struct ServerResponseBody_Attn_asyncrd *)unsolmsg->GetData();

	 // Explicit redirection request
	 if (rd && (strlen(rd->host) > 0)) {
	    Info(XrdClientDebug::kUSERDEBUG,
		 "ProcessUnsolicitedMsg", "Requested redir to " << rd->host <<
		 ":" << ntohl(rd->port));

	    fConnModule->SetRequestedDestHost(rd->host, ntohl(rd->port));
	 }

	 // Other objects may be interested in this async resp
	 return kUNSOL_CONTINUE;
	 break;

      case kXR_asyncwt:
	 // Puts the client in wait state

	 struct ServerResponseBody_Attn_asyncwt *wt;
	 wt = (struct ServerResponseBody_Attn_asyncwt *)unsolmsg->GetData();

	 if (wt) {
	    Info(XrdClientDebug::kUSERDEBUG,
		 "ProcessUnsolicitedMsg", "Pausing client for " << ntohl(wt->wsec) <<
		 " seconds.");

	    fConnModule->SetREQPauseState(ntohl(wt->wsec));
	 }

	 // Other objects may be interested in this async resp
	 return kUNSOL_CONTINUE;
	 break;

      case kXR_asyncgo:
	 // Resumes from pause state

	 Info(XrdClientDebug::kUSERDEBUG,
	      "ProcessUnsolicitedMsg", "Resuming from pause.");

	    fConnModule->SetREQPauseState(0);

	 // Other objects may be interested in this async resp
	 return kUNSOL_CONTINUE;
	 break;

      case kXR_asynresp:
	// A response to a request which got a kXR_waitresp as a response
	
	// We pass it direcly to the connmodule for processing
	// The processing will tell if the streamid matched or not,
	// in order to stop further processing
	return fConnModule->ProcessAsynResp(unsolmsg);
	break;

      default:

	Info(XrdClientDebug::kUSERDEBUG,
	      "ProcessUnsolicitedMsg", "Empty message");

	// Propagate the message
	return kUNSOL_CONTINUE;

      } // switch

      
   }
   else
       // Let's see if the message is a communication error message
       if (unsolmsg->GetStatusCode() != XrdClientMessage::kXrdMSC_ok)
	   return fConnModule->ProcessAsynResp(unsolmsg);


   return kUNSOL_CONTINUE;
}



//_____________________________________________________________________________
bool XrdClientAdmin::Protocol(kXR_int32 &proto, kXR_int32 &kind)
{
   ClientRequest protoRequest;

   memset( &protoRequest, 0, sizeof(protoRequest) );

   fConnModule->SetSID(protoRequest.header.streamid);

   protoRequest.header.requestid = kXR_protocol;

   char buf[8]; // For now 8 bytes are returned... in future could increase with more infos
   bool ret = fConnModule->SendGenCommand(&protoRequest, NULL,
					  NULL, buf, FALSE, (char *)"Protocol");
  
   memcpy(&proto, buf, sizeof(proto));
   memcpy(&kind, buf + sizeof(proto), sizeof(kind));

   proto = ntohl(proto);
   kind  = ntohl(kind);
    
   return ret;
}

//_____________________________________________________________________________
bool XrdClientAdmin::Prepare(vecString vs, kXR_char option, kXR_char prty)
{
  // Send a bulk prepare request for a vector of paths
  // Split a huge prepare list into smaller chunks

   XrdOucString buf;

   if (vs.GetSize() < 75) {
     joinStrings(buf, vs);
     return Prepare(buf.c_str(), option, prty);
   }


   for (int i = 0; i < vs.GetSize()+50; i++) {
     joinStrings(buf, vs, i, i+49);

     if (!Prepare(buf.c_str(), option, prty)) return false;
   }

   return true;
}

//_____________________________________________________________________________
bool XrdClientAdmin::Prepare(const char *buf, kXR_char option, kXR_char prty)
{
   // Send a bulk prepare request for a '\n' separated list in buf

   ClientRequest prepareRequest;

   memset( &prepareRequest, 0, sizeof(prepareRequest) );

   fConnModule->SetSID(prepareRequest.header.streamid);

   prepareRequest.header.requestid    = kXR_prepare;
   prepareRequest.prepare.options     = option;
   prepareRequest.prepare.prty        = prty;

   prepareRequest.header.dlen = strlen(buf);

   bool ret = fConnModule->SendGenCommand(&prepareRequest, buf,
                                          NULL, NULL , FALSE, (char *)"Prepare");

   return ret;
}

//_____________________________________________________________________________
bool  XrdClientAdmin::DirList(const char *dir, vecString &entries) {
   bool ret;
   // asks the server for the content of a directory
   ClientRequest DirListFileRequest;
   kXR_char *dl;
  
   memset( &DirListFileRequest, 0, sizeof(ClientRequest) );
   fConnModule->SetSID(DirListFileRequest.header.streamid);
   DirListFileRequest.header.requestid = kXR_dirlist;

   DirListFileRequest.dirlist.dlen = strlen(dir);
  
   // Note that the connmodule has to dynamically alloc the space for the answer
   ret = fConnModule->SendGenCommand(&DirListFileRequest, dir,
				     reinterpret_cast<void **>(&dl), 0, TRUE, (char *)"DirList");
  
   // Now parse the answer building the entries vector
   if (ret) {

      kXR_char *entry, *startp = dl, *endp = dl;

      while (endp) {

	 if ( (endp = (kXR_char *)strchr((const char*)startp, '\n')) ) {
            entry = (kXR_char *)malloc(endp-startp+1);
            memset((char *)entry, 0, endp-startp+1);
	    strncpy((char *)entry, (char *)startp, endp-startp);
	    endp++;
	 }
	 else
	    entry = (kXR_char *)strdup((char *)startp);
      
	 if (entry && strlen((char *)entry)) {
	    XrdOucString e((const char *)entry);

	    entries.Push_back(e);
	    free(entry);
	 }

	 startp = endp;
      }

   
  
   }

   if (dl) free(dl);
   return(ret);

}


//_____________________________________________________________________________
long XrdClientAdmin::GetChecksum(kXR_char *path, kXR_char **chksum)
{
   ClientRequest chksumRequest;

   memset( &chksumRequest, 0, sizeof(chksumRequest) );

   fConnModule->SetSID(chksumRequest.header.streamid);

   chksumRequest.query.requestid = kXR_query;
   chksumRequest.query.infotype = kXR_Qcksum;
   chksumRequest.query.dlen = strlen((char *) path);

   bool ret = fConnModule->SendGenCommand(&chksumRequest, (const char*) path,
					  (void **)chksum, NULL, TRUE,
					  (char *)"GetChecksum");
  
   if (ret) return (fConnModule->LastServerResp.dlen);
   else return 0;
}

int XrdClientAdmin::LocalLocate(kXR_char *path, XrdClientVector<XrdClientLocate_Info> &res,
                                bool writable, bool nowait, bool all) {
  // Fires a locate req towards the currently connected server, and pushes the
  // results into the res vector
  //
  // If 'all' is false, returns the position in the vector of the found info (-1 if
  // not found); else returns the number of non-data servers.

   ClientRequest locateRequest;
   char *resp = 0;
   int retval = (all) ? 0 : -1;

   memset( &locateRequest, 0, sizeof(locateRequest) );

   fConnModule->SetSID(locateRequest.header.streamid);

   locateRequest.locate.requestid = kXR_locate;
   if (nowait) locateRequest.locate.options = kXR_nowait;
   locateRequest.locate.dlen = strlen((char *) path);

   // Resp is allocated inside the call
   bool ret = fConnModule->SendGenCommand(&locateRequest, (const char*) path,
					  (void **)&resp, 0, true,
					  (char *)"LocalLocate");

   if (!ret) return -2;
   if (!resp) return -1;
   if (!strlen(resp)) {
     free(resp);
     return -1;
   }

 

   // Parse the output
   XrdOucString rs(resp), s;
   free(resp);
   int from = 0;
   while ((from = rs.tokenize(s,from,' ')) != -1) {

      // If a token is bad, we keep the ones processed previously
      if (s.length() < 8 || (s[2] != '[') || (s[4] != ':')) {
         Error("LocalLocate", "Invalid server response. Resp: '" << s << "'");
         continue;
      }

      XrdClientLocate_Info nfo;

      // Info type
      switch (s[0]) {
      case 'S':
         nfo.Infotype = XrdClientLocate_Info::kXrdcLocDataServer;
         break;
      case 's':
         nfo.Infotype = XrdClientLocate_Info::kXrdcLocDataServerPending;
         break;
      case 'M':
         nfo.Infotype = XrdClientLocate_Info::kXrdcLocManager;
         break;
      case 'm':
         nfo.Infotype = XrdClientLocate_Info::kXrdcLocManagerPending;
         break;
      default:
         Info(XrdClientDebug::kNODEBUG, "LocalLocate",
               "Unknown info type: '" << s << "'");
      }

      // Write capabilities
      nfo.CanWrite = (s[1] == 'w') ? 1 : 0;

      // Endpoint url
      s.erase(0, s.find("[::")+3);
      s.replace("]","");
      strcpy((char *)nfo.Location, s.c_str());

      res.Push_back(nfo);

      if (nfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServer) {
         if (!all) {
            if (!writable || nfo.CanWrite) {
               retval = res.GetSize() - 1;
               break;
            }
         }
      } else {
         if (all)
            // Count non-dataservers
            retval++;
      }
   }

   return retval;
}

//_____________________________________________________________________________
bool XrdClientAdmin::Locate(kXR_char *path, XrdClientLocate_Info &resp, bool writable)
{
   // Find out any exact location of file 'path' and save the corresponding
   // URL in resp.
   // Returns true if found
   // If the retval is false and writable==true , resp contains a non writable url
   //  if there is one

   bool found = false;
   int depth = 0;
   memset(&resp, 0, sizeof(resp));

   if (!fConnModule) return 0;
   if (!fConnModule->IsConnected()) return 0;


   // Old servers will use what's there
   if (fConnModule->GetServerProtocol() < 0x290) {
     long id, flags, modtime;
     long long size;

     bool ok = Stat((const char *)path, id, size, flags, modtime);
     if (ok && (fConnModule->LastServerResp.status == 0)) {
       resp.Infotype = XrdClientLocate_Info::kXrdcLocDataServer;
       resp.CanWrite = 1;
       strcpy((char *)resp.Location, fConnModule->GetCurrentUrl().HostWPort.c_str());
     }
     fConnModule->GoBackToRedirector();
     return ok;
   }


   XrdClientUrlInfo currurl(fConnModule->GetCurrentUrl().GetUrl());
   if (!currurl.HostWPort.length()) return 0;

   // Set up the starting point in the vectored queue
   XrdClientVector<XrdClientLocate_Info> hosts;
   XrdClientLocate_Info nfo;
   nfo.Infotype = XrdClientLocate_Info::kXrdcLocManager;
   nfo.CanWrite = true;
   strcpy((char *)nfo.Location, currurl.HostWPort.c_str());
   hosts.Push_back(nfo);
   bool firsthost = true;
   XrdClientLocate_Info currnfo;
   bool stoprecursion = true;

   do {
     
     // Figure out how many checks at max we have to do to accomodate this level
     int qrytodo = hosts.GetSize();
     int pos = 0;

     stoprecursion = true;

     // Expand a level, i.e. ask to all the masters and remove items from the list
     for (int ii = 0; ii < qrytodo; ii++) {

       // Take the first item to process
       currnfo = hosts[pos];

       // If it's a master, we have to contact it, otherwise take the next
       if ((currnfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServer) ||
	   (currnfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServerPending)) {
	 pos++;
	 continue;
       }

       stoprecursion = false;
	 
       // Here, currnfo is pointing to a master we have to contact
       currurl.TakeUrl((char *)currnfo.Location);

       // Connect to the given host. At the beginning we are connected to the starting point
       // A failed connection is just ignored. Only one attempt is performed. Timeouts are
       // honored.
       if (!firsthost) {
	 firsthost = false;

	 fConnModule->Disconnect(false);
	 if (fConnModule->GoToAnotherServer(currurl) != kOK) {
	   pos++;
	   continue;
	 }
       }

       // We are connected, do the locate
       int posds = LocalLocate(path, hosts, writable, true);

       found = (posds > -1) ? 1 : 0;

       if (found) {
          resp = hosts[posds];
          break;
       }

       // We did not finish, take the next
       hosts.Erase(pos);
     }

     depth++;
   } while ( !found && (depth <= 4) && !stoprecursion);
   
   if (depth > 4)
     Error("Locate",
	   "The cluster exposes too many levels.");

   if (!found && hosts.GetSize()) {
     // If not found, we check anyway in the remaining list
     // to pick a pending one if present
     for (int ii = 0; ii < hosts.GetSize(); ii++) {
       currnfo = hosts[ii];
       if ( (currnfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServer) ||
	    (currnfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServerPending) ) {
	     resp = currnfo;
	     
	     if (!writable || resp.CanWrite) {
	       found = true;
	       break;
	     }
	     
	   }


     }

   }

   // At the end we want to rewind to the main redirector in any case
   fConnModule->GoBackToRedirector();

   return found;
}


//_____________________________________________________________________________
bool XrdClientAdmin::Locate(kXR_char *path, XrdClientVector<XrdClientLocate_Info> &hosts)
{
   // Find out any exact location of file 'path' and save the corresponding
   // URL in resp.
   // Returns true if found at least one

   int depth = 0;
   hosts.Clear();

   if (!fConnModule) return 0;
   if (!fConnModule->IsConnected()) return 0;


   // Old servers will use what's there
   if (fConnModule->GetServerProtocol() < 0x290) {
     long id, flags, modtime;
     long long size;
     XrdClientLocate_Info resp;

     bool ok = Stat((const char *)path, id, size, flags, modtime);
     if (ok && (fConnModule->LastServerResp.status == 0)) {
       resp.Infotype = XrdClientLocate_Info::kXrdcLocDataServer;
       resp.CanWrite = 1;
       strcpy((char *)resp.Location, fConnModule->GetCurrentUrl().HostWPort.c_str());
       hosts.Push_back(resp);
     }
     fConnModule->GoBackToRedirector();
     return ok;
   }

   XrdClientUrlInfo currurl(fConnModule->GetCurrentUrl().GetUrl());
   if (!currurl.HostWPort.length()) return 0;

   // Set up the starting point in the vectored queue
   XrdClientLocate_Info nfo;
   nfo.Infotype = XrdClientLocate_Info::kXrdcLocManager;
   nfo.CanWrite = true;
   strcpy((char *)nfo.Location, currurl.HostWPort.c_str());
   hosts.Push_back(nfo);
   bool firsthost = true;
   XrdClientLocate_Info currnfo;
   bool stoprecursion = true;

   do {
     
     // Figure out how many checks at max we have to do to accomodate this level
     int qrytodo = hosts.GetSize();
     int pos = 0;

     stoprecursion = true;

     // Expand a level, i.e. ask to all the masters and remove items from the list
     for (int ii = 0; ii < qrytodo; ii++) {

       // Take the first item to process
       currnfo = hosts[pos];

       // If it's a master, we have to contact it, otherwise take the next
       if ((currnfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServer) ||
	   (currnfo.Infotype == XrdClientLocate_Info::kXrdcLocDataServerPending)) {
	 pos++;
	 continue;
       }

       stoprecursion = false;
	 
       // Here, currnfo is pointing to a master we have to contact
       currurl.TakeUrl((char *)currnfo.Location);

       // Connect to the given host. At the beginning we are connected to the starting point
       // A failed connection is just ignored. Only one attempt is performed. Timeouts are
       // honored.
       if (!firsthost) {
	 firsthost = false;

	 fConnModule->Disconnect(false);
	 if (fConnModule->GoToAnotherServer(currurl) != kOK) {
	   pos++;
	   continue;
	 }
       }

       // We are connected, do the locate
       LocalLocate(path, hosts, true, false, true);

       // We did not finish, take the next
       hosts.Erase(pos);
     }

     depth++;
   } while ( (depth <= 4) && !stoprecursion);
   
   if (depth > 4)
     Error("Locate",
	   "The cluster exposes too many levels.");

   // At the end we want to rewind to the main redirector in any case
   fConnModule->GoBackToRedirector();

   return (hosts.GetSize() > 0);
}

bool XrdClientAdmin::Truncate(const char *path, long long newsize) {

   ClientRequest truncateRequest;
   int l = strlen(path);
   if (!l) return false;

   memset( &truncateRequest, 0, sizeof(truncateRequest) );

   fConnModule->SetSID(truncateRequest.header.streamid);

   truncateRequest.header.requestid     = kXR_truncate;
   truncateRequest.truncate.offset      = newsize;

   truncateRequest.header.dlen = l;

   bool ret = fConnModule->SendGenCommand(&truncateRequest, path,
                                          NULL, NULL , FALSE, (char *)"Truncate");

   return ret;


}



// Quickly jump to the former redirector. Useful after having been redirected.
void XrdClientAdmin::GoBackToRedirector() {

  if (fConnModule)
    fConnModule->GoBackToRedirector();



}
