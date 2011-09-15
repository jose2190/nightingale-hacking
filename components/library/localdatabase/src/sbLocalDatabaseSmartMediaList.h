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

#ifndef __SBLOCALDATABASESMARTMEDIALIST_H__
#define __SBLOCALDATABASESMARTMEDIALIST_H__

#include <sbILocalDatabaseSmartMediaList.h>
#include <sbILocalDatabaseMediaItem.h>

#include <nsTArray.h>
#include <nsAutoPtr.h>
#include <nsCOMPtr.h>
#include <nsCOMArray.h>
#include <nsDataHashtable.h>
#include <nsStringGlue.h>
#include <nsAutoLock.h>

#include <nsIClassInfo.h>
#include <nsIObserver.h>
#include <nsIArray.h>
#include <sbIMediaListListener.h>
#include <sbIPropertyArray.h>

#include <sbWeakReference.h>

class sbIDatabaseQuery;
class sbILocalDatabaseLibrary;
class sbILocalDatabasePropertyCache;
class sbIMediaItem;
class sbIMediaList;
class sbIPropertyInfo;
class sbIPropertyManager;
class sbISQLBuilderCriterion;
class sbISQLSelectBuilder;

typedef nsDataHashtable<nsStringHashKey, nsString> sbStringMap;

/* Use this macro to declare functions that forward the behavior of this interface to another object in a safe way. */
#define NS_FORWARD_SAFE_SBIMEDIALIST_ALL_BUT_TYPEANDNAME(_to) \
  NS_IMETHOD GetLength(PRUint32 *aLength) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetLength(aLength); } \
  NS_IMETHOD GetItemByGuid(const nsAString & aGuid, sbIMediaItem **_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetItemByGuid(aGuid, _retval); } \
  NS_IMETHOD GetItemByIndex(PRUint32 aIndex, sbIMediaItem **_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetItemByIndex(aIndex, _retval); } \
  NS_IMETHOD EnumerateAllItems(sbIMediaListEnumerationListener *aEnumerationListener, PRUint16 aEnumerationType) { return !_to ? NS_ERROR_NULL_POINTER : _to->EnumerateAllItems(aEnumerationListener, aEnumerationType); } \
  NS_IMETHOD EnumerateItemsByProperty(const nsAString & aPropertyID, const nsAString & aPropertyValue, sbIMediaListEnumerationListener *aEnumerationListener, PRUint16 aEnumerationType) { return !_to ? NS_ERROR_NULL_POINTER : _to->EnumerateItemsByProperty(aPropertyID, aPropertyValue, aEnumerationListener, aEnumerationType); } \
  NS_IMETHOD EnumerateItemsByProperties(sbIPropertyArray *aProperties, sbIMediaListEnumerationListener *aEnumerationListener, PRUint16 aEnumerationType) { return !_to ? NS_ERROR_NULL_POINTER : _to->EnumerateItemsByProperties(aProperties, aEnumerationListener, aEnumerationType); } \
  NS_IMETHOD GetItemsByProperty(const nsAString & aPropertyID, const nsAString & aPropertyValue, nsIArray **_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetItemsByProperty(aPropertyID, aPropertyValue, _retval); } \
  NS_IMETHOD GetItemCountByProperty(const nsAString & aPropertyID, const nsAString & aPropertyValue, PRUint32 *_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetItemCountByProperty(aPropertyID, aPropertyValue, _retval); } \
  NS_IMETHOD GetItemsByProperties(sbIPropertyArray *aProperties, nsIArray **_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetItemsByProperties(aProperties, _retval); } \
  NS_IMETHOD IndexOf(sbIMediaItem *aMediaItem, PRUint32 aStartFrom, PRUint32 *_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->IndexOf(aMediaItem, aStartFrom, _retval); } \
  NS_IMETHOD LastIndexOf(sbIMediaItem *aMediaItem, PRUint32 aStartFrom, PRUint32 *_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->LastIndexOf(aMediaItem, aStartFrom, _retval); } \
  NS_IMETHOD Contains(sbIMediaItem *aMediaItem, PRBool *_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->Contains(aMediaItem, _retval); } \
  NS_IMETHOD GetIsEmpty(PRBool *aIsEmpty) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetIsEmpty(aIsEmpty); } \
  NS_IMETHOD GetUserEditableContent(PRBool *aUserEditableContent) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetUserEditableContent(aUserEditableContent); } \
  NS_IMETHOD Add(sbIMediaItem *aMediaItem) { return !_to ? NS_ERROR_NULL_POINTER : _to->Add(aMediaItem); } \
  NS_IMETHOD AddItem(sbIMediaItem *aMediaItem, sbIMediaItem ** aNewMediaItem) { return !_to ? NS_ERROR_NULL_POINTER : _to->AddItem(aMediaItem, aNewMediaItem); } \
  NS_IMETHOD AddAll(sbIMediaList *aMediaList) { return !_to ? NS_ERROR_NULL_POINTER : _to->AddAll(aMediaList); } \
  NS_IMETHOD AddSome(nsISimpleEnumerator *aMediaItems) { return !_to ? NS_ERROR_NULL_POINTER : _to->AddSome(aMediaItems); } \
  NS_IMETHOD AddMediaItems(nsISimpleEnumerator *aMediaItems, sbIAddMediaItemsListener *aListener, PRBool aAsync) { return !_to ? NS_ERROR_NULL_POINTER : _to->AddMediaItems(aMediaItems, aListener, aAsync); } \
  NS_IMETHOD Remove(sbIMediaItem *aMediaItem) { return !_to ? NS_ERROR_NULL_POINTER : _to->Remove(aMediaItem); } \
  NS_IMETHOD RemoveByIndex(PRUint32 aIndex) { return !_to ? NS_ERROR_NULL_POINTER : _to->RemoveByIndex(aIndex); } \
  NS_IMETHOD RemoveSome(nsISimpleEnumerator *aMediaItems) { return !_to ? NS_ERROR_NULL_POINTER : _to->RemoveSome(aMediaItems); } \
  NS_IMETHOD Clear(void) { return !_to ? NS_ERROR_NULL_POINTER : _to->Clear(); } \
  NS_IMETHOD AddListener(sbIMediaListListener *aListener, PRBool aOwnsWeak, PRUint32 aFlags, sbIPropertyArray *aPropertyFilter) { return !_to ? NS_ERROR_NULL_POINTER : _to->AddListener(aListener, aOwnsWeak, aFlags, aPropertyFilter); } \
  NS_IMETHOD RemoveListener(sbIMediaListListener *aListener) { return !_to ? NS_ERROR_NULL_POINTER : _to->RemoveListener(aListener); } \
  NS_IMETHOD CreateView(sbIMediaListViewState* aState, sbIMediaListView **_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->CreateView(aState, _retval); } \
  NS_IMETHOD GetDistinctValuesForProperty(const nsAString& aPropertyID, nsIStringEnumerator** _retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetDistinctValuesForProperty(aPropertyID, _retval); } \
  NS_IMETHOD RunInBatchMode(sbIMediaListBatchCallback *aCallback, nsISupports *aUserData) { return !_to ? NS_ERROR_NULL_POINTER : _to->RunInBatchMode(aCallback, aUserData); }

/* Use this macro to declare functions that forward the behavior of this interface to another object in a safe way. */
#define SB_FORWARD_SAFE_SBILOCALDATABASEMEDIAITEM(_to) \
  NS_SCRIPTABLE NS_IMETHOD GetMediaItemId(PRUint32 *aMediaItemId) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetMediaItemId(aMediaItemId); } \
  NS_SCRIPTABLE NS_IMETHOD GetPropertyBag(sbILocalDatabaseResourcePropertyBag * *aPropertyBag) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetPropertyBag(aPropertyBag); } \
  NS_SCRIPTABLE NS_IMETHOD SetPropertyBag(sbILocalDatabaseResourcePropertyBag * aPropertyBag) { return !_to ? NS_ERROR_NULL_POINTER : _to->SetPropertyBag(aPropertyBag); } \
  NS_IMETHOD_(void) SetSuppressNotifications(PRBool aSuppress) { if (_to) _to->SetSuppressNotifications(aSuppress); }

class sbLocalDatabaseSmartMediaListCondition : public sbILocalDatabaseSmartMediaListCondition
{
friend class sbLocalDatabaseSmartMediaList;
public:
  NS_DECL_ISUPPORTS
  NS_DECL_SBILOCALDATABASESMARTMEDIALISTCONDITION

  sbLocalDatabaseSmartMediaListCondition(const nsAString& aPropertyID,
                                         const nsAString& aOperatorString,
                                         const nsAString& aLeftValue,
                                         const nsAString& aRightValue,
                                         const nsAString& aDisplayUnit);

  virtual ~sbLocalDatabaseSmartMediaListCondition();

  nsresult ToString(nsAString& _retval);

protected:
  PRLock* mLock;

  nsString mPropertyID;
  nsString mOperatorString;

  nsString mLeftValue;
  nsString mRightValue;
  nsString mDisplayUnit;

private:
  nsCOMPtr<sbIPropertyOperator> mOperator;
};

class sbLocalDatabaseSmartMediaList : public sbILocalDatabaseMediaItem,
                                      public sbILocalDatabaseSmartMediaList,
                                      public sbIMediaListListener,
                                      public sbSupportsWeakReference,
                                      public nsIObserver,
                                      public nsIClassInfo
{
typedef nsRefPtr<sbLocalDatabaseSmartMediaListCondition> sbRefPtrCondition;
typedef nsTArray<PRUint32> sbMediaItemIdArray;
public:
  NS_DECL_ISUPPORTS
  NS_DECL_SBILOCALDATABASESMARTMEDIALIST
  NS_DECL_SBIMEDIALISTLISTENER
  NS_DECL_NSIOBSERVER
  NS_DECL_NSICLASSINFO

  NS_FORWARD_SAFE_SBIMEDIAITEM(mItem)
  NS_FORWARD_SAFE_SBILIBRARYRESOURCE(mItem)
  SB_FORWARD_SAFE_SBILOCALDATABASEMEDIAITEM(mLocalDBItem)

  /* Forward all media list functions to mList except for the
     type getter */
  NS_FORWARD_SAFE_SBIMEDIALIST_ALL_BUT_TYPEANDNAME(mList)
  NS_IMETHOD GetType(nsAString& aType);
  NS_IMETHOD GetName(nsAString& aName);
  NS_IMETHOD SetName(const nsAString& aName);
  
  /* Throws NS_ERROR_NOT_AVAILABLE if no condition is available */
  NS_IMETHOD GetListContentType(PRUint16* aContentType);

  nsresult Init(sbIMediaItem* aMediaItem);

  sbLocalDatabaseSmartMediaList();
  virtual ~sbLocalDatabaseSmartMediaList();

private:

  nsresult RebuildMatchTypeNoneNotRandom();

  nsresult RebuildMatchTypeNoneRandom();

  nsresult RebuildMatchTypeAnyAll();

  nsresult AddMediaItemsTempTable(const nsAutoString& tempTableName,
                                  sbMediaItemIdArray& aArray,
                                  PRUint32 aStart,
                                  PRUint32 aLength);

  nsresult GetRollingLimit(const nsAString& aSql,
                           PRUint32 aRollingLimitColumnIndex,
                           PRUint32* aRow);

  nsresult CreateSQLForCondition(sbRefPtrCondition& aCondition,
                                 PRBool aIsLastCondition,
                                 nsAString& _retval);

  nsresult AddCriterionForCondition(sbISQLSelectBuilder* aBuilder,
                                    sbRefPtrCondition& aCondition,
                                    sbIPropertyInfo* aInfo);

  nsresult AddSelectColumnAndJoin(sbISQLSelectBuilder* aBuilder,
                                  const nsAString& aBaseTableAlias,
                                  PRBool aAddOrderBy);

  nsresult AddLimitColumnAndJoin(sbISQLSelectBuilder* aBuilder,
                                 const nsAString& aBaseTableAlias);

  nsresult CreateQueries();

  nsresult GetCopyToListQuery(const nsAString& aTempTableName,
                              nsAString& aSql);


  nsresult CreateTempTable(nsAString& aName);

  nsresult DropTempTable(const nsAString& aName);

  nsresult ExecuteQuery(const nsAString& aSql);

  nsresult MakeTempTableName(nsAString& aName);

  nsresult GetMediaItemIdRange(PRUint32* aMin, PRUint32* aMax);

  nsresult GetRowCount(const nsAString& aTableName,
                       PRUint32* _retval);

  void ShuffleArray(sbMediaItemIdArray& aArray);

  nsresult GetConditionNeedsNull(sbRefPtrCondition& aCondition,
                                 sbIPropertyInfo* aInfo,
                                 PRBool &bNeedIsNull);

  nsresult MediaListGuidToDB(nsAString &val, PRUint32 &v);

  PRInt64 ParseDateTime(nsAString &aDateTime);

  PRInt64 StripTime(PRInt64 aDateTime);

  void SPrintfInt64(nsAString &aString, PRInt64 aValue);
  PRInt64 ScanfInt64d(nsAString &aString);
  nsresult ScanfInt64(nsAString &aString, PRInt64 *aRetVal);

  nsresult ReadConfiguration();

  nsresult WriteConfiguration();

  PRMonitor* mInnerMonitor;

  nsCOMPtr<sbIMediaItem> mItem;
  nsCOMPtr<sbILocalDatabaseMediaItem> mLocalDBItem;
  nsCOMPtr<sbIMediaList> mList;

  PRMonitor* mConditionsMonitor;
  nsTArray<sbRefPtrCondition> mConditions;

  PRUint32    mMatchType;
  PRUint32    mLimitType;
  PRUint64    mLimit;
  nsString    mSelectPropertyID;
  PRBool      mSelectDirection;
  PRBool      mRandomSelection;
  PRMonitor * mAutoUpdateMonitor;
  PRUint32    mAutoUpdate;
  PRUint32    mNotExistsMode;

  nsCOMPtr<sbIPropertyManager> mPropMan;
  nsCOMPtr<sbILocalDatabasePropertyCache> mPropertyCache;
  nsCOMPtr<sbILocalDatabaseLibrary> mLocalDatabaseLibrary;

  PRMonitor * mListenersMonitor;
  nsCOMArray<sbILocalDatabaseSmartMediaListListener> mListeners;

  nsString mClearListQuery;

  PRMonitor * mSourceMonitor;
  nsString mSourceLibraryGuid;
};

#endif /* __SBLOCALDATABASESMARTMEDIALIST_H__ */
