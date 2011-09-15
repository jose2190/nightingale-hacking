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
 * \file sbMetadataManager.h
 * \brief 
 */

#ifndef __METADATA_MANAGER_H__
#define __METADATA_MANAGER_H__

// INCLUDES ===================================================================
#include <nscore.h>
#include <prlock.h>
#include <prlog.h>
#include "sbIMetadataManager.h"
#include "sbIMetadataHandler.h"
#include <nsCOMPtr.h>
#include <nsStringGlue.h>

#include <set>
#include <list>

// DEFINES ====================================================================
#define SONGBIRD_METADATAMANAGER_CONTRACTID               \
  "@songbirdnest.com/Songbird/MetadataManager;1"
#define SONGBIRD_METADATAMANAGER_CLASSNAME                \
  "Songbird Metadata Manager Interface"
#define SONGBIRD_METADATAMANAGER_CID                      \
{ /* 32bfdede-854d-448f-bada-7a3b4b192034 */              \
  0x32bfdede,                                             \
  0x854d,                                                 \
  0x448f,                                                 \
  {0xba, 0xda, 0x7a, 0x3b, 0x4b, 0x19, 0x20, 0x34}        \
}
// FUNCTIONS ==================================================================

// CLASSES ====================================================================
class sbMetadataManager : public sbIMetadataManager
{
  NS_DECL_ISUPPORTS
  NS_DECL_SBIMETADATAMANAGER

  sbMetadataManager();
  virtual ~sbMetadataManager();

  static sbMetadataManager *GetSingleton();
  static void DestroySingleton();

  struct sbMetadataHandlerItem
  {
    nsCOMPtr<sbIMetadataHandler> m_Handler;
    PRInt32 m_Vote;
    bool operator < ( const sbMetadataManager::sbMetadataHandlerItem &T ) const
    {
      return m_Vote < T.m_Vote;
    }
    bool operator == ( const sbMetadataManager::sbMetadataHandlerItem &T ) const
    {
      return m_Vote == T.m_Vote;
    }
    #if PR_LOGGING
      nsCString m_ContractID;
    #endif
  };
  class handlerlist_t : public std::set< sbMetadataHandlerItem > {};
  class contractlist_t : public std::list< nsCString > {};
  contractlist_t m_ContractList;
  PRLock *m_pContractListLock;

  nsresult GetHandlerInternal(sbIMetadataHandler *aHandler, 
                              const nsAString &strURL, 
                              sbIMetadataHandler **_retval);
};

extern sbMetadataManager *gMetadataManager;

#endif // __METADATA_MANAGER_H__

