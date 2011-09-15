/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2010 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */


#ifndef __SBLIBRARYUTILS_H__
#define __SBLIBRARYUTILS_H__

// Mozilla inclues
#include <nsCOMArray.h>
#include <nsCOMPtr.h>
#include <pratom.h>
#include <nsServiceManagerUtils.h>
#include <nsComponentManagerUtils.h>
#include <nsStringAPI.h>

// Mozilla interfaces
#include <nsIMutableArray.h>

// Songbird includes
#include <sbMemoryUtils.h>

// Songbird interfaces
#include <sbILibrary.h>
#include <sbILibraryManager.h>

class sbIMediaItem;
class sbILibrary;
class nsIIOService;
class sbIPropertyOperator;

class sbLibraryBatchHelper
{
public:
  sbLibraryBatchHelper() :
    mDepth(0)
  {
    MOZ_COUNT_CTOR(sbLibraryBatchHelper);
  }

  ~sbLibraryBatchHelper()
  {
    MOZ_COUNT_DTOR(sbLibraryBatchHelper);
  }

  void Begin()
  {
    NS_ASSERTION(mDepth >= 0, "Illegal batch depth, mismatched calls!");
    PR_AtomicIncrement(&mDepth);
  }

  void End()
  {
#ifdef DEBUG
    PRInt32 depth = PR_AtomicDecrement(&mDepth);
    NS_ASSERTION(depth >= 0, "Illegal batch depth, mismatched calls!");
#else
    PR_AtomicDecrement(&mDepth);
#endif
  }

  PRUint32 Depth()
  {
    return (PRUint32) mDepth;
  }

  PRBool IsActive()
  {
    return mDepth > 0;
  }

private:
  PRInt32 mDepth;
};

class sbLibraryUtils
{
public:
  /**
   * Given an item and a library, attempt to locate a matching item in the
   * library.  Returns null (and success) if not found.
   *
   * \param aItem    The item to find; not necessarily owned by aLibrary
   * \param aLibrary The library to look in
   * \return         The found item, or null
   */
  static nsresult GetItemInLibrary(/* in */  sbIMediaItem * aItem,
                                   /* in */  sbILibrary   * aLibrary,
                                   /* out */ sbIMediaItem **_retval);

  /**
   * This function search for itens in aMediaList with the same URL's.
   * The origin URL of aMediaItem is used, if it doesn't exist then the
   * content URL is used. aMediaList is then search by content and then
   * origin URL
   * \param aItem The item to find matches for
   * \param aMediaList The list to search in, can be a library
   * \return The media items found, or empty array. aCopies may be null
   *         in which case the function will return NS_ERROR_NOT_AVAILABLE
   *         if no copies are found
   */
  static nsresult FindItemsWithSameURL(sbIMediaItem *    aMediaItem,
                                       sbIMediaList *    aMediaList,
                                       nsIMutableArray * aCopies);

  /**
   * aList is searched for items that have origin ID's that match
   * aMediaItem's ID and items that has the same origin ID's as the
   * aMediaItem's origin ID's
   * \param aItem The item to find a copies of
   * \param aList The list to search
   * \return The media items found, or empty array. aCopies may be null
   *         in which case the function will return NS_ERROR_NOT_AVAILABLE
   *         if no copies are found
   */
  static nsresult FindCopiesByID(sbIMediaItem * aMediaItem,
                                 sbIMediaList * aList,
                                 nsIMutableArray * aCopies);

  /**
   * Searches aList for items that have ID's that match aMediaItem's origin ID
   * \param aItem The item to find a match for
   * \param aLibrary The library to look in
   * \return The media items found, or empty array. aCopies may be null
   *         in which case the function will return NS_ERROR_NOT_AVAILABLE
   *         if no copies are found
   */
  static nsresult FindOriginalsByID(sbIMediaItem * aMediaItem,
                                    sbIMediaList * aList,
                                    nsIMutableArray * aCopies);
  /**
   * Return the origin media item for the media item specified by aItem.
   *
   * \param aItem The item for which to get the origin media item.
   * \return      The origin media item.
   */
  static nsresult GetOriginItem(/* in */ sbIMediaItem*   aItem,
                                /* out */ sbIMediaItem** _retval);
  /**
   * Attempt to get the content length of the media item.  If the content length
   * property is not set, attempt to determine the content legnth from the
   * content source and update the content length property.
   *
   * \param aItem    The item to look up
   * \return         The content length of the item (or throw an exception)
   */
  static nsresult GetContentLength(/* in */  sbIMediaItem * aItem,
                                   /* out */ PRInt64      * _retval = nsnull);

  /**
   * Set the content length of the media item
   *
   * \param aItem    The item to set the content length to
   * \param aURI     The URI to retrive the content length from
   */
  static nsresult SetContentLength(/* in */  sbIMediaItem * aItem,
                                   /* in */  nsIURI       * aURI);

  /**
   * \brief Return a library content URI for the URI specified by aURI.
   *        A library content URI is a specially formatted URI for use within
   *        Songbird libraries and is formatted to facilitate searching for
   *        equivalent URI's (e.g., "file:" URI's are all lower case on
   *        Windows).
   *        URI's provided to createMediaItem and related methods must be
   *        library content URI's.
   *
   * \param aURI                URI for which to get content URI.
   * \param aIOService          Optional IO service object
   *
   * \return                    Library content URI.
   */
  static nsresult GetContentURI(/* in  */ nsIURI*   aURI,
                                /* out */ nsIURI** _retval,
                                /* in  */ nsIIOService * aIOService = nsnull);

  /**
   * \brief Return a media list content type.
   *
   * \param aMediaList          Media list for while to get the content type.
   *
   * \return                    Media list content type.
   */
  static nsresult GetMediaListContentType(/* in  */ sbIMediaList *aMediaList,
                                          /* out */ PRUint16 *aListContentType);

  /**
   * \brief Return a library content URI for the file specified by aFile.
   *        Special processing is required to convert an nsIFile to a library
   *        content URI (see bug 6227).  getFileContentURI must be used instead
   *        of nsIIOService.newFileURI for generating library content URI's.
   *
   * \param aFile               File for which to get content URI.
   *
   * \return                    Library content URI.
   */
  static nsresult GetFileContentURI(/* in  */ nsIFile*  aFile,
                                    /* out */ nsIURI** _retval);

  static nsresult GetItemsByProperty(sbIMediaList * aMediaList,
                                     nsAString const & aPropertyName,
                                     nsAString const & aValue,
                                     nsCOMArray<sbIMediaItem> & aMediaItems);

  /**
   * Returns the media lists matching the content type
   * \param aLibrary the library to look for playlists
   * \param aContentType The content type to filter by
   * \param aMediaLists the returned collection of media lists
   */
  static nsresult GetMediaListByContentType(sbILibrary * aLibrary,
                                            PRUint32 aContentType,
                                            nsIArray ** aMediaLists);

  /**
   * Returns the equality operator for the content property
   *
   * \return                    The equal operator
   */
  static PRUint32 GetEqualOperator(sbIPropertyOperator ** aOperator);

  /**
   * \brief Suggest a unique name for playlist.
   *
   * \param aLibrary  an sbILibrary.
   * \param aListName the playlist name to append id to.
   * \return a unique playlist name.
   */
  static nsresult SuggestUniqueNameForPlaylist(
                    /* in  */ sbILibrary *aLibrary,
                    /* in  */ nsAString const & aListName,
                    /* out */ nsAString & aName);
  /**
   * \brief Links a copy to its original. It will take into account the
   * libraries the items belong to. It will only link from main library to
   * non-main library. And if necessary if the copy is in the main library 
   * it will link the original to the copy
   * \param aOriginal The original to link aCopy to
   * \param aCopy The copy to be linked
   */

  static nsresult LinkCopy(sbIMediaItem * aOriginal, sbIMediaItem * aCopy);
};

/**
 * Retrieves the main library
 */
inline
nsresult GetMainLibrary(sbILibrary ** aMainLibrary) {
  nsresult rv;
  nsCOMPtr<sbILibraryManager> libManager =
      do_GetService("@songbirdnest.com/Songbird/library/Manager;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILibrary> mainLibrary;
  return libManager->GetMainLibrary(aMainLibrary);
}

/**
 * Tests whether the library passed is the main library
 */
inline
PRBool sbIsMainLibrary(sbILibrary * aLibrary)
{
  NS_ENSURE_ARG_POINTER(aLibrary);

  nsresult rv;

  nsCOMPtr<sbILibrary> mainLibrary;
  rv = GetMainLibrary(getter_AddRefs(mainLibrary));
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  PRBool isEqual;
  rv = mainLibrary->Equals(aLibrary, &isEqual);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  return isEqual;
}

/**
 * Retrieves the ID of the main library
 */
inline
nsresult GetMainLibraryId(nsAString & aLibraryId) {

  nsCOMPtr<sbILibrary> mainLibrary;
  nsresult rv = GetMainLibrary(getter_AddRefs(mainLibrary));
  NS_ENSURE_SUCCESS(rv, rv);

  return mainLibrary->GetGuid(aLibraryId);
}

//
// Auto-disposal class wrappers.
//
//   sbAutoRemoveMediaItem      Wrapper to auto-remove a media item from its
//                              library.
//

SB_AUTO_NULL_CLASS(sbAutoRemoveMediaItem,
                   nsCOMPtr<sbIMediaItem>,
                   {
                     nsCOMPtr<sbILibrary> library;
                     nsresult rv = mValue->GetLibrary(getter_AddRefs(library));
                     if (NS_SUCCEEDED(rv))
                       library->Remove(mValue);
                   });

#endif // __SBLIBRARYUTILS_H__

