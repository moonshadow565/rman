```sh
Usage: rbun-add.exe [options] output input

Adds one or more bundles into first first bundle.

Positional arguments:
output          Bundle file to write into. [required]
input           Bundle file or folder to write from.

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
--buffer        Size for buffer before flush to disk in killobytes [64, 1048576] [default: 33554432]
```

```sh
Usage: rbun-chk.exe [options] input

Checks one or more bundles for errors.

Positional arguments:
input           Bundle file or folder to read from.

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
--no-extract    Do not even attempt to extract chunk. [default: false]
--no-hash       Do not verify hash. [default: false]
```

```sh
Usage: rbun-ls.exe [options] input

Lists contents of one or more bundles.
Output is in CSV format as follows:
BundlID,ChunkID,SizeCompressed,SizeUncompressed

Positional arguments:
input           Bundle file or folder to read from.

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
```

```sh
Usage: rbun-usage.exe [options] input

Collects size usage statistics on one or more bundle.

Positional arguments:
input           Bundle file(s) or folder to read from.

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
```

```sh
Usage: rman [options] action manifest

Positional arguments:
action                  action: list, bundles, chunks, download [required]
manifest                .manifest or .json [required]

Optional arguments:
-h --help               shows help message and exits [default: false]
-v --version            prints version information and exits [default: false]
-v --verify             Skip: verified chunks. [default: false]
-e --exist              Skip: existing files. [default: false]
-n --nowrite            Skip: writing files to disk. [default: false]
-l --lang               Filter: language(none for international files). [default: ""]
-p --path               Filter: path with regex match. [default: <not representable>]
-o --output             Directory: to store and verify files from. [default: "."]
-d --download           Url: to download from. [default: "http://lol.secure.dyn.riotcdn.net/channels/public"]
-a --archive            Directory: to use as cache archive for bundles. [default: ""]
-m --mode               Mode: of range downloading: full, one, multi. [default: <not representable>]
-r --retry              Number: of retrys for failed bundles. [default: 0]
-c --connections        Number: of connections per downloaded file. [default: 64]
--curl-verbose          Curl: verbose logging. [default: false]
--curl-buffer           Curl: buffer size in bytes [1024, 524288]. [default: 0]
--curl-proxy            Curl: proxy. [default: ""]
--curl-useragent        Curl: user agent string. [default: ""]
--curl-cookiefile       Curl: cookie file or '-' to disable cookie engine. [default: ""]
--curl-cookielist       Curl: cookie list string. [default: ""]
```

```sh
Usage: rman-bl.exe [options] manifest

Lists bundle names used in manifest.

Positional arguments:
manifest        Manifest file to read from. [required]

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
```

```sh
Usage: rman-dl.exe [options] manifest output

Downloads or repairs files in manifest.

Positional arguments:
manifest                Manifest file to read from. [required]
output                  Output directory to store and verify files from. [default: "."]

Optional arguments:
-h --help               shows help message and exits [default: false]
-v --version            prints version information and exits [default: false]
-l --filter-lang        Filter by language(none for international files) with regex match. [default: <not representable>]
-p --filter-path        Filter by path with regex match. [default: <not representable>]
--no-verify             Force force full without verify. [default: false]
--no-write              Do not write to file. [default: false]
--no-progress           Do not print progress. [default: false]
--cache                 Cache file path. [default: ""]
--cache-readonly        Do not write to cache. [default: false]
--cache-buffer          Size for cache buffer in killobytes [64, 1048576] [default: 33554432]
--cdn                   Source url to download files from. [default: "http://lol.secure.dyn.riotcdn.net/channels/public"]
--cdn-retry             Number of retries to download from url. [default: 3]
--cdn-workers           Number of connections per downloaded file. [default: 32]
--cdn-verbose           Curl: verbose logging. [default: false]
--cdn-buffer            Curl buffer size in killobytes [1, 512]. [default: 512]
--cdn-proxy             Curl: proxy. [default: ""]
--cdn-useragent         Curl: user agent string. [default: ""]
--cdn-cookiefile        Curl cookie file or '-' to disable cookie engine. [default: ""]
--cdn-cookielist        Curl: cookie list string. [default: ""]
```

```sh
Usage: rman-ls.exe [options] manifest

Lists files in manifest.
Output is in CSV format as follows:
Path,Size,ID,Lang1;Lang2;Lang3...

Positional arguments:
manifest                Manifest file to read from. [required]

Optional arguments:
-h --help               shows help message and exits [default: false]
-v --version            prints version information and exits [default: false]
-l --filter-lang        Filter: language(none for international files). [default: <not representable>]
-p --filter-path        Filter: path with regex match. [default: <not representable>]
```
