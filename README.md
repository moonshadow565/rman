List or download rito manifests and more.

```sh
Usage: fckrman [options] action manifest

Positional arguments:
action          action: list, download, json[Required]
manifest        .manifest or .json[Required]

Optional arguments:
-h --help       show this help message and exit
-v --verify     Skip: verified chunks
-e --exist      Skip: existing files.
-l --lang       Filter: language(none for international files).
-p --path       Filter: path regex
-u --update     Filter: update from old manifest.
-r --retry      Number of retrys for failed bundles
-d --download   Url: to download from.
-o --output     Directory: output
```
