# Asset Cooker


![Asset Cooker](data/icons/chef-hat-heart/chef-hat-heart.png)

Asset Cooker is a build system aimed at game assets, for custom engines. It leverages Windows' USN journals to robustly track which files change, and only cook what needs to be cooked.

## Motivation 

Game engines generally want their data in a different format from the one used by authoring tools (eg. .dds rather than .psd for textures) but while there are some tools and libraries to do that conversion, there isn't really an efficient, engine agnostic and open source build system for running these conversion commands. 

> Let's use CMake for game assets<BR>
>  \- sickos

Builds systems meant for code can somewhat be used for this but usually fall short for several reasons:
- They need to check the state of the entire base every time they run, which can be prohibitively slow (and frustrating, especially when there's not actually anything to do)
- They use file times to check what needs to be built, which is not very reliable in non-code file workflows (eg. copy pasting files from explorer copies the timestamps)

In addition, game assets often need a bit more flexibility, hackability and debuggability than eg. building many cpp files.

## Enters Asset Cooker

Leave it running in the background and it will cook assets as soon as files change. Close it, and it will know if files changed when it starts again, thanks to the USN journals. 

It's got a nice UI with many buttons. It lists all the files, all the commands, which ones are dirty, their output, their dependencies, etc.

It's simple to use, define rules for cooking assets in TOML or LUA and look at it go. Single exe, no dependencies.

### Config File

This file must be named `config.toml` and be placed in the current directory (the directory from which Asset Cooker is launched).

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
```

There is no limit on the number of Repos, so it's generally good to have several and separate directories dedicated to inputs from those dedicated to outputs (or intermediate files).

### Rule File

By default Asset Cooker will look for `rule.toml` in the current directory, but this is configurable in `config.toml`. The file can be in toml (simpler) or lua (more powerful if you have many rules). 

The rules format closely follow the structs and variable naming of the C++ (the only notable difference being that the C++ member variables start with an extra `m`).

Before going into details, here is one example of rule to convert any PNG/TGA file ending with `_albedo` to a BC1 DDS, using [TexConv](https://github.com/microsoft/DirectXTex/wiki/Texconv).

```toml
[[Rule]]
Name = "Texture BC1"
InputFilters = [ 
    { Repo = "Source", PathPattern = "*_albedo.png" },
    { Repo = "Source", PathPattern = "*_albedo.tga" },
]
CommandLine = '{ Repo:Tools }texconv.exe -y -nologo -sepalpha -dx10 -m 8 -f BC1_UNORM -o "{ Repo:Bin }{ Dir }" "{ Repo:Source }{ Path }"'
OutputPaths = [ '{ Repo:Bin }{ Dir }{ File }.dds' ]
```

The `InputFilters` is what determines which files will be considered by the rule. Here it's only files in the `Source` Repo. Note the `*` wildcard in the `PathPattern` which can match any sequence of characters (`?` is also supported for matching a single character). `InputFilter` is an array, so you can have as many as you want for one rule (instead of using complicated regexes).

The `CommandLine` is what will be run for every matching file. It's a format string which supports a small set of extra arguments (full list below). For example here `{ Repo:Tools }` will be replaced by the path of the Repo named "Tools", and `{ Path }` will be replaced by the path of the matched input file (relative to its Repo).

The `OutputPaths` is the expected output of that command. It's an array in case there are several, but here there's a single one.

Here's the same example in `lua` (although it is not taking advantage of its programmability):

```lua
Rule = {
    {
        Name = "Texture BC1",
        InputFilters = {
            { Repo = "Source", PathPattern = "*_albedo.png" },
            { Repo = "Source", PathPattern = "*_albedo.tga" },
        },
        CommandLine = '{ Repo:Tools }texconv.exe -y -nologo -sepalpha -dx10 -m 8 -f BC1_UNORM -o "{ Repo:Bin }{ Dir }" "{ Repo:Source }{ Path }"'
        OutputPaths = { '{ Repo:Bin }{ Dir }{ File }.dds' },
    }
}
```

#### Rule Details

```toml

```



## Contributing 
Open an issue before doing a pull request. It's a hobby project, please be nice. 










