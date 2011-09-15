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

/**
* \file  DeviceManager.cpp
* \Windows Media device Component Implementation.
*/
#include "DeviceManager.h"
#include "DeviceBase.h"

#include <nsAutoLock.h>
#include <nsComponentManagerUtils.h>
#include <nsIComponentRegistrar.h>
#include <nsIObserverService.h>
#include <nsISimpleEnumerator.h>
#include <nsServiceManagerUtils.h>
#include <nsISupportsPrimitives.h>
#include <nsXPCOM.h>
#include <prlog.h>

#include <sbILibraryManager.h>

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbDeviceManagerObsolete:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* gDevicemanagerLog = nsnull;
#define LOG(args) PR_LOG(gDevicemanagerLog, PR_LOG_DEBUG, args)
#else
#define LOG(args) /* nothing */
#endif

#define SB_DEVICE_PREFIX "@songbirdnest.com/Songbird/OldDeviceImpl/"

// This allows us to be initialized once and only once.
PRBool sbDeviceManagerObsolete::sServiceInitialized = PR_FALSE;

// Whether or not we've already loaded all supported devices
PRBool sbDeviceManagerObsolete::sDevicesLoaded = PR_FALSE;

// This is a sanity check to make sure that we're finalizing properly
PRBool sbDeviceManagerObsolete::sServiceFinalized = PR_FALSE;

NS_IMPL_THREADSAFE_ISUPPORTS2(sbDeviceManagerObsolete,
                              sbIDeviceManager,
                              nsIObserver)

sbDeviceManagerObsolete::sbDeviceManagerObsolete()
: mLock(nsnull),
  mLastRequestedIndex(nsnull)
{
#ifdef PR_LOGGING
  if (!gDevicemanagerLog)
    gDevicemanagerLog = PR_NewLogModule("sbDeviceManagerObsolete");
#endif

  LOG(("DeviceManagerObsolete[0x%x] - Created", this));
}

sbDeviceManagerObsolete::~sbDeviceManagerObsolete()
{
  NS_ASSERTION(sbDeviceManagerObsolete::sServiceFinalized,
               "DeviceManagerObsolete never finalized!");
  if (mLock)
    nsAutoLock::DestroyLock(mLock);

  LOG(("DeviceManagerObsolete[0x%x] - Destroyed", this));
}

NS_IMETHODIMP
sbDeviceManagerObsolete::Initialize()
{
  LOG(("DeviceManager[0x%x] - Initialize", this));

  // Test to make sure that we haven't been initialized yet. If consumers are
  // doing the right thing (using getService) then we should never get here
  // more than once. If they do the wrong thing (createInstance) then we'll
  // fail on them so that they fix their code.
  NS_ENSURE_FALSE(sbDeviceManagerObsolete::sServiceInitialized,
                  NS_ERROR_ALREADY_INITIALIZED);

  mLock = nsAutoLock::NewLock("sbDeviceManagerObsolete::mLock");
  NS_ENSURE_TRUE(mLock, NS_ERROR_OUT_OF_MEMORY);

  mLastRequestedCategory = EmptyString();

  // Register with the observer service to continue initialization after the.
  // profile has been loaded. Also register for XPCOM shutdown notification.

  nsresult rv;
  nsCOMPtr<nsIObserverService> observerService =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // We can't really start until the library manager has loaded
  rv = observerService->AddObserver(this, SB_LIBRARY_MANAGER_READY_TOPIC,
                                    PR_FALSE);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to add library manager observer");

  // Since we depend on the library, we need to shut down before it.  
  rv = observerService->AddObserver(this, SB_LIBRARY_MANAGER_BEFORE_SHUTDOWN_TOPIC,
                                    PR_FALSE);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                   "Failed to add library before shutdown observer");

  // "xpcom-shutdown" is called right before the app will terminate
  rv = observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID,
                                    PR_FALSE);
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Failed to add shutdown observer");


  // XXX Don't add any calls here that could possibly fail! We've already added
  //     ourselves to the observer service so if we fail now the observer
  //     service will hold an invalid pointer and later cause a crash at
  //     shutdown. Add any dangerous calls above *before* the call to
  //     AddObserver.

  // Set the static variable so that we won't initialize again.
  sbDeviceManagerObsolete::sServiceInitialized = PR_TRUE;

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::Finalize()
{
  LOG(("DeviceManagerObsolete[0x%x] - Finalize", this));

  // Make sure we aren't called more than once
  NS_ENSURE_FALSE(sbDeviceManagerObsolete::sServiceFinalized, NS_ERROR_UNEXPECTED);

  // Loop through the array and call Finalize() on all the devices.
  nsresult rv;

  nsAutoLock autoLock(mLock);

  PRInt32 count = mSupportedDevices.Count();
  for (PRInt32 index = 0; index < count; index++) {
    nsCOMPtr<sbIDeviceBase> device = mSupportedDevices.ObjectAt(index);
    NS_ASSERTION(device, "Null pointer in mSupportedDevices");

    rv = device->Finalize();
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "A device failed to finalize");
  }

  sbDeviceManagerObsolete::sServiceFinalized = PR_TRUE;

  return NS_OK;
}

// Instantiate all supported devices.
// This is done by iterating through all registered XPCOM components and
// finding the components with @songbirdnest.com/Songbird/Device/ prefix for the
// contract ID for the interface.
NS_IMETHODIMP
sbDeviceManagerObsolete::LoadSupportedDevices()
{
  LOG(("DeviceManagerObsolete[0x%x] - LoadSupportedDevices", this));

  // Make sure we aren't called more than once
  NS_ENSURE_TRUE(sbDeviceManagerObsolete::sServiceInitialized,
                 NS_ERROR_ALREADY_INITIALIZED);
  NS_ENSURE_FALSE(sbDeviceManagerObsolete::sDevicesLoaded, NS_ERROR_UNEXPECTED);

  nsAutoLock autoLock(mLock);

  // Get the component registrar
  nsresult rv;
  nsCOMPtr<nsIComponentRegistrar> registrar;
  rv = NS_GetComponentRegistrar(getter_AddRefs(registrar));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISimpleEnumerator> simpleEnumerator;
  rv = registrar->EnumerateContractIDs(getter_AddRefs(simpleEnumerator));
  NS_ENSURE_SUCCESS(rv, rv);

  // Enumerate through the contractIDs and look for our prefix
  nsCOMPtr<nsISupports> element;
  PRBool more = PR_FALSE;
  while(NS_SUCCEEDED(simpleEnumerator->HasMoreElements(&more)) && more) {

    rv = simpleEnumerator->GetNext(getter_AddRefs(element));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsISupportsCString> contractString =
      do_QueryInterface(element, &rv);
    if NS_FAILED(rv) {
      NS_WARNING("QueryInterface failed");
      continue;
    }

    nsCAutoString contractID;
    rv = contractString->GetData(contractID);
    if NS_FAILED(rv) {
      NS_WARNING("GetData failed");
      continue;
    }

    NS_NAMED_LITERAL_CSTRING(prefix, SB_DEVICE_PREFIX);

    if (!StringBeginsWith(contractID, prefix))
      continue;

    // Create an instance of the device
    nsCOMPtr<sbIDeviceBase> device =
      do_CreateInstance(contractID.get(), &rv);
    if (!device) {
      NS_WARNING("Failed to create device!");
      continue;
    }

    // And initialize the device
    rv = device->Initialize();

    if(NS_FAILED(rv)) {
      NS_WARNING("Device failed to initialize!");
      continue;
    }

    // If everything has succeeded then we can add it to our array
    PRBool ok = mSupportedDevices.AppendObject(device);

    // Make sure that our array is behaving properly. If not we're in trouble.
    NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);
  }

  sbDeviceManagerObsolete::sDevicesLoaded = PR_TRUE;

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::GetDeviceCount(PRUint32* aDeviceCount)
{
  NS_ENSURE_ARG_POINTER(aDeviceCount);
  NS_ENSURE_TRUE(sbDeviceManagerObsolete::sDevicesLoaded, NS_ERROR_UNEXPECTED);

  nsAutoLock autoLock(mLock);

	*aDeviceCount = (PRUint32)mSupportedDevices.Count();
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::GetCategoryByIndex(PRUint32 aIndex,
                                            nsAString& _retval)
{
  NS_ENSURE_TRUE(sbDeviceManagerObsolete::sDevicesLoaded, NS_ERROR_UNEXPECTED);

  nsAutoLock autoLock(mLock);

  NS_ENSURE_ARG_MAX(aIndex, (PRUint32)mSupportedDevices.Count());

  nsCOMPtr<sbIDeviceBase> device = mSupportedDevices.ObjectAt(aIndex);
  NS_ENSURE_TRUE(device, NS_ERROR_NULL_POINTER);

  nsresult rv = device->GetDeviceCategory(_retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::GetDeviceByIndex(PRUint32 aIndex,
                                          sbIDeviceBase** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(sbDeviceManagerObsolete::sDevicesLoaded, NS_ERROR_UNEXPECTED);

  nsAutoLock autoLock(mLock);

  NS_ENSURE_ARG_MAX(aIndex, (PRUint32)mSupportedDevices.Count());

  nsCOMPtr<sbIDeviceBase> device = mSupportedDevices.ObjectAt(aIndex);
  NS_ENSURE_TRUE(device, NS_ERROR_UNEXPECTED);

  NS_ADDREF(*_retval = device);

  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::HasDeviceForCategory(const nsAString& aCategory,
                                              PRBool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(sbDeviceManagerObsolete::sDevicesLoaded, NS_ERROR_UNEXPECTED);

  nsAutoLock autoLock(mLock);

  PRUint32 dummy;
  nsresult rv = GetIndexForCategory(aCategory, &dummy);

  *_retval = NS_SUCCEEDED(rv);
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::GetDeviceByCategory(const nsAString& aCategory,
                                             sbIDeviceBase** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_TRUE(sbDeviceManagerObsolete::sDevicesLoaded, NS_ERROR_UNEXPECTED);

  nsAutoLock autoLock(mLock);

  PRUint32 index;
  nsresult rv = GetIndexForCategory(aCategory, &index);

  // If a supporting device wasn't found then return an error
  NS_ENSURE_SUCCESS(rv, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<sbIDeviceBase> device =
    do_QueryInterface(mSupportedDevices.ObjectAt(index));
  NS_ENSURE_TRUE(device, NS_ERROR_UNEXPECTED);

  NS_ADDREF(*_retval = device);
  return NS_OK;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::GetIndexForCategory(const nsAString& aCategory,
                                             PRUint32* _retval)
{
  // We don't bother checking arguments or locking because we assume that we
  // have already done so in our public methods.

  // First check to see if we've already looked up the result.
  if (!mLastRequestedCategory.IsEmpty() &&
      aCategory.Equals(mLastRequestedCategory)) {
    *_retval = mLastRequestedIndex;
    return NS_OK;
  }

  // Otherwise loop through the array and try to find the category.
  PRInt32 count = mSupportedDevices.Count();
  for (PRInt32 index = 0; index < count; index++) {
    nsCOMPtr<sbIDeviceBase> device = mSupportedDevices.ObjectAt(index);
    if (!device) {
      NS_WARNING("Null pointer in mSupportedDevices");
      continue;
    }

    nsAutoString category;
    nsresult rv = device->GetDeviceCategory(category);
    if (NS_FAILED(rv)) {
      NS_WARNING("GetDeviceCategory Failed");
      continue;
    }

    if (category.Equals(aCategory)) {
      mLastRequestedCategory = category;
      *_retval = mLastRequestedIndex = index;
      return NS_OK;
    }
  }

  // Not found
  mLastRequestedCategory = EmptyString();
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
sbDeviceManagerObsolete::Observe(nsISupports* aSubject,
                                 const char* aTopic,
                                 const PRUnichar* aData)
{
  LOG(("DeviceManagerObsolete[0x%x] - Observe: %s", this, aTopic));

  nsresult rv;
  nsCOMPtr<nsIObserverService> observerService =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (strcmp(aTopic, SB_LIBRARY_MANAGER_READY_TOPIC) == 0) {
    // The profile has been loaded so now we can go hunting for devices
    rv = LoadSupportedDevices();
    NS_ENSURE_SUCCESS(rv, rv);

    // Notify any observers that we're ready to go.
    rv = observerService->NotifyObservers(NS_ISUPPORTS_CAST(sbIDeviceManager*, this),
                                          SB_DEVICE_MANAGER_READY_TOPIC,
                                          nsnull);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else if (strcmp(aTopic, SB_LIBRARY_MANAGER_BEFORE_SHUTDOWN_TOPIC) == 0) {
    // The profile is about to be unloaded so finalize our devices
    rv = Finalize();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    // Remove ourselves from the observer service
    rv = observerService->RemoveObserver(this, SB_LIBRARY_MANAGER_READY_TOPIC);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                     "Failed to remove library manager observer");

    rv = observerService->RemoveObserver(this,
                                         SB_LIBRARY_MANAGER_BEFORE_SHUTDOWN_TOPIC);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                     "Failed to remove library manager before shutdown observer");

    rv = observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
    NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                     "Failed to remove shutdown observer");

  }

  return NS_OK;
}
