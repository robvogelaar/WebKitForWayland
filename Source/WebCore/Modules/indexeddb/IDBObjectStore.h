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

#ifndef IDBObjectStore_h
#define IDBObjectStore_h

#include "Dictionary.h"
#include "ExceptionCode.h"
#include "ScriptWrappable.h"
#include <wtf/RefCounted.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

#if ENABLE(INDEXED_DATABASE)

namespace Deprecated {
class ScriptValue;
}

namespace JSC {
class ExecState;
}

namespace WebCore {

class DOMStringList;
class IDBAny;
class IDBIndex;
class IDBKeyPath;
class IDBKeyRange;
class IDBRequest;
class IDBTransaction;
class ScriptExecutionContext;

class IDBObjectStore : public RefCounted<IDBObjectStore> {
public:
    virtual ~IDBObjectStore() { }

    // Implement the IDBObjectStore IDL
    virtual int64_t id() const = 0;
    virtual const String name() const = 0;
    virtual RefPtr<IDBAny> keyPathAny() const = 0;
    virtual const IDBKeyPath keyPath() const = 0;
    virtual RefPtr<DOMStringList> indexNames() const = 0;
    virtual RefPtr<IDBTransaction> transaction() const = 0;
    virtual bool autoIncrement() const = 0;

    virtual RefPtr<IDBRequest> add(JSC::ExecState&, Deprecated::ScriptValue&, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> put(JSC::ExecState&, Deprecated::ScriptValue&, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> openCursor(ScriptExecutionContext*, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> openCursor(ScriptExecutionContext*, IDBKeyRange*, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> openCursor(ScriptExecutionContext*, const Deprecated::ScriptValue& key, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> openCursor(ScriptExecutionContext*, IDBKeyRange*, const String& direction, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> openCursor(ScriptExecutionContext*, const Deprecated::ScriptValue& key, const String& direction, ExceptionCode&) = 0;

    virtual RefPtr<IDBRequest> get(ScriptExecutionContext*, const Deprecated::ScriptValue& key, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> get(ScriptExecutionContext*, IDBKeyRange*, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> add(JSC::ExecState&, Deprecated::ScriptValue&, const Deprecated::ScriptValue& key, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> put(JSC::ExecState&, Deprecated::ScriptValue&, const Deprecated::ScriptValue& key, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> deleteFunction(ScriptExecutionContext*, IDBKeyRange*, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> deleteFunction(ScriptExecutionContext*, const Deprecated::ScriptValue& key, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> clear(ScriptExecutionContext*, ExceptionCode&) = 0;

    virtual RefPtr<IDBIndex> createIndex(ScriptExecutionContext*, const String& name, const IDBKeyPath&, bool unique, bool multiEntry, ExceptionCode&) = 0;

    virtual RefPtr<IDBIndex> index(const String& name, ExceptionCode&) = 0;
    virtual void deleteIndex(const String& name, ExceptionCode&) = 0;

    virtual RefPtr<IDBRequest> count(ScriptExecutionContext*, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> count(ScriptExecutionContext*, IDBKeyRange*, ExceptionCode&) = 0;
    virtual RefPtr<IDBRequest> count(ScriptExecutionContext*, const Deprecated::ScriptValue& key, ExceptionCode&) = 0;

protected:
    IDBObjectStore();
};

} // namespace WebCore

#endif

#endif // IDBObjectStore_h
