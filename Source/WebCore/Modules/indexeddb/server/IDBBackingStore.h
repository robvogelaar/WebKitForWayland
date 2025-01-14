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

#ifndef IDBBackingStore_h
#define IDBBackingStore_h

#if ENABLE(INDEXED_DATABASE)

#include "IDBDatabaseInfo.h"
#include "IDBError.h"

namespace WebCore {

class IDBKeyData;
class IDBObjectStoreInfo;
class IDBResourceIdentifier;
class IDBTransactionInfo;
class ThreadSafeDataBuffer;

namespace IDBServer {

class IDBBackingStore {
public:
    virtual ~IDBBackingStore() { }

    virtual const IDBDatabaseInfo& getOrEstablishDatabaseInfo() = 0;

    virtual IDBError beginTransaction(const IDBTransactionInfo&) = 0;
    virtual IDBError abortTransaction(const IDBResourceIdentifier& transactionIdentifier) = 0;
    virtual IDBError commitTransaction(const IDBResourceIdentifier& transactionIdentifier) = 0;

    virtual IDBError createObjectStore(const IDBResourceIdentifier& transactionIdentifier, const IDBObjectStoreInfo&) = 0;
    virtual IDBError keyExistsInObjectStore(const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyData&, bool& keyExists) = 0;
    virtual IDBError deleteRecord(const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyData&) = 0;
    virtual IDBError putRecord(const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyData&, const ThreadSafeDataBuffer& value) = 0;
    virtual IDBError getRecord(const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyData&, ThreadSafeDataBuffer& outValue) = 0;
};

} // namespace IDBServer
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
#endif // IDBBackingStore_h
