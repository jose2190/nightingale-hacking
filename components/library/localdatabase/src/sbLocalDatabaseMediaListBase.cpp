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

#include "sbLocalDatabaseMediaListBase.h"

#include <nsIMutableArray.h>
#include <nsIStringBundle.h>
#include <nsISimpleEnumerator.h>
#include <nsIStringEnumerator.h>
#include <nsIURI.h>
#include <nsIVariant.h>
#include <sbICascadeFilterSet.h>
#include <sbIDatabaseQuery.h>
#include <sbIDatabaseResult.h>
#include <sbIDevice.h>
#include <sbIDeviceManager.h>
#include <sbIFilterableMediaListView.h>
#include <sbILibrary.h>
#include <sbILocalDatabaseGUIDArray.h>
#include <sbILocalDatabaseLibrary.h>
#include <sbIMediaList.h>
#include <sbIMediaListListener.h>
#include <sbIMediaListView.h>
#include <sbIPropertyArray.h>
#include <sbIPropertyInfo.h>
#include <sbIPropertyManager.h>
#include <sbISearchableMediaListView.h>
#include <sbISortableMediaListView.h>

#include <DatabaseQuery.h>
#include <nsAutoLock.h>
#include <nsComponentManagerUtils.h>
#include <nsServiceManagerUtils.h>
#include <nsHashKeys.h>
#include <nsMemory.h>
#include <nsNetUtil.h>
#include <pratom.h>
#include <sbSQLBuilderCID.h>
#include <sbTArrayStringEnumerator.h>
#include <sbPropertiesCID.h>
#include <sbStandardProperties.h>
#include <sbDebugUtils.h>

#include "sbLocalDatabaseCascadeFilterSet.h"
#include "sbLocalDatabaseCID.h"
#include "sbLocalDatabaseGUIDArray.h"
#include "sbLocalDatabaseLibrary.h"
#include "sbLocalDatabasePropertyCache.h"
#include "sbLocalMediaListBaseEnumerationListener.h"

#define DEFAULT_PROPERTIES_URL "chrome://songbird/locale/songbird.properties"

NS_IMPL_ISUPPORTS_INHERITED1(sbLocalDatabaseMediaListBase,
                             sbLocalDatabaseMediaItem,
                             sbIMediaList)

sbLocalDatabaseMediaListBase::sbLocalDatabaseMediaListBase()
: mFullArrayMonitor(nsnull),
  mListContentType(sbIMediaList::CONTENTTYPE_NONE),
  mLockedEnumerationActive(PR_FALSE)
{
}

sbLocalDatabaseMediaListBase::~sbLocalDatabaseMediaListBase()
{
  if (mFullArrayMonitor) {
    nsAutoMonitor::DestroyMonitor(mFullArrayMonitor);
  }
}

nsresult
sbLocalDatabaseMediaListBase::Init(sbLocalDatabaseLibrary* aLibrary,
                                   const nsAString& aGuid,
                                   PRBool aOwnsLibrary)
{
  mFullArrayMonitor =
    nsAutoMonitor::NewMonitor("sbLocalDatabaseMediaListBase::mFullArrayMonitor");
  NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_OUT_OF_MEMORY);

  // Initialize our base classes
  nsresult rv = sbLocalDatabaseMediaListListener::Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sbLocalDatabaseMediaItem::Init(aLibrary, aGuid, aOwnsLibrary);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool success = mFilteredProperties.Init();
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  nsStringHashKey *key = mFilteredProperties.PutEntry(NS_LITERAL_STRING(SB_PROPERTY_CONTENTURL));
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);

  key = mFilteredProperties.PutEntry(NS_LITERAL_STRING(SB_PROPERTY_CREATED));
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);

  key = mFilteredProperties.PutEntry(NS_LITERAL_STRING(SB_PROPERTY_UPDATED));
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);

  key = mFilteredProperties.PutEntry(NS_LITERAL_STRING(SB_PROPERTY_GUID));
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);

  key = mFilteredProperties.PutEntry(NS_LITERAL_STRING(SB_PROPERTY_HASH));
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

already_AddRefed<sbLocalDatabaseLibrary>
sbLocalDatabaseMediaListBase::GetNativeLibrary()
{
  NS_ASSERTION(mLibrary, "mLibrary is null!");
  sbLocalDatabaseLibrary* result = mLibrary;
  NS_ADDREF(result);
  return result;
}

void sbLocalDatabaseMediaListBase::SetArray(sbILocalDatabaseGUIDArray * aArray)
{
  mFullArray = aArray;
}

/**
 * \brief Adds multiple filters to a GUID array.
 *
 * This method enumerates a hash table and calls AddFilter on a GUIDArray once
 * for each key. It constructs a string enumerator for the string array that
 * the hash table contains.
 *
 * This method expects to be handed an sbILocalDatabaseGUIDArray pointer as its
 * aUserData parameter.
 */
/* static */ PLDHashOperator PR_CALLBACK
sbLocalDatabaseMediaListBase::AddFilterToGUIDArrayCallback(nsStringHashKey::KeyType aKey,
                                                           sbStringArray* aEntry,
                                                           void* aUserData)
{
  NS_ASSERTION(aEntry, "Null entry in the hash?!");
  NS_ASSERTION(aUserData, "Null userData!");

  // Make a string enumerator for the string array.
  nsCOMPtr<nsIStringEnumerator> valueEnum =
    new sbTArrayStringEnumerator(aEntry);

  // If we failed then we're probably out of memory. Hope we do better on the
  // next key?
  NS_ENSURE_TRUE(valueEnum, PL_DHASH_NEXT);

  // Unbox the guidArray.
  nsCOMPtr<sbILocalDatabaseGUIDArray> guidArray =
    static_cast<sbILocalDatabaseGUIDArray*>(aUserData);

  // Set the filter.
  nsresult SB_UNUSED_IN_RELEASE(rv) =
   guidArray->AddFilter(aKey, valueEnum, PR_FALSE);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "AddFilter failed!");

  return PL_DHASH_NEXT;
}

/**
 * Internal method that may be inside the monitor.
 */
nsresult
sbLocalDatabaseMediaListBase::EnumerateAllItemsInternal(sbIMediaListEnumerationListener* aEnumerationListener)
{
  sbGUIDArrayEnumerator enumerator(mLibrary, mFullArray);
  return EnumerateItemsInternal(&enumerator, aEnumerationListener);
}

/**
 * Internal method that may be inside the monitor.
 */
nsresult
sbLocalDatabaseMediaListBase::EnumerateItemsByPropertyInternal(const nsAString& aID,
                                                               nsIStringEnumerator* aValueEnum,
                                                               sbIMediaListEnumerationListener* aEnumerationListener)
{
  // Make a new GUID array to talk to the database.
  nsCOMPtr<sbILocalDatabaseGUIDArray> guidArray;
  nsresult rv = mFullArray->Clone(getter_AddRefs(guidArray));
  NS_ENSURE_SUCCESS(rv, rv);

  // Clone copies the filters... which we don't want.
  rv = guidArray->ClearFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  // Set the filter.
  rv = guidArray->AddFilter(aID, aValueEnum, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  // And make an enumerator to return the filtered items.
  sbGUIDArrayEnumerator enumerator(mLibrary, guidArray);

  return EnumerateItemsInternal(&enumerator, aEnumerationListener);
}

/**
 * Internal method that may be inside the monitor.
 */
nsresult
sbLocalDatabaseMediaListBase::EnumerateItemsByPropertiesInternal(sbStringArrayHash* aPropertiesHash,
                                                                 sbIMediaListEnumerationListener* aEnumerationListener)
{
  nsCOMPtr<sbILocalDatabaseGUIDArray> guidArray;
  nsresult rv = mFullArray->Clone(getter_AddRefs(guidArray));
  NS_ENSURE_SUCCESS(rv, rv);

  // Clone copies the filters... which we don't want.
  rv = guidArray->ClearFilters();
  NS_ENSURE_SUCCESS(rv, rv);

  // Now that our hash table is set up we call AddFilter for each property
  // id and all its associated values.
  PRUint32 filterCount =
    aPropertiesHash->EnumerateRead(AddFilterToGUIDArrayCallback, guidArray);

  // Make sure we actually added some filters here. Otherwise something went
  // wrong and the results are not going to be what the caller expects.
  PRUint32 hashCount = aPropertiesHash->Count();
  NS_ENSURE_TRUE(filterCount == hashCount, NS_ERROR_UNEXPECTED);

  // Finally make an enumerator to return the filtered items.
  sbGUIDArrayEnumerator enumerator(mLibrary, guidArray);
  return EnumerateItemsInternal(&enumerator, aEnumerationListener);
}

nsresult
sbLocalDatabaseMediaListBase::MakeStandardQuery(sbIDatabaseQuery** _retval)
{
  nsresult rv;
  nsCOMPtr<sbIDatabaseQuery> query =
    do_CreateInstance(SONGBIRD_DATABASEQUERY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString databaseGuid;
  rv = mLibrary->GetDatabaseGuid(databaseGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = query->SetDatabaseGUID(databaseGuid);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> databaseLocation;
  rv = mLibrary->GetDatabaseLocation(getter_AddRefs(databaseLocation));
  NS_ENSURE_SUCCESS(rv, rv);

  if (databaseLocation) {
    rv = query->SetDatabaseLocation(databaseLocation);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = query->SetAsyncQuery(PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = query);
  return NS_OK;
}

nsresult
sbLocalDatabaseMediaListBase::GetFilteredPropertiesForNewItem(sbIPropertyArray* aProperties,
                                                              sbIPropertyArray** _retval)
{
  NS_ASSERTION(aProperties, "aProperties is null");
  NS_ASSERTION(_retval, "_retval is null");

  nsresult rv;
  nsCOMPtr<sbIMutablePropertyArray> mutableArray =
    do_CreateInstance(SB_MUTABLEPROPERTYARRAY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILibrary> library;
  rv = GetLibrary(getter_AddRefs(library));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasContentType = PR_FALSE;
  PRUint32 length;
  rv = aProperties->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < length; i++) {
    nsCOMPtr<sbIProperty> property;
    rv = aProperties->GetPropertyAt(i, getter_AddRefs(property));
    NS_ENSURE_SUCCESS(rv, rv);

    nsString id;
    rv = property->GetId(id);
    NS_ENSURE_SUCCESS(rv, rv);

    // We never want these properties to be copied to a new item.
    if (mFilteredProperties.GetEntry(id)) {
      continue;
    }

    nsString value;
    rv = property->GetValue(value);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mutableArray->AppendProperty(id, value);
    NS_ENSURE_SUCCESS(rv, rv);

    if (id.EqualsLiteral(SB_PROPERTY_CONTENTTYPE)) {
      hasContentType = PR_TRUE;
    }
  }

  if (!hasContentType) {
    // no content type given; assume audio
    rv = mutableArray->AppendProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTTYPE),
                                      NS_LITERAL_STRING("audio"));
  }

  NS_ADDREF(*_retval = mutableArray);
  return NS_OK;
}

/* static */ nsresult
sbLocalDatabaseMediaListBase::GetOriginProperties(
                              sbIMediaItem *             aSourceItem,
                              sbIMutablePropertyArray *  aProperties)
{
  NS_ENSURE_ARG_POINTER(aSourceItem);
  NS_ENSURE_ARG_POINTER(aProperties);

  nsresult rv;

  // Determine whether the target list belongs to a device:
  nsCOMPtr<sbIDeviceManager2> deviceMgr =
    do_GetService("@songbirdnest.com/Songbird/DeviceManager;2", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIDevice> targetDev;
  rv = deviceMgr->GetDeviceForItem(static_cast<sbIMediaList *>(this),
                                   getter_AddRefs(targetDev));
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "GetDeviceForItem() failed");

  // We only set SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY where it is maintained
  // and currently there are only main library listeners in place to update the
  // property in device libraries, so set SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY
  // only if the the target list belongs to a device.
  PRBool targetIsDevice = (NS_SUCCEEDED(rv) && (targetDev != NULL));

  // Get the origin library:
  nsCOMPtr<sbILibrary> originLib;
  rv = aSourceItem->GetLibrary(getter_AddRefs(originLib));
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the main library:
  nsCOMPtr<sbILibrary> mainLib;
  rv = GetMainLibrary(getter_AddRefs(mainLib));
  NS_ENSURE_SUCCESS(rv, rv);

  // Ensure there is no conflicting value for
  // SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY:
  rv = RemoveProperty(
          aProperties,
          NS_LITERAL_STRING(SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY));
  NS_ENSURE_SUCCESS(rv, rv);

  // Set SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY and remove any existing
  // origin guids if the origin library is the main library.  This will
  // force the code below to regenerate the origin guids whenever an
  // item is copied from the main library:
  if (originLib == mainLib) {
    // Remove SB_PROPERTY_ORIGINLIBRARYGUID:
    rv = RemoveProperty(aProperties,
                        NS_LITERAL_STRING(SB_PROPERTY_ORIGINLIBRARYGUID));
    NS_ENSURE_SUCCESS(rv, rv);

    // Remove SB_PROPERTY_ORIGINITEMGUID:
    rv = RemoveProperty(aProperties,
                        NS_LITERAL_STRING(SB_PROPERTY_ORIGINITEMGUID));
    NS_ENSURE_SUCCESS(rv, rv);

    // Set SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY if the target is a
    // device library:
    if (targetIsDevice) {
      rv = aProperties->AppendProperty(
        NS_LITERAL_STRING(SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY),
        NS_LITERAL_STRING("1"));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  else if (targetIsDevice) {
    // The source library is not the main library, but our target is a device.
    // We need to verify SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY by checking if
    // an item with a guid matching this item's originItemGuid is in the main
    // library.
    rv = sbLibraryUtils::FindOriginalsByID(aSourceItem,
                                           mainLib,
                                           nsnull);
    if (NS_SUCCEEDED(rv)) {
      // We found the original item in the main library, set the
      // SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY flag to true
      rv = aProperties->AppendProperty(
        NS_LITERAL_STRING(SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY),
        NS_LITERAL_STRING("1"));
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      // We could not find the original item in the main library, set the
      // SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY flag to false
      rv = aProperties->AppendProperty(
        NS_LITERAL_STRING(SB_PROPERTY_ORIGIN_IS_IN_MAIN_LIBRARY),
        NS_LITERAL_STRING("0"));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  nsCOMPtr<sbILibrary> thisLibrary;
  rv = GetLibrary(getter_AddRefs(thisLibrary));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool copyingToMainLibrary;
  rv = thisLibrary->Equals(mainLib, &copyingToMainLibrary);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we're not copying to the main library set the origin guids
  if (!copyingToMainLibrary) {
    // Set SB_PROPERTY_ORIGINLIBRARYGUID if it is not already set:
    nsAutoString originLibGuid;
    rv = aProperties->GetPropertyValue(
      NS_LITERAL_STRING(SB_PROPERTY_ORIGINLIBRARYGUID),
      originLibGuid);
    if (rv == NS_ERROR_NOT_AVAILABLE || originLibGuid.IsEmpty()) {
      rv = originLib->GetGuid(originLibGuid);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = aProperties->AppendProperty(
        NS_LITERAL_STRING(SB_PROPERTY_ORIGINLIBRARYGUID),
        originLibGuid);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Set SB_PROPERTY_ORIGINITEMGUID if it is not already set:
    nsAutoString originItemGuid;
    rv = aProperties->GetPropertyValue(
      NS_LITERAL_STRING(SB_PROPERTY_ORIGINITEMGUID),
      originItemGuid);
    if (rv == NS_ERROR_NOT_AVAILABLE || originItemGuid.IsEmpty()) {
      rv = aSourceItem->GetGuid(originItemGuid);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = aProperties->AppendProperty(
        NS_LITERAL_STRING(SB_PROPERTY_ORIGINITEMGUID),
        originItemGuid);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  return NS_OK;
}

/* static */ nsresult
sbLocalDatabaseMediaListBase::RemoveProperty(
                              sbIMutablePropertyArray * aPropertyArray,
                              const nsAString &         aProperty)
{
  NS_ENSURE_ARG_POINTER(aPropertyArray);

  nsresult rv;

  // Get the nsIMutableArray for access to RemoveElementAt():
  nsCOMPtr<nsIMutableArray> mutableArray =
    do_QueryInterface(aPropertyArray, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 length;
  rv = aPropertyArray->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  // Iterate over the array and remove any elements with the
  // given property id:
  PRUint32 i = 0;
  while (i < length) {
    nsCOMPtr<sbIProperty> prop;
    rv = aPropertyArray->GetPropertyAt(i, getter_AddRefs(prop));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString id;
    rv = prop->GetId(id);
    NS_ENSURE_SUCCESS(rv, rv);

    // If the property ID matches the given ID, remove it.  The index
    // does not advance in this case, but the length decreases.
    // Otherwise, advance to the next element:
    if (id == aProperty) {
      rv = mutableArray->RemoveElementAt(i);
      NS_ENSURE_SUCCESS(rv, rv);
      --length;
    }
    else {
      i++;
    }
  }

  return NS_OK;
}

/**
 * \brief Enumerates the items to the given listener.
 */
nsresult
sbLocalDatabaseMediaListBase::EnumerateItemsInternal(sbGUIDArrayEnumerator* aEnumerator,
                                                     sbIMediaListEnumerationListener* aListener)
{
  // Loop until we explicitly return.
  while (PR_TRUE) {

    PRBool hasMore;
    nsresult rv = aEnumerator->HasMoreElements(&hasMore);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!hasMore) {
      return NS_OK;
    }

    nsCOMPtr<nsISupports> supports;
    rv = aEnumerator->GetNext(getter_AddRefs(supports));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint16 stepResult;
    rv = aListener->OnEnumeratedItem(this, item, &stepResult);
    NS_ENSURE_SUCCESS(rv, rv);

    // Stop enumerating if the listener requested it.
    if (stepResult == sbIMediaListEnumerationListener::CANCEL) {
      return NS_ERROR_ABORT;
    }
  }

  NS_NOTREACHED("Uh, how'd we get here?");
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetName(nsAString& aName)
{
  nsAutoString unlocalizedName;
  nsresult rv = GetProperty(NS_LITERAL_STRING(SB_PROPERTY_MEDIALISTNAME),
                            unlocalizedName);
  NS_ENSURE_SUCCESS(rv, rv);

  // If the property doesn't exist just return an empty string.
  if (unlocalizedName.IsEmpty()) {
    aName.Assign(unlocalizedName);
    return NS_OK;
  }

  // See if this string should be localized. The basic format we're looking for
  // is:
  //
  //   &[chrome://songbird/songbird.properties#]library.name
  //
  // The url (followed by '#') is optional.

  const PRUnichar *start, *end;
  PRUint32 length = unlocalizedName.BeginReading(&start, &end);

  static const PRUnichar sAmp = '&';

  // Bail out if this can't be a localizable string.
  if (length <= 1 || *start != sAmp) {
    aName.Assign(unlocalizedName);
    return NS_OK;
  }

  // Skip the ampersand
  start++;
  nsDependentSubstring stringKey(start, end - start), propertiesURL;

  static const PRUnichar sHash = '#';

  for (const PRUnichar* current = start; current < end; current++) {
    if (*current == sHash) {
      stringKey.Rebind(current + 1, end - current - 1);
      propertiesURL.Rebind(start, current - start);

      break;
    }
  }

  nsCOMPtr<nsIStringBundleService> bundleService =
    do_GetService("@mozilla.org/intl/stringbundle;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIStringBundle> bundle;

  if (!propertiesURL.IsEmpty()) {
    nsCOMPtr<nsIURI> propertiesURI;
    rv = NS_NewURI(getter_AddRefs(propertiesURI), propertiesURL);

    if (NS_SUCCEEDED(rv)) {
      PRBool schemeIsChrome;
      rv = propertiesURI->SchemeIs("chrome", &schemeIsChrome);

      if (NS_SUCCEEDED(rv) && schemeIsChrome) {
        nsCAutoString propertiesSpec;
        rv = propertiesURI->GetSpec(propertiesSpec);

        if (NS_SUCCEEDED(rv)) {
          rv = bundleService->CreateBundle(propertiesSpec.get(),
                                           getter_AddRefs(bundle));
        }
      }
    }
  }

  if (!bundle) {
    rv = bundleService->CreateBundle(DEFAULT_PROPERTIES_URL,
                                     getter_AddRefs(bundle));
  }

  if (NS_SUCCEEDED(rv)) {
    nsAutoString localizedName;
    rv = bundle->GetStringFromName(stringKey.BeginReading(),
                                   getter_Copies(localizedName));
    if (NS_SUCCEEDED(rv)) {
      aName.Assign(localizedName);
      return NS_OK;
    }
  }

  aName.Assign(unlocalizedName);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::SetName(const nsAString& aName)
{
  nsresult rv = SetProperty(NS_LITERAL_STRING(SB_PROPERTY_MEDIALISTNAME), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetType(nsAString& aType)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetLength(PRUint32* aLength)
{
  NS_ENSURE_ARG_POINTER(aLength);

  NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
  nsAutoMonitor mon(mFullArrayMonitor);

  nsresult rv = mFullArray->GetLength(aLength);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetItemByGuid(const nsAString& aGuid,
                                            sbIMediaItem** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<sbIMediaItem> item;
  nsresult rv = mLibrary->GetMediaItem(aGuid, getter_AddRefs(item));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = item);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetItemByIndex(PRUint32 aIndex,
                                             sbIMediaItem** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;

  nsAutoString guid;
  {
    NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
    nsAutoMonitor mon(mFullArrayMonitor);

    rv = mFullArray->GetGuidByIndex(aIndex, guid);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<sbIMediaItem> item;
  rv = mLibrary->GetMediaItem(guid, getter_AddRefs(item));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = item);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetListContentType(PRUint16* aContentType)
{
  NS_ENSURE_ARG_POINTER(aContentType);

  // Cached list content type is available. No need to go further.
  if (mListContentType > sbIMediaList::CONTENTTYPE_NONE) {
    *aContentType = mListContentType;
    return NS_OK;
  }

  // Set the default value.
  *aContentType = sbIMediaList::CONTENTTYPE_NONE;

  // Do some quick check on some types of lists that not belongs to
  nsAutoString customType;
  nsresult rv = GetProperty(NS_LITERAL_STRING(SB_PROPERTY_CUSTOMTYPE),
                            customType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (customType.Equals(NS_LITERAL_STRING("download")))
    return NS_OK;

  // "video-togo" list is always video
  if (customType.Equals(NS_LITERAL_STRING("video-togo"))) {
    *aContentType = sbIMediaList::CONTENTTYPE_VIDEO;
    return NS_OK;
  }

  PRUint32 length;
  rv = mFullArray->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  // Empty list == audio list.
  if (!length) {
    *aContentType = sbIMediaList::CONTENTTYPE_AUDIO;
    mListContentType = sbIMediaList::CONTENTTYPE_AUDIO;
    return NS_OK;
  }

  PRUint32 audioLength = 0, videoLength = 0;
  rv = GetItemCountByProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTTYPE),
                              NS_LITERAL_STRING("audio"),
                              &audioLength);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = GetItemCountByProperty(NS_LITERAL_STRING(SB_PROPERTY_CONTENTTYPE),
                              NS_LITERAL_STRING("video"),
                              &videoLength);
  NS_ENSURE_SUCCESS(rv, rv);

  // audio | video == mix
  if (audioLength > 0) {
    *aContentType |= sbIMediaList::CONTENTTYPE_AUDIO;
  }

  if (videoLength > 0) {
    *aContentType |= sbIMediaList::CONTENTTYPE_VIDEO;
  }

  mListContentType = *aContentType;

  return NS_OK;
}

/**
 * See sbIMediaList
 */
NS_IMETHODIMP
sbLocalDatabaseMediaListBase::EnumerateAllItems(sbIMediaListEnumerationListener* aEnumerationListener,
                                                PRUint16 aEnumerationType)
{
  NS_ENSURE_ARG_POINTER(aEnumerationListener);

  nsresult rv;

  switch (aEnumerationType) {

    case sbIMediaList::ENUMERATIONTYPE_LOCKING: {
      NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
      nsAutoMonitor mon(mFullArrayMonitor);

      // Don't reenter!
      NS_ENSURE_FALSE(mLockedEnumerationActive, NS_ERROR_FAILURE);
      mLockedEnumerationActive = PR_TRUE;

      PRUint16 stepResult;
      rv = aEnumerationListener->OnEnumerationBegin(this, &stepResult);

      if (NS_SUCCEEDED(rv)) {
        if (stepResult == sbIMediaListEnumerationListener::CONTINUE) {
          rv = EnumerateAllItemsInternal(aEnumerationListener);
        }
        else {
          // The user cancelled the enumeration.
          rv = NS_ERROR_ABORT;
        }
      }

      mLockedEnumerationActive = PR_FALSE;

    } break; // ENUMERATIONTYPE_LOCKING

    case sbIMediaList::ENUMERATIONTYPE_SNAPSHOT: {
      PRUint16 stepResult;
      rv = aEnumerationListener->OnEnumerationBegin(this, &stepResult);

      if (NS_SUCCEEDED(rv)) {
        if (stepResult == sbIMediaListEnumerationListener::CONTINUE) {
          rv = EnumerateAllItemsInternal(aEnumerationListener);
        }
        else {
          // The user cancelled the enumeration.
          rv = NS_ERROR_ABORT;
        }
      }
    } break; // ENUMERATIONTYPE_SNAPSHOT

    default: {
      NS_NOTREACHED("Invalid enumeration type");
      rv = NS_ERROR_INVALID_ARG;
    } break;
  }

  aEnumerationListener->OnEnumerationEnd(this, rv);
  return NS_OK;
}

/**
 * See sbIMediaList
 */
NS_IMETHODIMP
sbLocalDatabaseMediaListBase::EnumerateItemsByProperty(const nsAString& aID,
                                                       const nsAString& aValue,
                                                       sbIMediaListEnumerationListener* aEnumerationListener,
                                                       PRUint16 aEnumerationType)
{
  NS_ENSURE_ARG_POINTER(aEnumerationListener);

  nsresult rv = NS_ERROR_UNEXPECTED;

  // A property id must be specified.
  NS_ENSURE_TRUE(!aID.IsEmpty(), NS_ERROR_INVALID_ARG);

  // Get the sortable format of the value
  nsCOMPtr<sbIPropertyManager> propMan =
    do_GetService(SB_PROPERTYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIPropertyInfo> info;
  rv = propMan->GetPropertyInfo(aID, getter_AddRefs(info));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString sortableValue;
  rv = info->MakeSortable(aValue, sortableValue);
  NS_ENSURE_SUCCESS(rv, rv);

  // Make a single-item string array to hold our property value.
  sbStringArray valueArray(1);
  nsString* value = valueArray.AppendElement(sortableValue);
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  // Make a string enumerator for it.
  nsCOMPtr<nsIStringEnumerator> valueEnum =
    new sbTArrayStringEnumerator(&valueArray);
  NS_ENSURE_TRUE(valueEnum, NS_ERROR_OUT_OF_MEMORY);

  switch (aEnumerationType) {

    case sbIMediaList::ENUMERATIONTYPE_LOCKING: {
      NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
      nsAutoMonitor mon(mFullArrayMonitor);

      // Don't reenter!
      NS_ENSURE_FALSE(mLockedEnumerationActive, NS_ERROR_FAILURE);
      mLockedEnumerationActive = PR_TRUE;

      PRUint16 stepResult;
      rv = aEnumerationListener->OnEnumerationBegin(this, &stepResult);

      if (NS_SUCCEEDED(rv)) {
        if (stepResult == sbIMediaListEnumerationListener::CONTINUE) {
          rv = EnumerateItemsByPropertyInternal(aID, valueEnum,
                                                aEnumerationListener);
        }
        else {
          // The user cancelled the enumeration.
          rv = NS_ERROR_ABORT;
        }
      }

      mLockedEnumerationActive = PR_FALSE;

    } break; // ENUMERATIONTYPE_LOCKING

    case sbIMediaList::ENUMERATIONTYPE_SNAPSHOT: {
      PRUint16 stepResult;
      rv = aEnumerationListener->OnEnumerationBegin(this, &stepResult);

      if (NS_SUCCEEDED(rv)) {
        if (stepResult == sbIMediaListEnumerationListener::CONTINUE) {
          rv = EnumerateItemsByPropertyInternal(aID, valueEnum,
                                                aEnumerationListener);
        }
        else {
          // The user cancelled the enumeration.
          rv = NS_ERROR_ABORT;
        }
      }
    } break; // ENUMERATIONTYPE_SNAPSHOT

    default: {
      NS_NOTREACHED("Invalid enumeration type");
      rv = NS_ERROR_INVALID_ARG;
    } break;
  }

  aEnumerationListener->OnEnumerationEnd(this, rv);
  return NS_OK;
}

/**
 * See sbIMediaList
 */
NS_IMETHODIMP
sbLocalDatabaseMediaListBase::EnumerateItemsByProperties(sbIPropertyArray* aProperties,
                                                         sbIMediaListEnumerationListener* aEnumerationListener,
                                                         PRUint16 aEnumerationType)
{
  NS_ENSURE_ARG_POINTER(aProperties);
  NS_ENSURE_ARG_POINTER(aEnumerationListener);

  PRUint32 propertyCount;
  nsresult rv = aProperties->GetLength(&propertyCount);
  NS_ENSURE_SUCCESS(rv, rv);

  // It doesn't make sense to call this method without specifying any properties
  // so it is probably a caller error if we have none.
  NS_ENSURE_STATE(propertyCount);

  // The guidArray needs AddFilter called only once per property with an
  // enumerator that contains all the values. We were given an array of
  // id/value pairs, so this is a little tricky. We make a hash table that
  // uses the property id for a key and an array of values as its data. Then
  // we load the arrays in a loop and finally call AddFilter as an enumeration
  // function.

  sbStringArrayHash propertyHash;

  // Init with the propertyCount as the number of buckets to create. This will
  // probably be too many, but it's likely less than the default of 16.
  propertyHash.Init(propertyCount);

  nsCOMPtr<sbIPropertyManager> propMan =
    do_GetService(SB_PROPERTYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Load the hash table with properties from the array.
  for (PRUint32 index = 0; index < propertyCount; index++) {

    // Get the property.
    nsCOMPtr<sbIProperty> property;
    rv = aProperties->GetPropertyAt(index, getter_AddRefs(property));
    SB_CONTINUE_IF_FAILED(rv);

    // Get the id of the property. This will be the key for the hash table.
    nsString propertyID;
    rv = property->GetId(propertyID);
    SB_CONTINUE_IF_FAILED(rv);

    // Get the string array associated with the key. If it doesn't yet exist
    // then we need to create it.
    sbStringArray* stringArray;
    PRBool arrayExists = propertyHash.Get(propertyID, &stringArray);
    if (!arrayExists) {
      NS_NEWXPCOM(stringArray, sbStringArray);
      SB_CONTINUE_IF_FALSE(stringArray);

      // Try to add the array to the hash table.
      PRBool success = propertyHash.Put(propertyID, stringArray);
      if (!success) {
        NS_WARNING("Failed to add string array to property hash!");

        // Make sure to delete the new array, otherwise it will leak.
        NS_DELETEXPCOM(stringArray);
        continue;
      }
    }
    NS_ASSERTION(stringArray, "Must have a valid pointer here!");

    // Now we need a slot for the property value.
    nsString* valueString = stringArray->AppendElement();
    SB_CONTINUE_IF_FALSE(valueString);

    // Make the value sortable and assign it
    nsCOMPtr<sbIPropertyInfo> info;
    rv = propMan->GetPropertyInfo(propertyID, getter_AddRefs(info));
    SB_CONTINUE_IF_FAILED(rv);

    nsAutoString value;
    rv = property->GetValue(value);
    SB_CONTINUE_IF_FAILED(rv);

    nsAutoString sortableValue;
    rv = info->MakeSortable(value, *valueString);
    SB_CONTINUE_IF_FAILED(rv);
  }

  switch (aEnumerationType) {

    case sbIMediaList::ENUMERATIONTYPE_LOCKING: {
      NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
      nsAutoMonitor mon(mFullArrayMonitor);

      // Don't reenter!
      NS_ENSURE_FALSE(mLockedEnumerationActive, NS_ERROR_FAILURE);
      mLockedEnumerationActive = PR_TRUE;

      PRUint16 stepResult;
      rv = aEnumerationListener->OnEnumerationBegin(this, &stepResult);

      if (NS_SUCCEEDED(rv)) {
        if (stepResult == sbIMediaListEnumerationListener::CONTINUE) {
          rv = EnumerateItemsByPropertiesInternal(&propertyHash,
                                                  aEnumerationListener);
        }
        else {
          // The user cancelled the enumeration.
          rv = NS_ERROR_ABORT;
        }
      }

      mLockedEnumerationActive = PR_FALSE;

    } break; // ENUMERATIONTYPE_LOCKING

    case sbIMediaList::ENUMERATIONTYPE_SNAPSHOT: {
      PRUint16 stepResult;
      rv = aEnumerationListener->OnEnumerationBegin(this, &stepResult);

      if (NS_SUCCEEDED(rv)) {
        if (stepResult == sbIMediaListEnumerationListener::CONTINUE) {
          rv = EnumerateItemsByPropertiesInternal(&propertyHash,
                                                  aEnumerationListener);
        }
        else {
          // The user cancelled the enumeration.
          rv = NS_ERROR_ABORT;
        }
      }
    } break; // ENUMERATIONTYPE_SNAPSHOT

    default: {
      NS_NOTREACHED("Invalid enumeration type");
      rv = NS_ERROR_INVALID_ARG;
    } break;
  }

  aEnumerationListener->OnEnumerationEnd(this, rv);
  return NS_OK;
}


NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetItemsByProperty(const nsAString & aPropertyID,
                                                 const nsAString & aPropertyValue,
                                                 nsIArray **_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsRefPtr<sbLocalMediaListBaseEnumerationListener> enumerator;
  NS_NEWXPCOM(enumerator, sbLocalMediaListBaseEnumerationListener);
  NS_ENSURE_TRUE(enumerator, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = enumerator->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnumerateItemsByProperty(aPropertyID,
                                aPropertyValue,
                                enumerator,
                                sbIMediaList::ENUMERATIONTYPE_LOCKING);
  NS_ENSURE_SUCCESS(rv, rv);

  return enumerator->GetArray(_retval);
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetItemCountByProperty(const nsAString & aPropertyID,
                                                     const nsAString & aPropertyValue,
                                                     PRUint32 *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsRefPtr<sbLocalMediaListBaseEnumerationListener> enumerator;
  NS_NEWXPCOM(enumerator, sbLocalMediaListBaseEnumerationListener);
  NS_ENSURE_TRUE(enumerator, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = enumerator->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnumerateItemsByProperty(aPropertyID,
                                aPropertyValue,
                                enumerator,
                                sbIMediaList::ENUMERATIONTYPE_LOCKING);
  NS_ENSURE_SUCCESS(rv, rv);

  return enumerator->GetArrayLength(_retval);
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetItemsByProperties(sbIPropertyArray *aProperties,
                                                   nsIArray **_retval)
{
  NS_ENSURE_ARG_POINTER(aProperties);
  NS_ENSURE_ARG_POINTER(_retval);

  nsRefPtr<sbLocalMediaListBaseEnumerationListener> enumerator;
  NS_NEWXPCOM(enumerator, sbLocalMediaListBaseEnumerationListener);
  NS_ENSURE_TRUE(enumerator, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = enumerator->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnumerateItemsByProperties(aProperties,
                                  enumerator,
                                  sbIMediaList::ENUMERATIONTYPE_LOCKING);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = enumerator->GetArray(_retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::IndexOf(sbIMediaItem* aMediaItem,
                                      PRUint32 aStartFrom,
                                      PRUint32* _retval)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(_retval);

  PRUint32 count;

  NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
  nsAutoMonitor mon(mFullArrayMonitor);

  nsresult rv = mFullArray->GetLength(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  // Do some sanity checking.
  NS_ENSURE_TRUE(count > 0, NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_ARG_MAX(aStartFrom, count - 1);

  nsAutoString testGUID;
  rv = aMediaItem->GetGuid(testGUID);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 index = aStartFrom; index < count; index++) {
    nsAutoString itemGUID;
    rv = mFullArray->GetGuidByIndex(index, itemGUID);
    SB_CONTINUE_IF_FAILED(rv);

    if (testGUID.Equals(itemGUID)) {
      *_retval = index;
      return NS_OK;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::LastIndexOf(sbIMediaItem* aMediaItem,
                                          PRUint32 aStartFrom,
                                          PRUint32* _retval)
{
  NS_ENSURE_ARG_POINTER(aMediaItem);
  NS_ENSURE_ARG_POINTER(_retval);

  NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
  nsAutoMonitor mon(mFullArrayMonitor);

  PRUint32 count;
  nsresult rv = mFullArray->GetLength(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  // Do some sanity checking.
  NS_ENSURE_TRUE(count > 0, NS_ERROR_UNEXPECTED);
  NS_ENSURE_ARG_MAX(aStartFrom, count - 1);

  nsAutoString testGUID;
  rv = aMediaItem->GetGuid(testGUID);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 index = count - 1; index >= aStartFrom; index--) {
    nsAutoString itemGUID;
    rv = mFullArray->GetGuidByIndex(index, itemGUID);
    SB_CONTINUE_IF_FAILED(rv);

    if (testGUID.Equals(itemGUID)) {
      *_retval = index;
      return NS_OK;
    }
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::Contains(sbIMediaItem* aMediaItem,
                                       PRBool* _retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetIsEmpty(PRBool* aIsEmpty)
{
  NS_ENSURE_ARG_POINTER(aIsEmpty);

  NS_ENSURE_TRUE(mFullArrayMonitor, NS_ERROR_FAILURE);
  nsAutoMonitor mon(mFullArrayMonitor);

  PRUint32 length;
  nsresult rv = mFullArray->GetLength(&length);
  NS_ENSURE_SUCCESS(rv, rv);

  *aIsEmpty = length == 0;
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetUserEditableContent(PRBool* aUserEditableContent)
{
  NS_ENSURE_ARG_POINTER(aUserEditableContent);

  // Item is readonly if it contains a "1" in the corresponding property.

  nsAutoString str;
  nsresult rv = GetProperty(NS_LITERAL_STRING(SB_PROPERTY_ISCONTENTREADONLY), str);
  NS_ENSURE_SUCCESS(rv, rv);

  *aUserEditableContent = !str.EqualsLiteral("1");

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::Add(sbIMediaItem* aMediaItem)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::AddItem(sbIMediaItem* aMediaItem,
                                      sbIMediaItem ** aNewMediaItem)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::AddAll(sbIMediaList* aMediaList)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::AddSome(nsISimpleEnumerator* aMediaItems)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::AddMediaItems(nsISimpleEnumerator* aMediaItems,
                                            sbIAddMediaItemsListener * aListener,
                                            PRBool aAsync)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::Remove(sbIMediaItem* aMediaItem)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::RemoveByIndex(PRUint32 aIndex)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::RemoveSome(nsISimpleEnumerator* aMediaItems)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::Clear()
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::AddListener(sbIMediaListListener* aListener,
                                          PRBool aOwnsWeak,
                                          PRUint32 aFlags,
                                          sbIPropertyArray* aPropertyFilter)
{
  return sbLocalDatabaseMediaListListener::AddListener(this,
                                                       aListener,
                                                       aOwnsWeak,
                                                       aFlags,
                                                       aPropertyFilter);
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::RemoveListener(sbIMediaListListener* aListener)
{
  return sbLocalDatabaseMediaListListener::RemoveListener(this, aListener);
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::CreateView(sbIMediaListViewState* aState,
                                         sbIMediaListView** _retval)
{
  NS_NOTREACHED("Not meant to be implemented in this base class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::GetDistinctValuesForProperty(const nsAString& aPropertyID,
                                                           nsIStringEnumerator** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<sbILocalDatabaseGUIDArray> guidArray;
  nsresult rv = mFullArray->Clone(getter_AddRefs(guidArray));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = guidArray->SetIsDistinct(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = guidArray->ClearSorts();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = guidArray->AddSort(aPropertyID, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  sbGUIDArrayValueEnumerator* enumerator =
    new sbGUIDArrayValueEnumerator(guidArray);
  NS_ENSURE_TRUE(enumerator, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*_retval = enumerator);
  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseMediaListBase::RunInBatchMode(sbIMediaListBatchCallback* aCallback,
                                             nsISupports* aUserData)
{
  NS_ENSURE_ARG_POINTER(aCallback);

  sbAutoBatchHelper helper(*this);

  return aCallback->RunBatched(aUserData);
}

NS_IMPL_ISUPPORTS1(sbGUIDArrayValueEnumerator, nsIStringEnumerator)

sbGUIDArrayValueEnumerator::sbGUIDArrayValueEnumerator(sbILocalDatabaseGUIDArray* aArray) :
  mArray(aArray),
  mLength(0),
  mNextIndex(0)
{
  NS_ASSERTION(aArray, "Null value passed to ctor");

  nsresult SB_UNUSED_IN_RELEASE(rv) = mArray->GetLength(&mLength);
  NS_ASSERTION(NS_SUCCEEDED(rv), "Could not get length");
}

sbGUIDArrayValueEnumerator::~sbGUIDArrayValueEnumerator()
{
}

NS_IMETHODIMP
sbGUIDArrayValueEnumerator::HasMore(PRBool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  *_retval = mNextIndex < mLength;
  return NS_OK;
}

NS_IMETHODIMP
sbGUIDArrayValueEnumerator::GetNext(nsAString& _retval)
{
  nsresult rv = mArray->GetSortPropertyValueByIndex(mNextIndex, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  mNextIndex++;

  return NS_OK;
}
