List or download rito manifests and more.

```sh
Usage: fckrman [options] action manifest 

Positional arguments:
action                  action: list, bundles, chunks, download, json[Required]
manifest                .manifest or .json[Required]

Optional arguments:
-h --help               show this help message and exit
-v --verify             Skip: verified chunks.
-e --exist              Skip: existing files.
-n --nowrite            Skip: writing files to disk.
-l --lang               Filter: language(none for international files).
-p --path               Filter: path with regex match.
-u --update             Filter: update from old manifest.
-o --output             Directory: to store and verify files from.
-d --download           Url: to download from.
-a --archive            Directory: to use as cache archive for bundles.
-m --mode               Mode: of range downloading: full, one, multi.
-r --retry              Number: of retrys for failed bundles.
-c --connections        Number: of connections per downloaded file.
--curl-verbose          Curl: verbose logging.
--curl-buffer           Curl: buffer size in bytes [1024, 524288].
--curl-proxy            Curl: proxy.
--curl-useragent        Curl: user agent string.
--curl-cookiefile       Curl: cookie file or '-' to disable cookie engine.
--curl-cookielist       Curl: cookie list string.
```
