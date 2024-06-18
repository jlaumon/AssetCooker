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

## Config File

This file contains a few settings and the list of Repos, which are the root folders watched by Asset Cooker. The file must be named config.toml and be placed in the current directory (the directory from which Asset Cooker is launched).

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

## Rule File

By default Asset Cooker will look for rule.toml in the current directory, but this is configurable in config.toml. The file can be in toml (simpler) or lua (more powerful if you have many rules). 

The rules format closely follow the structs and variable naming of the C++ (except for the m prefixes on member variables).

```toml

```


## Contributing 
Open an issue before doing a pull request. It's a hobby project, please be nice. 










