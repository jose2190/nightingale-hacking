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

#ifndef __SB_REMOTEFORWARDINGMACROS_H__
#define __SB_REMOTEFORWARDINGMACROS_H__

#define NS_FORWARD_SAFE_SBILIBRARYRESOURCE_NO_SETPROPERTY(_to) \
  NS_FORWARD_SAFE_SBILIBRARYRESOURCE_NO_SETPROPERTY_SETPROPERTIES(_to) \
  NS_IMETHOD SetProperties(sbIPropertyArray *aProperties) { return !_to ? NS_ERROR_NULL_POINTER : _to->SetProperties(aProperties); }

#define NS_FORWARD_SAFE_SBILIBRARYRESOURCE_NO_SETPROPERTY_SETPROPERTIES(_to) \
  NS_FORWARD_SAFE_SBILIBRARYRESOURCE_NO_SETGETPROPERTY_SETPROPERTIES(_to) \
  NS_IMETHOD GetProperty(const nsAString & aName, nsAString & _retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetProperty(aName, _retval); } \

#define NS_FORWARD_SAFE_SBILIBRARYRESOURCE_NO_SETGETPROPERTY_SETPROPERTIES(_to) \
  NS_IMETHOD GetGuid(nsAString & aGuid) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetGuid(aGuid); } \
  NS_IMETHOD GetCreated(PRInt64 *aCreated) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetCreated(aCreated); } \
  NS_IMETHOD GetUpdated(PRInt64 *aUpdated) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetUpdated(aUpdated); } \
  NS_IMETHOD GetUserEditable(PRBool *aUserEditable) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetUserEditable(aUserEditable); } \
  NS_IMETHOD GetPropertyIDs(nsIStringEnumerator * *aPropertyIDs) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetPropertyIDs(aPropertyIDs); } \
  NS_IMETHOD GetProperties(sbIPropertyArray *aPropertyIDs, sbIPropertyArray **_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetProperties(aPropertyIDs, _retval); } \
  NS_IMETHOD Equals(sbILibraryResource *aOtherLibraryResource, PRBool *_retval) { return !_to ? NS_ERROR_NULL_POINTER : _to->Equals(aOtherLibraryResource, _retval); }

#endif // __SB_REMOTEFORWARDINGMACROS_H__

