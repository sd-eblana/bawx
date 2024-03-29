/*
* UPnP Support for XBMC
* Copyright (c) 2006 c0diq (Sylvain Rebaud)
* Portions Copyright (c) by the authors of libPlatinum
*
* http://www.plutinosoft.com/blog/category/platinum/
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
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#include "Util.h"
#include "Application.h"

#include "utils/Network.h"
#include "utils/log.h"
#include "UPnP.h"
#include "FileSystem/UPnPVirtualPathDirectory.h"
#include "FileSystem/MusicDatabaseDirectory.h"
#include "FileSystem/VideoDatabaseDirectory.h"
#include "MusicDatabase.h"
#include "VideoDatabase.h"
#include "FileSystem/VideoDatabaseDirectory/DirectoryNode.h"
#include "FileSystem/VideoDatabaseDirectory/QueryParams.h"
#include "File.h"
#include "NptStrings.h"
#include "Platinum.h"
#include "PltMediaConnect.h"
#include "PltMediaRenderer.h"
#include "PltSyncMediaBrowser.h"
#include "PltDidl.h"
#include "NptNetwork.h"
#include "NptConsole.h"
#include "MusicInfoTag.h"
#include "FileSystem/Directory.h"
#include "URL.h"
#include "GUISettings.h"
#include "GUIUserMessages.h"
#include "Settings.h"
#include "StringUtils.h"
#include "FileItem.h"
#include "GUIWindowManager.h"
#include "GUIInfoManager.h"
#include "utils/TimeUtils.h"

#include <map>

using namespace std;
using namespace MUSIC_INFO;
using namespace DIRECTORY;
using namespace XFILE;

extern CGUIInfoManager g_infoManager;

NPT_SET_LOCAL_LOGGER("xbmc.upnp")

#define UPNP_DEFAULT_MAX_RETURNED_ITEMS 200
#define UPNP_DEFAULT_MIN_RETURNED_ITEMS 30

/*
# Play speed
#    1 normal
#    0 invalid
DLNA_ORG_PS = 'DLNA.ORG_PS'
DLNA_ORG_PS_VAL = '1'

# Convertion Indicator
#    1 transcoded
#    0 not transcoded
DLNA_ORG_CI = 'DLNA.ORG_CI'
DLNA_ORG_CI_VAL = '0'

# Operations
#    00 not time seek range, not range 
#    01 range supported
#    10 time seek range supported
#    11 both supported
DLNA_ORG_OP = 'DLNA.ORG_OP'
DLNA_ORG_OP_VAL = '01'

# Flags
#    senderPaced                      80000000  31
#    lsopTimeBasedSeekSupported       40000000  30
#    lsopByteBasedSeekSupported       20000000  29
#    playcontainerSupported           10000000  28
#    s0IncreasingSupported            08000000  27  
#    sNIncreasingSupported            04000000  26  
#    rtspPauseSupported               02000000  25  
#    streamingTransferModeSupported   01000000  24  
#    interactiveTransferModeSupported 00800000  23  
#    backgroundTransferModeSupported  00400000  22  
#    connectionStallingSupported      00200000  21  
#    dlnaVersion15Supported           00100000  20  
DLNA_ORG_FLAGS = 'DLNA.ORG_FLAGS'
DLNA_ORG_FLAGS_VAL = '01500000000000000000000000000000'
*/

/*----------------------------------------------------------------------
|   static
+---------------------------------------------------------------------*/
CUPnP* CUPnP::upnp = NULL;
// change to false for XBMC_PC if you want real UPnP functionality
// otherwise keep to true for xbox as it doesn't support multicast
// don't change unless you know what you're doing!
bool CUPnP::broadcast = true; 

namespace
{
  static const NPT_String JoinString(const NPT_List<NPT_String>& array, const NPT_String& delimiter)
{
    NPT_String result;

    for(NPT_List<NPT_String>::Iterator it = array.GetFirstItem(); it; it++ )
        result += (*it) + delimiter;

    if(result.IsEmpty())
        return "";
    else
        return result.SubString(delimiter.GetLength());
  }
}


/*----------------------------------------------------------------------
|   NPT_Console::Output
+---------------------------------------------------------------------*/
void 
NPT_Console::Output(const char* message)
{
    CLog::Log(LOGDEBUG, "%s", message);
}

/*----------------------------------------------------------------------
|   CDeviceHostReferenceHolder class
+---------------------------------------------------------------------*/
class CDeviceHostReferenceHolder
{
public:
    PLT_DeviceHostReference m_Device;
};

/*----------------------------------------------------------------------
|   CCtrlPointReferenceHolder class
+---------------------------------------------------------------------*/
class CCtrlPointReferenceHolder
{
public:
    PLT_CtrlPointReference m_CtrlPoint;
};

/*----------------------------------------------------------------------
|   CUPnPCleaner class
+---------------------------------------------------------------------*/
class CUPnPCleaner : public NPT_Thread
{
public:
    CUPnPCleaner(CUPnP* upnp) : NPT_Thread(true), m_UPnP(upnp) {}
    void Run() {
        delete m_UPnP;
    }

    CUPnP* m_UPnP;
};

/*----------------------------------------------------------------------
|   CUPnP::CUPnP
+---------------------------------------------------------------------*/
class CUPnPServer : public PLT_MediaServer, public PLT_FileMediaServerDelegate
{
public:
      CUPnPServer(const char*  file_root,
                      const char*  friendly_name,
                      bool         show_ip = false,
                      const char*  uuid = NULL,
                      NPT_UInt16   port = 0,
                      bool         port_rebind = false) :
      PLT_MediaServer(friendly_name,
                      show_ip,
                      uuid,
                      port,
                      port_rebind),
      PLT_FileMediaServerDelegate("/", file_root) {SetDelegate(this);}

    // PLT_MediaServer methods
    virtual NPT_Result OnBrowseMetadata(PLT_ActionReference&          action, 
                                        const char*                   object_id, 
                                        const char*                   filter,
                                        NPT_UInt32                    starting_index,
                                        NPT_UInt32                    requested_count,
                                        const NPT_List<NPT_String>&   sort_criteria,
                                        const PLT_HttpRequestContext& context);
    virtual NPT_Result OnBrowseDirectChildren(PLT_ActionReference&          action, 
                                              const char*		            object_id, 
                                              const char*                   filter,
                                              NPT_UInt32                    starting_index,
                                              NPT_UInt32                    requested_count,
                                              const NPT_List<NPT_String>&   sort_criteria, 
                                              const PLT_HttpRequestContext& context);
    virtual NPT_Result OnSearchContainer(PLT_ActionReference&          action, 
                                         const char*                   container_id, 
                                         const char*                   search_criteria,
                                         NPT_UInt32                    starting_index,
                                         NPT_UInt32                    requested_count,
                                         const NPT_List<NPT_String>&   sort_criteria, 
                                         const PLT_HttpRequestContext& context);

    // PLT_FileMediaServer methods
    virtual NPT_Result ServeFile(NPT_HttpRequest&              request, 
                                 const NPT_HttpRequestContext& context,
                                 NPT_HttpResponse&             response,
                                 const NPT_String&             file_path);

    // class methods
    static NPT_Result PopulateObjectFromTag(CMusicInfoTag&         tag,
                                            PLT_MediaObject&       object,
                                            NPT_String*            file_path = NULL,
                                            PLT_MediaItemResource* resource = NULL);
    static NPT_Result PopulateObjectFromTag(CVideoInfoTag&         tag,
                                            PLT_MediaObject&       object,
                                            NPT_String*            file_path = NULL,
                                            PLT_MediaItemResource* resource = NULL);
    static PLT_MediaObject* BuildObject(const CFileItem&              item,
                                 NPT_String&                   file_path,
                                 bool                          with_count,
                                        const PLT_HttpRequestContext* context = NULL,
                                        CUPnPServer*                  upnp_server = NULL);
    static const char* GetContentTypeFromExtension(const char* extension, const PLT_HttpRequestContext* context = NULL);
    static NPT_String  GetContentType(const CFileItem& item, const PLT_HttpRequestContext* context = NULL);
    static NPT_String  GetContentType(const char* filename, const PLT_HttpRequestContext* context = NULL);
    static const CStdString& CorrectAllItemsSortHack(const CStdString &item);

private:
    PLT_MediaObject* Build(CFileItemPtr                  item, 
                           bool                          with_count, 
                           const PLT_HttpRequestContext& context,
                           const char*                   parent_id = NULL);
    NPT_Result       BuildResponse(PLT_ActionReference&          action,
                                   CFileItemList&                items,
                                   const char*                   filter,
                                   NPT_UInt32                    starting_index,
                                   NPT_UInt32                    requested_count,
                                   const NPT_List<NPT_String>&   sort_criteria,
                                   const PLT_HttpRequestContext& context,
                                   const char*                   parent_id /* = NULL */);

    // class methods
    static NPT_String GetParentFolder(NPT_String file_path) {       
        int index = file_path.ReverseFind("\\");
        if (index == -1) return "";

        return file_path.Left(index);
    }
    static NPT_String GetProtocolInfo(const CFileItem& item, 
                                      const char* protocol, 
                                      const PLT_HttpRequestContext* context = NULL);

public:
    // class members
    static NPT_UInt32 m_MaxReturnedItems;
};

NPT_UInt32 CUPnPServer::m_MaxReturnedItems = 0;

/*----------------------------------------------------------------------
|   CUPnPServer::GetContentType
+---------------------------------------------------------------------*/
NPT_String
CUPnPServer::GetContentType(const char* filename, 
                            const PLT_HttpRequestContext* context /* = NULL */)
{
    NPT_String ext = CUtil::GetExtension(filename).c_str();
    ext.TrimLeft('.');
    ext = ext.ToLowercase();

    return PLT_MediaObject::GetUPnPClass(ext, context);
}

/*----------------------------------------------------------------------
|   CUPnPServer::GetContentType
+---------------------------------------------------------------------*/
NPT_String
CUPnPServer::GetContentType(const CFileItem& item, 
                            const PLT_HttpRequestContext* context /* = NULL */)
{
    NPT_String ext = CUtil::GetExtension(item.m_strPath).c_str();
    if (item.HasVideoInfoTag() && !item.GetVideoInfoTag()->m_strFileNameAndPath.IsEmpty()) {
        ext = CUtil::GetExtension(item.GetVideoInfoTag()->m_strFileNameAndPath);
    } else if (item.HasMusicInfoTag() && !item.GetMusicInfoTag()->GetURL().IsEmpty()) {
        ext = CUtil::GetExtension(item.GetMusicInfoTag()->GetURL());
    }
    ext.TrimLeft('.');
    ext = ext.ToLowercase();

    NPT_String content;

    /* We always use Platinum content type first
       as it is defined to map extension to DLNA compliant content type
       or custom according to context (who asked for it) */
    if (!ext.IsEmpty()) {
        content = PLT_MediaObject::GetUPnPClass(ext, context);
    if (content == "application/octet-stream") content = "";
    }

    /* if Platinum couldn't map it, default to XBMC mapping */
    if (content.IsEmpty()) {
        NPT_String content = item.GetContentType().c_str();
        if (content == "application/octet-stream") content = "";
    }

    /* fallback to generic content type if not found */
    if( content.IsEmpty() ) {      
        if (item.IsVideo() || item.IsVideoDb() )
            content = "video/" + ext;
        else if (item.IsAudio() || item.IsMusicDb() )
            content = "audio/" + ext;
        else if (item.IsPicture() )
            content = "image/" + ext;
    }
    
    /* nothing we can figure out */
    if( content.IsEmpty() ) {
        content = "application/octet-stream";
    }

    return content;
}

/*----------------------------------------------------------------------
|   CUPnPServer::GetProtocolInfo
+---------------------------------------------------------------------*/
NPT_String
CUPnPServer::GetProtocolInfo(const CFileItem&              item, 
                             const char*                   protocol,
                             const PLT_HttpRequestContext* context /* = NULL */)
{
    NPT_String proto = protocol;

    /* fixup the protocol just in case nothing was passed */
    if (proto.IsEmpty()) {
        proto = CURI(item.m_strPath).GetProtocol();
    }

    /*
       map protocol to right prefix and use xbmc-get for
       unsupported UPnP protocols for other xbmc clients
       TODO: add rtsp ?
    */
    if (proto == "http") {
        proto = "http-get";
    } else {
        proto = "xbmc-get";
    }

    /* we need a valid extension to retrieve the mimetype for the protocol info */
    NPT_String content = GetContentType(item, context);
    proto += ":*:" + content + ":" + PLT_MediaObject::GetUPnPClass(content, context);
    return proto;
}

/*----------------------------------------------------------------------
|   CUPnPServer::PopulateObjectFromTag
+---------------------------------------------------------------------*/
NPT_Result
CUPnPServer::PopulateObjectFromTag(CMusicInfoTag&         tag,
                                   PLT_MediaObject&       object,
                                   NPT_String*            file_path, /* = NULL */
                                   PLT_MediaItemResource* resource   /* = NULL */)
{
    // some usefull buffers
    CStdStringArray strings;

    if (!tag.GetURL().IsEmpty() && file_path)
      *file_path = tag.GetURL();

    StringUtils::SplitString(tag.GetGenre(), " / ", strings);
    for(CStdStringArray::iterator it = strings.begin(); it != strings.end(); it++) {
        object.m_Affiliation.genres.Add((*it).c_str());
    }

    object.m_Title = tag.GetTitle();
    object.m_Affiliation.album = tag.GetAlbum();
    object.m_People.artists.Add(tag.GetArtist().c_str());
    object.m_People.artists.Add(tag.GetArtist().c_str(), "Performer");
    object.m_People.artists.Add(!tag.GetAlbumArtist().empty()?tag.GetAlbumArtist().c_str():tag.GetArtist().c_str(), "AlbumArtist");
    object.m_Creator = tag.GetArtist();
    object.m_MiscInfo.original_track_number = tag.GetTrackNumber();
    if (resource) resource->m_Duration = tag.GetDuration();

    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPServer::PopulateObjectFromTag
+---------------------------------------------------------------------*/
NPT_Result
CUPnPServer::PopulateObjectFromTag(CVideoInfoTag&         tag,
                                   PLT_MediaObject&       object,
                                   NPT_String*            file_path, /* = NULL */
                                   PLT_MediaItemResource* resource   /* = NULL */)
{
    // some usefull buffers
    CStdStringArray strings;

    if (!tag.m_strFileNameAndPath.IsEmpty() && file_path)
      *file_path = tag.m_strFileNameAndPath;

    if (tag.m_iDbId != -1 ) {
        if (tag.m_strShowTitle.IsEmpty()) {
          object.m_ObjectClass.type = "object.item.videoItem"; // XBox 360 wants object.item.videoItem instead of object.item.videoItem.movie, is WMP happy?
          object.m_Affiliation.album = "[Unknown Series]"; // required to make WMP to show title
          object.m_Title = tag.m_strTitle;
        } else {
          object.m_ObjectClass.type = "object.item.videoItem.videoBroadcast";
          object.m_Affiliation.album = tag.m_strShowTitle;
          object.m_Title = tag.m_strShowTitle + " - ";
          object.m_Title += "S" + ("0" + NPT_String::FromInteger(tag.m_iSeason)).Right(2);
          object.m_Title += "E" + ("0" + NPT_String::FromInteger(tag.m_iEpisode)).Right(2);
          object.m_Title += " : " + tag.m_strTitle;
        }
    }

    StringUtils::SplitString(tag.m_strGenre, " / ", strings);
    for(CStdStringArray::iterator it = strings.begin(); it != strings.end(); it++) {
        object.m_Affiliation.genres.Add((*it).c_str());
}

    for(CVideoInfoTag::iCast it = tag.m_cast.begin();it != tag.m_cast.end();it++) {
        object.m_People.actors.Add(it->strName.c_str(), it->strRole.c_str());
    }
    object.m_People.director = tag.m_strDirector;

    object.m_Description.description = tag.m_strTagLine;
    object.m_Description.long_description = tag.m_strPlot;
    if (resource) resource->m_Duration = StringUtils::TimeStringToSeconds(tag.m_strRuntime.c_str());

    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPServer::CorrectAllItemsSortHack
+---------------------------------------------------------------------*/
const CStdString&
CUPnPServer::CorrectAllItemsSortHack(const CStdString &item)
{
    // This is required as in order for the "* All Albums" etc. items to sort
    // correctly, they must have fake artist/album etc. information generated.
    // This looks nasty if we attempt to render it to the GUI, thus this (further)
    // workaround
    if ((item.size() == 1 && item[0] == 0x01) || (item.size() > 1 && ((unsigned char) item[1]) == 0xff))
        return StringUtils::EmptyString;

    return item;
}

/*----------------------------------------------------------------------
|   CUPnPServer::BuildObject
+---------------------------------------------------------------------*/
PLT_MediaObject*
CUPnPServer::BuildObject(const CFileItem&              item,
                         NPT_String&                   file_path,
                         bool                          with_count,
                         const PLT_HttpRequestContext* context /* = NULL */,
                         CUPnPServer*                  upnp_server /* = NULL */)
{
    PLT_MediaItemResource resource;
    PLT_MediaObject*      object = NULL;

    CLog::Log(LOGDEBUG, "Building didl for object '%s'", (const char*)item.m_strPath);

    // get list of ip addresses
    NPT_List<NPT_IpAddress> ips;
    NPT_CHECK_LABEL(PLT_UPnPMessageHelper::GetIPAddresses(ips), failure);

    // if we're passed an interface where we received the request from
    // move the ip to the top
    if (context && context->GetLocalAddress().GetIpAddress().ToString() != "0.0.0.0") {
        ips.Remove(context->GetLocalAddress().GetIpAddress());
        ips.Insert(ips.GetFirstItem(), context->GetLocalAddress().GetIpAddress());
    }

    if (!item.m_bIsFolder) {
        object = new PLT_MediaItem();
        object->m_ObjectID = item.m_strPath;

        /* Setup object type */
        if (item.IsMusicDb() || item.IsAudio()) {
            object->m_ObjectClass.type = "object.item.audioItem.musicTrack";
          
            if (item.HasMusicInfoTag()) {
                CMusicInfoTag *tag = (CMusicInfoTag*)item.GetMusicInfoTag();
                PopulateObjectFromTag(*tag, *object, &file_path, &resource);
                }
        } else if (item.IsVideoDb() || item.IsVideo()) {
            object->m_ObjectClass.type = "object.item.videoItem";
            object->m_Affiliation.album = "[Unknown Series]"; // required to make WMP to show title

            if (item.HasVideoInfoTag()) {
                CVideoInfoTag *tag = (CVideoInfoTag*)item.GetVideoInfoTag();
                PopulateObjectFromTag(*tag, *object, &file_path, &resource);
                    }
        } else if (item.IsPicture()) {
            object->m_ObjectClass.type = "object.item.imageItem.photo";
        } else {
            object->m_ObjectClass.type = "object.item";
        }
        
        // duration of zero is invalid
        if (resource.m_Duration == 0) resource.m_Duration = -1;

        // Set the resource file size
        resource.m_Size = item.m_dwSize;
        if (resource.m_Size == 0) {
            struct __stat64 info;
            if(CFile::Stat((const char*)file_path, &info) >= 0 && info.st_size >= 0) 
              resource.m_Size = info.st_size;
        }

        // set date
        if (item.m_dateTime.IsValid()) {
            object->m_Date = item.m_dateTime.GetAsLocalizedDate();
        }

        if (upnp_server) {
        // iterate through ip addresses and build list of resources
            // through http file server
            NPT_List<NPT_IpAddress>::Iterator ip = ips.GetFirstItem();
        while (ip) {
                resource.m_ProtocolInfo = PLT_ProtocolInfo(GetProtocolInfo(item, "http", context));
                resource.m_Uri = PLT_FileMediaServer::BuildSafeResourceUri(
                    upnp_server->m_URLBase, (*ip).ToString(), file_path);
            object->m_Resources.Add(resource);
            ++ip;
        }
        }

        // if the item is remote, add a direct link to the item
        if (CUtil::IsRemote((const char*)file_path)) {
            resource.m_ProtocolInfo = PLT_ProtocolInfo(
                CUPnPServer::GetProtocolInfo(item, CURI(item.m_strPath).GetProtocol(), context));
            resource.m_Uri = file_path;

            // if the direct link can be served directly using http, then push it in front
            // otherwise keep the xbmc-get resource last and let a compatible client look for it
            if (resource.m_ProtocolInfo.ToString().StartsWith("xbmc", true)) {
                object->m_Resources.Add(resource);
    } else {
                object->m_Resources.Insert(object->m_Resources.GetFirstItem(), resource);
            }
        }

        // Some upnp clients expect all audio items to have parent root id 4
#ifdef WMP_ID_MAPPING
        object->m_ParentID = "4";
#endif
    } else {
        PLT_MediaContainer* container = new PLT_MediaContainer;
        object = container;

        /* Assign a title and id for this container */
        container->m_ObjectID = item.m_strPath;
        container->m_ObjectClass.type = "object.container";
        container->m_ChildrenCount = -1;

        /* this might be overkill, but hey */
        if (item.IsMusicDb()) {
            MUSICDATABASEDIRECTORY::NODE_TYPE node = CMusicDatabaseDirectory::GetDirectoryType(item.m_strPath);
            switch(node) {
                case MUSICDATABASEDIRECTORY::NODE_TYPE_ARTIST: {
                  container->m_ObjectClass.type += ".person.musicArtist";
                      CMusicInfoTag *tag = (CMusicInfoTag*)item.GetMusicInfoTag();
                      if (tag) {
                          container->m_People.artists.Add(
                              CorrectAllItemsSortHack(tag->GetArtist()).c_str(), "Performer");
                          container->m_People.artists.Add(
                              CorrectAllItemsSortHack(!tag->GetAlbumArtist().empty()?tag->GetAlbumArtist():tag->GetArtist()).c_str(), "AlbumArtist");
                      }
#ifdef WMP_ID_MAPPING
                      // Some upnp clients expect all artists to have parent root id 107
                      container->m_ParentID = "107";
#endif
                  }
                  break;
                case MUSICDATABASEDIRECTORY::NODE_TYPE_ALBUM:
                case MUSICDATABASEDIRECTORY::NODE_TYPE_ALBUM_COMPILATIONS:
                case MUSICDATABASEDIRECTORY::NODE_TYPE_ALBUM_RECENTLY_ADDED:
                case MUSICDATABASEDIRECTORY::NODE_TYPE_YEAR_ALBUM: {
                  container->m_ObjectClass.type += ".album.musicAlbum";
                      // for Sonos to be happy
                      CMusicInfoTag *tag = (CMusicInfoTag*)item.GetMusicInfoTag();
                      if (tag) {
                          container->m_People.artists.Add(
                              CorrectAllItemsSortHack(tag->GetArtist()).c_str(), "Performer");
                          container->m_People.artists.Add(
                              CorrectAllItemsSortHack(!tag->GetAlbumArtist().empty()?tag->GetAlbumArtist():tag->GetArtist()).c_str(), "AlbumArtist");
                          container->m_Affiliation.album = CorrectAllItemsSortHack(tag->GetAlbum()).c_str();
                      }
#ifdef WMP_ID_MAPPING
                      // Some upnp clients expect all albums to have parent root id 7
                      container->m_ParentID = "7";
#endif
                  }
                  break;
                case MUSICDATABASEDIRECTORY::NODE_TYPE_GENRE:
                  container->m_ObjectClass.type += ".genre.musicGenre";
                  break;
                default:
                  break;
            }
        } else if (item.IsVideoDb()) {
            VIDEODATABASEDIRECTORY::NODE_TYPE node = CVideoDatabaseDirectory::GetDirectoryType(item.m_strPath);
            switch(node) {
                case VIDEODATABASEDIRECTORY::NODE_TYPE_GENRE:
                  container->m_ObjectClass.type += ".storageFolder";//".genre.movieGenre"; //360 wants object.container.storageFolder
                  break;
                case VIDEODATABASEDIRECTORY::NODE_TYPE_MOVIES_OVERVIEW:
                  container->m_ObjectClass.type += ".storageFolder";
                  break;
                default:
                  container->m_ObjectClass.type += ".storageFolder";
                  break;
            }
        } else if (item.IsPlayList()) {
            container->m_ObjectClass.type += ".playlistContainer";
        }

        /* Get the number of children for this container */
        if (with_count && upnp_server) {
            if( object->m_ObjectID.StartsWith("virtualpath://") ) {
              NPT_LargeSize count = 0;
                NPT_CHECK_LABEL(NPT_File::GetSize(file_path, count), failure);
                container->m_ChildrenCount = count;
            } else {
                /* this should be a standard path */
                // TODO - get file count of this directory
            }
        }        
    }
    
    // set a title for the object
    if( object->m_Title.IsEmpty() ) {
        if (!item.GetLabel().IsEmpty()) {
            CStdString title = item.GetLabel();
            if (item.IsPlayList() || !item.m_bIsFolder) CUtil::RemoveExtension(title);
            object->m_Title = title;
        } else {
            CStdString title, volumeNumber;
            CUtil::GetVolumeFromFileName(item.m_strPath, title, volumeNumber);
            if (!item.m_bIsFolder) CUtil::RemoveExtension(title);
            object->m_Title = title;
        }
    }
    // set a thumbnail if we have one
    if (item.HasThumbnail() && upnp_server) {
        object->m_ExtraInfo.album_art_uri = PLT_FileMediaServer::BuildSafeResourceUri(
            upnp_server->m_URLBase,
            (*ips.GetFirstItem()).ToString(),
            item.GetThumbnailImage());
    }

    return object;

failure:
      delete object;
    return NULL;
}

/*----------------------------------------------------------------------
|   CUPnPServer::Build
+---------------------------------------------------------------------*/
PLT_MediaObject* 
CUPnPServer::Build(CFileItemPtr                  item, 
                   bool                          with_count, 
                   const PLT_HttpRequestContext& context,
                   const char*                   parent_id /* = NULL */)
{
    PLT_MediaObject* object = NULL;
    NPT_String       path = item->m_strPath.c_str();
    NPT_String       share_name;
    NPT_String       file_path;

    //HACK: temporary disabling count as it thrashes HDD
    with_count = false;

    CLog::Log(LOGDEBUG, "Preparing upnp object for item '%s'", (const char*)path);

    if (!CUPnPVirtualPathDirectory::SplitPath(path, share_name, file_path)) {
        // db path handling

      file_path = item->m_strPath;
      share_name = "";

      if (path.StartsWith("boxeedb://music") || path.StartsWith("boxeedb://album") || path.StartsWith("boxeedb://artist") ) {
          CStdString label;
          if( path == "boxeedb://music" ) {              
              item->SetLabel("Music Library");
              item->SetLabelPreformated(true);
          } else {
              if( !item->HasMusicInfoTag() || !item->GetMusicInfoTag()->Loaded() )
                  item->LoadMusicTag();

              if( !item->HasThumbnail() )
                  item->SetCachedMusicThumb();

              if( item->GetLabel().IsEmpty() ) {
                  /* if no label try to grab it from node type */
                  if( CMusicDatabaseDirectory::GetLabel((const char*)path, label) ) {
                      item->SetLabel(label);
                      item->SetLabelPreformated(true);
                  }
              }
          }
      } else if (file_path.StartsWith("boxeedb://video")) {
          CStdString label;
          if( path == "boxeedb://video" ) {
              item->SetLabel("Video Library");
              item->SetLabelPreformated(true);
          } else {
              if( !item->HasVideoInfoTag() ) {
                  DIRECTORY::VIDEODATABASEDIRECTORY::CQueryParams params;
                  DIRECTORY::VIDEODATABASEDIRECTORY::CDirectoryNode::GetDatabaseInfo((const char*)path, params);

                  CVideoDatabase db;
                    if (!db.Open() ) return NULL;

                  if( params.GetMovieId() >= 0 )
                      db.GetMovieInfo((const char*)path, *item->GetVideoInfoTag(), params.GetMovieId());
                  else if( params.GetEpisodeId() >= 0 )
                      db.GetEpisodeInfo((const char*)path, *item->GetVideoInfoTag(), params.GetEpisodeId());
                  else if( params.GetTvShowId() >= 0 )
                      db.GetTvShowInfo((const char*)path, *item->GetVideoInfoTag(), params.GetTvShowId());
              }

              // try to grab title from tag
              if( item->HasVideoInfoTag() && !item->GetVideoInfoTag()->m_strTitle.IsEmpty() ) {
                  item->SetLabel( item->GetVideoInfoTag()->m_strTitle );
                  item->SetLabelPreformated(true);
              }

              // try to grab it from the folder
              if( item->GetLabel().IsEmpty() ) {
                  if( CVideoDatabaseDirectory::GetLabel((const char*)path, label) ) {
                      item->SetLabel(label);
                      item->SetLabelPreformated(true);
                  }
              }

              if( !item->HasThumbnail() )
                  item->SetCachedVideoThumb();
          }
      }

      //not a virtual path directory, new system
        object = BuildObject(*item.get(), file_path, with_count, &context, this);

        // set parent id if passed, otherwise it should have been determined
        if (object && parent_id) {
        object->m_ParentID = parent_id;
    }
    } else {
        // virtualpath:// handling

    path.TrimRight("/");
    if (file_path.GetLength()) {
        // make sure the path starts with something that is shared given the share
        if (!CUPnPVirtualPathDirectory::FindSourcePath(share_name, file_path, true)) goto failure;
        
        // this is not a virtual directory
            object = BuildObject(*item.get(), file_path, with_count, &context, this);
        if (!object) goto failure;

        // override object id & change the class if it's an item
        // and it's not been set previously
        if (object->m_ObjectClass.type == "object.item") {
            if (share_name == "virtualpath://upnpmusic")
                object->m_ObjectClass.type = "object.item.audioitem";
            else if (share_name == "virtualpath://upnpvideo")
                object->m_ObjectClass.type = "object.item.videoitem";
            else if (share_name == "virtualpath://upnppictures")
                object->m_ObjectClass.type = "object.item.imageitem";
        }

        if (parent_id) {
            object->m_ParentID = parent_id;
        } else {
            // populate parentid manually
            if (CUPnPVirtualPathDirectory::FindSourcePath(share_name, file_path)) {
                // found the file_path as one of the path of the share
                // this means the parent id is the share
                object->m_ParentID = share_name;
            } else {
                // we didn't find the path, find the parent path
                NPT_String parent_path = GetParentFolder(file_path);
                if (parent_path.IsEmpty()) goto failure;

                // try again with parent
                if (CUPnPVirtualPathDirectory::FindSourcePath(share_name, parent_path)) {
                    // found the file_path parent folder as one of the path of the share
                    // this means the parent id is the share
                    object->m_ParentID = share_name;
                } else {
                    object->m_ParentID = share_name + "/" + parent_path;
                }
            }
        }

        // old style, needs virtual path prefix
        if( !object->m_ObjectID.StartsWith("virtualpath://") )
            object->m_ObjectID = share_name + "/" + object->m_ObjectID;

    } else {
        object = new PLT_MediaContainer;
        object->m_Title = item->GetLabel();
        object->m_ObjectClass.type = "object.container";
        object->m_ObjectID = path;

        if (path == "virtualpath://upnproot") {
            // root
            object->m_ObjectID = "0";
            object->m_ParentID = "-1";
            // root has 5 children
                if (with_count) {
                    ((PLT_MediaContainer*)object)->m_ChildrenCount = 5;
                }
        } else if (share_name.GetLength() == 0) {
            // no share_name means it's virtualpath://X where X=music, video or pictures
            object->m_ParentID = "0";
            if (with_count || true) { // we can always count these, it's quick
                ((PLT_MediaContainer*)object)->m_ChildrenCount = 0;

                // look up number of shares
                VECSOURCES *shares = NULL;
                if (path == "virtualpath://upnpmusic") {
                    shares = g_settings.GetSourcesFromType("upnpmusic");
                } else if (path == "virtualpath://upnpvideo") {
                    shares = g_settings.GetSourcesFromType("upnpvideo");
                } else if (path == "virtualpath://upnppictures") {
                    shares = g_settings.GetSourcesFromType("upnppictures");
                }

                // use only shares that would some path with local files
                if (shares) {
                    CUPnPVirtualPathDirectory dir;
                    for (unsigned int i = 0; i < shares->size(); i++) {
                        // Does this share contains any local paths?
                        CMediaSource &share = shares->at(i);
                        vector<CStdString> paths;

                        // reconstruct share name as it could have been replaced by
                        // a path if there was just one entry
                        NPT_String share_name = path + "/";
                        share_name += share.strName;
                        if (dir.GetMatchingSource((const char*)share_name, share, paths) && paths.size()) {
                            ((PLT_MediaContainer*)object)->m_ChildrenCount++;
                        }
                    }
                }
            }
        } else {
                // this is a share name
            CStdString mask;
            if (share_name.StartsWith("virtualpath://upnpmusic")) {
                object->m_ParentID = "virtualpath://upnpmusic";
                mask = g_stSettings.m_musicExtensions;
            } else if (share_name.StartsWith("virtualpath://upnpvideo")) {
                object->m_ParentID = "virtualpath://upnpvideo";
                mask = g_stSettings.m_videoExtensions;
            } else if (share_name.StartsWith("virtualpath://upnppictures")) {
                object->m_ParentID = "virtualpath://upnppictures";
                mask = g_stSettings.m_pictureExtensions;
            } else {
                // weird!
                goto failure;
            }

            if (with_count) {
                ((PLT_MediaContainer*)object)->m_ChildrenCount = 0;

                // get all the paths for a given share
                CMediaSource share;
                CUPnPVirtualPathDirectory dir;
                vector<CStdString> paths;
                if (!dir.GetMatchingSource((const char*)share_name, share, paths)) goto failure;
                for (unsigned int i=0; i<paths.size(); i++) {
                    // FIXME: this is not efficient, we only need the number of items given a mask
                    // and not the list of items

                    // retrieve all the files for a given path
                   CFileItemList items;
                   if (CDirectory::GetDirectory(paths[i], items, mask)) {
                       // update childcount
                       ((PLT_MediaContainer*)object)->m_ChildrenCount += items.Size();
                   }
                }
            }
        }
    }
    }

    // remap Root virtualpath://upnproot/ to id "0"
    if (object->m_ObjectID == "virtualpath://upnproot/")
        object->m_ObjectID = "0";

    // remap Parent Root virtualpath://upnproot/ to id "0"
    if (object->m_ParentID == "virtualpath://upnproot/")
        object->m_ParentID = "0";

    return object;

failure:
      delete object;
    return NULL;
}

/*----------------------------------------------------------------------
|   TranslateWMPObjectId
+---------------------------------------------------------------------*/
static NPT_String TranslateWMPObjectId(NPT_String id)
{
    if (id == "0") {
        id = "virtualpath://upnproot/";
    } else if (id == "15") {
        // Xbox 360 asking for videos
        id = "videodb://";
    } else if (id == "16") {
        // Xbox 360 asking for photos
    } else if (id == "107") {
        // Sonos uses 107 for artists root container id
        id = "musicdb://2/";
    } else if (id == "7") {
        // Sonos uses 7 for albums root container id
        id = "musicdb://3/";
    } else if (id == "4") {
        // Sonos uses 4 for tracks root container id
        id = "musicdb://4/";
    }

    CLog::Log(LOGDEBUG, "UPnP Translated id to '%s'", (const char*)id);
    return id;
}

/*----------------------------------------------------------------------
|   CUPnPServer::OnBrowseMetadata
+---------------------------------------------------------------------*/
NPT_Result
CUPnPServer::OnBrowseMetadata(PLT_ActionReference&          action, 
                              const char*                   object_id, 
                              const char*                   filter,
                              NPT_UInt32                    starting_index,
                              NPT_UInt32                    requested_count,
                              const NPT_List<NPT_String>&   sort_criteria,
                              const PLT_HttpRequestContext& context)
{
    NPT_COMPILER_UNUSED(sort_criteria);
    NPT_COMPILER_UNUSED(requested_count);
    NPT_COMPILER_UNUSED(starting_index);

    NPT_String                     didl;
    NPT_Reference<PLT_MediaObject> object;
    NPT_String                     id = TranslateWMPObjectId(object_id);
    CMediaSource                   share;
    CUPnPVirtualPathDirectory      dir;
    vector<CStdString>             paths;
    CFileItemPtr                   item;

    CLog::Log(LOGINFO, "Received UPnP Browse Metadata request for object '%s'", (const char*)object_id);

    if (id.StartsWith("virtualpath://")) {
        id.TrimRight("/");
        if (id == "virtualpath://upnproot") {
            id += "/";
            item.reset(new CFileItem((const char*)id, true));
            item->SetLabel("Root");
            item->SetLabelPreformated(true);
            object = Build(item, true, context);
        } else if (id == "virtualpath://upnpmusic") {
            id += "/";
            item.reset(new CFileItem((const char*)id, true));
            item->SetLabel("Music Files");
            item->SetLabelPreformated(true);
            object = Build(item, true, context);
        } else if (id == "virtualpath://upnpvideo") {
            id += "/";
            item.reset(new CFileItem((const char*)id, true));
            item->SetLabel("Video Files");
            item->SetLabelPreformated(true);
            object = Build(item, true, context);
        } else if (id == "virtualpath://upnppictures") {
            id += "/";
            item.reset(new CFileItem((const char*)id, true));
            item->SetLabel("Picture Files");
            item->SetLabelPreformated(true);
            object = Build(item, true, context);
        } else if (dir.GetMatchingSource((const char*)id, share, paths)) {
            id += "/";
            item.reset(new CFileItem((const char*)id, true));
            item->SetLabel(share.strName);
            item->SetLabelPreformated(true);
            object = Build(item, true, context);
        } else {
            NPT_String share_name, file_path;
            if (!CUPnPVirtualPathDirectory::SplitPath(id, share_name, file_path)) 
                return NPT_FAILURE;

            NPT_String parent_path = GetParentFolder(file_path);
            if (parent_path.IsEmpty()) return NPT_FAILURE;

            NPT_FileInfo info;
            NPT_CHECK(NPT_File::GetInfo(file_path, &info));

            item.reset(new CFileItem((const char*)id, (info.m_Type==NPT_FileInfo::FILE_TYPE_DIRECTORY)?true:false));
            item->SetLabel((const char*)file_path.SubString(parent_path.GetLength()+1));
            item->SetLabelPreformated(true);

            // get file size
            if (info.m_Type == NPT_FileInfo::FILE_TYPE_REGULAR) {
                item->m_dwSize = info.m_Size;
            }

            object = Build(item, true, context);
        }
    } else {
        // determine if it's a container by calling CDirectory::Exists
        item.reset(new CFileItem((const char*)id, CDirectory::Exists((const char*)id)));

        // determine parent id for shared paths only
        // otherwise let db find out
        CStdString parent;
        if (!CUtil::GetParentPath((const char*)id, parent)) parent = "0";

//#ifdef WMP_ID_MAPPING
//        if (!id.StartsWith("musicdb://") && !id.StartsWith("videodb://")) {
//            parent = "";
//        }
//#endif

        object = Build(item, true, context, parent.empty()?NULL:parent.c_str());
    }

    if (object.IsNull()) {
        /* error */
        NPT_LOG_WARNING_1("CUPnPServer::OnBrowseMetadata - Object null (%s)", object_id);
        action->SetError(701, "No Such Object.");
        return NPT_FAILURE;
    }

    NPT_String tmp;    
    NPT_CHECK(PLT_Didl::ToDidl(*object.AsPointer(), filter, tmp));

    /* add didl header and footer */
    didl = didl_header + tmp + didl_footer;

    NPT_CHECK(action->SetArgumentValue("Result", didl));
    NPT_CHECK(action->SetArgumentValue("NumberReturned", "1"));
    NPT_CHECK(action->SetArgumentValue("TotalMatches", "1"));

    // update ID may be wrong here, it should be the one of the container?
    NPT_CHECK(action->SetArgumentValue("UpdateId", "0"));

    // TODO: We need to keep track of the overall SystemUpdateID of the CDS

    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPServer::OnBrowseDirectChildren
+---------------------------------------------------------------------*/
NPT_Result
CUPnPServer::OnBrowseDirectChildren(PLT_ActionReference&          action, 
                                    const char*                   object_id, 
                                    const char*                   filter,
                                    NPT_UInt32                    starting_index,
                                    NPT_UInt32                    requested_count,
                                    const NPT_List<NPT_String>&   sort_criteria,
                                    const PLT_HttpRequestContext& context)
{
    CFileItemList items;
    NPT_String    parent_id = TranslateWMPObjectId(object_id);

    CLog::Log(LOGINFO, "Received UPnP Browse DirectChildren request for object '%s'", (const char*)object_id);
    
    items.m_strPath = parent_id;
    if (!items.Load()) {
        // cache anything that takes more than a second to retrieve
        unsigned int time = CTimeUtils::GetTimeMS() + 1000;

        if (parent_id.StartsWith("virtualpath://")) {
            CUPnPVirtualPathDirectory dir;
            dir.GetDirectory((const char*)parent_id, items);
        } else {
            CDirectory::GetDirectory((const char*)parent_id, items);
        }

        if (items.CacheToDiscAlways() || (items.CacheToDiscIfSlow() && time < CTimeUtils::GetTimeMS())) {
          items.Save();
    }
    }

    // Don't pass parent_id if action is Search not BrowseDirectChildren, as
    // we want the engine to determine the best parent id, not necessarily the one
    // passed
    NPT_String action_name = action->GetActionDesc().GetName();
    return BuildResponse(
        action, 
        items, 
        filter,
        starting_index,
        requested_count,
        sort_criteria,
        context,
        (action_name.Compare("Search", true)==0)?NULL:parent_id.GetChars());
}

/*----------------------------------------------------------------------
|   CUPnPServer::BuildResponse
+---------------------------------------------------------------------*/
NPT_Result
CUPnPServer::BuildResponse(PLT_ActionReference&          action, 
                           CFileItemList&                items, 
                           const char*                   filter,
                           NPT_UInt32                    starting_index,
                           NPT_UInt32                    requested_count,
                           const NPT_List<NPT_String>&   sort_criteria,
                           const PLT_HttpRequestContext& context,
                           const char*                   parent_id /* = NULL */)
{
    NPT_COMPILER_UNUSED(sort_criteria);

    CLog::Log(LOGDEBUG, "Building UPnP response with filter '%s', starting @ %d with %d requested",
        (const char*)filter,
        starting_index,
        requested_count);

    // won't return more than UPNP_MAX_RETURNED_ITEMS items at a time to keep things smooth
    // 0 requested means as many as possible
    NPT_UInt32 max_count  = (requested_count == 0)?m_MaxReturnedItems:min((unsigned long)requested_count, (unsigned long)m_MaxReturnedItems);
    NPT_UInt32 stop_index = min((unsigned long)(starting_index + max_count), (unsigned long)items.Size()); // don't return more than we can

    NPT_Cardinal count = 0;
    NPT_String didl = didl_header;
    PLT_MediaObjectReference object;
    for (unsigned long i=starting_index; i<stop_index; ++i) {
        object = Build(items[i], true, context, parent_id);
        if (object.IsNull()) {
            continue;
        }

        NPT_String tmp;
        NPT_CHECK(PLT_Didl::ToDidl(*object.AsPointer(), filter, tmp));

        // Neptunes string growing is dead slow for small additions
        if(didl.GetCapacity() < tmp.GetLength() + didl.GetLength()) {
            didl.Reserve((tmp.GetLength() + didl.GetLength())*2);
        }
        didl += tmp;
        ++count;
    }

    didl += didl_footer;

    CLog::Log(LOGDEBUG, "Returning UPnP response with %d items out of %d total matches",
        count,
        items.Size());

    NPT_CHECK(action->SetArgumentValue("Result", didl));
    NPT_CHECK(action->SetArgumentValue("NumberReturned", NPT_String::FromInteger(count)));
    NPT_CHECK(action->SetArgumentValue("TotalMatches", NPT_String::FromInteger(items.Size())));
    NPT_CHECK(action->SetArgumentValue("UpdateId", "0"));
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   FindSubCriteria
+---------------------------------------------------------------------*/
static
NPT_String 
FindSubCriteria(NPT_String criteria, const char* name)
{
    NPT_String result;
    int search = criteria.Find(name);
    if (search >= 0) {
        criteria = criteria.Right(criteria.GetLength() - search - NPT_StringLength(name));
        criteria.TrimLeft(" ");
        if (criteria.GetLength()>0 && criteria[0] == '=') {
            criteria.TrimLeft("= ");
            if (criteria.GetLength()>0 && criteria[0] == '\"') {
                search = criteria.Find("\"", 1);
                if (search > 0) result = criteria.SubString(1, search-1);
            }
        }
    }
    return result;
}

/*----------------------------------------------------------------------
|   CUPnPServer::OnSearchContainer
+---------------------------------------------------------------------*/
NPT_Result
CUPnPServer::OnSearchContainer(PLT_ActionReference&          action, 
                               const char*                   object_id, 
                               const char*                   search_criteria,
                               NPT_UInt32                    starting_index,
                               NPT_UInt32                    requested_count,
                               const NPT_List<NPT_String>&   sort_criteria,
                               const PLT_HttpRequestContext& context)

{
    CLog::Log(LOGDEBUG, "Received Search request for object '%s' with search '%s'", 
        (const char*)object_id,
        (const char*)search_criteria);

    NPT_String id = object_id;
    if (id.StartsWith("boxeedb://music")) {
        // we browse for all tracks given a genre, artist or album
        if (NPT_String(search_criteria).Find("object.item.audioItem") >= 0) {
            if (!id.EndsWith("/")) id += "/";
            NPT_Cardinal count = id.SubString(10).Split("/").GetItemCount();
            // remove extra empty node count
            count = count?count-1:0;

            // genre
            if (id.StartsWith("musicdb://1/")) {
                // all tracks of all genres
                if (count == 1)
                    id += "-1/-1/-1/";
                // all tracks of a specific genre
                else if (count == 2)
                id += "-1/-1/";
                // all tracks of a specific genre of a specfic artist
                else if (count == 3)
                    id += "-1/";
            } else if (id.StartsWith("musicdb://2/")) {
                // all tracks by all artists
                if (count == 1)
                    id += "-1/-1/";
                // all tracks of a specific artist
                else if (count == 2)
                id += "-1/";
            } else if (id.StartsWith("musicdb://3/")) {
                // all albums ?
                if (count == 1) id += "-1/";
            }
        }
        return OnBrowseDirectChildren(action, id, "*", starting_index, requested_count, sort_criteria, context);
    } else if (NPT_String(search_criteria).Find("object.item.audioItem") >= 0) {
        // look for artist, album & genre filters
        NPT_String genre = FindSubCriteria(search_criteria, "upnp:genre");
        NPT_String album = FindSubCriteria(search_criteria, "upnp:album");
        NPT_String artist = FindSubCriteria(search_criteria, "upnp:artist");
        // sonos looks for microsoft specific stuff
        artist = artist.GetLength()?artist:FindSubCriteria(search_criteria, "microsoft:artistPerformer");
        artist = artist.GetLength()?artist:FindSubCriteria(search_criteria, "microsoft:artistAlbumArtist");
        artist = artist.GetLength()?artist:FindSubCriteria(search_criteria, "microsoft:authorComposer");

        CMusicDatabase database;
        database.Open();

        if (genre.GetLength() > 0) {
            // all tracks by genre filtered by artist and/or album
            CStdString strPath;
            strPath.Format("musicdb://1/%ld/%ld/%ld/",
                database.GetGenreByName((const char*)genre),
                database.GetArtistByName((const char*)artist), // will return -1 if no artist
                database.GetAlbumByName((const char*)album));  // will return -1 if no album

            return OnBrowseDirectChildren(action, strPath.c_str(), "*", starting_index, requested_count, sort_criteria, context);
        } else if (artist.GetLength() > 0) {
            // all tracks by artist name filtered by album if passed
            CStdString strPath;
            strPath.Format("musicdb://2/%ld/%ld/",
                database.GetArtistByName((const char*)artist),
                database.GetAlbumByName((const char*)album)); // will return -1 if no album

            return OnBrowseDirectChildren(action, strPath.c_str(), "*", starting_index, requested_count, sort_criteria, context);
        } else if (album.GetLength() > 0) {
            // all tracks by album name
            CStdString strPath;
            strPath.Format("musicdb://3/%ld/",
                database.GetAlbumByName((const char*)album));

            return OnBrowseDirectChildren(action, strPath.c_str(), "*", starting_index, requested_count, sort_criteria, context);
        }

        // browse all songs
        return OnBrowseDirectChildren(action, "musicdb://4/", "*", starting_index, requested_count, sort_criteria, context);
    } else if (NPT_String(search_criteria).Find("object.container.album.musicAlbum") >= 0) {
        // sonos filters by genre
        NPT_String genre = FindSubCriteria(search_criteria, "upnp:genre");

        // 360 hack: artist/albums using search
        NPT_String artist = FindSubCriteria(search_criteria, "upnp:artist");
        // sonos looks for microsoft specific stuff
        artist = artist.GetLength()?artist:FindSubCriteria(search_criteria, "microsoft:artistPerformer");
        artist = artist.GetLength()?artist:FindSubCriteria(search_criteria, "microsoft:artistAlbumArtist");
        artist = artist.GetLength()?artist:FindSubCriteria(search_criteria, "microsoft:authorComposer");

            CMusicDatabase database;
            database.Open();

        if (genre.GetLength() > 0) {
            CStdString strPath;
            strPath.Format("musicdb://1/%ld/%ld/",
                database.GetGenreByName((const char*)genre),
                database.GetArtistByName((const char*)artist)); // no artist should return -1
            return OnBrowseDirectChildren(action, strPath.c_str(), "*", starting_index, requested_count, sort_criteria, context);
        } else if (artist.GetLength() > 0) {
            CStdString strPath;
            strPath.Format("musicdb://2/%ld/",
                database.GetArtistByName((const char*)artist));
            return OnBrowseDirectChildren(action, strPath.c_str(), "*", starting_index, requested_count, sort_criteria, context);
        }

        // all albums
        return OnBrowseDirectChildren(action, "musicdb://3/", "*", starting_index, requested_count, sort_criteria, context);
    } else if (NPT_String(search_criteria).Find("object.container.person.musicArtist") >= 0) {
        // Sonos filters by genre
        NPT_String genre = FindSubCriteria(search_criteria, "upnp:genre");
        if (genre.GetLength() > 0) {
            CMusicDatabase database;
            database.Open();
            CStdString strPath;
            strPath.Format("musicdb://1/%ld/", database.GetGenreByName((const char*)genre));
            return OnBrowseDirectChildren(action, strPath.c_str(), "*", starting_index, requested_count, sort_criteria, context);
        }
        return OnBrowseDirectChildren(action, "musicdb://2/", "*", starting_index, requested_count, sort_criteria, context);
    }  else if (NPT_String(search_criteria).Find("object.container.genre.musicGenre") >= 0) {
        return OnBrowseDirectChildren(action, "musicdb://1/", "*", starting_index, requested_count, sort_criteria, context);
    } else if (NPT_String(search_criteria).Find("object.container.playlistContainer") >= 0) {
        return OnBrowseDirectChildren(action, "special://musicplaylists/", "*", starting_index, requested_count, sort_criteria, context);
    } else if (NPT_String(search_criteria).Find("object.item.videoItem") >= 0) {
      CFileItemList items, itemsall;

      CVideoDatabase database;
      if(!database.Open()) {
        action->SetError(800, "Internal Error");
        return NPT_SUCCESS;
      }

      if (!database.GetMoviesNav("videodb://1/2/", items)) {
        action->SetError(800, "Internal Error");
        return NPT_SUCCESS;
      }
      itemsall.Append(items);
      items.Clear();

      // TODO - set proper base url for this
      if (!database.GetEpisodesNav("videodb://2/0/", items)) {
        action->SetError(800, "Internal Error");
        return NPT_SUCCESS;
      }
      itemsall.Append(items);
      items.Clear();

      return BuildResponse(action, itemsall, "*", starting_index, requested_count, sort_criteria, context, NULL);
  } else if (NPT_String(search_criteria).Find("object.item.imageItem") >= 0) {
      return NPT_SUCCESS;
  }

  return NPT_FAILURE;
}

/*----------------------------------------------------------------------
|   CUPnPServer::ServeFile
+---------------------------------------------------------------------*/
NPT_Result 
CUPnPServer::ServeFile(NPT_HttpRequest&              request, 
                       const NPT_HttpRequestContext& context,
                       NPT_HttpResponse&             response,
                       const NPT_String&             file_path)
{
    CLog::Log(LOGDEBUG, "Received request to serve '%s'", (const char*)file_path);

    // File requested
    NPT_String path = m_URLBase.GetPath();
    if (path.Compare(request.GetUrl().GetPath().Left(path.GetLength()), true) == 0 &&
        file_path.Left(8).Compare("stack://", true) == 0) {
        
        NPT_List<NPT_String> files = file_path.SubString(8).Split(" , ");
        if (files.GetItemCount() == 0) {
            response.SetStatus(404, "File Not Found");
            return NPT_SUCCESS;
        }

        NPT_String output;
        output.Reserve(file_path.GetLength()*2);

        NPT_List<NPT_String>::Iterator url = files.GetFirstItem();
        for (;url;url++) {
            output += PLT_FileMediaServer::BuildSafeResourceUri(
                          m_URLBase,
                          context.GetLocalAddress().GetIpAddress().ToString(),
                          *url);
            output += "\n\r";
        }

        PLT_HttpHelper::SetBody(response, (const char*)output, output.GetLength());
        return NPT_SUCCESS;
    }

    return PLT_FileMediaServerDelegate::ServeFile(request, context, response,file_path);
}

/*----------------------------------------------------------------------
|   CUPnPRenderer
+---------------------------------------------------------------------*/
class CUPnPRenderer : public PLT_MediaRenderer
{
public:
    CUPnPRenderer(const char*  friendly_name,
                  bool         show_ip = false,
                  const char*  uuid = NULL,
                  unsigned int port = 0);

    void UpdateState();

    // Http server handler
    virtual NPT_Result ProcessHttpRequest(NPT_HttpRequest&              request,
                                          const NPT_HttpRequestContext& context,
                                          NPT_HttpResponse&             response);

    // AVTransport methods
    virtual NPT_Result OnNext(PLT_ActionReference& action);
    virtual NPT_Result OnPause(PLT_ActionReference& action);
    virtual NPT_Result OnPlay(PLT_ActionReference& action);
    virtual NPT_Result OnPrevious(PLT_ActionReference& action);
    virtual NPT_Result OnStop(PLT_ActionReference& action);
    virtual NPT_Result OnSeek(PLT_ActionReference& action);
    virtual NPT_Result OnSetAVTransportURI(PLT_ActionReference& action);

    // RenderingControl methods
    virtual NPT_Result OnSetVolume(PLT_ActionReference& action);
    virtual NPT_Result OnSetMute(PLT_ActionReference& action);

private:
    NPT_Result SetupServices();
    NPT_Result GetMetadata(NPT_String& meta);
    NPT_Result PlayMedia(const char* uri,
                         const char* metadata = NULL,
                         PLT_Action* action = NULL);
};

/*----------------------------------------------------------------------
|   CUPnPRenderer::CUPnPRenderer
+---------------------------------------------------------------------*/
CUPnPRenderer::CUPnPRenderer(const char*  friendly_name,
                             bool         show_ip /* = false */,
                             const char*  uuid /* = NULL */,
                             unsigned int port /* = 0 */) :
    PLT_MediaRenderer(friendly_name,
                    show_ip, 
                    uuid, 
                    port)
{
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::SetupServices
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::SetupServices()
{
    NPT_CHECK(PLT_MediaRenderer::SetupServices());

    // update what we can play
    PLT_Service* service = NULL;
    NPT_CHECK_FATAL(FindServiceByType("urn:schemas-upnp-org:service:ConnectionManager:1", service));
    service->SetStateVariable("SinkProtocolInfo"
        ,"http-get:*:*:*"
        ",xbmc-get:*:*:*"
        ",http-get:*:audio/mpegurl:*"
        ",http-get:*:audio/mpeg:*"
        ",http-get:*:audio/mpeg3:*"
        ",http-get:*:audio/mp3:*"
        ",http-get:*:audio/basic:*"
        ",http-get:*:audio/midi:*"
        ",http-get:*:audio/ulaw:*"
        ",http-get:*:audio/ogg:*"
        ",http-get:*:audio/DVI4:*"
        ",http-get:*:audio/G722:*"
        ",http-get:*:audio/G723:*"
        ",http-get:*:audio/G726-16:*"
        ",http-get:*:audio/G726-24:*"
        ",http-get:*:audio/G726-32:*"
        ",http-get:*:audio/G726-40:*"
        ",http-get:*:audio/G728:*"
        ",http-get:*:audio/G729:*"
        ",http-get:*:audio/G729D:*"
        ",http-get:*:audio/G729E:*"
        ",http-get:*:audio/GSM:*"
        ",http-get:*:audio/GSM-EFR:*"
        ",http-get:*:audio/L8:*"
        ",http-get:*:audio/L16:*"
        ",http-get:*:audio/LPC:*"
        ",http-get:*:audio/MPA:*"
        ",http-get:*:audio/PCMA:*"
        ",http-get:*:audio/PCMU:*"
        ",http-get:*:audio/QCELP:*"
        ",http-get:*:audio/RED:*"
        ",http-get:*:audio/VDVI:*"
        ",http-get:*:audio/ac3:*"
        ",http-get:*:audio/vorbis:*"
        ",http-get:*:audio/speex:*"
        ",http-get:*:audio/x-aiff:*"
        ",http-get:*:audio/x-pn-realaudio:*"
        ",http-get:*:audio/x-realaudio:*"
        ",http-get:*:audio/x-wav:*"
        ",http-get:*:audio/x-ms-wma:*"
        ",http-get:*:audio/x-mpegurl:*"
        ",http-get:*:application/x-shockwave-flash:*"
        ",http-get:*:application/ogg:*"
        ",http-get:*:application/sdp:*"
        ",http-get:*:image/gif:*"
        ",http-get:*:image/jpeg:*"
        ",http-get:*:image/ief:*"
        ",http-get:*:image/png:*"
        ",http-get:*:image/tiff:*"
        ",http-get:*:video/avi:*"
        ",http-get:*:video/mpeg:*"
        ",http-get:*:video/fli:*"
        ",http-get:*:video/flv:*"
        ",http-get:*:video/quicktime:*"
        ",http-get:*:video/vnd.vivo:*"
        ",http-get:*:video/vc1:*"
        ",http-get:*:video/ogg:*"
        ",http-get:*:video/mp4:*"
        ",http-get:*:video/BT656:*"
        ",http-get:*:video/CelB:*"
        ",http-get:*:video/JPEG:*"
        ",http-get:*:video/H261:*"
        ",http-get:*:video/H263:*"
        ",http-get:*:video/H263-1998:*"
        ",http-get:*:video/H263-2000:*"
        ",http-get:*:video/MPV:*"
        ",http-get:*:video/MP2T:*"
        ",http-get:*:video/MP1S:*"
        ",http-get:*:video/MP2P:*"
        ",http-get:*:video/BMPEG:*"
        ",http-get:*:video/x-ms-wmv:*"
        ",http-get:*:video/x-ms-avi:*"
        ",http-get:*:video/x-flv:*"
        ",http-get:*:video/x-fli:*"
        ",http-get:*:video/x-ms-asf:*"
        ",http-get:*:video/x-ms-asx:*"
        ",http-get:*:video/x-ms-wmx:*"
        ",http-get:*:video/x-ms-wvx:*"
        ",http-get:*:video/x-msvideo:*"
        );
    return NPT_SUCCESS;
    }

/*----------------------------------------------------------------------
|   CUPnPRenderer::ProcessHttpRequest
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::ProcessHttpRequest(NPT_HttpRequest&              request,
                                  const NPT_HttpRequestContext& context,
                                  NPT_HttpResponse&             response)
{
    // get the address of who sent us some data back
    NPT_String  ip_address = context.GetRemoteAddress().GetIpAddress().ToString();
    NPT_String  method     = request.GetMethod();
    NPT_String  protocol   = request.GetProtocol();
    NPT_HttpUrl url        = request.GetUrl();

    if (url.GetPath() == "/thumb.jpg") {
        NPT_HttpUrlQuery query(url.GetQuery());
        NPT_String filepath = query.GetField("path");
        if (!filepath.IsEmpty()) {
            NPT_HttpEntity* entity = response.GetEntity();
            if (entity == NULL) return NPT_ERROR_INVALID_STATE;

            // check the method
            if (request.GetMethod() != NPT_HTTP_METHOD_GET &&
                request.GetMethod() != NPT_HTTP_METHOD_HEAD) {
                response.SetStatus(405, "Method Not Allowed");
                return NPT_SUCCESS;
}

            // ensure that the request's path is a valid thumb path
            if (CUtil::IsRemote(filepath.GetChars()) ||
                !filepath.StartsWith(g_settings.GetUserDataFolder())) {
                response.SetStatus(404, "Not Found");
                return NPT_SUCCESS;
            }

            // prevent hackers from accessing files outside of our root
            if ((filepath.Find("/..") >= 0) || (filepath.Find("\\..") >=0)) {
                return NPT_FAILURE;
            }

            // open the file
            NPT_File file(filepath);
            NPT_Result result = file.Open(NPT_FILE_OPEN_MODE_READ);
            if (NPT_FAILED(result)) {
                response.SetStatus(404, "Not Found");
                return NPT_SUCCESS;
            }
            NPT_InputStreamReference stream;
            file.GetInputStream(stream);
            entity->SetContentType(CUPnPServer::GetContentType(filepath));
            entity->SetInputStream(stream, true);

            return NPT_SUCCESS;
        }
    }

    return SetupResponse(request, context, response);
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::UpdateState
+---------------------------------------------------------------------*/
void 
CUPnPRenderer::UpdateState()
{
    PLT_Service *avt, *rct;
    if(NPT_FAILED(FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", avt)))
        return;
    if(NPT_FAILED(FindServiceByType("urn:schemas-upnp-org:service:RenderingControl:1", rct)))
        return;

    CStdString buffer;
    int volume;
    if (g_stSettings.m_bMute) {
        rct->SetStateVariable("Mute", "1");
        volume = g_stSettings.m_iPreMuteVolumeLevel;
    } else {
        rct->SetStateVariable("Mute", "0");
        volume = g_application.GetVolume();
    }

    buffer.Format("%d", volume);
    rct->SetStateVariable("Volume", buffer.c_str());

    buffer.Format("%d", 256 * (volume * 60 - 60) / 100);
    rct->SetStateVariable("VolumeDb", buffer.c_str());

    if (g_application.IsPlaying() || g_application.IsPaused()) {
        if (g_application.IsPaused()) {
            avt->SetStateVariable("TransportState", "PAUSED_PLAYBACK");
        } else {
        avt->SetStateVariable("TransportState", "PLAYING");
        }

        avt->SetStateVariable("TransportStatus", "OK");
        avt->SetStateVariable("TransportPlaySpeed", (const char*)NPT_String::FromInteger(g_application.GetPlaySpeed()));
        avt->SetStateVariable("NumberOfTracks", "1");
        avt->SetStateVariable("CurrentTrack", "1");

        buffer = g_infoManager.GetCurrentPlayTime(TIME_FORMAT_HH_MM_SS);
        avt->SetStateVariable("RelativeTimePosition", buffer.c_str());
        StringUtils::SecondsToTimeString((long)g_infoManager.GetTotalPlayTime(), buffer, TIME_FORMAT_HH_MM_SS);
        avt->SetStateVariable("AbsoluteTimePosition", buffer.c_str());

        buffer = g_infoManager.GetDuration(TIME_FORMAT_HH_MM_SS);
        if (buffer.length() > 0) {
          avt->SetStateVariable("CurrentTrackDuration", buffer.c_str());
          avt->SetStateVariable("CurrentMediaDuration", buffer.c_str());
        } else {
          avt->SetStateVariable("CurrentTrackDuration", "00:00:00");
          avt->SetStateVariable("CurrentMediaDuration", "00:00:00");
        }

        avt->SetStateVariable("AVTransportURI", g_application.CurrentFile().c_str());
        avt->SetStateVariable("CurrentTrackURI", g_application.CurrentFile().c_str());

        NPT_String metadata;
        avt->GetStateVariableValue("AVTransportURIMetaData", metadata);
        // try to recreate the didl dynamically if not set
        if (metadata.IsEmpty()) {
            GetMetadata(metadata);
        }
        avt->SetStateVariable("CurrentTrackMetadata", metadata);
        avt->SetStateVariable("AVTransportURIMetaData", metadata);
    } else {
        avt->SetStateVariable("TransportState", "STOPPED");
        avt->SetStateVariable("TransportPlaySpeed", "1");
        avt->SetStateVariable("NumberOfTracks", "0");
        avt->SetStateVariable("CurrentTrack", "0");
        avt->SetStateVariable("RelativeTimePosition", "00:00:00");
        avt->SetStateVariable("AbsoluteTimePosition", "00:00:00");
        avt->SetStateVariable("CurrentTrackDuration", "00:00:00");
        avt->SetStateVariable("CurrentMediaDuration", "00:00:00");
    }
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::GetMetadata
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::GetMetadata(NPT_String& meta)
{
    NPT_Result res = NPT_FAILURE;
    const CFileItem &item = g_application.CurrentFileItem();
    NPT_String file_path;
    PLT_MediaObject* object = CUPnPServer::BuildObject(item, file_path, false);
    if (object) {
        // fetch the path to the thumbnail
        CStdString thumb = g_infoManager.GetImage(MUSICPLAYER_COVER, -1); //TODO: Only audio for now

        NPT_String ip;
        if (g_application.getNetwork().GetFirstConnectedInterface()) {
            ip = g_application.getNetwork().GetFirstConnectedInterface()->GetCurrentIPAddress().c_str();
        }
        // build url, use the internal device http server to serv the image
        NPT_HttpUrlQuery query;
        query.AddField("path", thumb.c_str());
        object->m_ExtraInfo.album_art_uri = NPT_HttpUrl(
            ip,
            m_URLDescription.GetPort(),
            "/thumb.jpg",
            query.ToString()).ToString();

        res = PLT_Didl::ToDidl(*object, "*", meta);
        delete object;
    }
    return res;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnNext
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnNext(PLT_ActionReference& action)
{
    g_application.getApplicationMessenger().PlayListPlayerNext();
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnPause
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnPause(PLT_ActionReference& action)
{
    if(!g_application.IsPaused())
      g_application.getApplicationMessenger().MediaPause();
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnPlay
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnPlay(PLT_ActionReference& action)
{
    if (g_application.IsPaused()) {
      g_application.getApplicationMessenger().MediaPause();
    } else if (!g_application.IsPlaying()) {
        NPT_String uri, meta;
        PLT_Service* service;
        // look for value set previously by SetAVTransportURI
        NPT_CHECK_SEVERE(FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", service));
        NPT_CHECK_SEVERE(service->GetStateVariableValue("AVTransportURI", uri));
        NPT_CHECK_SEVERE(service->GetStateVariableValue("AVTransportURIMetaData", meta));

        // if not set, use the current file being played
        PlayMedia(uri, meta);
    }
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnPrevious
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnPrevious(PLT_ActionReference& action)
{
    g_application.getApplicationMessenger().PlayListPlayerPrevious();
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnStop
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnStop(PLT_ActionReference& action)
{
    g_application.getApplicationMessenger().MediaStop();
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnSetAVTransportURI
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnSetAVTransportURI(PLT_ActionReference& action)
{
    NPT_String uri, meta;
    PLT_Service* service;
    NPT_CHECK_SEVERE(FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", service));

    NPT_CHECK_SEVERE(action->GetArgumentValue("CurrentURI",uri));
    NPT_CHECK_SEVERE(action->GetArgumentValue("CurrentURIMetaData",meta));

    // if not playing already, just keep around uri & metadata
    // and wait for play command
    if (!g_application.IsPlaying()) {
        service->SetStateVariable("TransportState", "STOPPED");
        service->SetStateVariable("TransportStatus", "OK");
        service->SetStateVariable("TransportPlaySpeed", "1");
        service->SetStateVariable("AVTransportURI", uri);
        service->SetStateVariable("AVTransportURIMetaData", meta);

        NPT_CHECK_SEVERE(action->SetArgumentsOutFromStateVariable());
        return NPT_SUCCESS;
    }

    return PlayMedia(uri, meta, action.AsPointer());
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::PlayMedia
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::PlayMedia(const char* uri, const char* meta, PLT_Action* action)
{
    bool bImageFile = false;
    PLT_Service* service;
    NPT_CHECK_SEVERE(FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", service));

    service->SetStateVariable("TransportState", "TRANSITIONING");
    service->SetStateVariable("TransportStatus", "OK");
    service->SetStateVariable("TransportPlaySpeed", "1");

    PLT_MediaObjectListReference list;
    PLT_MediaObject*             object = NULL;

    if (meta && NPT_SUCCEEDED(PLT_Didl::FromDidl(meta, list))) {
      list->Get(0, object);
    }

    if(object) {
        CFileItem item(uri, false);

        PLT_MediaItemResource* res = object->m_Resources.GetFirstItem();
        for(NPT_Cardinal i = 0; i < object->m_Resources.GetItemCount(); i++) {
          if(object->m_Resources[i].m_Uri == uri) { 
            res = &object->m_Resources[i];
            break;
          }
        }
        for(NPT_Cardinal i = 0; i < object->m_Resources.GetItemCount(); i++) {
            if(object->m_Resources[i].m_ProtocolInfo.ToString().StartsWith("xbmc-get:")) {
            res = &object->m_Resources[i];
            item.m_strPath = res->m_Uri;
            break;
          }
        }

        NPT_String proto, mask, content, extra;
        if (res && res->m_ProtocolInfo.IsValid()) {
            item.SetContentType((const char*)res->m_ProtocolInfo.GetContentType());
        }

        item.m_dateTime.SetFromDateString((const char*)object->m_Date);
        item.m_strTitle = (const char*)object->m_Title;
        item.SetLabel((const char*)object->m_Title);
        item.SetLabelPreformated(true);
        item.SetThumbnailImage((const char*)object->m_ExtraInfo.album_art_uri);
        if       (object->m_ObjectClass.type.StartsWith("object.item.audioItem")) {            
            if(NPT_SUCCEEDED(CUPnP::PopulateTagFromObject(*item.GetMusicInfoTag(), *object, res)))
                item.SetLabelPreformated(false);
        } else if(object->m_ObjectClass.type.StartsWith("object.item.videoItem")) {
            if(NPT_SUCCEEDED(CUPnP::PopulateTagFromObject(*item.GetVideoInfoTag(), *object, res)))
                item.SetLabelPreformated(false);
        } else if(object->m_ObjectClass.type.StartsWith("object.item.imageItem")) {
            bImageFile = true;
        }
        bImageFile?g_application.getApplicationMessenger().PictureShow(item.m_strPath)
                  :g_application.getApplicationMessenger().MediaPlay(item);
    } else {
        bImageFile = NPT_String(PLT_MediaObject::GetUPnPClass(uri)).StartsWith("object.item.imageItem", true);

        bImageFile?g_application.getApplicationMessenger().PictureShow((const char*)uri)
                  :g_application.getApplicationMessenger().MediaPlay((const char*)uri);
    }

    if (!g_application.IsPlaying()) {
        service->SetStateVariable("TransportState", "STOPPED");
        service->SetStateVariable("TransportStatus", "ERROR_OCCURRED");
    } else {
        service->SetStateVariable("AVTransportURI", uri);
        service->SetStateVariable("AVTransportURIMetaData", meta);
    }

    if (action) {
    NPT_CHECK_SEVERE(action->SetArgumentsOutFromStateVariable());
    }
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnSetVolume
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnSetVolume(PLT_ActionReference& action)
{
    NPT_String volume;
    NPT_CHECK_SEVERE(action->GetArgumentValue("DesiredVolume",volume));
    g_application.SetVolume(atoi((const char*)volume));
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnSetMute
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnSetMute(PLT_ActionReference& action)
{
    NPT_String mute;
    NPT_CHECK_SEVERE(action->GetArgumentValue("DesiredMute",mute));
    if((mute == "1") ^ g_stSettings.m_bMute)
        g_application.Mute();
    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CUPnPRenderer::OnSeek
+---------------------------------------------------------------------*/
NPT_Result
CUPnPRenderer::OnSeek(PLT_ActionReference& action)
{
    if (!g_application.IsPlaying()) return NPT_ERROR_INVALID_STATE;

    NPT_String unit, target;
    NPT_CHECK_SEVERE(action->GetArgumentValue("Unit", unit));
    NPT_CHECK_SEVERE(action->GetArgumentValue("Target", target));

    if (!unit.Compare("REL_TIME")) {
        // converts target to seconds
        NPT_UInt32 seconds;
        NPT_CHECK_SEVERE(PLT_Didl::ParseTimeStamp(target, seconds));
        g_application.SeekTime(seconds);
    }

    return NPT_SUCCESS;
}

/*----------------------------------------------------------------------
|   CRendererReferenceHolder class
+---------------------------------------------------------------------*/
class CRendererReferenceHolder
{
public:
    PLT_DeviceHostReference m_Device;
};

/*----------------------------------------------------------------------
|   CMediaBrowser class
+---------------------------------------------------------------------*/
class CMediaBrowser : public PLT_SyncMediaBrowser,
                      public PLT_MediaContainerChangesListener
{
public:
  CMediaBrowser(PLT_CtrlPointReference& ctrlPoint)
    : PLT_SyncMediaBrowser(ctrlPoint, true)
  {
      SetContainerListener(this);
      m_nameMapping.clear();
  }

  // PLT_MediaBrowser methods
  virtual bool OnMSAdded(PLT_DeviceDataReference& device)
  {
    SendMSUpdate(device,GUI_MSG_ADD_ITEM);

    AddFriendlyName( (const char*)device->GetUUID(),(const char*)device->GetUUID(),(const char*)device->GetFriendlyName()); ;

    return PLT_SyncMediaBrowser::OnMSAdded(device);
  }
  virtual void OnMSRemoved(PLT_DeviceDataReference& device)
  {
    PLT_SyncMediaBrowser::OnMSRemoved(device);

    SendMSUpdate(device,GUI_MSG_REMOVE_ITEM);

    PLT_SyncMediaBrowser::OnMSRemoved(device);
    m_nameMapping[(const char*)device->GetUUID()].clear();
  }

  // PLT_MediaContainerChangesListener methods
  virtual void OnContainerChanged(PLT_DeviceDataReference& device,
                                  const char*              item_id,
                                  const char*              update_id)
  {
    NPT_String path = "upnp://"+device->GetUUID()+"/";

    if (!NPT_StringsEqual(item_id, "0"))
    {
        CStdString id = item_id;
        CUtil::URLEncode(id);
        path += id.c_str();
        path += "/";
    }

    SendMSUpdate(device,GUI_MSG_CHANGE_ITEM,path.GetChars());
  }
  void AddFriendlyName( const CStdString& device_id, const CStdString& object_id, const CStdString& friendly_name )
  {
    (m_nameMapping[device_id])[object_id] = friendly_name;
  }
  bool GetFriendlyName( const CStdString& device_id, const CStdString& object_id, CStdString& friendly_name )
  {
    if( m_nameMapping[device_id].size() == 0 ) return false;
    if( !m_nameMapping[device_id][object_id] ) return false;
    friendly_name = m_nameMapping[device_id][object_id];
    return true;
  }
  bool GetFriendlyPath( const CStdString& path, CStdString& friendly_path )
  {
    if( !path.Left(7).Equals("upnp://") ) return false;
    if( path.Equals("upnp://all") )
    {
      friendly_path = path;
      return true;
    }
    
    CStdString device_id;
    int slash = path.Find("/", 7);
    if( -1 == slash ) device_id = path.Right(7);
    else device_id = path.Mid(7,slash-7);
    
    if( !m_nameMapping[device_id].size() ) return false;
    
    friendly_path = "upnp://" + m_nameMapping[device_id][device_id];
    while( -1 != slash )
    {
      int nxt = path.Find("/", slash+1);
      CStdString object_id;

      if( -1 == nxt && slash == (int)path.size()-1 ) break;         // trailing slash
      else if( -1 == nxt ) object_id = path.Right( slash+1 );  // trailing object id
      else object_id = path.Mid(slash+1, nxt-slash-1);         // path part object id

      friendly_path += "/" + (m_nameMapping[device_id][object_id] ? m_nameMapping[device_id][object_id] : object_id);
      slash = nxt;
    }
    if( path.Right(1).Equals("/") ) friendly_path += "/";
    return true;
  }
private:

  void SendMSUpdate(const PLT_DeviceDataReference& device, int iCommandMsg , const CStdString& customPath="")
  {
    NPT_String path = "upnp://"+device->GetUUID()+"/";
    NPT_String friendlyName = device->GetFriendlyName();
    NPT_String iconURL = device->GetIconUrl("image/jpeg");

    vector<CStdString> messageStrings;
    messageStrings.push_back(customPath.IsEmpty()?path.GetChars():customPath.c_str());
    messageStrings.push_back(friendlyName.GetChars());
    messageStrings.push_back(iconURL.GetChars());

    CGUIMessage message(GUI_MSG_MANAGE_ITEM, 0, 0 , iCommandMsg);
    message.SetStringParams(messageStrings);
    g_windowManager.SendThreadMessage(message);
  }

  std::map<CStdString /*device_id*/, std::map<CStdString /*object_id*/, CStdString /*friendly_name*/> > m_nameMapping;
};


/*----------------------------------------------------------------------
|   CUPnP::CUPnP
+---------------------------------------------------------------------*/
CUPnP::CUPnP() :
    m_MediaBrowser(NULL),
    m_ServerHolder(new CDeviceHostReferenceHolder()),
    m_RendererHolder(new CRendererReferenceHolder()),
    m_CtrlPointHolder(new CCtrlPointReferenceHolder())
{
    broadcast = false;
    
    // initialize upnp in broadcast listening mode for xbmc
    m_UPnP = new PLT_UPnP(1900, !broadcast);

    // keep main IP around
    if (g_application.getNetwork().GetFirstConnectedInterface()) {
        m_IP = g_application.getNetwork().GetFirstConnectedInterface()->GetCurrentIPAddress().c_str();
    }
    NPT_List<NPT_IpAddress> list;
    if (NPT_SUCCEEDED(PLT_UPnPMessageHelper::GetIPAddresses(list))) {
        m_IP = (*(list.GetFirstItem())).ToString();
    }

    // start upnp monitoring
    if( NPT_SUCCESS != m_UPnP->Start() )
    {
        CLog::Log(LOGWARNING, "UPnP client failed to start - another application may be preventing access to the UPnP ports");
}
}

/*----------------------------------------------------------------------
|   CUPnP::~CUPnP
+---------------------------------------------------------------------*/
CUPnP::~CUPnP()
{
    m_UPnP->Stop();
    StopClient();
    StopServer();

    delete m_UPnP;
    delete m_ServerHolder;
    delete m_RendererHolder;
    delete m_CtrlPointHolder;
}

/*----------------------------------------------------------------------
|   CUPnP::GetInstance
+---------------------------------------------------------------------*/
CUPnP*
CUPnP::GetInstance()
{
    if (!upnp) {
        upnp = new CUPnP();
    }

    return upnp;
}

/*----------------------------------------------------------------------
|   CUPnP::ReleaseInstance
+---------------------------------------------------------------------*/
void
CUPnP::ReleaseInstance(bool bWait)
{
    if (upnp) {
        CUPnP* _upnp = upnp;
        upnp = NULL;

        if (bWait) {
            delete _upnp;
        } else {
        // since it takes a while to clean up
        // starts a detached thread to do this
        CUPnPCleaner* cleaner = new CUPnPCleaner(_upnp);
        cleaner->Start();
    }
}
}

/*----------------------------------------------------------------------
|   CUPnP::StartClient
+---------------------------------------------------------------------*/
void
CUPnP::StartClient()
{
    if (!m_CtrlPointHolder->m_CtrlPoint.IsNull()) return;

    // create controlpoint, pass NULL to avoid sending a multicast search
    m_CtrlPointHolder->m_CtrlPoint = new PLT_CtrlPoint(broadcast?NULL:"upnp:rootdevice");

    // ignore our own server
    if (!m_ServerHolder->m_Device.IsNull()) {
        m_CtrlPointHolder->m_CtrlPoint->IgnoreUUID(m_ServerHolder->m_Device->GetUUID());
    }

    // start it
    m_UPnP->AddCtrlPoint(m_CtrlPointHolder->m_CtrlPoint);

    // start browser
    m_MediaBrowser = new CMediaBrowser(m_CtrlPointHolder->m_CtrlPoint);

    CLog::Log(LOGNOTICE, "starting upnp client");
}

/*----------------------------------------------------------------------
|   CUPnP::StopClient
+---------------------------------------------------------------------*/
void
CUPnP::StopClient()
{
    if (m_CtrlPointHolder->m_CtrlPoint.IsNull()) return;

    m_UPnP->RemoveCtrlPoint(m_CtrlPointHolder->m_CtrlPoint);
    m_CtrlPointHolder->m_CtrlPoint = NULL;

    delete m_MediaBrowser;
    m_MediaBrowser = NULL;
}

/*----------------------------------------------------------------------
|   CUPnP::CreateServer
+---------------------------------------------------------------------*/
CUPnPServer*
CUPnP::CreateServer(int port /* = 0 */)
{
    CUPnPServer* device =
        new CUPnPServer("XBMC: Media Server:",
                        g_settings.m_UPnPUUIDServer.length()?g_settings.m_UPnPUUIDServer.c_str():NULL,
                        port);

    // trying to set optional upnp values for XP UPnP UI Icons to detect us
    // but it doesn't work anyways as it requires multicast for XP to detect us
    device->m_PresentationURL =
        NPT_HttpUrl(m_IP,
                    atoi(g_guiSettings.GetString("servers.webserverport")),
                    "/").ToString();

    device->m_ModelName        = "Boxee Media Center";
    device->m_ModelNumber      = "1.0";
    device->m_ModelDescription = "Boxee Media Center - Media Server";
    device->m_ModelURL         = "http://www.boxee.tv/";
    device->m_Manufacturer     = "Boxee, Inc.";
    device->m_ManufacturerURL  = "http://www.boxee.tv/";

    return device;
}

/*----------------------------------------------------------------------
|   CUPnP::StartServer
+---------------------------------------------------------------------*/
void
CUPnP::StartServer()
{
    if (!m_ServerHolder->m_Device.IsNull()) return;

    // load upnpserver.xml so that g_settings.m_vecUPnPMusiCMediaSources, etc.. are loaded
    CStdString filename;
    CUtil::AddFileToFolder(g_settings.GetUserDataFolder(), "upnpserver.xml", filename);
    g_settings.LoadUPnPXml(filename);

    // create the server with a XBox compatible friendlyname and UUID from upnpserver.xml if found
    m_ServerHolder->m_Device = CreateServer(g_settings.m_UPnPPortServer);

    // tell controller to ignore ourselves from list of upnp servers
    if (!m_CtrlPointHolder->m_CtrlPoint.IsNull()) {
        m_CtrlPointHolder->m_CtrlPoint->IgnoreUUID(m_ServerHolder->m_Device->GetUUID());
    }

    // start server
    NPT_Result res = m_UPnP->AddDevice(m_ServerHolder->m_Device);
    if (NPT_FAILED(res)) {
        // if the upnp device port was not 0, it could have failed because
        // of port being in used, so restart with a random port
        if (g_settings.m_UPnPPortServer > 0) m_ServerHolder->m_Device = CreateServer(0);

        // tell controller to ignore ourselves from list of upnp servers
        if (!m_CtrlPointHolder->m_CtrlPoint.IsNull()) {
            m_CtrlPointHolder->m_CtrlPoint->IgnoreUUID(m_ServerHolder->m_Device->GetUUID());
    }

        res = m_UPnP->AddDevice(m_ServerHolder->m_Device);
    }

    // save port but don't overwrite saved settings if port was random
    if (NPT_SUCCEEDED(res)) {
        if (g_settings.m_UPnPPortServer == 0) {
            g_settings.m_UPnPPortServer = m_ServerHolder->m_Device->GetPort();
        }
        CUPnPServer::m_MaxReturnedItems = UPNP_DEFAULT_MAX_RETURNED_ITEMS;
        if (g_settings.m_UPnPMaxReturnedItems > 0) {
            // must be > UPNP_DEFAULT_MIN_RETURNED_ITEMS
            CUPnPServer::m_MaxReturnedItems = max(UPNP_DEFAULT_MIN_RETURNED_ITEMS, g_settings.m_UPnPMaxReturnedItems);
        }
        g_settings.m_UPnPMaxReturnedItems = CUPnPServer::m_MaxReturnedItems;
    }

    // save UUID
    g_settings.m_UPnPUUIDServer = m_ServerHolder->m_Device->GetUUID();
    g_settings.SaveUPnPXml(filename);
}

/*----------------------------------------------------------------------
|   CUPnP::StopServer
+---------------------------------------------------------------------*/
void
CUPnP::StopServer()
{
    if (m_ServerHolder->m_Device.IsNull()) return;

    m_UPnP->RemoveDevice(m_ServerHolder->m_Device);
    m_ServerHolder->m_Device = NULL;
}

/*----------------------------------------------------------------------
|   CUPnP::CreateRenderer
+---------------------------------------------------------------------*/
CUPnPRenderer*
CUPnP::CreateRenderer(int port /* = 0 */)
{
    CUPnPRenderer* device =
        new CUPnPRenderer("BOXEE: Media Renderer",
                          true,
                          (g_settings.m_UPnPUUIDRenderer.length() ? g_settings.m_UPnPUUIDRenderer.c_str() : NULL),
                          port);

    device->m_PresentationURL =
        NPT_HttpUrl(m_IP,
                    atoi(g_guiSettings.GetString("servers.webserverport")),
                    "/").ToString();
    device->m_ModelName = "BOXEE";
    device->m_ModelNumber = "1.0";
    device->m_ModelDescription = "BOXEE - Media Renderer";
    device->m_ModelURL = "http://www.boxee.tv/";
    device->m_Manufacturer = "BOXEE";
    device->m_ManufacturerURL = "http://www.boxee.tv/";

    return device;
}

/*----------------------------------------------------------------------
|   CUPnP::StartRenderer
+---------------------------------------------------------------------*/
void CUPnP::StartRenderer()
{
    if (!m_RendererHolder->m_Device.IsNull()) return;

    CStdString filename;
    CUtil::AddFileToFolder(g_settings.GetUserDataFolder(), "upnpserver.xml", filename);
    g_settings.LoadUPnPXml(filename);

    m_RendererHolder->m_Device = CreateRenderer(g_settings.m_UPnPPortRenderer);

    // tell controller to ignore ourselves from list of upnp servers
    if (!m_CtrlPointHolder->m_CtrlPoint.IsNull()) {
        m_CtrlPointHolder->m_CtrlPoint->IgnoreUUID(m_RendererHolder->m_Device->GetUUID());
    }

    NPT_Result res = m_UPnP->AddDevice(m_RendererHolder->m_Device);

    // failed most likely because port is in use, try again with random port now
    if (NPT_FAILED(res) && g_settings.m_UPnPPortRenderer != 0) {
        m_RendererHolder->m_Device = CreateRenderer(0);

        // tell controller to ignore ourselves from list of upnp servers
        if (!m_CtrlPointHolder->m_CtrlPoint.IsNull()) {
            m_CtrlPointHolder->m_CtrlPoint->IgnoreUUID(m_RendererHolder->m_Device->GetUUID());
    }

        res = m_UPnP->AddDevice(m_RendererHolder->m_Device);
    }

    // save port but don't overwrite saved settings if random
    if (NPT_SUCCEEDED(res) && g_settings.m_UPnPPortRenderer == 0) {
        g_settings.m_UPnPPortRenderer = m_RendererHolder->m_Device->GetPort();
    }

    // save UUID
    g_settings.m_UPnPUUIDRenderer = m_RendererHolder->m_Device->GetUUID();
    g_settings.SaveUPnPXml(filename);
}

/*----------------------------------------------------------------------
|   CUPnP::StopRenderer
+---------------------------------------------------------------------*/
void CUPnP::StopRenderer()
{
    if (m_RendererHolder->m_Device.IsNull()) return;

    m_UPnP->RemoveDevice(m_RendererHolder->m_Device);
    m_RendererHolder->m_Device = NULL;
}

/*----------------------------------------------------------------------
|   CUPnP::UpdateState
+---------------------------------------------------------------------*/
void CUPnP::UpdateState()
{
  if (!m_RendererHolder->m_Device.IsNull())
      ((CUPnPRenderer*)m_RendererHolder->m_Device.AsPointer())->UpdateState();  
}

int CUPnP::PopulateTagFromObject(CMusicInfoTag&          tag,
                                 PLT_MediaObject&       object,
                                 PLT_MediaItemResource* resource /* = NULL */)
{
    tag.SetTitle((const char*)object.m_Title);
    tag.SetArtist((const char*)object.m_Creator);
    tag.SetAlbum((const char*)object.m_Affiliation.album);
    for(PLT_PersonRoles::Iterator it = object.m_People.artists.GetFirstItem(); it; it++) {
        if     (it->role == "")            tag.SetArtist((const char*)it->name);
        else if(it->role == "Performer")   tag.SetArtist((const char*)it->name);
        else if(it->role == "AlbumArtist") tag.SetAlbumArtist((const char*)it->name);
    }
    tag.SetTrackNumber(object.m_MiscInfo.original_track_number);
    tag.SetGenre((const char*)JoinString(object.m_Affiliation.genres, " / "));
    if(resource)
        tag.SetDuration(resource->m_Duration);
    tag.SetLoaded();
    return NPT_SUCCESS;
}

int CUPnP::PopulateTagFromObject(CVideoInfoTag&         tag,
                                 PLT_MediaObject&       object,
                                 PLT_MediaItemResource* resource /* = NULL */)
{
    tag.m_strTitle    = object.m_Title;
    tag.m_strGenre    = JoinString(object.m_Affiliation.genres, " / ");
    tag.m_strDirector = object.m_People.director;
    tag.m_strTagLine  = object.m_Description.description;
    tag.m_strPlot     = object.m_Description.long_description;
    if(resource)
      tag.m_strRuntime.Format("%d",resource->m_Duration);
    return NPT_SUCCESS;
}

// Media browser fences
void CUPnP::AddFriendlyName( const CStdString& device_id, const CStdString& object_id, const CStdString& friendly_name )
{
  if(m_MediaBrowser) ((CMediaBrowser*)m_MediaBrowser)->AddFriendlyName(device_id,object_id,friendly_name);
}
bool CUPnP::GetFriendlyName( const CStdString& device_id, const CStdString& object_id, CStdString& friendly_name )
{
  return (m_MediaBrowser ? ((CMediaBrowser*)m_MediaBrowser)->GetFriendlyName(device_id,object_id,friendly_name) : false);
}
bool CUPnP::GetFriendlyPath( const CStdString& path, CStdString& friendly_path )
{
  return (m_MediaBrowser ? ((CMediaBrowser*)m_MediaBrowser)->GetFriendlyPath( path, friendly_path ) : false);
}
