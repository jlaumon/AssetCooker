# Asset Cooker


![Asset Cooker](data/icons/chef-hat-heart/chef-hat-heart.png)

Asset Cooker is a build system aimed at game assets, for custom engines. 

It's FAST! It generally starts in less than a second, even with hundred of thousands of files. 

It uses Windows' USN journals to robustly track which files change, and only cook what needs to be cooked. 

[Release Trailer](https://www.youtube.com/watch?v=hvbVC4m6BOo)<br/>
[![Asset Cooker - Release Trailer](https://img.youtube.com/vi/hvbVC4m6BOo/0.jpg)](https://www.youtube.com/watch?v=hvbVC4m6BOo)

## Motivation 

Game engines generally want their data in a different format than the one used by authoring tools (eg. .dds rather than .psd for textures) but while there are some tools and libraries to do that conversion, there isn't really an efficient, engine agnostic and open source build system for running these conversion commands. 

> Let's use CMake for game assets<BR>
>  \- sickos

Builds systems meant for code can somewhat be used for this but usually fall short for several reasons:
- They need to check the state of the entire base every time they run, which can be prohibitively slow (and frustrating, especially when there is actually nothing to do)
- They use file times to check what needs to be built, which is not very reliable in non-code file workflows (eg. copy pasting files from explorer copies the timestamps)

In addition, game assets often need a bit more flexibility, hackability and debuggability than eg. building many cpp files.

## Enters Asset Cooker

Leave it running in the background and it will cook assets as soon as files change. Close it, and it will know if files have changed when it starts again, thanks to the USN journals. 

It's got a nice UI with many buttons. It lists all the files, all the commands, which ones are dirty, their output, their dependencies, etc.

It's simple to use, define rules for cooking assets in TOML or LUA and look at it go. Single exe, no dependencies.

It is fast. 

## Getting Started

### Building

See the [Releases](https://github.com/jlaumon/AssetCooker/releases) section to get a pre-built executable. 

Otherwise:
- Clone and **init the submodules**.
- Run `premake.bat` to generate AssetCooke.sln, and use Visual Studio to compile it.

### Running

You will need to create **two files** before Asset Cooker can do anything: a config file and a rules file. 

A minimal example is provided in the example/minimal directory. You can copy AssetCooker.exe there to play with it. Read on for the explanations.

#### The Config File

The config file must be named `config.toml` and be placed in the current directory (the directory from which Asset Cooker is launched).

It contains a few settings and the list of Repos, which are the root folders watched by Asset Cooker.

```toml
# Path to the rule file (optional)
# Extension can be toml or lua.
RuleFile = "rules.toml" 

# Path to the log directory (optional)
LogDirectory = "Logs"

# Path to the cache directory (optional)
CacheDirectory = "Cache"

# Window title (optional)
WindowTitle = "Asset Cooker" 

# Repo (array, mandatory)
[[Repo]]
Name = "Source" # Name of the Repo (mandatory). Must be unique.
Path = 'data/source' # Path to the repo (mandatory). Can be absolute or relative to current directory.

[[Repo]]
Name = "Bin"
Path = 'data/bin'
NoOrphanFiles = true # True if the Repo should only contain files that are used by Commands (optional, default: false). 
                     # See Tools -> Find Orphan Files to delete files that are not used by any Command.
```

It is generally a good idea to have at least one Repo for inputs and one for outputs (and perhaps also one for intermediate files).

### The Rules File

The rules file contains the rules that tell Asset Cooker what to do with the files in its Repos.

By default Asset Cooker will look for `rules.toml` in the current directory, but this is configurable in `config.toml`. The file can be in toml (simpler) or lua (more powerful if you have many rules).

Here is an example of rule to convert any PNG/TGA file ending with `_albedo` to a BC1 DDS, using [TexConv](https://github.com/microsoft/DirectXTex/wiki/Texconv).

```toml
[[Rule]]
Name = "Texture BC1"
InputFilters = [ 
    { Repo = "Source", PathPattern = "*_albedo.png" },
    { Repo = "Source", PathPattern = "*_albedo.tga" },
]
CommandLine = '{ Repo:Tools }texconv.exe -y -nologo -sepalpha -dx10 -m 8 -f BC1_UNORM_SRGB -o "{ Repo:Bin }{ Dir }" "{ Repo:Source }{ Path }"'
OutputPaths = [ '{ Repo:Bin }{ Dir }{ File }.dds' ]
```

The `InputFilters` is what determines which files will be considered by the rule. Here it's only files in the `Source` Repo. Note the `*` wildcard in the `PathPattern` which can match any sequence of characters (`?` is also supported for matching a single character). `InputFilter` is an array, so you can have as many as you want for one rule (instead of using complicated regexes).

The `CommandLine` is what will be run for every matching file. It's a format string which supports a small set of extra arguments (the Command Variables, [full list below](#command-variables-reference). For example here `{ Repo:Tools }` will be replaced by the path of the Repo named "Tools", and `{ Path }` will be replaced by the path of the matched input file (relative to its Repo).

The `OutputPaths` is the expected output of that command. It's an array in case there are several, but here there's a single one.

Here's the same example in `lua` (without taking any advantage of lua's programmability):

```lua
Rule = {
    {
        Name = "Texture BC1",
        InputFilters = {
            { Repo = "Source", PathPattern = "*_albedo.png" },
            { Repo = "Source", PathPattern = "*_albedo.tga" },
        },
        CommandLine = '{ Repo:Tools }texconv.exe -y -nologo -sepalpha -dx10 -m 8 -f BC1_UNORM_SRGB -o "{ Repo:Bin }{ Dir }" "{ Repo:Source }{ Path }"',
        OutputPaths = { '{ Repo:Bin }{ Dir }{ File }.dds' },
    }
}
```

#### Rule Reference

Here is the full list of variables supported by Rules.

| Variable           | Type              | Default Value | Description                                                                                                                                                                  |
|--------------------|-------------------|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Name               | string            |               | Name used to identify the rule int the UI.                                                                                                                                   |
| Priority           | int               | 0             | Specifies the order in which commands are executed. Lower numbers first.                                                                                                     |
| Version            | int               | 0             | Change this value to force all commands to run again.                                                                                                                        |
| MatchMoreRules     | bool              | false         | If true, files matched by this rule will also be tested against other rules. Rules are tested in declaration order.                                                          |
| CommandType        | string            | "CommandLine" | The type of command to run.<br>`"CommandLine"`: The user-provided command line is run (see CommandLine).<br>`"CopyFile"`: The matched input file is copied to OutputPath[0]. |
| CommandLine        | string            |               | The command line to run (if CommandType is `"CommandLine"`). Supports [Command Variables](#command-variables-reference).                                                     |
| InputFilters       | InputFilter array |               | The filters used to match input files. See [InputFilter](#inputfilter-reference). Must contain at least one InputFilter.                                                     |
| InputPaths         | string array      | empty         | Extra inputs for the command. Supports [Command Variables](#command-variables-reference).                                                                                    |
| OutputPaths        | string array      | empty         | Outputs of the command. Supports [Command Variables](#command-variables-reference).                                                                                          |
| DepFile            | DepFile           | empty         | The DepFile description, if a dep file should be used. See [DepFile](#depfile-reference).                                                                                    |

#### InputFilter Reference

Here is the full list of variables supported by InputFilters.

| Variable    | Type   | Description                                                                                                               |
|-------------|--------|---------------------------------------------------------------------------------------------------------------------------|
| Repo        | string | Name of the Repo the file must be from.                                                                                   |
| PathPattern | string | A pattern to match the file path against. Supports wildcards `*` (any sequence of characters) and `?` (single character). |

#### DepFile Reference

Here is the full list of variables supported by DepFiles.

| Variable    | Type              | Default Value | Description                                                                                                                                                                  |
|-------------|-------------------|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Path        | string            |               | The path of the Dep File. Supports [Command Variables](#command-variables-reference).                                                                                        |
| Format      | string            | "AssetCooker" | The format of Dep File to expect.<br>`"AssetCooker"`: The [AssetCooker custom Dep File format](#asset-cooker-depfile-format).<br>`"Make"`: The standard make .d file format (supported by many compilers).   |
| CommandLine | string            | ""            | An optional command line to generate the DepFile (if the main CommandLine cannot generate it directly). Supports [Command Variable](#command-variables-references).          |

##### Asset Cooker DepFile Format

The "AssetCooker" DepFile format is very simple: one dependency per line, inputs are preceded by `INPUTS:`, outputs by `OUTPUT:`. Relative paths are accepted.

Example:
```
INPUT: D:/path/to/input.txt
INPUT: D:/other/input.txt
OUTPUT: D:/outputs/file.txt
```


### Command Variables Reference

Here is the full list of Command Variables supported paths and command lines in Rules.

In the texture compression example above, consider a setup where the Repo `Source` is pointing to `D:\bin` and the matched file is `D:\source\textures\brick_albedo.png`.

| Variable                  | Will be replaced by                                                | Example                     |
|---------------------------|--------------------------------------------------------------------|-----------------------------|
| `{ Ext }`                 | The extension of the input file.                                   | `.png`                      |
| `{ File }`                | The name of the input file (without extension).                    | `brick_albedo`              |
| `{ Dir }`                 | The directory part of the input file path.                         | `textures\`                 |
| `{ Dir_NoTrailingSlash }` | The directory part of the input file path, without trailing slash. | `textures`                  |
| `{ Path }`                | The path of the input file.                                        | `textures\brick_albedo.png` |
| `{ Repo:Bin }`            | The path of the Repo named "Source".                               | `D:\bin`                    |

So the OutputPath described as `'{ Repo:Bin }{ Dir }{ File }.dds'` will become `D:\bin\textures\brick_albedo.dds`.

#### Slicing

Command Variables also support a slice syntax similar to Python (eg. `{ File[1:3] }`).
- The supported parameters are `[start:end]`
- They are optional (default value for `start` is 0, default value for `end` is the length of the string)
- They can index out-of-bounds (index is clamped)
- They can be negative (index from the end of the string instead).

| Example                | Result             |
|------------------------|--------------------|
| `{ File }`             | `brick_albedo`     |
| `{ File[:5] }`         | `brick`            |
| `{ File[6:] }`         | `albedo`           |
| `{ File[5:6] }`        | `_`                |
| `{ File[:-6] }`        | `albedo`           |
| `{ File[1:1000] }`     | `rick_albedo`      |

## Command Line Options

- `-working_dir some/path`: Use `some/path` as the working directory (Current Directory in Windows terminology). The Config File is read from there, all relative paths are relative to there. Accepts both relative and absolute paths. 
- `-no_ui`: Run without UI, cook everything then exit. Exit code is 0 on success. 
- `-test`: Run unit tests then exit. Exit code is 0 on success. Note: Does nothing when Asset Cooker is compiled in Release mode (tests are disabled). 

## Remote Control API

It's possible to have basic control over Asset Cooker from another process through a C API. With this, your game can start Asset Cooker, wait for it to finish cooking, check if there were errors, etc.

For more information, take a look at the [Remote Control example](examples/RemoteControl/Readme.md) and [AssetCookerAPI.h](api/AssetCookerAPI.h).

## Contributing 
Open an issue before doing a pull request. It's a hobby project, please be nice.

## FAQ

> Is support for other platforms planned?

At this point, no. USN Journals are a Windows' only feature, so adding a new platform is a lot of work.

> Is UTF-8 supported?

Not really. Most of the code is UTF-8 aware, but not all of it. It would also be hard to properly support because NTFS case-insensitivity is rather opaque, and Asset Cooker would need to get that really right.








