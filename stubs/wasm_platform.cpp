// Browser platform glue: exposes the HTTP-served game data as a normal directory tree.
#include <emscripten.h>
#include <emscripten/wasmfs.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static void MkdirP(const std::string& path)
{
	for (size_t i = 1; i <= path.size(); ++i)
		if (i == path.size() || path[i] == '/')
			mkdir(path.substr(0, i).c_str(), 0777);
}

// Called at the very start of main(), on the game thread (PROXY_TO_PTHREAD).
// Files are fetched lazily with HTTP Range requests; only what the game reads is downloaded.
void WasmMountGameData()
{
	char* manifest = (char*)EM_ASM_PTR({
		var x = new XMLHttpRequest();
		x.open('GET', 'data_manifest.txt', false);
		x.send(null);
		if (x.status != 200) return 0;
		return stringToNewUTF8(x.responseText);
	});

	MkdirP("/home/web_user");
	MkdirP("/0ad");
	setenv("HOME", "/home/web_user", 1);
	setenv("ICU_DATA", "/0ad/data/icu", 1);

	if (!manifest)
	{
		fprintf(stderr, "WasmMountGameData: data_manifest.txt not found; game data is unavailable\n");
		return;
	}

	backend_t backend = wasmfs_create_fetch_backend("data", 1024 * 1024);
	wasmfs_create_directory("/0ad/data", 0777, backend);

	int count = 0;
	for (char* line = strtok(manifest, "\n"); line; line = strtok(nullptr, "\n"))
	{
		std::string rel(line);
		while (!rel.empty() && (rel.back() == '\r' || rel.back() == ' ')) rel.pop_back();
		if (rel.empty()) continue;
		std::string full = "/0ad/data/" + rel;
		size_t slash = full.rfind('/');
		MkdirP(full.substr(0, slash));
		wasmfs_create_file(full.c_str(), 0444, backend);
		++count;
	}
	free(manifest);
	fprintf(stderr, "WasmMountGameData: mounted %d files\n", count);
}
