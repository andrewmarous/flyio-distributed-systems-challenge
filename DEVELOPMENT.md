# C++ development

The project uses Bazel 9.3.0, C++23, and nlohmann/json.
Configure your editor to use clangd.

## Build

```sh
bazel build //lib:maelstrom
```

## Maelstrom integration tests

Maelstrom is an external test tool, not a C++ library dependency.
The setup script pins release 0.2.4 and verifies its archive with SHA-256.
Downloaded files live in `third_party/` and are ignored by Git.

Install Java 17 or newer, Graphviz (`dot`), and gnuplot.
On macOS with Homebrew:

```sh
brew install openjdk@21 graphviz gnuplot
export PATH="$(brew --prefix openjdk@21)/bin:$PATH"
```

Install or restore Maelstrom:

```sh
./tools/setup-maelstrom.sh
```

This script downloads the archive if absent, verifies its checksum, and replaces the extracted installation.
It requires `curl`, `tar`, and either `sha256sum` or `shasum`.

Build and run the full integration test for challenge 1 (echo):

```sh
./tools/test.sh --challenge 1
# Short form:
./tools/test.sh -c 1
```

The challenge flag is required. Challenges 4 through 6 return a not-implemented error.
Other challenge numbers and unknown arguments return a usage error.
Challenge 1 runs the echo workload with one node for 10 seconds.

Run challenge 2:

```sh
./tools/test.sh -c 2
```

Challenge 2 runs `unique-ids` for 30 seconds at rate 1000 with three nodes,
total availability, and network partitions.

Run challenge 3 (single-node broadcast, then five-node broadcast):

```sh
./tools/test.sh -c 3
```

Challenge 3 runs `broadcast` with one node, then five nodes. Each run uses 20 seconds at rate 10.
The single-node run must exit successfully before the five-node run starts.
Its final non-empty output line must exactly match `Everything looks good! ヽ(‘ー`)ノ`.
The same checks apply to the five-node run.
Each run saves its output in `output.log`, beside its `store/` directory.

The wrapper builds the target and resolves its executable through Bazel.
It then runs Maelstrom with an absolute executable path.
Each run writes results beneath `.cache/maelstrom/run-XXXXXX/store/`.
The wrapper prints that path and returns Maelstrom's exit status.

Release 0.2.4 has no `--store` option, so the wrapper selects the output directory through its working directory.
It invokes the Java archive directly to avoid platform-specific behavior in the upstream launcher.

Run unit tests separately:

```sh
bazel test //lib/...
```

## Refresh the compilation database

```sh
bazel run //:refresh_compile_commands
```

This command generates `compile_commands.json` in the repository root.
Run it after changes to source lists, dependencies, or compiler flags.
The generated database contains local paths. Do not commit it.

The `.clangd` file selects this database.
clangd infers header commands from nearby source files.
The extractor creates an `external` symlink for dependency headers.

When you add application targets, add them to the `targets` list in the root `BUILD.bazel`.

## Verify clangd

```sh
clangd --check=lib/message.hpp --compile-commands-dir=.
```

On macOS, install Xcode command-line tools or Xcode.
Bazel selects the compiler and SDK. The database records their paths.

The autoload flag in `.bazelrc` supports the extractor's legacy rules on Bazel 9.
Header extraction is disabled because its cache parser does not support Bazel 9.
