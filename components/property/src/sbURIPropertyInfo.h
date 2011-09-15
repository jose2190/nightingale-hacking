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

#ifndef __SBURIPROPERTYINFO_H__
#define __SBURIPROPERTYINFO_H__

#include <sbIPropertyManager.h>
#include "sbPropertyInfo.h"

#include <nsCOMPtr.h>
#include <nsStringGlue.h>

#include <nsIIOService.h>

class sbURIPropertyInfo : public sbPropertyInfo,
                          public sbIURIPropertyInfo
{
public:
  NS_DECL_ISUPPORTS
  NS_FORWARD_SBIPROPERTYINFO_NOSPECIFICS(sbPropertyInfo::);
  NS_DECL_SBIURIPROPERTYINFO

  sbURIPropertyInfo();
  virtual ~sbURIPropertyInfo();

  nsresult Init();

  NS_IMETHOD Validate(const nsAString & aValue, PRBool *_retval);
  NS_IMETHOD Sanitize(const nsAString & aValue, nsAString & _retval);
  NS_IMETHOD Format(const nsAString & aValue, nsAString & _retval);
  NS_IMETHOD MakeSearchable(const nsAString & aValue, nsAString & _retval);
  NS_IMETHOD MakeSortable(const nsAString & aValue, nsAString & _retval);

  NS_IMETHOD EnsureIOService();

  PRBool    IsInvalidEmpty(const nsAString &aValue);

private:
  nsresult InitializeOperators();

  PRLock*   mURISchemeConstraintLock;
  nsString  mURISchemeConstraint;

  PRLock*                mIOServiceLock;
  nsCOMPtr<nsIIOService> mIOService;
};

#endif /* __SBURIPROPERTYINFO_H__ */
