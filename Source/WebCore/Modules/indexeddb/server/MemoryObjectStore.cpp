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
#include "MemoryObjectStore.h"

#if ENABLE(INDEXED_DATABASE)

#include "Logging.h"
#include "MemoryBackingStoreTransaction.h"

namespace WebCore {
namespace IDBServer {

std::unique_ptr<MemoryObjectStore> MemoryObjectStore::create(const IDBObjectStoreInfo& info)
{
    return std::make_unique<MemoryObjectStore>(info);
}

MemoryObjectStore::MemoryObjectStore(const IDBObjectStoreInfo& info)
    : m_info(info)
{
}

MemoryObjectStore::~MemoryObjectStore()
{
    ASSERT(!m_writeTransaction);
}

void MemoryObjectStore::writeTransactionStarted(MemoryBackingStoreTransaction& transaction)
{
    LOG(IndexedDB, "MemoryObjectStore::writeTransactionStarted");

    ASSERT(!m_writeTransaction);
    m_writeTransaction = &transaction;
}

void MemoryObjectStore::writeTransactionFinished(MemoryBackingStoreTransaction& transaction)
{
    LOG(IndexedDB, "MemoryObjectStore::writeTransactionFinished");

    ASSERT_UNUSED(transaction, m_writeTransaction == &transaction);
    m_writeTransaction = nullptr;
}

bool MemoryObjectStore::containsRecord(const IDBKeyData& key)
{
    if (!m_keyValueStore)
        return false;

    return m_keyValueStore->contains(key);
}

void MemoryObjectStore::deleteRecord(const IDBKeyData& key)
{
    LOG(IndexedDB, "MemoryObjectStore::deleteRecord");

    ASSERT(m_writeTransaction);
    m_writeTransaction->recordValueChanged(*this, key);

    if (!m_keyValueStore)
        return;

    m_keyValueStore->remove(key);
    if (m_orderedKeys)
        m_orderedKeys->erase(key);
}

void MemoryObjectStore::putRecord(MemoryBackingStoreTransaction& transaction, const IDBKeyData& keyData, const ThreadSafeDataBuffer& value)
{
    LOG(IndexedDB, "MemoryObjectStore::putRecord");

    ASSERT(m_writeTransaction);
    ASSERT_UNUSED(transaction, m_writeTransaction == &transaction);

    m_writeTransaction->recordValueChanged(*this, keyData);

    setKeyValue(keyData, value);
}

void MemoryObjectStore::setKeyValue(const IDBKeyData& keyData, const ThreadSafeDataBuffer& value)
{
    if (!m_keyValueStore)
        m_keyValueStore = std::make_unique<KeyValueMap>();

    auto result = m_keyValueStore->set(keyData, value);
    if (result.isNewEntry && m_orderedKeys)
        m_orderedKeys->insert(keyData);
}

ThreadSafeDataBuffer MemoryObjectStore::valueForKey(const IDBKeyData& keyData) const
{
    LOG(IndexedDB, "MemoryObjectStore::valueForKey");

    if (!m_keyValueStore)
        return ThreadSafeDataBuffer();

    return m_keyValueStore->get(keyData);
}

} // namespace IDBServer
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
