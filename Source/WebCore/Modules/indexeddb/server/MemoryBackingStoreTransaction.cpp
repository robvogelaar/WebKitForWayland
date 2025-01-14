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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MemoryBackingStoreTransaction.h"

#if ENABLE(INDEXED_DATABASE)

#include "IndexedDB.h"
#include "Logging.h"
#include "MemoryIDBBackingStore.h"
#include "MemoryObjectStore.h"
#include <wtf/TemporaryChange.h>

namespace WebCore {
namespace IDBServer {

std::unique_ptr<MemoryBackingStoreTransaction> MemoryBackingStoreTransaction::create(MemoryIDBBackingStore& backingStore, const IDBTransactionInfo& info)
{
    return std::make_unique<MemoryBackingStoreTransaction>(backingStore, info);
}

MemoryBackingStoreTransaction::MemoryBackingStoreTransaction(MemoryIDBBackingStore& backingStore, const IDBTransactionInfo& info)
    : m_backingStore(backingStore)
    , m_info(info)
{
    if (m_info.mode() == IndexedDB::TransactionMode::VersionChange)
        m_originalDatabaseInfo = std::make_unique<IDBDatabaseInfo>(m_backingStore.getOrEstablishDatabaseInfo());
}

MemoryBackingStoreTransaction::~MemoryBackingStoreTransaction()
{
    ASSERT(!m_inProgress);
}

void MemoryBackingStoreTransaction::addNewObjectStore(MemoryObjectStore& objectStore)
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::addNewObjectStore()");

    ASSERT(isVersionChange());

    ASSERT(!m_objectStores.contains(&objectStore));
    m_objectStores.add(&objectStore);
    m_versionChangeAddedObjectStores.add(&objectStore);

    objectStore.writeTransactionStarted(*this);
}

void MemoryBackingStoreTransaction::addExistingObjectStore(MemoryObjectStore& objectStore)
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::addExistingObjectStore");

    ASSERT(isWriting());

    ASSERT(!m_objectStores.contains(&objectStore));
    m_objectStores.add(&objectStore);

    objectStore.writeTransactionStarted(*this);
}

void MemoryBackingStoreTransaction::recordValueChanged(MemoryObjectStore& objectStore, const IDBKeyData& key)
{
    ASSERT(m_objectStores.contains(&objectStore));

    if (m_isAborting)
        return;

    auto originalAddResult = m_originalValues.add(&objectStore, nullptr);
    if (originalAddResult.isNewEntry)
        originalAddResult.iterator->value = std::make_unique<KeyValueMap>();

    auto* map = originalAddResult.iterator->value.get();

    auto addResult = map->add(key, ThreadSafeDataBuffer());
    if (!addResult.isNewEntry)
        return;

    addResult.iterator->value = objectStore.valueForKey(key);
}

void MemoryBackingStoreTransaction::abort()
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::abort()");

    TemporaryChange<bool> change(m_isAborting, true);

    if (m_originalDatabaseInfo) {
        ASSERT(m_info.mode() == IndexedDB::TransactionMode::VersionChange);
        m_backingStore.setDatabaseInfo(*m_originalDatabaseInfo);
    }

    for (auto objectStore : m_objectStores) {
        auto keyValueMap = m_originalValues.get(objectStore);
        if (!keyValueMap)
            continue;

        for (auto entry : *keyValueMap) {
            objectStore->deleteRecord(entry.key);
            objectStore->setKeyValue(entry.key, entry.value);
        }

        m_originalValues.remove(objectStore);
    }


    finish();

    for (auto objectStore : m_versionChangeAddedObjectStores)
        m_backingStore.removeObjectStoreForVersionChangeAbort(*objectStore);
}

void MemoryBackingStoreTransaction::commit()
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::commit()");

    finish();
}

void MemoryBackingStoreTransaction::finish()
{
    m_inProgress = false;

    if (!isWriting())
        return;

    for (auto objectStore : m_objectStores)
        objectStore->writeTransactionFinished(*this);
}

} // namespace IDBServer
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
