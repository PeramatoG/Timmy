/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/core/resource_path_resolver.h"

#include <cstdlib>
#include <system_error>
#include <utility>

namespace timmy::core {

    namespace {

        // Reads a process environment value without interpreting its encoding.
        std::optional<std::filesystem::path> processEnvironment(std::string_view name)
        {
#ifdef _WIN32
            const std::wstring wideName(name.begin(), name.end());
            if (const wchar_t* value = _wgetenv(wideName.c_str());
                value != nullptr && value[0] != L'\0')
            {
                return std::filesystem::path(value);
            }
#else
            const std::string ownedName(name);
            if (const char* value = std::getenv(ownedName.c_str());
                value != nullptr && value[0] != '\0')
            {
                return std::filesystem::path(value);
            }
#endif
            return std::nullopt;
        }

        // Converts a native filesystem path to portable UTF-8 diagnostic text.
        std::string diagnosticPath(const std::filesystem::path& path)
        {
            const auto utf8 = path.generic_u8string();
            return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
        }

        // Checks a candidate without throwing on inaccessible filesystem entries.
        bool resourceExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return !path.empty() && std::filesystem::is_regular_file(path, error);
        }

        // Returns a normalized absolute path when the filesystem permits it.
        std::filesystem::path usablePath(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(path, error);
            return error ? path.lexically_normal() : absolute.lexically_normal();
        }

    } // namespace

    // Resolves an explicit override, supplied file, or executable-relative resource.
    ResourcePathResult resolveResourcePath(
        const ResourcePathRequest& request,
        EnvironmentLookup environmentLookup)
    {
        if (!environmentLookup)
        {
            environmentLookup = processEnvironment;
        }

        if (const auto overrideValue =
                environmentLookup(request.environmentVariable))
        {
            const std::filesystem::path candidate(*overrideValue);
            if (resourceExists(candidate))
            {
                return { usablePath(candidate), true, {} };
            }
            return {
                candidate,
                false,
                "Resource from " + std::string(request.environmentVariable)
                    + " was not found: " + diagnosticPath(candidate)
            };
        }

        if (resourceExists(request.suppliedPath))
        {
            return { usablePath(request.suppliedPath), true, {} };
        }

        auto directory = request.executablePath.parent_path();
        while (!directory.empty())
        {
            const auto candidate = directory / request.developmentRelativePath;
            if (resourceExists(candidate))
            {
                return { usablePath(candidate), true, {} };
            }
            const auto parent = directory.parent_path();
            if (parent == directory)
            {
                break;
            }
            directory = parent;
        }

        const auto missing = request.suppliedPath.empty()
            ? request.developmentRelativePath
            : request.suppliedPath;
        return {
            missing,
            false,
            "Resource was not found: " + diagnosticPath(missing)
        };
    }

} // namespace timmy::core
