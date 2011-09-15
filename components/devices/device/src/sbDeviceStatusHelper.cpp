/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 :miv */
/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2011 POTI, Inc.
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

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//
// Device status helper services.
//
//   These services may only be used within the request lock.
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

/**
 * \file  sbDeviceStatusHelper.cpp
 * \brief Songbird Device Status Helper Services Source.
 */

//------------------------------------------------------------------------------
//
// Device status helper imported services.
//
//------------------------------------------------------------------------------

// Self imports.
#include "sbDeviceStatusHelper.h"

// Local imports.
// Songbird imports.

#include <sbIDeviceEvent.h>
#include <sbVariantUtils.h>

#ifdef PR_LOGGING
extern PRLogModuleInfo* gBaseDeviceLog;
#endif

#undef LOG
#define LOG(args) PR_LOG(gBaseDeviceLog, PR_LOG_WARN, args)

//------------------------------------------------------------------------------
//
// Device status helper defs.
//
//------------------------------------------------------------------------------

//
// gDeviceStateStringList       List of device state strings.
// STATE_STRING                 Macro for getting the state string.
//

static const char* gDeviceStateStringList[] = {
  "IDLE",
  "SYNCING",
  "COPYING",
  "DELETING",
  "UPDATING",
  "MOUNTING",
  "DOWNLOADING",
  "UPLOADING",
  "DOWNLOAD_PAUSED",
  "UPLOAD_PAUSED",
  "DISCONNECTED",
  "BUSY",
  "CANCEL",
  "TRANSCODE",
  "FORMATTING"
};

inline char const *
DeviceStateString(PRUint32 aState)
{
  return aState < NS_ARRAY_LENGTH(gDeviceStateStringList) ?
   gDeviceStateStringList[aState] : "unknown";
}


sbDeviceStatusAutoOperationComplete::sbDeviceStatusAutoOperationComplete(
                                   sbDeviceStatusHelper * aStatus,
                                   sbDeviceStatusHelper::Operation aOperation,
                                   TransferRequest * aRequest,
                                   PRUint32 aBatchCount) :
                                     mRequest(aRequest),
                                     mBatchCount(aBatchCount),
                                     mStatus(aStatus),
                                     mResult(NS_ERROR_FAILURE),
                                     mOperation(aOperation)
{
  PRBool copyAfterTranscode =
    (mOperation == sbDeviceStatusHelper::OPERATION_TYPE_WRITE &&
     mRequest->destinationCompatibility ==
       sbBaseDevice::TransferRequest::COMPAT_NEEDS_TRANSCODING);

  const PRUint32 batchIndex = mRequest->GetBatchIndex();

  // If this is the start of a batch or is not a batch thingy do start op.
  //
  // Some device has two phases for transcoding (MTP for example).
  // If this is the last copying operation in the queue after the last
  // transcoding, do a start op (initialization). The values have been
  // destroyed by the auto complete destructor of the last transcoding
  // operation already.
  if (batchIndex == 0 ||
      (copyAfterTranscode && batchIndex == aBatchCount - 1)) {

    // Not a new batch if this is a write operation after transcoding.
    mStatus->OperationStart(mOperation,
                            batchIndex + 1,
                            aBatchCount,
                            mRequest->itemType,
                            IsItemOp(mOperation) ? mRequest->list : nsnull,
                            IsItemOp(mOperation) ? mRequest->item : nsnull,
                            !copyAfterTranscode);
  }
  if (IsItemOp(mOperation)) {
    // Update item status
    mStatus->ItemStart(mRequest->list,
                       mRequest->item,
                       batchIndex + 1,
                       aBatchCount,
                       mRequest->itemType);
  }
}

sbDeviceStatusAutoOperationComplete::sbDeviceStatusAutoOperationComplete(
                                 sbDeviceStatusHelper * aStatus,
                                 sbDeviceStatusHelper::Operation aOperation) :
                                   mRequest(nsnull),
                                   mBatchCount(0),
                                   mStatus(aStatus),
                                   mResult(NS_ERROR_FAILURE),
                                   mOperation(aOperation)
{
  mStatus->OperationStart(mOperation,
                          -1,
                          -1,
                          -1,
                          nsnull,
                          nsnull);
}

sbDeviceStatusAutoOperationComplete::sbDeviceStatusAutoOperationComplete(
                                   sbDeviceStatusHelper * aStatus,
                                   sbDeviceStatusHelper::Operation aOperation,
                                   TransferRequest * aRequest,
                                   PRInt32 aBatchCount) :
                                     mRequest(aRequest),
                                     mBatchCount(aBatchCount),
                                     mStatus(aStatus),
                                     mResult(NS_ERROR_FAILURE),
                                     mOperation(aOperation) {

  mStatus->OperationStart(mOperation,
                          0,
                          mBatchCount,
                          aRequest->itemType,
                          IsItemOp(mOperation) ? mRequest->list : nsnull,
                          IsItemOp(mOperation) ? mRequest->item : nsnull);
}

sbDeviceStatusAutoOperationComplete::~sbDeviceStatusAutoOperationComplete() {
  Complete();
}

void sbDeviceStatusAutoOperationComplete::Complete()
{
  if (mStatus && mRequest) {
    const PRUint32 batchIndex = mRequest->GetBatchIndex() + 1;
    if (IsItemOp(mOperation)) {
      mStatus->ItemComplete(mResult);
    }
    if (batchIndex == mBatchCount) {
      mStatus->OperationComplete(mResult);
    }
  }
  // We've completed it, lets make sure we don't do it again
  mStatus = nsnull;
  mRequest = nsnull;
}

void sbDeviceStatusAutoOperationComplete::Transfer(
                           sbDeviceStatusAutoOperationComplete & aDestination)
{
  aDestination = *this;
  // Prevent us from auto completing since aDestination now will
  mStatus = nsnull;
  mRequest = nsnull;
}

sbDeviceStatusAutoOperationComplete &
sbDeviceStatusAutoOperationComplete::operator = (
                             sbDeviceStatusAutoOperationComplete const & aOther)
{
  mRequest = aOther.mRequest;
  mBatchCount = aOther.mBatchCount;
  mStatus = aOther.mStatus;
  mResult = aOther.mResult;
  mOperation = aOther.mOperation;
  return *this;
}


//------------------------------------------------------------------------------
//
// Device status helper operation services.
//
//------------------------------------------------------------------------------

/**
 * Process the start of a new operation of the type specified by aOperationType.
 * The current operation item number is specified by aItemNum and the operation
 * total item count is specified by aItemCount.
 *
 * \param aOperationType        Type of operation.
 * \param aItemNum              Current item number
 * \param aItemCount            Current total item count.
 * \param aMediaList            Operation media list. Defaulted to nsnull.
 * \param aMediaItem            Operation media item. Defaulted to nsnull.
 */

void
sbDeviceStatusHelper::OperationStart(sbDeviceStatusHelper::Operation aOperationType,
                                     PRInt32  aItemNum,
                                     PRInt32  aItemCount,
                                     PRInt32  aItemType,
                                     sbIMediaList* aMediaList,
                                     sbIMediaItem* aMediaItem,
                                     PRBool aNewBatch)
{
  // Check if we're already started. The initial batch item might have
  // completed but might not have been removed from the queue, thus
  // we might need to "restart" the operation
  if (aItemNum > 1 && mOperationType != OPERATION_TYPE_NONE) {
    return;
  }

  // Update the current operation type.
  mOperationType = aOperationType;
  if (aMediaList) {
    mMediaList = aMediaList;
  }
  if (aMediaItem) {
    mMediaItem = aMediaItem;
  }

  mItemNum = aItemNum;
  mItemCount = aItemCount;
  mItemType = aItemType;

  if (aNewBatch)
    mStatus->SetIsNewBatch(true);

  // Dispatch operation dependent status processing.
  switch (mOperationType)
  {
    case OPERATION_TYPE_MOUNT :
      LOG(("sbDeviceStatusHelper::OperationStart mount\n"));
      UpdateStatus(NS_LITERAL_STRING("mounting"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MOUNTING_START,
                  sbNewVariant(NS_ISUPPORTS_CAST(sbIDevice*, mDevice)));
      break;

    case OPERATION_TYPE_WRITE :
      LOG(("sbDeviceStatusHelper::OperationStart write\n"));
      UpdateStatus(NS_LITERAL_STRING("writing"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MEDIA_WRITE_START,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_TRANSCODE :
      LOG(("sbDeviceStatusHelper::OperationStart transcode\n"));
      UpdateStatus(NS_LITERAL_STRING("transcoding"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSCODE_START,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_DELETE :
      LOG(("sbDeviceStatusHelper::OperationStart delete\n"));
      UpdateStatus(NS_LITERAL_STRING("deleting"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_START,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_READ :
      LOG(("sbDeviceStatusHelper::OperationStart read\n"));
      UpdateStatus(NS_LITERAL_STRING("reading"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MEDIA_READ_START,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_FORMAT :
      LOG(("sbDeviceStatusHelper::OperationStart format\n"));
      UpdateStatus(NS_LITERAL_STRING("formatting"),
                   NS_LITERAL_STRING("Starting"),
                   0,
                   0,
                   0.0,
                   0);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_FORMATTING_START,
                  sbNewVariant(NS_ISUPPORTS_CAST(sbIDevice*, mDevice)));
      break;

    case OPERATION_TYPE_DOWNLOAD :
      LOG(("sbDeviceStatusHelper::OperationStart download\n"));
      UpdateStatus(NS_LITERAL_STRING("downloading"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_DOWNLOAD_START,
                  sbNewVariant(mMediaItem));
      break;

    default :
      break;
  }
}

/**
 * Process the completion of the current operation with the result specified by
 * aResult.
 *
 * \param aResult               Completion result of operation.
 */

void
sbDeviceStatusHelper::OperationComplete(nsresult aResult)
{
  // Set the state message according to the completion result.
  nsString stateMessage;
  if (NS_SUCCEEDED(aResult))
    stateMessage.AssignLiteral("Completed");
  else
    stateMessage.AssignLiteral("Failed");

  // Dispatch operation dependent status processing.
  LOG(("sbDeviceStatusHelper::OperationComplete %s.\n",
       NS_ConvertUTF16toUTF8(stateMessage).get()));
  switch(mOperationType)
  {
    case OPERATION_TYPE_MOUNT :
      LOG(("sbDeviceStatusHelper::OperationComplete mount.\n"));
      UpdateStatus(NS_LITERAL_STRING("mounting"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MOUNTING_END,
                  sbNewVariant(NS_ISUPPORTS_CAST(sbIDevice*, mDevice)));
      break;

    case OPERATION_TYPE_WRITE :
      LOG(("sbDeviceStatusHelper::OperationComplete write.\n"));
      UpdateStatus(NS_LITERAL_STRING("writing"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MEDIA_WRITE_END,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_TRANSCODE :
      LOG(("sbDeviceStatusHelper::OperationStart transcode\n"));
      UpdateStatus(NS_LITERAL_STRING("transcoding"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      break;

    case OPERATION_TYPE_READ :
      LOG(("sbDeviceStatusHelper::OperationComplete read.\n"));
      UpdateStatus(NS_LITERAL_STRING("reading"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MEDIA_READ_END,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_DELETE :
      LOG(("sbDeviceStatusHelper::OperationComplete delete.\n"));
      UpdateStatus(NS_LITERAL_STRING("deleting"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_END,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_FORMAT :
      LOG(("sbDeviceStatusHelper::OperationComplete format.\n"));
      UpdateStatus(NS_LITERAL_STRING("formatting"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_FORMATTING_END,
                  sbNewVariant(NS_ISUPPORTS_CAST(sbIDevice*, mDevice)));
      break;

    case OPERATION_TYPE_DOWNLOAD :
      LOG(("sbDeviceStatusHelper::OperationComplete download\n"));
      UpdateStatus(NS_LITERAL_STRING("downloading"),
                   stateMessage,
                   0,
                   0,
                   1.0,
                   mItemType);
      break;

    default :
      break;
  }

  // Clear the current operation state.
  mOperationType = OPERATION_TYPE_NONE;
  mMediaList = nsnull;
  mMediaItem = nsnull;
}


//------------------------------------------------------------------------------
//
// Device status helper item services.
//
//------------------------------------------------------------------------------

/**
 * Process the start of item processing of the item with item number specified
 * by aItemNum out of the total item count specified by aItemCount.  Don't
 * update the current item num or total item count if the corresponding
 * arguments are negative.
 *
 * \param aItemNum              Current item number.  Defaults to -1.
 * \param aItemCount            Current total item count.  Defaults to -1.
 */

void
sbDeviceStatusHelper::ItemStart(PRInt32     aItemNum,
                                PRInt32     aItemCount,
                                PRInt32     aItemType)
{
  // Update current item number and item count.
  mItemNum = aItemNum;
  mItemCount = aItemCount;

  // Update current item type
  mItemType = aItemType;

  // Dispatch operation dependent status processing.
  switch(mOperationType)
  {
    case OPERATION_TYPE_MOUNT :
      LOG(("sbDeviceStatusHelper::ItemStart mount\n"));
      UpdateStatus(NS_LITERAL_STRING("mounting"),
                   NS_LITERAL_STRING("InProgress"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_MOUNTING_PROGRESS,
                  sbNewVariant(NS_ISUPPORTS_CAST(sbIDevice*, mDevice)));
      break;

    case OPERATION_TYPE_WRITE :
      LOG(("sbDeviceStatusHelper::ItemStart write\n"));
      UpdateStatus(NS_LITERAL_STRING("writing"),
                   NS_LITERAL_STRING("InProgress"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_START,
                  sbNewVariant(mMediaItem));
      break;
    case OPERATION_TYPE_DELETE:
      LOG(("sbDeviceStatusHelper::ItemStart delete\n"));
      UpdateStatus(NS_LITERAL_STRING("deleting"),
                   NS_LITERAL_STRING("InProgress"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_PROGRESS,
                  sbNewVariant(mMediaItem));
      break;
    case OPERATION_TYPE_TRANSCODE :
      LOG(("sbDeviceStatusHelper::ItemStart transcode\n"));
      UpdateStatus(NS_LITERAL_STRING("transcoding"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_START,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_READ :
      LOG(("sbDeviceStatusHelper::ItemStart read\n"));
      UpdateStatus(NS_LITERAL_STRING("reading"),
                   NS_LITERAL_STRING("InProgress"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_START,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_DOWNLOAD :
      LOG(("sbDeviceStatusHelper::ItemStart download\n"));
      UpdateStatus(NS_LITERAL_STRING("downloading"),
                   NS_LITERAL_STRING("Starting"),
                   aItemNum,
                   aItemCount,
                   0.0,
                   aItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_START,
                  sbNewVariant(mMediaItem));
      break;

    default :
      break;
  }
}


/**
 * Process the start of item processing of the media item specified by
 * aMediaItem within the media list specified by aMediaList with item number
 * specified by aItemNum out of the total item count specified by aItemCount.
 * Don't update the current item num or total item count if the corresponding
 * arguments are negative.
 *
 * \param aMediaList            Media list.
 * \param aMediaItem            Media item being started.
 * \param aItemNum              Current item number.  Defaults to -1.
 * \param aItemCount            Current total item count.  Defaults to -1.
 */

void
sbDeviceStatusHelper::ItemStart(sbIMediaList* aMediaList,
                                sbIMediaItem* aMediaItem,
                                PRInt32       aItemNum,
                                PRInt32       aItemCount,
                                PRInt32       aItemType)
{
  // Validate arguments.
  NS_ENSURE_TRUE(aMediaItem, /* void */);

  // Update the current media item and list.
  mMediaList = aMediaList;
  mMediaItem = aMediaItem;

  // Apply default status processing.
  ItemStart(aItemNum, aItemCount, aItemType);
}


/**
 * Process the progress of the current item with the progress specified by
 * aProgress.
 *
 * \param aProgress             Progress of current item.
 */

void
sbDeviceStatusHelper::ItemProgress(double aProgress)
{
  // Dispatch operation dependent status processing.
  switch(mOperationType)
  {
    case OPERATION_TYPE_WRITE :
      LOG(("sbDeviceStatusHelper::ItemProgress write\n"));
      UpdateStatus(NS_LITERAL_STRING("writing"),
                   NS_LITERAL_STRING("InProgress"),
                   mItemNum,
                   mItemCount,
                   aProgress,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_PROGRESS,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_TRANSCODE :
      LOG(("sbDeviceStatusHelper::ItemProgress transcode\n"));
      UpdateStatus(NS_LITERAL_STRING("transcoding"),
                   NS_LITERAL_STRING("InProgress"),
                   mItemNum,
                   mItemCount,
                   aProgress,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_PROGRESS,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_READ :
      LOG(("sbDeviceStatusHelper::ItemProcess read\n"));
      UpdateStatus(NS_LITERAL_STRING("reading"),
                   NS_LITERAL_STRING("InProgress"),
                   mItemNum,
                   mItemCount,
                   aProgress,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_PROGRESS,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_DOWNLOAD :
      LOG(("sbDeviceStatusHelper::ItemProgress download\n"));
      UpdateStatus(NS_LITERAL_STRING("downloading"),
                   NS_LITERAL_STRING("InProgress"),
                   mItemNum,
                   mItemCount,
                   aProgress,
                   mItemType);
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_PROGRESS,
                  sbNewVariant(mMediaItem));
      break;

    default :
      break;
  }
}


/**
 * Process the completion of the current item with the result specified by
 * aResult.
 *
 * \param aResult               Completion result of item.
 */

void
sbDeviceStatusHelper::ItemComplete(nsresult aResult)
{
  // Post an error event on a failure result.
  if (NS_FAILED(aResult)) {
    mDevice->CreateAndDispatchEvent
               (sbIDeviceEvent::EVENT_DEVICE_ERROR_UNEXPECTED,
                sbNewVariant(mMediaItem),
                PR_TRUE);
  }

  // Dispatch operation dependent status processing.
  switch(mOperationType)
  {
    case OPERATION_TYPE_TRANSCODE :
    case OPERATION_TYPE_WRITE :
    case OPERATION_TYPE_DOWNLOAD :
      LOG(("sbDeviceStatusHelper::ItemComplete write/transcode/download\n"));
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_END,
                  sbNewVariant(mMediaItem));
      break;

    case OPERATION_TYPE_READ :
      LOG(("sbDeviceStatusHelper::ItemComplete read\n"));
      mDevice->CreateAndDispatchEvent
                 (sbIDeviceEvent::EVENT_DEVICE_TRANSFER_END,
                  sbNewVariant(mMediaItem));
      break;

    default :
      break;
  }
}


//------------------------------------------------------------------------------
//
// Device status helper public services.
//
//------------------------------------------------------------------------------

/**
 * Destroy a device status helper object.
 */

sbDeviceStatusHelper::~sbDeviceStatusHelper()
{
  MOZ_COUNT_DTOR(sbDeviceStatusHelper);
}


/**
 * Initialize the device status helper object.
 */

nsresult
sbDeviceStatusHelper::Initialize()
{
  nsresult rv;

  // Get the device ID and set it up for auto-disposal.
  nsID* deviceID;
  rv = mDevice->GetId(&deviceID);
  NS_ENSURE_SUCCESS(rv, rv);
  sbAutoNSMemPtr autoDeviceID(deviceID);

  // Create the base device status object.
  mStatus =
    do_CreateInstance("@songbirdnest.com/Songbird/Device/DeviceStatus;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Initialize the base status object
  char deviceIDString[NSID_LENGTH];
  deviceID->ToProvidedString(deviceIDString);
  rv = mStatus->Init(NS_ConvertASCIItoUTF16(deviceIDString, NSID_LENGTH-1));
  NS_ENSURE_SUCCESS(rv, rv);

  // Set the state to idle.
  ChangeState(sbIDevice::STATE_IDLE);

  return NS_OK;
}


/**
 * Change the device state to the state specified by aState.
 *
 * \param aState                New device state.
 */

nsresult
sbDeviceStatusHelper::ChangeState(PRUint32 aState)
{
  nsresult rv;

  NS_ENSURE_TRUE(mStatus, NS_ERROR_NOT_INITIALIZED);

  // Get the current state.
  PRUint32 currentState;
  PRUint32 currentSubState;
  rv = mStatus->GetCurrentState(&currentState);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mStatus->GetCurrentSubState(&currentSubState);
  NS_ENSURE_SUCCESS(rv, rv);

  // Reset item status.
  rv = mStatus->SetMediaItem(nsnull);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mStatus->SetMediaList(nsnull);
  NS_ENSURE_SUCCESS(rv, rv);
  mMediaList = nsnull;
  mMediaItem = nsnull;

  // Determine whether to change the main state or the sub-state.
  PRBool changeMainState = PR_TRUE;
  if (aState != sbIDevice::STATE_IDLE) {
    if (currentState == sbIDevice::STATE_SYNCING ||
        currentState == sbIDevice::STATE_CANCEL ||
        (currentState == sbIDevice::STATE_MOUNTING &&
         aState != sbIDevice::STATE_SYNCING)) {
      changeMainState = PR_FALSE;
    }
  }

  // Determine the new main and sub-states.
  PRUint32 state, subState;
  if (changeMainState) {
    state = aState;
    subState = sbIDevice::STATE_IDLE;
  } else {
    state = currentState;
    subState = aState;
  }

  // Log progress.
  LOG(("sbDeviceStatusHelper::ChangeState change state from %s:%s to %s:%s\n",
       DeviceStateString(currentState),
       DeviceStateString(currentSubState),
       DeviceStateString(state),
       DeviceStateString(subState)));

  // Update the device states.
  if (state != currentState) {
    rv = mStatus->SetCurrentState(state);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mDevice->SetState(state);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = mStatus->SetCurrentSubState(subState);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Return in aCurrentStatus the current device status.
 *
 * \param aCurrentStatus        Returned current device status.
 */

nsresult
sbDeviceStatusHelper::GetCurrentStatus(sbIDeviceStatus** aCurrentStatus)
{
  NS_ENSURE_ARG_POINTER(aCurrentStatus);

  NS_IF_ADDREF(*aCurrentStatus = mStatus);
  return NS_OK;
}


/**
 * Update the device status as specified by aOperation, aStateMessage, aItemNum,
 * aItemCount, and aProgress.
 *
 * \param aOperation            Current operation.
 * \param aStateMessage         Current state message.
 * \param aItemNum              Current item number
 * \param aItemCount            Current total item count.
 * \param aProgress             Progress of current item.
 */

nsresult
sbDeviceStatusHelper::UpdateStatus(const nsAString& aOperation,
                                   const nsAString& aStateMessage,
                                   PRInt32          aItemNum,
                                   PRInt32          aItemCount,
                                   double           aProgress,
                                   PRInt32          aItemType)
{
  nsresult rv;

  // Log progress.
  LOG(("sbDeviceStatusHelper::UpdateStatus item: %d, count: %d, progress %f, type %d\n",
       aItemNum, aItemCount, aProgress, aItemType));

  NS_ENSURE_TRUE(mStatus, NS_ERROR_NOT_INITIALIZED);

  // If the current operation has items, update the work item progress and type.
  if (aItemCount > 0) {
    rv = mStatus->SetWorkItemProgress(aItemNum);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mStatus->SetWorkItemProgressEndCount(aItemCount);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mStatus->SetWorkItemType(aItemType);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Update the status.
  rv = mStatus->SetCurrentOperation(aOperation);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mStatus->SetStateMessage(aStateMessage);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mStatus->SetMediaItem(mMediaItem);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mStatus->SetMediaList(mMediaList);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mStatus->SetProgress(aProgress);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

//------------------------------------------------------------------------------
//
// Device status helper private services.
//
//------------------------------------------------------------------------------

/**
 * Construct a device status helper object.
 *
 * \param aDevice               Device for which to construct status.
 */

sbDeviceStatusHelper::sbDeviceStatusHelper(sbBaseDevice* aDevice) :
  mDevice(aDevice),
  mOperationType(OPERATION_TYPE_NONE),
  mItemNum(-1),
  mItemCount(-1)
{
  MOZ_COUNT_CTOR(sbDeviceStatusHelper);
  // Validate arguments.
  NS_ASSERTION(aDevice, "aDevice is null");
}

