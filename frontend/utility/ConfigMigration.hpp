#pragma once

#include <cctype>
#include <filesystem>
#include <functional>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

enum class ConfigMigrationResult { NoSource, Existing, Imported };

// Browser caches, cookies and session storage carry no user settings and are
// often held open by another application (the CEF browser). Skipping them
// keeps the import fast and lets it succeed while those files are locked.
// Only paths under `plugin_config` are treated as volatile; everything else,
// including `global.ini`, must copy or the import fails.
inline bool IsVolatileConfigPath(const std::filesystem::path &relative)
{
	auto lower = [](const std::string &value) {
		std::string out;
		out.reserve(value.size());
		for (const unsigned char c : value) {
			out += static_cast<char>(std::tolower(c));
		}
		return out;
	};
	if (relative.empty()) {
		return false;
	}
	auto begin = relative.begin();
	const auto first = lower(begin->generic_u8string());
	if (first != "plugin_config") {
		return false;
	}
	static const char *const kVolatileComponents[] = {
		"cache",        "cache_data",      "code cache",   "gpucache",        "dawncache",
		"grshadercache",	"shadercache",     "service worker", "session storage", "indexeddb",
		"blob_storage", 	"crashpad",
	};
	static const char *const kVolatileFiles[] = {
		"cookies", "cookies-journal", "singletonlock", "singletonsocket", "singletoncookie", "lockfile",
	};
	for (auto it = std::next(begin); it != relative.end(); ++it) {
		const auto name = lower(it->generic_u8string());
		for (const auto *const volatileDir : kVolatileComponents) {
			if (name == volatileDir) {
				return true;
			}
		}
	}
	const auto last = lower(relative.filename().generic_u8string());
	for (const auto *const volatileFile : kVolatileFiles) {
		if (last == volatileFile) {
			return true;
		}
	}
	return false;
}

// The platform pointer API may append a separator to the config base. Keep
// default Locations values stable without changing independently chosen roots.
inline std::filesystem::path ConfigBaseDirectory(const char *root)
{
	auto base = std::filesystem::u8path(root);
	if (base.has_relative_path() && base.filename().empty()) {
		base = base.parent_path();
	}
	return base;
}

inline void CopyConfigSnapshot(const std::filesystem::path &source, const std::filesystem::path &destination,
			       std::set<std::filesystem::path> &active, std::vector<std::filesystem::path> &skipped,
			       const std::filesystem::path &relative = {})
{
	namespace fs = std::filesystem;
	const auto canonical = fs::canonical(source);
	const auto target = fs::weakly_canonical(destination);
	const auto relativeOut = target.lexically_relative(canonical);
	if (!relativeOut.empty() && *relativeOut.begin() != "..") {
		throw std::runtime_error("Configuration copy destination is inside its source");
	}
	if (!relative.empty() && IsVolatileConfigPath(relative)) {
		skipped.push_back(relative);
		return;
	}
	if (fs::is_directory(source)) {
		if (!active.insert(canonical).second) {
			throw std::runtime_error("Configuration contains a directory symlink cycle");
		}
		fs::create_directories(destination);
		for (const auto &entry : fs::directory_iterator(source)) {
			const auto childRelative = relative.empty() ? fs::path(entry.path().filename())
								       : relative / entry.path().filename();
			CopyConfigSnapshot(entry.path(), destination / entry.path().filename(), active, skipped,
					   childRelative);
		}
		active.erase(canonical);
	} else if (fs::is_regular_file(source)) {
		fs::create_directories(destination.parent_path());
		try {
			fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
		} catch (const fs::filesystem_error &) {
			// The file is unreadable (for example, locked by another
			// application). Skip it rather than abort the import.
			skipped.push_back(relative);
		}
	} else {
		throw std::runtime_error("Configuration contains a file that cannot be safely copied");
	}
}

// The caller holds the frontend migration lock. Overrides pair an original
// settings path with its relative destination inside the independent copy.
inline ConfigMigrationResult
MigrateAppConfig(const std::filesystem::path &source, const std::filesystem::path &destination,
		 const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> &overrides,
		 const std::function<void(const std::filesystem::path &)> &finalize,
		 std::vector<std::filesystem::path> *skippedOut = nullptr)
{
	namespace fs = std::filesystem;
	if (fs::symlink_status(destination).type() != fs::file_type::not_found) {
		return ConfigMigrationResult::Existing;
	}
	if (!fs::exists(source)) {
		return ConfigMigrationResult::NoSource;
	}
	const auto staging = destination.parent_path() / (destination.filename().string() + ".migration");
	fs::create_directories(destination.parent_path());
	if (!fs::create_directory(staging)) {
		throw std::runtime_error("An unfinished configuration migration already exists");
	}
	struct Cleanup {
		fs::path path;
		~Cleanup()
		{
			std::error_code error;
			fs::remove_all(path, error);
		}
	} cleanup{staging};
	std::set<fs::path> active;
	std::vector<fs::path> skipped;
	CopyConfigSnapshot(source, staging, active, skipped);
	// The global settings are the anchor of the import; everything else can
	// be missing and the user still has a working profile.
	if (fs::exists(source / "global.ini") && !fs::exists(staging / "global.ini")) {
		throw std::runtime_error(
			"the global.ini settings file could not be copied; close the other application using it and try again");
	}
	// Session markers describe the other app, not transferable user settings.
	fs::remove_all(staging / ".sentinel");
	for (const auto &[original, relative] : overrides) {
		if (relative.empty() || relative.is_absolute()) {
			throw std::runtime_error("Configuration override must be a relative settings path");
		}
		for (const auto &part : relative) {
			if (part == "..") {
				throw std::runtime_error("Configuration override escapes the settings directory");
			}
		}
		fs::remove_all(staging / relative);
		if (fs::exists(original)) {
			CopyConfigSnapshot(original, staging / relative, active, skipped);
		}
	}
	if (skippedOut) {
		// Append: MigrateAppConfigLocations collects the primary root's and
		// every selected root's skipped entries into one list.
		skippedOut->insert(skippedOut->end(), skipped.begin(), skipped.end());
	}
	finalize(staging);
	fs::rename(staging, destination);
	return ConfigMigrationResult::Imported;
}

// Import each selected storage root into its own Rust namespace. The primary
// global config is published last, after its Locations are finalized.
inline ConfigMigrationResult
MigrateAppConfigLocations(const std::filesystem::path &base, const std::vector<std::filesystem::path> &selected,
			  const std::function<void(const std::filesystem::path &)> &finalize,
			  std::vector<std::filesystem::path> *skippedOut = nullptr)
{
	namespace fs = std::filesystem;
	const auto primary = fs::canonical(base);
	std::vector<fs::path> created;
	std::vector<fs::path> skipped;
	try {
		const auto result = MigrateAppConfig(base / "obs-studio", base / "obs-studio-rust", {}, [&](const fs::path &copy) {
			for (const auto &root : selected) {
				if (fs::canonical(root) == primary) {
					continue;
				}
				const auto destination = root / "obs-studio-rust";
				auto result =
					MigrateAppConfig(root / "obs-studio", destination, {}, [](const fs::path &) {}, &skipped);
				if (result == ConfigMigrationResult::Imported) {
					created.push_back(destination);
				}
			}
			finalize(copy);
		}, &skipped);
		if (skippedOut) {
			*skippedOut = std::move(skipped);
		}
		return result;
	} catch (...) {
		// Only directories created by this transaction are rolled back.
		// Existing Rust roots and all original settings are untouched.
		for (auto it = created.rbegin(); it != created.rend(); ++it) {
			std::error_code error;
			fs::remove_all(*it, error);
		}
		throw;
	}
}
