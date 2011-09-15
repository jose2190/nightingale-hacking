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

#ifndef __SB_LOCALDATABASE_MEDIALISTLISTENER_H__
#define __SB_LOCALDATABASE_MEDIALISTLISTENER_H__

#include <nsAutoPtr.h>
#include <nsCOMPtr.h>
#include <nsCOMArray.h>
#include <nsTHashtable.h>
#include <nsHashKeys.h>
#include <nsTArray.h>
#include <prlock.h>
#include <sbIMediaList.h>
#include <sbIMediaListListener.h>
#include <sbIPropertyArray.h>

class nsIProxyObjectManager;
class nsIWeakReference;
class nsIWeakReference;
class sbIMediaItem;
class sbIMediaList;
class sbLocalDatabaseMediaListBase;

class sbListenerInfo
{
friend class sbLocalDatabaseMediaListListener;
public:
  sbListenerInfo();
  ~sbListenerInfo();

  nsresult Init(nsIProxyObjectManager *aProxyObjMgr,
                sbIMediaListListener* aListener,
                PRUint32 aCurrentBatchDepth,
                PRUint32 aFlags,
                sbIPropertyArray* aPropertyFilter);
  nsresult Init(nsIProxyObjectManager *aProxyObjMgr,
                nsIWeakReference* aWeakListener,
                PRUint32 aCurrentBatchDepth,
                PRUint32 aFlags,
                sbIPropertyArray* aPropertyFilter);
  
  PRBool ShouldNotify(PRUint32 aFlag, sbIPropertyArray* aProperties = nsnull);

  void BeginBatch();
  void EndBatch();
  void SetShouldStopNotifying(PRUint32 aFlag);
  void GetDebugAddress(nsAString& mDebugAddress);

private:

  nsresult InitPropertyFilter(sbIPropertyArray* aPropertyFilter);

  PRBool mIsGone;
  nsCOMPtr<nsISupports> mRef;
  nsCOMPtr<nsIWeakReference> mWeak;
  nsCOMPtr<sbIMediaListListener> mProxy;
  PRUint32 mFlags;
  PRBool mHasPropertyFilter;
  nsTHashtable<nsStringHashKey> mPropertyFilter;
  nsTArray<PRUint32> mStopNotifiyingStack;
  nsString mDebugAddress;
};

class sbWeakMediaListListenerWrapper : public sbIMediaListListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_SBIMEDIALISTLISTENER

  sbWeakMediaListListenerWrapper(nsIWeakReference* aWeakListener);
  ~sbWeakMediaListListenerWrapper();

private:
  already_AddRefed<sbIMediaListListener> GetListener();

  nsCOMPtr<nsIWeakReference> mWrappedWeak;
};

class sbLocalDatabaseMediaListListener
{
  friend class sbAutoBatchHelper;

  struct ListenerAndDebugAddress {
    ListenerAndDebugAddress(sbIMediaListListener* aListener,
                            const nsAString& aDebugAddress) :
      listener(aListener),
      debugAddress(aDebugAddress)
    {}

    nsCOMPtr<sbIMediaListListener> listener;
    nsString debugAddress;
  };

  // This is used to mark things to stop notifying for the sweep
  struct StopNotifyFlags {
    StopNotifyFlags(sbIMediaListListener* aListener,
                    PRUint32 aListenerFlags,
                    PRBool aIsGone) :
      listener(aListener),
      listenerFlags(aListenerFlags),
      isGone(aIsGone)
    {}
    nsCOMPtr<sbIMediaListListener> listener;
    PRUint32 listenerFlags;
    PRBool isGone;
  };

  typedef nsTArray<ListenerAndDebugAddress> sbMediaListListenersArray;
  typedef nsTArray<StopNotifyFlags> sbStopNotifyArray;
  typedef nsAutoPtr<sbListenerInfo> sbListenerInfoAutoPtr;

public:
  sbLocalDatabaseMediaListListener();
  virtual ~sbLocalDatabaseMediaListListener();

protected:
  // Initialize the instance.
  nsresult Init();

  // Add a listener to the array
  nsresult AddListener(sbLocalDatabaseMediaListBase* aList,
                       sbIMediaListListener* aListener,
                       PRBool aOwnsWeak = PR_FALSE,
                       PRUint32 aFlags = sbIMediaList::LISTENER_FLAGS_ALL,
                       sbIPropertyArray* aPropertyFilter = nsnull);

  // Remove a listener from the array
  nsresult RemoveListener(sbLocalDatabaseMediaListBase* aList,
                          sbIMediaListListener* aListener);

  // Return the number of listeners
  PRUint32 ListenerCount();

  // Enumerate listeners and call OnItemAdded
  void NotifyListenersItemAdded(sbIMediaList* aList,
                                sbIMediaItem* aItem,
                                PRUint32 aIndex);

  // Enumerate listeners and call OnBeforeItemRemoved
  void NotifyListenersBeforeItemRemoved(sbIMediaList* aList,
                                        sbIMediaItem* aItem,
                                        PRUint32 aIndex);

  // Enumerate listeners and call OnAfterItemRemoved
  void NotifyListenersAfterItemRemoved(sbIMediaList* aList,
                                       sbIMediaItem* aItem,
                                       PRUint32 aIndex);

  // Enumerate listeners and call OnItemUpdated
  void NotifyListenersItemUpdated(sbIMediaList* aList,
                                  sbIMediaItem* aItem,
                                  sbIPropertyArray* aProperties);

  // Enumerate listeners and call OnItemMoved
  void NotifyListenersItemMoved(sbIMediaList* aList,
                                PRUint32 aFromIndex,
                                PRUint32 aToIndex);

  // Enumerate listeners and call OnBeforeListCleared
  void NotifyListenersBeforeListCleared(sbIMediaList* aList,
                                        PRBool        aExcludeLists);

  // Enumerate listeners and call OnListCleared
  void NotifyListenersListCleared(sbIMediaList* aList,
                                  PRBool        aExcludeLists);

  // Enumerate listeners and call OnBatchBegin
  void NotifyListenersBatchBegin(sbIMediaList* aList);

  // Enumerate listeners and call OnBatchEnd
  void NotifyListenersBatchEnd(sbIMediaList* aList);

private:

  nsresult SnapshotListenerArray(sbMediaListListenersArray& aArray,
                                 PRUint32 aFlags,
                                 sbIPropertyArray* aPropertyFilter = nsnull);
  void SweepListenerArray(sbStopNotifyArray& aStopNotifying);

  nsTArray<sbListenerInfoAutoPtr> mListenerArray;

  PRLock* mListenerArrayLock;

  PRUint32 mBatchDepth;
};

#endif /* __SB_LOCALDATABASE_MEDIALISTLISTENER_H__ */

