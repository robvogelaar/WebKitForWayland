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
#include "InProcessIDBServer.h"

#if ENABLE(INDEXED_DATABASE)

#include "IDBConnectionToClient.h"
#include "IDBConnectionToServer.h"
#include "IDBKeyData.h"
#include "IDBOpenDBRequestImpl.h"
#include "IDBRequestData.h"
#include "IDBResultData.h"
#include "Logging.h"
#include <wtf/RunLoop.h>

namespace WebCore {

Ref<InProcessIDBServer> InProcessIDBServer::create()
{
    Ref<InProcessIDBServer> server = adoptRef(*new InProcessIDBServer);
    server->m_server->registerConnection(server->connectionToClient());
    return WTF::move(server);
}

InProcessIDBServer::InProcessIDBServer()
    : m_server(IDBServer::IDBServer::create())
{
    relaxAdoptionRequirement();
    m_connectionToServer = IDBClient::IDBConnectionToServer::create(*this);
    m_connectionToClient = IDBServer::IDBConnectionToClient::create(*this);
}

uint64_t InProcessIDBServer::identifier() const
{
    // An instance of InProcessIDBServer always has a 1:1 relationship with its instance of IDBServer.
    // Therefore the connection identifier between the two can always be "1".
    return 1;
}

IDBClient::IDBConnectionToServer& InProcessIDBServer::connectionToServer() const
{
    return *m_connectionToServer;
}

IDBServer::IDBConnectionToClient& InProcessIDBServer::connectionToClient() const
{
    return *m_connectionToClient;
}

void InProcessIDBServer::deleteDatabase(IDBRequestData& requestData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, requestData] {
        m_server->deleteDatabase(requestData);
    });
}

void InProcessIDBServer::didDeleteDatabase(const IDBResultData& resultData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resultData] {
        m_connectionToServer->didDeleteDatabase(resultData);
    });
}

void InProcessIDBServer::openDatabase(IDBRequestData& requestData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, requestData] {
        m_server->openDatabase(requestData);
    });
}

void InProcessIDBServer::didOpenDatabase(const IDBResultData& resultData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resultData] {
        m_connectionToServer->didOpenDatabase(resultData);
    });
}

void InProcessIDBServer::didAbortTransaction(const IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, transactionIdentifier, error] {
        m_connectionToServer->didAbortTransaction(transactionIdentifier, error);
    });
}

void InProcessIDBServer::didCommitTransaction(const IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, transactionIdentifier, error] {
        m_connectionToServer->didCommitTransaction(transactionIdentifier, error);
    });
}

void InProcessIDBServer::didCreateObjectStore(const IDBResultData& resultData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resultData] {
        m_connectionToServer->didCreateObjectStore(resultData);
    });
}

void InProcessIDBServer::didPutOrAdd(const IDBResultData& resultData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resultData] {
        m_connectionToServer->didPutOrAdd(resultData);
    });
}

void InProcessIDBServer::didGetRecord(const IDBResultData& resultData)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resultData] {
        m_connectionToServer->didGetRecord(resultData);
    });
}

void InProcessIDBServer::abortTransaction(IDBResourceIdentifier& resourceIdentifier)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resourceIdentifier] {
        m_server->abortTransaction(resourceIdentifier);
    });
}

void InProcessIDBServer::commitTransaction(IDBResourceIdentifier& resourceIdentifier)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resourceIdentifier] {
        m_server->commitTransaction(resourceIdentifier);
    });
}

void InProcessIDBServer::createObjectStore(const IDBRequestData& resultData, const IDBObjectStoreInfo& info)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, resultData, info] {
        m_server->createObjectStore(resultData, info);
    });
}

void InProcessIDBServer::putOrAdd(const IDBRequestData& requestData, IDBKey* key, SerializedScriptValue& value, const IndexedDB::ObjectStoreOverwriteMode overwriteMode)
{
    RefPtr<InProcessIDBServer> self(this);
    IDBKeyData keyData(key);
    auto valueData = ThreadSafeDataBuffer::copyVector(value.data());

    RunLoop::current().dispatch([this, self, requestData, keyData, valueData, overwriteMode] {
        m_server->putOrAdd(requestData, keyData, valueData, overwriteMode);
    });
}

void InProcessIDBServer::getRecord(const IDBRequestData& requestData, IDBKey* key)
{
    RefPtr<InProcessIDBServer> self(this);
    IDBKeyData keyData(key);

    RunLoop::current().dispatch([this, self, requestData, keyData] {
        m_server->getRecord(requestData, keyData);
    });
}

void InProcessIDBServer::fireVersionChangeEvent(IDBServer::UniqueIDBDatabaseConnection& connection, uint64_t requestedVersion)
{
    RefPtr<InProcessIDBServer> self(this);
    uint64_t databaseConnectionIdentifier = connection.identifier();
    RunLoop::current().dispatch([this, self, databaseConnectionIdentifier, requestedVersion] {
        m_connectionToServer->fireVersionChangeEvent(databaseConnectionIdentifier, requestedVersion);
    });
}

void InProcessIDBServer::databaseConnectionClosed(uint64_t databaseConnectionIdentifier)
{
    RefPtr<InProcessIDBServer> self(this);
    RunLoop::current().dispatch([this, self, databaseConnectionIdentifier] {
        m_server->databaseConnectionClosed(databaseConnectionIdentifier);
    });
}

} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
