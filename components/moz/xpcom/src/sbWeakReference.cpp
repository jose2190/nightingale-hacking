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

#include "sbWeakReference.h"
#include "nsCOMPtr.h"

class sbWeakReference : public nsIWeakReference
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEAKREFERENCE

private:
  friend class sbSupportsWeakReference;

  sbWeakReference(sbSupportsWeakReference* referent) 
    : mReferentLock(nsnull)
    , mReferent(referent) {
    // ...I can only be constructed by an |sbSupportsWeakReference|
    mReferentLock = nsAutoLock::NewLock("sbWeakReference::mReferentLock");
    NS_WARN_IF_FALSE(mReferentLock, "Failed to create lock.");
  }

  ~sbWeakReference() {
    // ...I will only be destroyed by calling |delete| myself.
    if (mReferent) {
      mReferent->NoticeProxyDestruction();
    }

    if (mReferentLock) {
      nsAutoLock::DestroyLock(mReferentLock);
    }
  }

  void NoticeReferentDestruction() {
    NS_ENSURE_TRUE(mReferentLock, /*void*/);
    nsAutoLock lock(mReferentLock);
    // ...called (only) by an |sbSupportsWeakReference| from _its_ dtor.
    mReferent = nsnull;
  }

  PRLock *mReferentLock;
  sbSupportsWeakReference*  mReferent;
};

NS_COM_GLUE nsresult
sbSupportsWeakReference::GetWeakReference(nsIWeakReference** aInstancePtr) 
{
  NS_ENSURE_ARG_POINTER(aInstancePtr);
  NS_ENSURE_TRUE(mProxyLock, NS_ERROR_NOT_INITIALIZED);

  nsAutoLock lock(mProxyLock);

  if (!mProxy) {
    mProxy = new sbWeakReference(this);
  }
  
  *aInstancePtr = mProxy;

  if ( !*aInstancePtr ) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NS_ADDREF(*aInstancePtr);

  return NS_OK;
}

NS_IMPL_THREADSAFE_ISUPPORTS1(sbWeakReference, nsIWeakReference)

NS_IMETHODIMP
sbWeakReference::QueryReferent(const nsIID& aIID, void** aInstancePtr) 
{
  NS_ENSURE_TRUE(mReferentLock, NS_ERROR_NOT_INITIALIZED);

  nsAutoLock lock(mReferentLock);
  return mReferent ? 
    mReferent->QueryInterface(aIID, aInstancePtr) : NS_ERROR_NULL_POINTER;
}

void
sbSupportsWeakReference::ClearWeakReferences() 
{
  NS_ENSURE_TRUE(mProxyLock, /*void*/);

  nsAutoLock lock(mProxyLock);

  if (mProxy) {
    mProxy->NoticeReferentDestruction();
    mProxy = nsnull;
  }
}
