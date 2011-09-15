/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
 */

#include "sbRemoteLibraryBase.h"
#include "sbRemotePlayer.h"

#include <nsICategoryManager.h>
#include <nsIDocument.h>
#include <nsIDOMDocument.h>
#include <nsIDOMDocumentEvent.h>
#include <nsIDOMEvent.h>
#include <nsIDOMEventTarget.h>
#include <nsIDOMWindow.h>
#include <nsIFile.h>
#include <nsIMutableArray.h>
#include <nsIObserver.h>
#include <nsIPrefService.h>
#include <nsIPresShell.h>
#include <nsISimpleEnumerator.h>
#include <nsIStringBundle.h>
#include <nsIStringEnumerator.h>
#include <nsISupportsPrimitives.h>
#include <nsIWindowWatcher.h>
#include <sbILibrary.h>
#include <sbILibraryConstraints.h>
#include <sbILibraryManager.h>
#include <sbIFilterableMediaListView.h>
#include <sbIMediaList.h>
#include <sbIMediaListView.h>
#include <sbIFileMetadataService.h>
#include <sbIPlaylistReader.h>
#include <sbIPropertyArray.h>
#include <sbIWrappedMediaItem.h>
#include <sbIWrappedMediaList.h>
#include <sbIJobProgress.h>

#include <jsapi.h>
#include <nsArrayEnumerator.h>
#include <nsCOMArray.h>
#include <nsEnumeratorUtils.h>
#include <nsEventDispatcher.h>
#include <nsNetUtil.h>
#include <nsServiceManagerUtils.h>
#include <nsStringGlue.h>
#include <nsTArray.h>
#include <nsTHashtable.h>
#include <prlog.h>
#include <sbPropertiesCID.h>
#include "sbRemoteMediaItem.h"
#include "sbRemoteMediaList.h"
#include "sbRemoteSiteMediaList.h"
#include "sbRemoteAPIUtils.h"
#include "sbScriptableFilter.h"
#include "sbScriptableFilterItems.h"
#include "sbScriptableFunction.h"
#include "sbRemoteWrappingStringEnumerator.h"
#include <sbStandardProperties.h>
#include "sbURIChecker.h"
#include <sbILibraryStatistics.h>

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbRemoteLibrary:5
 */
#ifdef PR_LOGGING
PRLogModuleInfo* gRemoteLibraryLog = nsnull;
#endif

#undef LOG
#define LOG(args) LOG_LIB(args)
#define TRACE(args) TRACE_LIBS(args)

// Observer for the PlaylistReader that notifies the callback and optionally
// launches a metadata job when the playlist is loaded.
class sbPlaylistReaderObserver : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS

  sbPlaylistReaderObserver(sbRemotePlayer* aRemotePlayer,
                           sbICreateMediaListCallback* aCallback,
                           PRBool aShouldScan) :
    mRemotePlayer(aRemotePlayer),
    mCallback(aCallback),
    mShouldScan(aShouldScan)
  {
    NS_ASSERTION(aRemotePlayer, "aRemotePlayer is null");
    NS_ASSERTION(aCallback, "aCallback is null");
  }

  NS_IMETHODIMP Observe( nsISupports *aSubject,
                         const char *aTopic,
                         const PRUnichar *aData)
  {
    NS_ENSURE_ARG_POINTER(aSubject);
    LOG(( "sbPlaylistReaderObserver::Observe(%s)", aTopic ));

    nsresult rv;

    nsCOMPtr<sbIMediaList> mediaList ( do_QueryInterface( aSubject, &rv ) );
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 length;
    rv = mediaList->GetLength( &length );
    NS_ENSURE_SUCCESS(rv, rv);

    if (mShouldScan && length) {

      nsCOMPtr<nsIMutableArray> mediaItems =
        do_CreateInstance( "@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv );
      NS_ENSURE_SUCCESS(rv, rv);

      for ( PRUint32 index = 0; index < length; index++ ) {

        nsCOMPtr<sbIMediaItem> item;
        rv = mediaList->GetItemByIndex( index, getter_AddRefs ( item ) );
        NS_ENSURE_SUCCESS(rv, rv);

        rv = mediaItems->AppendElement( item, PR_FALSE );
        NS_ENSURE_SUCCESS(rv, rv);
      }

      nsCOMPtr<sbIFileMetadataService> metadataService =
        do_GetService( "@songbirdnest.com/Songbird/FileMetadataService;1", &rv );
      NS_ENSURE_SUCCESS(rv, rv);
      nsCOMPtr<sbIJobProgress> job;

      rv = metadataService->Read( mediaItems, getter_AddRefs(job) );
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (mCallback) {
      nsCOMPtr<sbIMediaList> wrappedList;
      rv = SB_WrapMediaList(mRemotePlayer,
                            mediaList,
                            getter_AddRefs(wrappedList));
      NS_ENSURE_SUCCESS(rv, rv);
      mCallback->OnCreated(wrappedList); // ignore the return value

      /* The observer is only ever called on playlist creation, so we don't
       * need to hang on to the callback; clear it out now to help reduce
       * leakage (this lets things be GCed earlier).
       */
      mCallback = nsnull;
    }

    return NS_OK;
  }

private:

  nsRefPtr<sbRemotePlayer> mRemotePlayer;
  nsCOMPtr<sbICreateMediaListCallback> mCallback;
  PRBool mShouldScan;
};
NS_IMPL_ISUPPORTS1( sbPlaylistReaderObserver, nsIObserver )

class sbRemoteLibraryEnumCallback : public sbIMediaListEnumerationListener
{
  public:
    NS_DECL_ISUPPORTS

    sbRemoteLibraryEnumCallback( nsCOMArray<sbIMediaItem>& aArray ) :
      mArray(aArray) { }

    NS_IMETHODIMP OnEnumerationBegin( sbIMediaList*, PRUint16* _retval )
    {
      NS_ENSURE_ARG(_retval);
      *_retval = sbIMediaListEnumerationListener::CONTINUE;
      return NS_OK;
    }
    NS_IMETHODIMP OnEnumerationEnd( sbIMediaList*, nsresult )
    {
      return NS_OK;
    }
    NS_IMETHODIMP OnEnumeratedItem( sbIMediaList*, sbIMediaItem* aItem, PRUint16* _retval )
    {
      NS_ENSURE_ARG(_retval);
      *_retval = sbIMediaListEnumerationListener::CONTINUE;

      mArray.AppendObject( aItem );

      return NS_OK;
    }
  private:
    nsCOMArray<sbIMediaItem>& mArray;
};
NS_IMPL_ISUPPORTS1( sbRemoteLibraryEnumCallback, sbIMediaListEnumerationListener )

/**
 * Simple struct to allow sorting of scopeURLs
 */
struct sbRemoteLibraryScopeURLSet {
  sbRemoteLibraryScopeURLSet( const nsACString& path,
                              sbIMediaItem* item )
  : scopePath(path),
    item(item),
    length(path.Length())
  {
    NS_ASSERTION( item, "Null pointer!");
  }

  PRBool operator ==(const sbRemoteLibraryScopeURLSet& rhs) const
  {
    return (length == rhs.length) && (scopePath.Equals(rhs.scopePath));
  }

  PRBool operator <(const sbRemoteLibraryScopeURLSet& rhs) const
  {
    return length < rhs.length;
  }

  const nsCString scopePath;
  const nsCOMPtr<sbIMediaItem> item;
  const PRUint32 length;
};


NS_IMPL_ISUPPORTS11( sbRemoteLibraryBase,
                     nsISecurityCheckedComponent,
                     nsIXPCScriptable,
                     sbISecurityAggregator,
                     sbIRemoteMediaList,
                     sbIMediaList,
                     sbIWrappedMediaList,
                     sbIWrappedMediaItem,
                     sbIMediaItem,
                     sbILibraryResource,
                     sbIRemoteLibrary,
                     sbIScriptableFilterResult )

sbRemoteLibraryBase::sbRemoteLibraryBase(sbRemotePlayer* aRemotePlayer) :
  mShouldScan(PR_TRUE),
  mEnumerationResult(NS_ERROR_NOT_INITIALIZED),
  mRemotePlayer(aRemotePlayer),
  mIgnoreHiddenPlaylists(PR_TRUE),
  mAllowDuplicates(PR_FALSE)
{
  NS_ASSERTION(aRemotePlayer, "aRemotePlayer is null");
#ifdef PR_LOGGING
  if (!gRemoteLibraryLog) {
    gRemoteLibraryLog = PR_NewLogModule("sbRemoteLibrary");
  }
  LOG_LIB(("sbRemoteLibraryBase::sbRemoteLibraryBase()"));
#endif
}

sbRemoteLibraryBase::~sbRemoteLibraryBase()
{
  LOG_LIB(("sbRemoteLibraryBase::~sbRemoteLibraryBase()"));
}

// ---------------------------------------------------------------------------
//
//                          sbILibraryResource
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemoteLibraryBase::SetProperty( const nsAString& aID, const nsAString& aValue )
{
  LOG_LIB(( "sbRemoteLibraryBase::SetProperty( %s, %s )",
            NS_LossyConvertUTF16toASCII(aID).get(),
            NS_LossyConvertUTF16toASCII(aValue).get() ));

  nsresult rv;
  // rules
  // 1) Don't allow modification of http://songbird properties for the main library
  // 2) ONLY allow modificatoin of the hidden property for site libraries

  // Find out if we are trying to set properties on the main library
  PRBool isMain;
  nsCOMPtr<sbIMediaItem> item( do_QueryInterface( (sbIRemoteLibrary*)this, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  rv = SB_IsFromLibName( item, NS_LITERAL_STRING("main"), &isMain );
  NS_ENSURE_SUCCESS( rv, rv );

  if (isMain) {
    // are we trying to set a songbird property?
    if ( StringBeginsWith( aID,
                           NS_LITERAL_STRING("http://songbirdnest.com/") ) ) {
      // don't allow songbird properties to be modified on main library
      LOG_LIB(( "sbRemoteLibraryBase::SetProperty() - DENIED" ));
      return NS_ERROR_FAILURE;
    }
  } else {
    // all libraries that aren't the main library

    // are we trying to set the hidden??
    if ( aID.EqualsLiteral(SB_PROPERTY_HIDDEN) ) {
      // see if we are a sitelib. if not, don't allow hidden to be set.
      nsCOMPtr<sbIRemoteSiteLibrary> siteLib( do_QueryInterface((sbIRemoteLibrary*) this, &rv ) );
      if ( NS_FAILED(rv) || !siteLib ) {
        // don't allow the hidden property set outside of site libraries
        LOG_LIB(( "sbRemoteLibraryBase::SetProperty() - DENIED" ));
        return NS_ERROR_FAILURE;
      }
    }
  }

  // otherwise, just forward on to delegate impl.
  LOG_LIB(( "sbRemoteLibraryBase::SetProperty() - ALLOWED" ));
  rv = mRemMediaList->SetProperty( aID, aValue );
  return rv;
}

// ---------------------------------------------------------------------------
//
//                          sbISecurityAggregator
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP 
sbRemoteLibraryBase::GetRemotePlayer(sbIRemotePlayer * *aRemotePlayer)
{
  NS_ENSURE_STATE(mRemotePlayer);
  NS_ENSURE_ARG_POINTER(aRemotePlayer);

  nsresult rv;
  *aRemotePlayer = nsnull;

  nsCOMPtr<sbIRemotePlayer> remotePlayer;

  rv = mRemotePlayer->QueryInterface( NS_GET_IID( sbIRemotePlayer ), getter_AddRefs( remotePlayer ) );
  NS_ENSURE_SUCCESS( rv, rv );

  remotePlayer.swap( *aRemotePlayer );

  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                        sbIRemoteLibrary
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemoteLibraryBase::GetScanMediaOnCreation( PRBool *aShouldScan )
{
  NS_ENSURE_ARG_POINTER(aShouldScan);
  *aShouldScan = mShouldScan;
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::SetScanMediaOnCreation( PRBool aShouldScan )
{
  mShouldScan = aShouldScan;
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::ConnectToDefaultLibrary( const nsAString &aLibName )
{
  LOG_LIB(( "sbRemoteLibraryBase::ConnectToDefaultLibrary(guid:%s)",
            NS_LossyConvertUTF16toASCII(aLibName).get() ));

  nsAutoString guid;
  nsresult rv = GetLibraryGUID(aLibName, guid);
  if ( NS_SUCCEEDED(rv) ) {
    LOG_LIB(( "sbRemoteLibraryBase::ConnectToDefaultLibrary(%s) -- IS a default library",
              NS_LossyConvertUTF16toASCII(guid).get() ));

    // See if the library manager has it lying around.
    nsCOMPtr<sbILibraryManager> libManager(
        do_GetService( "@songbirdnest.com/Songbird/library/Manager;1", &rv ) );
    NS_ENSURE_SUCCESS( rv, rv );

    rv = libManager->GetLibrary( guid, getter_AddRefs(mLibrary) );
    NS_ENSURE_SUCCESS( rv, rv );

    rv = InitInternalMediaList();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return rv;
}

NS_IMETHODIMP
sbRemoteLibraryBase::CreateMediaItem( const nsAString& aURL,
                                      sbIMediaItem** _retval )
{
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_STATE(mLibrary);

  LOG_LIB(("sbRemoteLibraryBase::CreateMediaItem()"));

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURL);
  NS_ENSURE_SUCCESS(rv, rv);

  // Only allow the creation of media items with http(s) schemes
  PRBool validScheme;
  uri->SchemeIs("http", &validScheme);
  if (!validScheme) {
    uri->SchemeIs("https", &validScheme);
    if (!validScheme) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  // We use PR_TRUE here so that 2 sites can create a media item to the same
  // URI and get 2 different objects in the database and don't overwrite
  // each other.
  nsCOMPtr<sbIMediaItem> mediaItem;
  rv = mLibrary->CreateMediaItem(uri,
                                 nsnull,
                                 mAllowDuplicates,
                                 getter_AddRefs(mediaItem));
  NS_ENSURE_SUCCESS(rv, rv);

  // Set the OriginPage property so we know where it came from
  rv = mRemotePlayer->SetOriginScope( mediaItem, aURL );
  NS_ENSURE_SUCCESS(rv, rv);

  if (mShouldScan) {

    nsCOMPtr<sbIFileMetadataService> metadataService =
      do_GetService( "@songbirdnest.com/Songbird/FileMetadataService;1", &rv );
    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to get FileMetadataService!");

    if(NS_SUCCEEDED(rv)) {
      LOG_LIB(("sbRemoteLibraryBase::CreateMediaItem() -- doing a MD scan"));

      nsCOMPtr<nsIMutableArray> mediaItems = 
        do_CreateInstance("@songbirdnest.com/moz/xpcom/threadsafe-array;1", &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = mediaItems->AppendElement(mediaItem, PR_FALSE);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<sbIJobProgress> job;
      rv = metadataService->Read( mediaItems, getter_AddRefs(job) );
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // This will wrap in the appropriate site/regular RemoteMediaItem
  rv = SB_WrapMediaItem(mRemotePlayer, mediaItem, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  mRemotePlayer->GetNotificationManager()
    ->Action(sbRemoteNotificationManager::eUpdatedWithItems, mLibrary);

  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::CreateSimpleMediaList( const nsAString& aName,
                                            const nsAString& aSiteID,
                                            sbIRemoteMediaList** _retval )
{
  NS_ENSURE_ARG(!aName.IsEmpty());
  NS_ENSURE_ARG_POINTER(_retval);

  nsString siteID;
  if (aSiteID.IsEmpty()) {
    siteID.Assign(aName);
  }
  else {
    siteID.Assign(aSiteID);
  }

  nsresult rv;
  nsCOMPtr<sbIMediaList> mediaList;
  nsCOMPtr<sbIRemoteMediaList> remMediaList = GetMediaListBySiteID(siteID);
  if (remMediaList) {
    nsCOMPtr<sbIWrappedMediaList> wrappedList =
      do_QueryInterface( remMediaList, &rv );
    NS_ENSURE_SUCCESS( rv, rv );

    mediaList = wrappedList->GetMediaList();
    NS_ENSURE_TRUE( mediaList, NS_ERROR_FAILURE );
  }
  else {
    // Now we create one.
    rv = mLibrary->CreateMediaList( NS_LITERAL_STRING("simple"), nsnull,
                                    getter_AddRefs(mediaList) );
    NS_ENSURE_SUCCESS( rv, rv );

    // Set the OriginPage property so we know where it came from
    nsCOMPtr<sbIMediaItem> listAsItem( do_QueryInterface( mediaList, &rv ) );
    NS_ENSURE_SUCCESS( rv, rv) ;

    rv = mRemotePlayer->SetOriginScope( listAsItem, siteID );
    NS_ENSURE_SUCCESS( rv, rv );

    rv = SB_WrapMediaList( mRemotePlayer, mediaList,
                           getter_AddRefs(remMediaList) );
    NS_ENSURE_SUCCESS( rv, rv );
  }

  rv = mediaList->SetProperty( NS_LITERAL_STRING(SB_PROPERTY_MEDIALISTNAME),
                               aName );
  NS_ENSURE_SUCCESS( rv, rv );

  mRemotePlayer->GetNotificationManager()->
    Action( sbRemoteNotificationManager::eUpdatedWithPlaylists, mLibrary );

  NS_ADDREF(*_retval = remMediaList);
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::CreateMediaListFromURL( const nsAString& aName,
                                             const nsAString& aURL,
                                             sbICreateMediaListCallback* aCallback,
                                             const nsAString& aSiteID )
{
  NS_ENSURE_ARG(!aName.IsEmpty());
  NS_ENSURE_ARG(!aURL.IsEmpty());
  NS_ENSURE_STATE(mLibrary);

  LOG_LIB(("sbRemoteLibraryBase::CreateMediaListFromURL(%s)",
            NS_LossyConvertUTF16toASCII(aURL).get() ));

  nsString siteID;
  if (aSiteID.IsEmpty()) {
    siteID.Assign(aName);
  }
  else {
    siteID.Assign(aSiteID);
  }

  nsresult rv;
  nsCOMPtr<sbIMediaList> mediaList;
  nsCOMPtr<sbIRemoteMediaList> remMediaList = GetMediaListBySiteID(siteID);
  if (remMediaList) {
    nsCOMPtr<sbIWrappedMediaList> wrappedList =
      do_QueryInterface( remMediaList, &rv );
    NS_ENSURE_SUCCESS( rv, rv );

    mediaList = wrappedList->GetMediaList();
    NS_ENSURE_TRUE( mediaList, NS_ERROR_FAILURE );
  }
  else {
    rv = mLibrary->CreateMediaList( NS_LITERAL_STRING("simple"), nsnull,
                                    getter_AddRefs(mediaList) );
    NS_ENSURE_SUCCESS(rv, rv);

    // Set the OriginPage property so we know where it came from
    nsCOMPtr<sbIMediaItem> mediaItem(do_QueryInterface(mediaList));
    NS_ENSURE_TRUE(mediaItem, NS_ERROR_FAILURE);

    rv = mRemotePlayer->SetOriginScope( mediaItem, siteID );
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mediaList->SetProperty( NS_LITERAL_STRING(SB_PROPERTY_MEDIALISTNAME),
                               aName );
  NS_ENSURE_SUCCESS( rv, rv );

  mRemotePlayer->GetNotificationManager()
      ->Action(sbRemoteNotificationManager::eUpdatedWithPlaylists, mLibrary);

  nsCOMPtr<sbIPlaylistReaderManager> manager =
       do_GetService( "@songbirdnest.com/Songbird/PlaylistReaderManager;1",
                      &rv );
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), aURL);
  NS_ENSURE_SUCCESS(rv, rv);

  // Only allow the creation of media lists with http(s) schemes
  PRBool validScheme;
  uri->SchemeIs("http", &validScheme);
  if (!validScheme) {
    uri->SchemeIs("https", &validScheme);
    if (!validScheme) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  nsCOMPtr<sbIPlaylistReaderListener> lstnr =
      do_CreateInstance( "@songbirdnest.com/Songbird/PlaylistReaderListener;1",
                         &rv );
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<sbPlaylistReaderObserver> readerObserver =
    new sbPlaylistReaderObserver(mRemotePlayer, aCallback, mShouldScan);
  NS_ENSURE_TRUE( readerObserver, NS_ERROR_OUT_OF_MEMORY );

  nsCOMPtr<nsIObserver> observer( do_QueryInterface( readerObserver, &rv ) );
  NS_ENSURE_SUCCESS(rv, rv);

  rv = lstnr->SetObserver( observer );
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 dummy;
  rv = manager->LoadPlaylist( uri, mediaList, EmptyString(), true, lstnr, &dummy );
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetMediaListBySiteID( const nsAString &aSiteID,
                                           sbIRemoteMediaList **_retval )
{
  NS_ENSURE_ARG(!aSiteID.IsEmpty());
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<sbIRemoteMediaList> list = GetMediaListBySiteID(aSiteID);
  NS_IF_ADDREF(*_retval = list);
  return NS_OK;
}

/**
 * Determines if the library is the site library of the given remote player
 */
static PRBool IsSiteLibrary(sbILibrary *aLibrary, sbIRemotePlayer *aRemotePlayer) {
  PRBool result = PR_FALSE;
  nsCOMPtr<sbIRemoteLibrary> siteLibrary;
  nsresult rv = aRemotePlayer->GetSiteLibrary( getter_AddRefs(siteLibrary) );
  if ( NS_SUCCEEDED(rv) ) {
    nsCOMPtr<sbIMediaItem> siteLibraryAsItem = do_QueryInterface(siteLibrary);
    nsCOMPtr<sbIMediaItem> libraryAsItem = do_QueryInterface(aLibrary);
    PRBool equal = PR_FALSE;
    result = ( siteLibraryAsItem &&
               libraryAsItem &&
               NS_SUCCEEDED( siteLibraryAsItem->Equals( libraryAsItem, &equal ) ) &&
               equal );
  }
  return result;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetPlaylists( nsISimpleEnumerator** _retval )
{
  LOG_LIB(("sbRemoteLibraryBase::GetPlaylists()"));
  
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_STATE(mLibrary);

  nsresult rv;
  nsCOMPtr<sbIMediaList> mediaList = do_QueryInterface( mLibrary, &rv );
  NS_ENSURE_SUCCESS( rv, rv );

  mIgnoreHiddenPlaylists = !IsSiteLibrary(mLibrary, mRemotePlayer);
  
  rv = mediaList->EnumerateItemsByProperty( NS_LITERAL_STRING(SB_PROPERTY_ISLIST),
                                            NS_LITERAL_STRING("1"),
                                            this,
                                            sbIMediaList::ENUMERATIONTYPE_SNAPSHOT );
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<nsISimpleEnumerator> playlistEnum;
  if ( NS_SUCCEEDED(mEnumerationResult) ) {
    // Make an enumerator for the contents of mEnumerationArray.
    playlistEnum =
      new sbScriptableFilterItems( mEnumerationArray, mRemotePlayer );
    if ( !playlistEnum ) {
      NS_WARNING("Failed to make array enumerator");
    }
  }
  else {
    NS_WARNING("Item enumeration failed!");
    rv = mEnumerationResult;
  }

  if (!playlistEnum) {
    *_retval = nsnull;
    return NS_OK;
  }

  NS_ADDREF( *_retval = playlistEnum );

  // Reset the array and result codes for next time.
  mEnumerationArray.Clear();
  mEnumerationResult = NS_ERROR_NOT_INITIALIZED;

  return rv;
}

// ---------------------------------------------------------------------------
//
//                          sbIScriptableFilterResult
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemoteLibraryBase::GetArtists( nsIStringEnumerator** _retval )
{
  LOG_LIB(("sbRemoteLibraryBase::GetArtists()"));
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;
  
  nsCOMPtr<sbIMediaListView> view;
  rv = mRemMediaList->CreateView( nsnull, getter_AddRefs(view) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsCOMPtr<sbIFilterableMediaListView> filterView =
    do_QueryInterface( view, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsRefPtr<sbScriptableFilter> filter =
    new sbScriptableFilter( filterView,
                            NS_LITERAL_STRING(SB_PROPERTY_ARTISTNAME),
                            mRemotePlayer );
  NS_ENSURE_TRUE( filter, NS_ERROR_OUT_OF_MEMORY );
  
  NS_ADDREF(*_retval = filter);
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetAlbums( nsIStringEnumerator** _retval )
{
  LOG_LIB(("sbRemoteLibraryBase::GetAlbums()"));
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;
  
  nsCOMPtr<sbIMediaListView> view;
  rv = mRemMediaList->CreateView( nsnull, getter_AddRefs(view) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsCOMPtr<sbIFilterableMediaListView> filterView =
    do_QueryInterface( view, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsRefPtr<sbScriptableFilter> filter =
    new sbScriptableFilter( filterView,
                            NS_LITERAL_STRING(SB_PROPERTY_ALBUMNAME),
                            mRemotePlayer);
  NS_ENSURE_TRUE( filter, NS_ERROR_OUT_OF_MEMORY );
  
  NS_ADDREF(*_retval = filter);
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetGenres( nsIStringEnumerator** _retval )
{
  LOG_LIB(("sbRemoteLibraryBase::GetGenres()"));
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;
  
  nsCOMPtr<sbIMediaListView> view;
  rv = mRemMediaList->CreateView( nsnull, getter_AddRefs(view) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsCOMPtr<sbIFilterableMediaListView> filterView =
    do_QueryInterface( view, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsRefPtr<sbScriptableFilter> filter =
    new sbScriptableFilter( filterView,
                            NS_LITERAL_STRING(SB_PROPERTY_GENRE),
                            mRemotePlayer);
  NS_ENSURE_TRUE( filter, NS_ERROR_OUT_OF_MEMORY );
  
  NS_ADDREF(*_retval = filter);
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetYears( nsIStringEnumerator** _retval )
{
  LOG_LIB(("sbRemoteLibraryBase::GetYears()"));
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;
  
  nsCOMPtr<sbIMediaListView> view;
  rv = mRemMediaList->CreateView( nsnull, getter_AddRefs(view) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsCOMPtr<sbIFilterableMediaListView> filterView =
    do_QueryInterface( view, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsRefPtr<sbScriptableFilter> filter =
    new sbScriptableFilter( filterView,
                            NS_LITERAL_STRING(SB_PROPERTY_YEAR),
                            mRemotePlayer );
  NS_ENSURE_TRUE( filter, NS_ERROR_OUT_OF_MEMORY );
  
  NS_ADDREF(*_retval = filter);
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetItems( nsISupports** _retval )
{
  LOG_LIB(("sbRemoteLibraryBase::Items()"));
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv;
  
  nsCOMPtr<sbIMediaListView> view;
  rv = mRemMediaList->CreateView( nsnull, getter_AddRefs(view) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsCOMPtr<sbIFilterableMediaListView> filterView =
    do_QueryInterface( view, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsRefPtr<sbScriptableFilterItems> items =
    new sbScriptableFilterItems( filterView, mRemotePlayer);
  NS_ENSURE_TRUE( items, NS_ERROR_OUT_OF_MEMORY );
  
  *_retval = NS_ISUPPORTS_CAST( nsIXPCScriptable*, items );
  NS_ADDREF(*_retval);
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetMostPlayedArtists(nsIVariant** _retval)
{
  LOG_LIB(("sbRemoteLibraryBase::GetMostPlayedArtists()"));
  NS_ENSURE_ARG_POINTER(_retval);
  nsresult rv = NS_OK;

  nsCOMPtr<sbILibraryStatistics> libraryStatistics = 
    do_QueryInterface( mLibrary, &rv );
  NS_ENSURE_SUCCESS( rv, rv );

  // get the most played artists in an array of variants
  nsCOMPtr<nsIArray> mostPlayedArtists;
  rv = libraryStatistics->CollectDistinctValues(
      NS_LITERAL_STRING(SB_PROPERTY_ARTISTNAME), 
      sbILibraryStatistics::COLLECT_SUM,
      NS_LITERAL_STRING(SB_PROPERTY_PLAYCOUNT), PR_FALSE, 100,
      getter_AddRefs(mostPlayedArtists));
  NS_ENSURE_SUCCESS( rv, rv );

  PRUint32 count;
  rv = mostPlayedArtists->GetLength(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  LOG_LIB(("sbRemoteLibraryBase::GetMostPlayedArtists() got %d artists", count));

  // create a variant to hold the array
  nsCOMPtr<nsIWritableVariant> variant = 
    do_CreateInstance(NS_VARIANT_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (count > 0) {
    // okay, create a variant array to hold these artists
    nsIVariant** arr = (nsIVariant**)NS_Alloc(sizeof(nsIVariant*)*count);
    if (!arr) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // put nsIVariants from mostPlayedArtists into arr
    for (PRUint32 i = 0; i < count; i++) {
      rv = mostPlayedArtists->QueryElementAt(i, NS_GET_IID(nsIVariant), 
          (void**)&arr[i]);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // put the array into the variant
    rv = variant->SetAsArray(nsIDataType::VTYPE_INTERFACE_IS,
        &NS_GET_IID(nsIVariant), count, arr);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // empty arrays are easy
    rv = variant->SetAsEmptyArray();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // pass it back
  return CallQueryInterface(variant, _retval);

  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetConstraint(sbILibraryConstraint * *aConstraint)
{
  nsresult rv;
  nsCOMPtr<sbILibraryConstraintBuilder> builder =
    do_CreateInstance( "@songbirdnest.com/Songbird/Library/ConstraintBuilder;1",
                       &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  return builder->Get(aConstraint);
}

// ---------------------------------------------------------------------------
//
//                            sbIWrappedMediaList
//
// ---------------------------------------------------------------------------

already_AddRefed<sbIMediaItem>
sbRemoteLibraryBase::GetMediaItem()
{
  return mRemMediaList->GetMediaItem();
}

already_AddRefed<sbIMediaList>
sbRemoteLibraryBase::GetMediaList()
{
  return mRemMediaList->GetMediaList();
}

// ---------------------------------------------------------------------------
//
//                     sbIMediaListEnumerationListener
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemoteLibraryBase::OnEnumerationBegin( sbIMediaList *aMediaList,
                                         PRUint16 *_retval )
{
  NS_ENSURE_ARG_POINTER(_retval);

  NS_ASSERTION( mEnumerationArray.Count() == 0,
                "Someone forgot to clear mEnumerationArray!" );
  NS_ASSERTION( mEnumerationResult == NS_ERROR_NOT_INITIALIZED,
                "Someone forgot to reset mEnumerationResult!" );

  *_retval = sbIMediaListEnumerationListener::CONTINUE;
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::OnEnumeratedItem( sbIMediaList *aMediaList,
                                       sbIMediaItem *aMediaItem,
                                       PRUint16 *_retval )
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(_retval);

  // If there is no outer guid then we want this. 
  // (Avoids smart playlists dupes, see bug 14896)
  nsString propValue;
  PRBool const isSmartStoragePlaylist = 
    NS_SUCCEEDED(aMediaItem->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_OUTERGUID), 
                                         propValue)) && 
      !propValue.IsEmpty();

  PRBool const isHiddenPlaylist = 
    NS_SUCCEEDED(aMediaItem->GetProperty(NS_LITERAL_STRING(SB_PROPERTY_HIDDEN),
                                         propValue)) &&
      propValue.EqualsLiteral("1");
  // Only process lists that aren't smart playlist storage lists. And if we're
  // ignore hidden playlists then don't process hidden ones either
  if (!isSmartStoragePlaylist && (!mIgnoreHiddenPlaylists || !isHiddenPlaylist)) {
    if (mEnumerationArray.AppendObject(aMediaItem)) {
      *_retval = sbIMediaListEnumerationListener::CONTINUE;
    }
    else {
      *_retval = sbIMediaListEnumerationListener::CANCEL;
    }
  }
  else { // If there was an outer guid, skip this one
    *_retval = sbIMediaListEnumerationListener::CONTINUE;
  }
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::OnEnumerationEnd( sbIMediaList *aMediaList,
                                       nsresult aStatusCode )
{
  mEnumerationResult = aStatusCode;
  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                           nsIXPCScriptable
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemoteLibraryBase::GetClassName( char * *aClassName )
{
  NS_ENSURE_ARG_POINTER(aClassName);
  *aClassName = ToNewCString( NS_LITERAL_CSTRING("SongbirdLibrary") );
  NS_ENSURE_TRUE( aClassName, NS_ERROR_OUT_OF_MEMORY );
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetScriptableFlags( PRUint32 *aScriptableFlags )
{
  NS_ENSURE_ARG_POINTER(aScriptableFlags);
  // XXX Mook: USE_JSSTUB_FOR_ADDPROPERTY is needed to define things on the
  //           prototype properly; even with it set scripts cannot add
  //           properties onto the object (because they're not allow to *set*)
  *aScriptableFlags = USE_JSSTUB_FOR_ADDPROPERTY |
                      DONT_ENUM_STATIC_PROPS |
                      DONT_ENUM_QUERY_INTERFACE |
                      WANT_GETPROPERTY |
                      WANT_NEWRESOLVE |
                      ALLOW_PROP_MODS_DURING_RESOLVE |
                      DONT_REFLECT_INTERFACE_NAMES ;
  return NS_OK;
}

NS_IMETHODIMP
sbRemoteLibraryBase::NewResolve( nsIXPConnectWrappedNative *wrapper,
                                 JSContext *cx,
                                 JSObject *obj,
                                 jsval id,
                                 PRUint32 flags,
                                 JSObject **objp,
                                 PRBool *_retval )
{
  LOG_LIB(("sbRemoteLibraryBase::NewResolve()"));
#ifdef DEBUG
  if ( JSVAL_IS_STRING(id) ) {
    nsDependentString jsid( (PRUnichar *)::JS_GetStringChars(JSVAL_TO_STRING(id)),
                            ::JS_GetStringLength(JSVAL_TO_STRING(id)));
    TRACE_LIB(( "   resolving %s", NS_LossyConvertUTF16toASCII(jsid).get() ));
  }
#endif

  NS_ENSURE_TRUE(mRemMediaList, NS_ERROR_FAILURE);

  return mRemMediaList->NewResolve( wrapper, cx, obj, id, flags, objp, _retval );
}

NS_IMETHODIMP
sbRemoteLibraryBase::GetProperty( nsIXPConnectWrappedNative *wrapper,
                                  JSContext * cx,
                                  JSObject * obj,
                                  jsval id,
                                  jsval * vp,
                                  PRBool *_retval )
{
  TRACE_LIB(("sbRemoteLibraryBase::GetProperty()"));
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_ARG_POINTER(vp);
  
  nsresult rv;

  if ( !JSVAL_IS_STRING(id) ) {
    // we don't care about non-strings
    return NS_OK;
  }
  
  nsDependentString jsid( (PRUnichar *)::JS_GetStringChars(JSVAL_TO_STRING(id)),
                          ::JS_GetStringLength(JSVAL_TO_STRING(id)));

  TRACE_LIB(( "   Getting property %s", NS_LossyConvertUTF16toASCII(jsid).get() ));
  
  nsCOMPtr<nsISupports> supports;
  
  { // getArtists(), getAlbums(), getGenres(), getYears()
    nsCOMPtr<nsIStringEnumerator> stringEnum;
    if ( jsid.EqualsLiteral("getArtists") ) {
      rv = GetArtists( getter_AddRefs(stringEnum) );
      NS_ENSURE_SUCCESS( rv, rv );
    } else if ( jsid.EqualsLiteral("getAlbums") ) {
      rv = GetAlbums( getter_AddRefs(stringEnum) );
      NS_ENSURE_SUCCESS( rv, rv );
    } else if ( jsid.EqualsLiteral("getGenres") ) {
      rv = GetGenres( getter_AddRefs(stringEnum) );
      NS_ENSURE_SUCCESS( rv, rv );
    } else if ( jsid.EqualsLiteral("getYears") ) {
      rv = GetYears( getter_AddRefs(stringEnum) );
      NS_ENSURE_SUCCESS( rv, rv );
    }
  
    if ( stringEnum ) {
      // make the callable wrapper
      nsRefPtr<sbScriptableLibraryFunction> func =
        new sbScriptableLibraryFunction( stringEnum, NS_GET_IID(nsIStringEnumerator) );
      NS_ENSURE_TRUE( func, NS_ERROR_OUT_OF_MEMORY );
      
      supports = NS_ISUPPORTS_CAST( nsIXPCScriptable*, func );
    }
  }

  if ( jsid.EqualsLiteral("getPlaylists") ) {
    nsCOMPtr<nsISimpleEnumerator> simpleEnum;
    rv = GetPlaylists( getter_AddRefs(simpleEnum) );
    NS_ENSURE_SUCCESS( rv, rv );

    nsRefPtr<sbScriptableLibraryFunction> func =
      new sbScriptableLibraryFunction( simpleEnum, NS_GET_IID(nsISimpleEnumerator) );
    NS_ENSURE_TRUE( simpleEnum, NS_ERROR_OUT_OF_MEMORY );
    
    supports = NS_ISUPPORTS_CAST( nsIXPCScriptable*, func );
  }

  if (supports) {
    // do the security check
    char* access;
    nsIID iid = NS_GET_IID(nsISupports);
    // note that an error return value also means access denied
    rv = mSecurityMixin->CanCallMethod( &iid,
                                        jsid.BeginReading(),
                                        &access );
    PRBool canCallMethod = NS_SUCCEEDED(rv);
    if (canCallMethod) {
      canCallMethod = !strcmp( access, "AllAccess" );
      NS_Free(access);
    }

    if ( !canCallMethod ) {
      JSAutoRequest ar(cx);
      
      // get an error message
      nsCOMPtr<nsIStringBundleService> bundleService =
        do_GetService( NS_STRINGBUNDLE_CONTRACTID, &rv );
      NS_ENSURE_SUCCESS( rv, rv );
      nsCOMPtr<nsIStringBundle> bundle;
      rv = bundleService->CreateBundle( "chrome://global/locale/security/caps.properties",
                                        getter_AddRefs(bundle) );
      NS_ENSURE_SUCCESS( rv, rv );

      char* classNameC;
      rv = this->GetClassName(&classNameC);
      NS_ENSURE_SUCCESS( rv, rv );
      nsString className =
        NS_ConvertASCIItoUTF16( nsDependentCString(classNameC) );
      NS_Free(classNameC);

      nsString errorMessage;
      const PRUnichar *formatStrings[] = {
        className.get(),
        jsid.get()
      };
      rv = bundle->FormatStringFromName( NS_LITERAL_STRING("CallMethodDenied").get(),
                                         formatStrings,
                                         sizeof(formatStrings) / sizeof(formatStrings[0]),
                                         getter_Copies(errorMessage) );
      NS_ENSURE_SUCCESS( rv, rv );
      
      JSString *jsstr = JS_NewUCStringCopyN( cx,
                                             reinterpret_cast<const jschar*>( errorMessage.get() ),
                                             errorMessage.Length() );
      if (jsstr)
        JS_SetPendingException( cx, STRING_TO_JSVAL(jsstr) );
      
      *_retval = JS_FALSE;
      return NS_OK;
    }
  
    // send it along to JS
    nsCOMPtr<nsIXPConnect> xpc;
    rv = wrapper->GetXPConnect( getter_AddRefs(xpc) );
    NS_ENSURE_SUCCESS( rv, rv );

    nsCOMPtr<nsIXPConnectJSObjectHolder> objHolder;
    rv = xpc->WrapNative( cx,
                          obj,
                          supports,
                          NS_GET_IID(nsISupports),
                          getter_AddRefs(objHolder) );
    NS_ENSURE_SUCCESS( rv, rv );
  
    JSObject* obj = nsnull;
    rv = objHolder->GetJSObject( &obj );
    NS_ENSURE_SUCCESS( rv, rv );
  
    *vp = OBJECT_TO_JSVAL(obj);
    *_retval = PR_TRUE;
    return NS_SUCCESS_I_DID_SOMETHING;
  }
  
  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                            Helper Methods
//
// ---------------------------------------------------------------------------

// If the libraryID is one of the default libraries we set the out param
// to the internal GUID of library as understood by the library manager
// and return NS_OK. If it is not one of the default libraries then
// return NS_ERROR_FAILURE. Also returns an error if the pref system is not
// available.
nsresult
sbRemoteLibraryBase::GetLibraryGUID( const nsAString &aLibraryID,
                                     nsAString &aLibraryGUID )
{
#ifdef PR_LOGGING
  // This method is static, so the log might not be initialized
  if (!gRemoteLibraryLog) {
    gRemoteLibraryLog = PR_NewLogModule("sbRemoteLibrary");
  }
  LOG_LIB(( "sbRemoteLibraryBase::GetLibraryGUID(%s)",
            NS_LossyConvertUTF16toASCII(aLibraryID).get() ));
#endif

  nsCAutoString prefKey;

  // match the 'magic' strings to the keys for the prefs
  if ( aLibraryID.EqualsLiteral("main") ) {
    prefKey.AssignLiteral("songbird.library.main");
  } else if ( aLibraryID.EqualsLiteral("web") ) {
    prefKey.AssignLiteral("songbird.library.web");
  }

  // right now just bail if it isn't a default
  if ( prefKey.IsEmpty() ) {
    LOG_LIB(("sbRemoteLibraryBase::GetLibraryGUID() -- not a default library"));
    // ultimately we need to be able to get the GUID for non-default libraries
    //   if we are going to allow the library manager to manage them.
    // We might want to do the string hashing here and add keys for
    //   songbird.library.site.hashkey
    return NS_ERROR_FAILURE;
  }

  // The value of the pref should be a library resourceGUID.
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> prefService =
                                 do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsString> supportsString;
  rv = prefService->GetComplexValue( prefKey.get(),
                                     NS_GET_IID(nsISupportsString),
                                     getter_AddRefs(supportsString) );
  //  Get the GUID for this library out of the supports string
  if (NS_SUCCEEDED(rv)) {
    // Use the value stored in the prefs.
    rv = supportsString->GetData(aLibraryGUID);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

already_AddRefed<sbIRemoteMediaList>
sbRemoteLibraryBase::GetMediaListBySiteID(const nsAString& aSiteID)
{
  NS_ASSERTION(!aSiteID.IsEmpty(), "Don't give me an empty ID!");

  nsresult rv;
  nsCOMPtr<sbIMutablePropertyArray> mutableArray =
    do_CreateInstance( SB_MUTABLEPROPERTYARRAY_CONTRACTID, &rv );
  NS_ENSURE_SUCCESS( rv, nsnull );

  rv = mutableArray->AppendProperty( NS_LITERAL_STRING(SB_PROPERTY_RAPISITEID),
                                     aSiteID );
  NS_ENSURE_SUCCESS( rv, nsnull );

  rv = mutableArray->AppendProperty( NS_LITERAL_STRING(SB_PROPERTY_ISLIST),
                                     NS_LITERAL_STRING("1") );
  NS_ENSURE_SUCCESS( rv, nsnull );

  nsCOMArray<sbIMediaItem> items;
  nsRefPtr<sbRemoteLibraryEnumCallback> listener =
    new sbRemoteLibraryEnumCallback(items);
  NS_ENSURE_TRUE( listener, nsnull );

  nsCOMPtr<sbIMediaList> libList = do_QueryInterface( mLibrary, &rv );
  NS_ENSURE_SUCCESS( rv, nsnull );

  rv = libList->EnumerateItemsByProperties( mutableArray,
                                            listener,
                                            sbIMediaList::ENUMERATIONTYPE_SNAPSHOT );
  NS_ENSURE_SUCCESS( rv, nsnull );

  if (items.Count() > 0) {
    nsCOMPtr<sbIMediaItem> foundItem = FindMediaItemWithMatchingScope(items);
    if (foundItem) {
      nsCOMPtr<sbIMediaList> list = do_QueryInterface( foundItem, &rv );
      NS_ASSERTION( NS_SUCCEEDED(rv), "Failed to QI to sbIMediaList!" );

      nsCOMPtr<sbIRemoteMediaList> retval;
      rv = SB_WrapMediaList( mRemotePlayer, list, getter_AddRefs(retval) );
      NS_ENSURE_SUCCESS( rv, nsnull );

      return retval.forget();
    }
  }

  // if we reach this point, we did not find the media list, return nsnull.
  return nsnull;
}

already_AddRefed<sbIMediaItem>
sbRemoteLibraryBase::FindMediaItemWithMatchingScope( const nsCOMArray<sbIMediaItem>& aMediaItems )
{
  nsCOMPtr<nsIURI> siteScopeURI = mRemotePlayer->GetSiteScopeURI();
  NS_ENSURE_TRUE( siteScopeURI, nsnull );

  nsCString siteHost;
  nsresult rv = siteScopeURI->GetHost(siteHost);
  NS_ENSURE_SUCCESS( rv, nsnull );

  PRUint32 itemCount = (PRUint32)aMediaItems.Count();
  NS_ASSERTION(itemCount > 0, "Empty items list!");

  // Build an array of site scope URLs
  nsTArray<sbRemoteLibraryScopeURLSet> scopeURLSet(itemCount);

  for (PRUint32 itemIndex = 0; itemIndex < itemCount; itemIndex++) {
    const nsCOMPtr<sbIMediaItem>& item = aMediaItems.ObjectAt(itemIndex);

    nsString scopeURL;
    rv = item->GetProperty( NS_LITERAL_STRING(SB_PROPERTY_RAPISCOPEURL),
                            scopeURL );
    NS_ENSURE_SUCCESS( rv, nsnull );

    NS_ASSERTION( !scopeURL.IsEmpty(), "Empty scope URL" );

    nsCOMPtr<nsIURI> scopeURI;
    rv = NS_NewURI( getter_AddRefs(scopeURI), scopeURL );
    NS_ENSURE_SUCCESS( rv, nsnull );

    nsCString host;
    rv = scopeURI->GetHost(host);
    NS_ENSURE_SUCCESS( rv, nsnull );

    rv = sbURIChecker::CheckDomain( host, siteScopeURI );
    if (NS_FAILED(rv)) {
      continue;
    }

    nsCString path;
    rv = scopeURI->GetPath(path);
    NS_ENSURE_SUCCESS( rv, nsnull );

    sbRemoteLibraryScopeURLSet* newSet =
      scopeURLSet.AppendElement( sbRemoteLibraryScopeURLSet( path, item ) );
    NS_ENSURE_TRUE( newSet, nsnull );
  }

  // Yay QuickSort!
  scopeURLSet.Sort();

  itemCount = scopeURLSet.Length();
  NS_ENSURE_TRUE(itemCount, nsnull);

  for (PRInt64 setIndex = itemCount - 1; setIndex >= 0; setIndex--) {
    const sbRemoteLibraryScopeURLSet& set = scopeURLSet.ElementAt((PRUint32)setIndex);

    nsCString path(set.scopePath);
    rv = sbURIChecker::CheckPath( path, siteScopeURI );
    if (NS_SUCCEEDED(rv)) {
      sbIMediaItem* retval = set.item;
      NS_ADDREF(retval);
      return retval;
    }
  }

  return nsnull;
}

