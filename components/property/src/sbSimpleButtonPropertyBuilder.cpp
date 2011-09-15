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

#include "sbSimpleButtonPropertyBuilder.h"
#include "sbSimpleButtonPropertyInfo.h"

#include <nsAutoPtr.h>

#include <sbIPropertyArray.h>
#include <sbIPropertyManager.h>
#include <nsIStringBundle.h>

NS_IMPL_ISUPPORTS_INHERITED1(sbSimpleButtonPropertyBuilder,
                             sbAbstractPropertyBuilder,
                             sbISimpleButtonPropertyBuilder)

NS_IMETHODIMP
sbSimpleButtonPropertyBuilder::Get(sbIPropertyInfo** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_STATE(!mPropertyID.IsEmpty());

  nsString displayName;
  nsresult rv = GetFinalDisplayName(displayName);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasLabel = PR_FALSE;
  nsString label;
  if (!mLabelKey.IsEmpty()) {
    rv = GetStringFromName(mBundle, mLabelKey, label);
    NS_ENSURE_SUCCESS(rv, rv);
    hasLabel = PR_TRUE;
  }
  else {
    if (!mLabel.IsEmpty()) {
      label = mLabel;
      hasLabel = PR_TRUE;
    }
  }

  nsRefPtr<sbSimpleButtonPropertyInfo> pi =
    new sbSimpleButtonPropertyInfo(mPropertyID,
                                   displayName,
                                   mDisplayNameKey,
                                   hasLabel,
                                   label,
                                   mRemoteReadable,
                                   mRemoteWritable,
                                   mUserViewable,
                                   mUserEditable);
  NS_ENSURE_TRUE(pi, NS_ERROR_OUT_OF_MEMORY);

  rv = pi->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = pi);
  return NS_OK;
}

NS_IMETHODIMP
sbSimpleButtonPropertyBuilder::GetLabel(nsAString& aLabel)
{
  aLabel = mLabel;
  return NS_OK;
}
NS_IMETHODIMP
sbSimpleButtonPropertyBuilder::SetLabel(const nsAString& aLabel)
{
  mLabel = aLabel;
  return NS_OK;
}

NS_IMETHODIMP
sbSimpleButtonPropertyBuilder::GetLabelKey(nsAString& aLabelKey)
{
  aLabelKey = mLabelKey;
  return NS_OK;
}
NS_IMETHODIMP
sbSimpleButtonPropertyBuilder::SetLabelKey(const nsAString& aLabelKey)
{
  mLabelKey = aLabelKey;
  return NS_OK;
}

