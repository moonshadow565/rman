Set of CLI tools for rito manifest and bundle files

```sh
Usage: rbun-chk.exe [options] input 

Checks one or more bundles for errors.

Positional arguments:
input         	Bundle file(s) or folder(s) to read from. [required]

Optional arguments:
-h --help     	shows help message and exits [default: false]
-v --version  	prints version information and exits [default: false]
--no-extract  	Do not even attempt to extract chunk. [default: false]
--no-hash     	Do not verify hash. [default: false]
--no-progress 	Do not print progress to cerr. [default: false]
```

```sh
Usage: rbun-ex.exe [options] output input 

Extracts one or more bundles.

Positional arguments:
output        	Directory to write chunks into. [required]
input         	Bundle file(s) or folder(s) to read from. [required]

Optional arguments:
-h --help     	shows help message and exits [default: false]
-v --version  	prints version information and exits [default: false]
--with-offset 	Put hex offset in name. [default: false]
-f --force    	Force overwrite existing files. [default: false]
--no-hash     	Do not verify hash. [default: false]
--no-progress 	Do not print progress to cerr. [default: false]
```

```sh
Usage: rbun-ls.exe [options] input 

Lists contents of one or more bundles.

Positional arguments:
input        	Bundle file(s) or folder(s) to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
--format     	Format output. [default: "{bundleId},{chunkId},{compressedSize},{uncompressedSize}"]
```

```sh
Usage: rbun-merge.exe [options] output input 

Adds one or more bundles into first first bundle.

Positional arguments:
output        	Bundle file to write into. [required]
input         	Bundle file(s) or folder to write from. [required]

Optional arguments:
-h --help     	shows help message and exits [default: false]
-v --version  	prints version information and exits [default: false]
--no-extract  	Do not even attempt to extract chunk. [default: false]
--no-hash     	Do not verify hash. [default: false]
--no-progress 	Do not print progress to cerr. [default: false]
--buffer      	Size for buffer before flush to disk in killobytes [64, 1048576] [default: 33554432]
```

```sh
Usage: rbun-usage.exe [options] input 

Collects size usage statistics on one or more bundle.

Positional arguments:
input        	Bundle file(s) or folder(s) to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
```

```sh
Usage: rman-bl.exe [options] manifest 

Lists bundle names used in manifest.

Positional arguments:
manifest     	Manifest file to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
--format     	Format output. [default: "/{bundleId}.bundle"]
```

```sh
Usage: rman-dl.exe [options] manifest output 

Downloads or repairs files in manifest.

Positional arguments:
manifest         	Manifest file to read from. [required]
output           	Output directory to store and verify files from. [default: "."]

Optional arguments:
-h --help        	shows help message and exits [default: false]
-v --version     	prints version information and exits [default: false]
-l --filter-lang 	Filter by language(none for international files) with regex match. [default: <not representable>]
-p --filter-path 	Filter by path with regex match. [default: <not representable>]
--no-verify      	Force force full without verify. [default: false]
--no-write       	Do not write to file. [default: false]
--no-progress    	Do not print progress. [default: false]
--cache          	Cache file path. [default: ""]
--cache-readonly 	Do not write to cache. [default: false]
--cache-buffer   	Size for cache buffer in killobytes [64, 1048576] [default: 33554432]
--cdn            	Source url to download files from. [default: "http://lol.secure.dyn.riotcdn.net/channels/public"]
--cdn-retry      	Number of retries to download from url. [default: 3]
--cdn-workers    	Number of connections per downloaded file. [default: 32]
--cdn-interval   	Curl poll interval in miliseconds. [default: 100]
--cdn-verbose    	Curl: verbose logging. [default: false]
--cdn-buffer     	Curl buffer size in killobytes [1, 512]. [default: 512]
--cdn-proxy      	Curl: proxy. [default: ""]
--cdn-useragent  	Curl: user agent string. [default: ""]
--cdn-cookiefile 	Curl cookie file or '-' to disable cookie engine. [default: ""]
--cdn-cookielist 	Curl: cookie list string. [default: ""]
```

```sh
Usage: rman-ls.exe [options] manifest 

Lists files in manifest.

Positional arguments:
manifest         	Manifest file to read from. [required]

Optional arguments:
-h --help        	shows help message and exits [default: false]
-v --version     	prints version information and exits [default: false]
--format         	Format output. [default: "{path},{size},{fileId},{langs}"]
-l --filter-lang 	Filter: language(none for international files). [default: <not representable>]
-p --filter-path 	Filter: path with regex match. [default: <not representable>]
```

```sh
Usage: rman-make.exe [options] outmanifest outbundle rootfolder input 

Lists bundle names used in manifest.

Positional arguments:
outmanifest   	Manifest to write into. [required]
outbundle     	Bundle file to write into. [required]
rootfolder    	Root folder to rebase from. [required]
input         	Files or folders for manifest. [required]

Optional arguments:
-h --help     	shows help message and exits [default: false]
-v --version  	prints version information and exits [default: false]
-a --append   	Do not print progress. [default: false]
--no-progress 	Do not print progress. [default: false]
--chunk-size  	Chunk size in kilobytes. [default: 256]
--level       	Compression level for zstd. [default: 6]
--buffer      	Size for buffer before flush to disk in killobytes [64, 1048576] [default: 33554432]
```

