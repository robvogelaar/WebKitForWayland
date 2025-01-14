#!/usr/bin/env python
#
# Copyright (c) 2015 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

import logging
import re
import os

log = logging.getLogger('global')

_FRAMEWORK_CONFIG_MAP = {
    "JavaScriptCore": {
        "macro_prefix": "JSC",
        "namespace": "JSC",
    },
    "WebCore": {
        "macro_prefix": "WEBCORE",
        "namespace": "WebCore",
    },
}

functionHeadRegExp = re.compile(r"(?:function|constructor)\s+\w+\s*\(.*?\)", re.MULTILINE | re.S)
functionNameRegExp = re.compile(r"(?:function|constructor)\s+(\w+)\s*\(", re.MULTILINE | re.S)
functionIsConstructorRegExp = re.compile(r"^constructor", re.MULTILINE | re.S)
functionParameterFinder = re.compile(r"^(?:function|constructor)\s+(?:\w+)\s*\(((?:\s*\w+)?\s*(?:\s*,\s*\w+)*)?\s*\)", re.MULTILINE | re.S)

multilineCommentRegExp = re.compile(r"\/\*.*?\*\/", re.MULTILINE | re.S)
singleLineCommentRegExp = re.compile(r"\/\/.*?\n", re.MULTILINE | re.S)


class ParseException(Exception):
    pass


class Framework:
    def __init__(self, name):
        self._settings = _FRAMEWORK_CONFIG_MAP[name]
        self.name = name

    def setting(self, key, default=''):
        return self._settings.get(key, default)

    @staticmethod
    def fromString(frameworkString):
        if frameworkString == "JavaScriptCore":
            return Frameworks.JavaScriptCore

        if frameworkString == "WebCore":
            return Frameworks.WebCore

        raise ParseException("Unknown framework: %s" % frameworkString)


class Frameworks:
    JavaScriptCore = Framework("JavaScriptCore")
    WebCore = Framework("WebCore")


class BuiltinObject:
    def __init__(self, object_name, functions):
        self.object_name = object_name
        self.functions = functions
        self.collection = None  # Set by the owning BuiltinsCollection

        for function in self.functions:
            function.object = self


class BuiltinFunction:
    def __init__(self, function_name, function_source, is_constructor, parameters):
        self.function_name = function_name
        self.function_source = function_source
        self.is_constructor = is_constructor
        self.parameters = parameters
        self.object = None  # Set by the owning BuiltinObject

    @staticmethod
    def fromString(function_string):
        function_source = multilineCommentRegExp.sub("", function_string)
        function_name = functionNameRegExp.findall(function_source)[0]
        is_constructor = functionIsConstructorRegExp.match(function_source) != None
        parameters = [s.strip() for s in functionParameterFinder.findall(function_source)[0].split(',')]
        if len(parameters[0]) == 0:
            parameters = []

        return BuiltinFunction(function_name, function_source, is_constructor, parameters)

    def __str__(self):
        interface = "%s(%s)" % (self.function_name, ', '.join(self.parameters))
        if self.is_constructor:
            interface = interface + " [Constructor]"

        return interface


class BuiltinsCollection:
    def __init__(self, framework_name):
        self._copyright_lines = set()
        self.objects = []
        self.framework = Framework.fromString(framework_name)
        log.debug("Created new Builtins collection.")

    def parse_builtins_file(self, filename, text):
        log.debug("Parsing builtins file: %s" % filename)

        parsed_copyrights = set(self._parse_copyright_lines(text))
        self._copyright_lines = self._copyright_lines.union(parsed_copyrights)

        log.debug("Found copyright lines:")
        for line in self._copyright_lines:
            log.debug(line)
        log.debug("")

        object_name, ext = os.path.splitext(os.path.basename(filename))
        log.debug("Parsing object: %s" % object_name)

        parsed_functions = self._parse_functions(text)
        for function in parsed_functions:
            function.object = object_name

        log.debug("Parsed functions:")
        for func in parsed_functions:
            log.debug(func)
        log.debug("")

        new_object = BuiltinObject(object_name, parsed_functions)
        new_object.collection = self
        self.objects.append(new_object)

    def copyrights(self):
        owner_to_years = dict()
        copyrightYearRegExp = re.compile(r"(\d{4})[, ]{0,2}")
        ownerStartRegExp = re.compile(r"[^\d, ]")

        # Returns deduplicated copyrights keyed on the owner.
        for line in self._copyright_lines:
            years = set(copyrightYearRegExp.findall(line))
            ownerIndex = ownerStartRegExp.search(line).start()
            owner = line[ownerIndex:]
            log.debug("Found years: %s and owner: %s" % (years, owner))
            if owner not in owner_to_years:
                owner_to_years[owner] = set()

            owner_to_years[owner] = owner_to_years[owner].union(years)

        result = []

        for owner, years in owner_to_years.items():
            sorted_years = list(years)
            sorted_years.sort()
            result.append("%s %s" % (', '.join(sorted_years), owner))

        return result

    def all_functions(self):
        result = []
        for object in self.objects:
            result.extend(object.functions)

        result.sort()
        return result

    # Private methods.

    def _parse_copyright_lines(self, text):
        licenseBlock = multilineCommentRegExp.findall(text)[0]
        licenseBlock = licenseBlock[:licenseBlock.index("Redistribution")]

        copyrightLines = []
        for line in licenseBlock.split("\n"):
            line = line.replace("/*", "")
            line = line.replace("*/", "")
            line = line.replace("*", "")
            line = line.replace("Copyright", "")
            line = line.replace("copyright", "")
            line = line.replace("(C)", "")
            line = line.replace("(c)", "")
            line = line.strip()

            if len(line) == 0:
                continue

            copyrightLines.append(line)

        return copyrightLines

    def _parse_functions(self, text):
        text = multilineCommentRegExp.sub("/**/", singleLineCommentRegExp.sub("//\n", text))

        matches = [func for func in functionHeadRegExp.finditer(text)]
        functionBounds = []
        start = 0
        end = 0
        for match in matches:
            start = match.start()
            if start < end:
                continue
            end = match.end()
            while text[end] != '{':
                end = end + 1
            depth = 1
            end = end + 1
            while depth > 0:
                if text[end] == '{':
                    depth = depth + 1
                elif text[end] == '}':
                    depth = depth - 1
                end = end + 1
            functionBounds.append((start, end))

        functionStrings = [text[start:end].strip() for (start, end) in functionBounds]
        return map(BuiltinFunction.fromString, functionStrings)
