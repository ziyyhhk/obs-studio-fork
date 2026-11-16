#include <utility/ConfigMigration.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void write(const fs::path &path, const std::string &text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

static std::string read(const fs::path &path)
{
	std::ifstream stream(path);
	return std::string(std::istreambuf_iterator<char>(stream), {});
}

static void require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		return 2;
	}
	const fs::path root(argv[1]);
	if (fs::exists(root)) {
		return 2;
	}
	fs::create_directories(root);
	struct Cleanup {
		fs::path path;
		~Cleanup()
		{
			std::error_code error;
			fs::remove_all(path, error);
		}
	} cleanup{root};
	const std::string mode(argv[2]);
	const auto source = root / "obs-studio";
	const auto destination = root / "obs-studio-rust";
	try {
		if (mode == "locations" || mode == "locations-existing" || mode == "locations-failure") {
			const auto selected = root / "selected";
			write(source / "global.ini", "original global");
			write(selected / "obs-studio/user.ini", "selected configuration");
			if (mode == "locations-existing") {
				write(selected / "obs-studio-rust/user.ini", "existing selected Rust");
			}
			bool failed = false;
			try {
				MigrateAppConfigLocations(root, {root, selected}, [&](const fs::path &copy) {
					if (mode == "locations-failure") {
						throw std::runtime_error(
							"injected failure after other roots were copied");
					}
					write(copy / "global.ini", selected.u8string());
				});
			} catch (const std::runtime_error &) {
				failed = true;
			}
			if (mode == "locations-failure") {
				require(failed && !fs::exists(destination) && !fs::exists(selected / "obs-studio-rust"),
					"failed multi-root import left a published config");
			} else {
				const auto expected = mode == "locations-existing" ? "existing selected Rust"
										   : "selected configuration";
				require(!failed && read(selected / "obs-studio-rust/user.ini") == expected,
					"selected storage root was lost or existing Rust settings overwritten");
				require(read(destination / "global.ini") == selected.u8string(),
					"storage choice was not preserved");
			}
			require(read(source / "global.ini") == "original global", "original global modified");
			require(read(selected / "obs-studio/user.ini") == "selected configuration",
				"original selected config modified");
			return 0;
		}
		if (mode == "volatile") {
			write(source / "plugin_config/obs-browser/cef.123/Network/Cookies", "cookie");
			write(source / "plugin_config/obs-browser/cef.123/Network/Cookies-journal", "cookie journal");
			write(source / "plugin_config/obs-browser/cef.123/Cache/data_0", "cache");
			write(source / "plugin_config/obs-browser/cef.123/cache_data/data_1", "cache");
			write(source / "plugin_config/obs-browser/cef.123/Code Cache/js/0", "js");
			write(source / "plugin_config/obs-browser/cef.123/GPUCache/0", "gpu");
			write(source / "plugin_config/obs-browser/cef.123/GrShaderCache/0", "shader");
			write(source / "plugin_config/obs-browser/cef.123/DawnCache/0", "dawn");
			write(source / "plugin_config/obs-browser/cef.123/IndexedDB/db", "db");
			write(source / "plugin_config/obs-browser/cef.123/Session Storage/1", "ss");
			write(source / "plugin_config/obs-browser/cef.123/Service Worker/worker", "sw");
			write(source / "plugin_config/obs-browser/cef.123/blob_storage/blob", "blob");
			write(source / "plugin_config/obs-browser/cef.123/Crashpad/db", "crash");
			write(source / "plugin_config/obs-browser/settings.ini", "[browser]\nkeep=1");
			write(source / "plugin_config/obs-websocket/enabled.txt", "keep ws");
			write(source / "plugin_config/obs-browser/not-a-cache/keep.txt", "keep me");
		}
		if (mode != "absent") {
			write(source / "global.ini", "upstream global");
			write(source / "user.ini", "upstream user");
			write(source / ".sentinel/run_old", "upstream active session");
			write(source / "basic/scenes/original.json", "upstream scene");
		}
		if (mode == "existing") {
			write(destination / "global.ini", "existing Rust");
		}
		const auto custom = root / "custom/obs-studio/basic/scenes";
		if (mode == "custom") {
			write(custom / "chosen.json", "custom scene");
		}
		std::vector<std::pair<fs::path, fs::path>> overrides;
		if (mode == "custom" || mode == "empty-custom") {
			overrides.emplace_back(custom, "basic/scenes");
		}
		bool finalized = false;
		bool failed = false;
		ConfigMigrationResult result = ConfigMigrationResult::NoSource;
		try {
			result = MigrateAppConfig(source, destination, overrides, [&](const fs::path &copy) {
				finalized = true;
				require(read(copy / "global.ini") == "upstream global", "global settings missing");
				if (mode == "failure") {
					throw std::runtime_error("injected finalization failure");
				}
				write(copy / "global.ini", "isolated Locations");
			});
		} catch (const std::runtime_error &) {
			failed = true;
		}
		if (mode == "failure") {
			require(failed && finalized, "finalization failure was not propagated");
			require(!fs::exists(destination), "failed migration exposed partial config");
		} else if (mode == "absent") {
			require(result == ConfigMigrationResult::NoSource && !finalized, "absent source imported");
			require(!fs::exists(destination), "absent source created config");
		} else if (mode == "existing") {
			require(result == ConfigMigrationResult::Existing && !finalized, "existing config imported");
			require(read(destination / "global.ini") == "existing Rust", "existing Rust overwritten");
		} else {
			require(result == ConfigMigrationResult::Imported && finalized, "first run did not import");
			require(!fs::exists(destination / ".sentinel"), "upstream crash sentinel was imported");
			require(read(destination / "user.ini") == "upstream user", "user settings missing");
			if (mode == "custom") {
				require(read(destination / "basic/scenes/chosen.json") == "custom scene",
					"custom root ignored");
				require(!fs::exists(destination / "basic/scenes/original.json"),
					"unselected scenes retained");
			} else if (mode == "empty-custom") {
				require(!fs::exists(destination / "basic/scenes"),
					"empty custom root mixed with defaults");
			} else if (mode == "volatile") {
				const auto browser = destination / "plugin_config/obs-browser/cef.123";
				require(!fs::exists(browser / "Network/Cookies"), "volatile cookie was imported");
				require(!fs::exists(browser / "Network/Cookies-journal"),
					"volatile cookie journal was imported");
				require(!fs::exists(browser / "Cache"), "volatile cache was imported");
				require(!fs::exists(browser / "cache_data"), "volatile cache_data was imported");
				require(!fs::exists(browser / "Code Cache"), "volatile code cache was imported");
				require(!fs::exists(browser / "GPUCache"), "volatile gpu cache was imported");
				require(!fs::exists(browser / "GrShaderCache"), "volatile shader cache was imported");
				require(!fs::exists(browser / "DawnCache"), "volatile dawn cache was imported");
				require(!fs::exists(browser / "IndexedDB"), "volatile indexeddb was imported");
				require(!fs::exists(browser / "Session Storage"), "volatile session storage was imported");
				require(!fs::exists(browser / "Service Worker"),
					"volatile service worker storage was imported");
				require(!fs::exists(browser / "blob_storage"), "volatile blob storage was imported");
				require(!fs::exists(browser / "Crashpad"), "volatile crashpad was imported");
				require(read(destination / "plugin_config/obs-browser/settings.ini") == "[browser]\nkeep=1",
					"browser plugin settings lost");
				require(read(destination / "plugin_config/obs-websocket/enabled.txt") == "keep ws",
					"websocket plugin settings lost");
				require(read(destination / "plugin_config/obs-browser/not-a-cache/keep.txt") == "keep me",
					"non-volatile plugin file skipped");
			} else {
				require(read(destination / "basic/scenes/original.json") == "upstream scene",
					"scene missing");
			}
			write(destination / "user.ini", "Rust changed");
		}
		if (mode != "absent") {
			require(read(source / "global.ini") == "upstream global", "upstream global changed");
			require(read(source / "user.ini") == "upstream user", "upstream user changed");
		}
		require(!fs::exists(root / "obs-studio-rust.migration"), "migration staging directory left behind");
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
