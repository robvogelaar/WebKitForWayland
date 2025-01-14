/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "IDBTransactionImpl.h"

#if ENABLE(INDEXED_DATABASE)

#include "DOMError.h"
#include "EventQueue.h"
#include "IDBDatabaseImpl.h"
#include "IDBError.h"
#include "IDBEventDispatcher.h"
#include "IDBObjectStore.h"
#include "IDBRequestImpl.h"
#include "IDBResultData.h"
#include "Logging.h"
#include "ScriptExecutionContext.h"
#include "TransactionOperation.h"

namespace WebCore {
namespace IDBClient {

Ref<IDBTransaction> IDBTransaction::create(IDBDatabase& database, const IDBTransactionInfo& info)
{
    return adoptRef(*new IDBTransaction(database, info));
}

IDBTransaction::IDBTransaction(IDBDatabase& database, const IDBTransactionInfo& info)
    : WebCore::IDBTransaction(database.scriptExecutionContext())
    , m_database(database)
    , m_info(info)
    , m_operationTimer(*this, &IDBTransaction::operationTimerFired)

{
    m_activationTimer = std::make_unique<Timer>(*this, &IDBTransaction::activationTimerFired);
    m_activationTimer->startOneShot(0);

    if (m_info.mode() == IndexedDB::TransactionMode::VersionChange)
        m_originalDatabaseInfo = std::make_unique<IDBDatabaseInfo>(m_database->info());

    suspendIfNeeded();
    m_state = IndexedDB::TransactionState::Inactive;
}

IDBTransaction::~IDBTransaction()
{
}

const String& IDBTransaction::mode() const
{
    switch (m_info.mode()) {
    case IndexedDB::TransactionMode::ReadOnly:
        return IDBTransaction::modeReadOnly();
    case IndexedDB::TransactionMode::ReadWrite:
        return IDBTransaction::modeReadWrite();
    case IndexedDB::TransactionMode::VersionChange:
        return IDBTransaction::modeVersionChange();
    }

    RELEASE_ASSERT_NOT_REACHED();
}

WebCore::IDBDatabase* IDBTransaction::db()
{
    return &m_database.get();
}

IDBConnectionToServer& IDBTransaction::serverConnection()
{
    return m_database->serverConnection();
}

RefPtr<DOMError> IDBTransaction::error() const
{
    ASSERT_NOT_REACHED();
    return nullptr;
}

RefPtr<WebCore::IDBObjectStore> IDBTransaction::objectStore(const String& objectStoreName, ExceptionCode& ec)
{
    LOG(IndexedDB, "IDBTransaction::objectStore");

    if (objectStoreName.isEmpty()) {
        ec = NOT_FOUND_ERR;
        return nullptr;
    }

    if (isFinishedOrFinishing()) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    auto iterator = m_referencedObjectStores.find(objectStoreName);
    if (iterator != m_referencedObjectStores.end())
        return iterator->value;

    bool found = false;
    for (auto& objectStore : m_info.objectStores()) {
        if (objectStore == objectStoreName) {
            found = true;
            break;
        }
    }

    auto* info = m_database->info().infoForExistingObjectStore(objectStoreName);
    if (!info) {
        ec = NOT_FOUND_ERR;
        return nullptr;
    }

    // Version change transactions are scoped to every object store in the database.
    if (!found && !isVersionChange()) {
        ec = NOT_FOUND_ERR;
        return nullptr;
    }

    auto objectStore = IDBObjectStore::create(*info, *this);
    m_referencedObjectStores.set(objectStoreName, &objectStore.get());

    return adoptRef(&objectStore.leakRef());
}

void IDBTransaction::abort(ExceptionCode& ec)
{
    LOG(IndexedDB, "IDBTransaction::abort");

    if (isFinishedOrFinishing()) {
        ec = INVALID_STATE_ERR;
        return;
    }

    m_state = IndexedDB::TransactionState::Aborting;

    m_database->abortTransaction(*this);
}

const char* IDBTransaction::activeDOMObjectName() const
{
    return "IDBTransaction";
}

bool IDBTransaction::canSuspendForPageCache() const
{
    return false;
}

bool IDBTransaction::hasPendingActivity() const
{
    if (m_state == IndexedDB::TransactionState::Inactive)
        return !m_transactionOperationQueue.isEmpty() || !m_transactionOperationMap.isEmpty();

    return m_state != IndexedDB::TransactionState::Finished;
}

bool IDBTransaction::isActive() const
{
    return m_state == IndexedDB::TransactionState::Active;
}

bool IDBTransaction::isFinishedOrFinishing() const
{
    return m_state == IndexedDB::TransactionState::Committing
        || m_state == IndexedDB::TransactionState::Aborting
        || m_state == IndexedDB::TransactionState::Finished;
}

void IDBTransaction::activationTimerFired()
{
    scheduleOperationTimer();
    m_activationTimer = nullptr;
}

void IDBTransaction::scheduleOperation(RefPtr<TransactionOperation>&& operation)
{
    ASSERT(!m_transactionOperationMap.contains(operation->identifier()));

    m_transactionOperationQueue.append(operation);
    m_transactionOperationMap.set(operation->identifier(), WTF::move(operation));

    scheduleOperationTimer();
}

void IDBTransaction::scheduleOperationTimer()
{
    if (!m_operationTimer.isActive())
        m_operationTimer.startOneShot(0);
}

void IDBTransaction::operationTimerFired()
{
    LOG(IndexedDB, "IDBTransaction::operationTimerFired");

    if (m_state == IndexedDB::TransactionState::Unstarted)
        return;

    if (!m_transactionOperationQueue.isEmpty()) {
        auto operation = m_transactionOperationQueue.takeFirst();
        operation->perform();

        return;
    }

    if (!isFinishedOrFinishing())
        commit();
}

void IDBTransaction::commit()
{
    LOG(IndexedDB, "IDBTransaction::commit");

    if (isFinishedOrFinishing())
        return;

    m_state = IndexedDB::TransactionState::Committing;

    m_database->commitTransaction(*this);
}

void IDBTransaction::finishAbortOrCommit()
{
    ASSERT(m_state != IndexedDB::TransactionState::Finished);
    m_state = IndexedDB::TransactionState::Finished;

    m_originalDatabaseInfo = nullptr;
}

void IDBTransaction::didAbort(const IDBError& error)
{
    LOG(IndexedDB, "IDBTransaction::didAbort");

    if (m_state == IndexedDB::TransactionState::Finished)
        return;

    m_database->didAbortTransaction(*this);

    m_idbError = error;
    fireOnAbort();

    finishAbortOrCommit();
}

void IDBTransaction::didCommit(const IDBError& error)
{
    LOG(IndexedDB, "IDBTransaction::didCommit");

    ASSERT(m_state == IndexedDB::TransactionState::Committing);

    if (error.isNull()) {
        m_database->didCommitTransaction(*this);
        fireOnComplete();
    } else {
        m_database->didAbortTransaction(*this);
        m_idbError = error;
        fireOnAbort();
    }

    finishAbortOrCommit();
}

void IDBTransaction::fireOnComplete()
{
    LOG(IndexedDB, "IDBTransaction::fireOnComplete");
    enqueueEvent(Event::create(eventNames().completeEvent, false, false));
}

void IDBTransaction::fireOnAbort()
{
    LOG(IndexedDB, "IDBTransaction::fireOnAbort");
    enqueueEvent(Event::create(eventNames().abortEvent, true, false));
}

void IDBTransaction::enqueueEvent(Ref<Event> event)
{
    ASSERT(m_state != IndexedDB::TransactionState::Finished);

    if (!scriptExecutionContext())
        return;

    event->setTarget(this);
    scriptExecutionContext()->eventQueue().enqueueEvent(&event.get());
}

bool IDBTransaction::dispatchEvent(PassRefPtr<Event> event)
{
    LOG(IndexedDB, "IDBTransaction::dispatchEvent");

    ASSERT(scriptExecutionContext());
    ASSERT(event->target() == this);
    ASSERT(event->type() == eventNames().completeEvent || event->type() == eventNames().abortEvent);

    Vector<RefPtr<EventTarget>> targets;
    targets.append(this);
    targets.append(db());

    return IDBEventDispatcher::dispatch(event.get(), targets);
}

Ref<IDBObjectStore> IDBTransaction::createObjectStore(const IDBObjectStoreInfo& info)
{
    LOG(IndexedDB, "IDBTransaction::createObjectStore");
    ASSERT(isVersionChange());

    Ref<IDBObjectStore> objectStore = IDBObjectStore::create(info, *this);

    auto operation = createTransactionOperation(*this, &IDBTransaction::didCreateObjectStoreOnServer, &IDBTransaction::createObjectStoreOnServer, info);
    scheduleOperation(WTF::move(operation));

    return WTF::move(objectStore);
}

void IDBTransaction::createObjectStoreOnServer(TransactionOperation& operation, const IDBObjectStoreInfo& info)
{
    LOG(IndexedDB, "IDBTransaction::createObjectStoreOnServer");

    ASSERT(isVersionChange());

    if (isFinishedOrFinishing())
        return;

    m_database->serverConnection().createObjectStore(operation, info);
}

void IDBTransaction::didCreateObjectStoreOnServer(const IDBResultData& resultData)
{
    LOG(IndexedDB, "IDBTransaction::didCreateObjectStoreOnServer");

    ASSERT_UNUSED(resultData, resultData.type() == IDBResultType::CreateObjectStoreSuccess);

    scheduleOperationTimer();
}

Ref<IDBRequest> IDBTransaction::requestGetRecord(ScriptExecutionContext& context, IDBObjectStore& objectStore, IDBKey& key)
{
    LOG(IndexedDB, "IDBTransaction::requestPutOrAdd");
    ASSERT(isActive());
    ASSERT(key.isValid());

    Ref<IDBRequest> request = IDBRequest::create(context, objectStore, *this);

    auto operation = createTransactionOperation(*this, request.get(), &IDBTransaction::didGetRecordOnServer, &IDBTransaction::getRecordOnServer, &key);
    scheduleOperation(WTF::move(operation));

    return WTF::move(request);
}

void IDBTransaction::getRecordOnServer(TransactionOperation& operation, RefPtr<IDBKey> key)
{
    LOG(IndexedDB, "IDBTransaction::getRecordOnServer");

    ASSERT(!isFinishedOrFinishing());

    serverConnection().getRecord(operation, key);
}

void IDBTransaction::didGetRecordOnServer(IDBRequest& request, const IDBResultData& resultData)
{
    LOG(IndexedDB, "IDBTransaction::didGetRecordOnServer");

    request.setResultToStructuredClone(resultData.resultData());
    request.requestCompleted(resultData);
}

Ref<IDBRequest> IDBTransaction::requestPutOrAdd(ScriptExecutionContext& context, IDBObjectStore& objectStore, IDBKey* key, SerializedScriptValue& value, IndexedDB::ObjectStoreOverwriteMode overwriteMode)
{
    LOG(IndexedDB, "IDBTransaction::requestPutOrAdd");
    ASSERT(isActive());
    ASSERT(!isReadOnly());
    ASSERT(objectStore.info().autoIncrement() || key);

    Ref<IDBRequest> request = IDBRequest::create(context, objectStore, *this);

    auto operation = createTransactionOperation(*this, request.get(), &IDBTransaction::didPutOrAddOnServer, &IDBTransaction::putOrAddOnServer, key, &value, overwriteMode);
    scheduleOperation(WTF::move(operation));

    return WTF::move(request);
}

void IDBTransaction::putOrAddOnServer(TransactionOperation& operation, RefPtr<IDBKey> key, RefPtr<SerializedScriptValue> value, const IndexedDB::ObjectStoreOverwriteMode& overwriteMode)
{
    LOG(IndexedDB, "IDBTransaction::putOrAddOnServer");

    ASSERT(!isFinishedOrFinishing());
    ASSERT(!isReadOnly());

    serverConnection().putOrAdd(operation, key, value, overwriteMode);
}

void IDBTransaction::didPutOrAddOnServer(IDBRequest& request, const IDBResultData& resultData)
{
    LOG(IndexedDB, "IDBTransaction::didPutOrAddOnServer");

    request.setResult(resultData.resultKey());
    request.requestCompleted(resultData);
}

void IDBTransaction::activate()
{
    ASSERT(m_state == IndexedDB::TransactionState::Unstarted || m_state == IndexedDB::TransactionState::Inactive);
    m_state = IndexedDB::TransactionState::Active;
}

void IDBTransaction::deactivate()
{
    if (m_state == IndexedDB::TransactionState::Unstarted || m_state == IndexedDB::TransactionState::Active)
        m_state = IndexedDB::TransactionState::Inactive;

    scheduleOperationTimer();
}

} // namespace IDBClient
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
