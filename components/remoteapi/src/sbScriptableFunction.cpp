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

#include "sbScriptableFunction.h"

#include <sbClassInfoUtils.h>

#include <nsMemory.h>
#include <nsIClassInfoImpl.h>
#include <nsStringGlue.h>

#include <jsapi.h>

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbScriptableFunction:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* gScriptableFunctionLog = nsnull;
#endif

#undef LOG
#define LOG(args) PR_LOG( gScriptableFunctionLog, PR_LOG_WARN, args )
#define TRACE(args) PR_LOG( gScriptableFunctionLog, PR_LOG_DEBUG, args )

NS_IMPL_ISUPPORTS2_CI( sbScriptableFunctionBase,
                       nsISecurityCheckedComponent,
                       nsIXPCScriptable )
NS_DECL_CLASSINFO(sbScriptableFunctionBase)
SB_IMPL_CLASSINFO_INTERFACES_ONLY(sbScriptableFunctionBase)


sbScriptableFunctionBase::sbScriptableFunctionBase()
{
#ifdef PR_LOGGING
  if (!gScriptableFunctionLog) {
    gScriptableFunctionLog = PR_NewLogModule( "sbScriptableFunction" );
  }
#endif
  TRACE(("sbScriptableFunctionBase::sbScriptableFunctionBase()"));
}

sbScriptableFunctionBase::~sbScriptableFunctionBase()
{
  TRACE(("sbScriptableFunctionBase::~sbScriptableFunctionBase()"));
}

// ---------------------------------------------------------------------------
//
//                          nsIXPCScriptable
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbScriptableFunctionBase::GetClassName(char **aClassName)
{
  LOG(("sbScriptableFunctionBase::GetClassName()"));
  NS_ENSURE_ARG_POINTER(aClassName);
  
  NS_NAMED_LITERAL_CSTRING( kClassName, "sbScriptableFunction" );
  *aClassName = ToNewCString(kClassName);
  return aClassName ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

NS_IMETHODIMP
sbScriptableFunctionBase::GetScriptableFlags(PRUint32 *aScriptableFlags)
{
  LOG(("sbScriptableFunctionBase::GetScriptableFlags()"));
  NS_ENSURE_ARG_POINTER(aScriptableFlags);
  *aScriptableFlags = DONT_ENUM_STATIC_PROPS |
                      DONT_ENUM_QUERY_INTERFACE |
                      WANT_CALL |
                      DONT_REFLECT_INTERFACE_NAMES ;
  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                          nsISecurityCheckedComponent
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbScriptableFunctionBase::CanCreateWrapper( const nsIID *iid, char **_retval )
{
  TRACE(("sbScriptableFunctionBase::CanCreateWrapper()"));

  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = ToNewCString( NS_LITERAL_CSTRING("AllAccess") );
  return NS_OK;
}

NS_IMETHODIMP
sbScriptableFunctionBase::CanCallMethod( const nsIID *iid,
                                         const PRUnichar *methodName,
                                         char **_retval )
{
  TRACE(( "sbScriptableFunctionBase::CanCallMethod() - %s",
          NS_LossyConvertUTF16toASCII(methodName).BeginReading() ));

  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = ToNewCString( NS_LITERAL_CSTRING("AllAccess") );
  return NS_OK;
}

NS_IMETHODIMP
sbScriptableFunctionBase::CanGetProperty( const nsIID *iid,
                                          const PRUnichar *propertyName,
                                          char **_retval )
{
  TRACE(( "sbScriptableFunctionBase::CanGetProperty() - %s",
          NS_LossyConvertUTF16toASCII(propertyName).BeginReading() ));

  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = ToNewCString( NS_LITERAL_CSTRING("AllAccess") );
  return NS_OK;
}

NS_IMETHODIMP
sbScriptableFunctionBase::CanSetProperty( const nsIID *iid,
                                          const PRUnichar *propertyName,
                                          char **_retval )
{
  TRACE(( "sbScriptableFunctionBase::CanSetProperty() - %s",
          NS_LossyConvertUTF16toASCII(propertyName).BeginReading() ));

  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = ToNewCString( NS_LITERAL_CSTRING("NoAccess") );
  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                       Class:  sbScriptableLibraryFunction
//
// ---------------------------------------------------------------------------

NS_IMPL_ISUPPORTS_INHERITED0( sbScriptableLibraryFunction,
                              sbScriptableFunctionBase )
 
sbScriptableLibraryFunction::sbScriptableLibraryFunction( nsISupports *aObject,
                                                          const nsIID& aIID )
  : mObject(aObject),
    mIID(aIID)
{
#ifdef PR_LOGGING
  if (!gScriptableFunctionLog) {
    gScriptableFunctionLog = PR_NewLogModule( "sbScriptableFunction" );
  }
#endif
  TRACE(("sbScriptableLibraryFunction::sbScriptableLibraryFunction()"));
}

sbScriptableLibraryFunction::~sbScriptableLibraryFunction()
{
  TRACE(("sbScriptableLibraryFunction::~sbScriptableLibraryFunction()"));
}

NS_IMETHODIMP
sbScriptableLibraryFunction::Call( nsIXPConnectWrappedNative *wrapper,
                                   JSContext *cx,
                                   JSObject *obj,
                                   PRUint32 argc,
                                   jsval *argv,
                                   jsval *vp,
                                   PRBool *_retval )
{
  TRACE(("sbScriptableLibraryFunction::Call()"));
  
  // this can get called because library.getArtists should map to a function
  // that returns library.artists, but instead it returns the object itself.
  // So we make the object callable and return itself.
  NS_ENSURE_ARG_POINTER(obj);
  NS_ENSURE_ARG_POINTER(vp);
  NS_ENSURE_ARG_POINTER(_retval);
  
  nsresult rv;
  
  nsCOMPtr<nsIXPConnect> xpc;
  rv = wrapper->GetXPConnect( getter_AddRefs(xpc) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  nsCOMPtr<nsIXPConnectJSObjectHolder> objectHolder;
  rv = xpc->WrapNative( cx, obj, mObject, mIID, getter_AddRefs(objectHolder) );
  NS_ENSURE_SUCCESS( rv, rv );
  
  JSObject* object;
  rv = objectHolder->GetJSObject( &object );
  NS_ENSURE_SUCCESS( rv, rv );
  
  *vp = OBJECT_TO_JSVAL(object);
  *_retval = PR_TRUE;
  
  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                       Class:  sbScriptableMediaListFunction
//
// ---------------------------------------------------------------------------

NS_IMPL_ISUPPORTS_INHERITED0( sbScriptableMediaListFunction,
                              sbScriptableFunctionBase )

sbScriptableMediaListFunction::sbScriptableMediaListFunction()
{
#ifdef PR_LOGGING
  if (!gScriptableFunctionLog) {
    gScriptableFunctionLog = PR_NewLogModule( "sbScriptableFunction" );
  }
#endif
  TRACE(("sbScriptableMediaListFunction::sbScriptableMediaListFunction()"));
}

sbScriptableMediaListFunction::~sbScriptableMediaListFunction()
{
  TRACE(("sbScriptableMediaListFunction::~sbScriptableMediaListFunction()"));
}

NS_IMETHODIMP
sbScriptableMediaListFunction::Call( nsIXPConnectWrappedNative *wrapper,
                                     JSContext *cx,
                                     JSObject *obj,
                                     PRUint32 argc,
                                     jsval *argv,
                                     jsval *vp,
                                     PRBool *_retval )
{
  TRACE(("sbScriptableMediaListFunction::Call()"));
  return NS_OK;
}
