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

#include "nsXPCOM.h"
#include "nsCOMPtr.h"
#include "nsIGenericFactory.h"
#include "nsIServiceManager.h"
#include "sbLeakCanary.h"
#include "sbTestHarnessConsoleListener.h"
#include "sbTestHarnessCID.h"
#include "sbTimingService.h"

NS_GENERIC_FACTORY_CONSTRUCTOR(sbTestHarnessConsoleListener)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(sbTimingService, Init);
NS_GENERIC_FACTORY_CONSTRUCTOR(sbLeakCanary)

static const nsModuleComponentInfo components[] =
{
  {
    "Test Harness Console Listener",
    SB_TESTHARNESSCONSOLELISTENER_CID,
    SB_TESTHARNESSCONSOLELISTENER_CONTRACTID,
    sbTestHarnessConsoleListenerConstructor,
    nsnull
  },
  {
    SB_TIMINGSERVICE_DESCRIPTION,
    SB_TIMINGSERVICE_CID,
    SB_TIMINGSERVICE_CONTRACTID,
    sbTimingServiceConstructor,
    nsnull
  },
  {
    "Test Harness Leak Canary",
    SB_LEAKCANARY_CID,
    SB_LEAKCANARY_CONTRACTID,
    sbLeakCanaryConstructor,
    nsnull
  }
};

NS_IMPL_NSGETMODULE(sbTestHarnessConsoleListenerModule, components)

