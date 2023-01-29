Set of CLI tools for rito manifest and bundle files

```sh
Usage: rbun-chk [options] input 

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
Usage: rbun-ex [options] output input 

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
Usage: rbun-ls [options] input 

Lists contents of one or more bundles.

Positional arguments:
input        	Bundle file(s) or folder(s) to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
--format     	Format output. [default: "{bundleId},{chunkId},{compressedSize},{uncompressedSize}"]
```

```sh
Usage: rbun-merge [options] output input 

Adds one or more bundles into first first bundle.

Positional arguments:
output             	Bundle file to write into. [required]
input              	Bundle file(s) or folder to write from. [required]

Optional arguments:
-h --help          	shows help message and exits [default: false]
-v --version       	prints version information and exits [default: false]
--level-recompress 	Re-compression level for zstd(0 to disable recompression). [default: 0]
--no-extract       	Do not extract and verify chunk hash. [default: false]
--no-progress      	Do not print progress to cerr. [default: false]
--newonly          	Force create new part regardless of size. [default: false]
--buffer           	Size for buffer before flush to disk in megabytes [1, 4096] [default: 32]
--limit            	Size for bundle limit in gigabytes [0, 4096] [default: 4096]
```

```sh
Usage: rbun-usage [options] input 

Collects size usage statistics on one or more bundle.

Positional arguments:
input        	Bundle file(s) or folder(s) to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
```

```sh
Usage: rman-bl [options] manifest 

Lists bundle names used in manifest.

Positional arguments:
manifest     	Manifest file to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
--format     	Format output. [default: "/{bundleId}.bundle"]
```

```sh
Usage: rman-chk [options] inmanifest inbundle 

Splits JRMAN .

Positional arguments:
inmanifest   	Manifest to read from. [required]
inbundle     	Source bundle to read from. [required]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
```

```sh
Usage: rman-dl [options] manifest output 

Downloads or repairs files in manifest.

Positional arguments:
manifest             	Manifest file to read from. [required]
output               	Output directory to store and verify files from. [default: "."]

Optional arguments:
-h --help            	shows help message and exits [default: false]
-v --version         	prints version information and exits [default: false]
-l --filter-lang     	Filter by language(none for international files) with regex match. [default: <not representable>]
-p --filter-path     	Filter by path with regex match. [default: <not representable>]
--no-verify          	Force force full without verify. [default: false]
--no-write           	Do not write to file. [default: false]
--no-progress        	Do not print progress. [default: false]
--cache              	Cache file path. [default: ""]
--cache-readonly     	Do not write to cache. [default: false]
--cache-newonly      	Force create new part regardless of size. [default: false]
--cache-buffer       	Size for cache buffer in megabytes [1, 4096] [default: 32]
--cache-limit        	Size for cache bundle limit in gigabytes [0, 4096] [default: 4]
--cdn                	Source url to download files from. [default: "http://lol.secure.dyn.riotcdn.net/channels/public"]
--cdn-lowspeed-time  	Curl seconds that the transfer speed should be below. [default: 0]
--cdn-lowspeed-limit 	Curl average transfer speed in killobytes per second that the transfer should be above. [default: 64]
--cdn-retry          	Number of retries to download from url. [default: 3]
--cdn-workers        	Number of connections per downloaded file. [default: 32]
--cdn-interval       	Curl poll interval in miliseconds. [default: 100]
--cdn-verbose        	Curl: verbose logging. [default: false]
--cdn-buffer         	Curl buffer size in killobytes [1, 512]. [default: 512]
--cdn-proxy          	Curl: proxy. [default: ""]
--cdn-useragent      	Curl: user agent string. [default: ""]
--cdn-cookiefile     	Curl cookie file or '-' to disable cookie engine. [default: ""]
--cdn-cookielist     	Curl: cookie list string. [default: ""]
```

```sh
Usage: rman-ls [options] manifest 

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
Usage: rman-make [options] outmanifest outbundle rootfolder input 

Lists bundle names used in manifest.

Positional arguments:
outmanifest          	Manifest to write into. [required]
outbundle            	Bundle file to write into. [required]
rootfolder           	Root folder to rebase from. [required]
input                	Files or folders for manifest. [default: {}]

Optional arguments:
-h --help            	shows help message and exits [default: false]
-v --version         	prints version information and exits [default: false]
--append             	Append manifest instead of overwriting. [default: false]
--no-progress        	Do not print progress. [default: false]
--strip-chunks       	[default: false]
--cdc                	Dumb chunking fallback algorithm fixed, bup [default: "fixed"]
--no-ar              	Regex of disable smart chunkers, can be any of: fsb, fsb5, load, mac_exe, mac_fat, pe, wad, wpk, zip [default: ""]
--ar-strict          	Do not fallback to dumb chunking on ar errors. [default: false]
--ar-min             	Smart chunking minimum size in killobytes [1, 4096]. [default: 4]
--chunk-size         	Chunk max size in killobytes [1, 8096]. [default: 1024]
--level              	Compression level for zstd. [default: 6]
--level-high-entropy 	Set compression level for high entropy chunks(0 for no special handling). [default: 0]
--newonly            	Force create new part regardless of size. [default: false]
--buffer             	Size for buffer before flush to disk in megabytes [1, 4096] [default: 32]
--limit              	Size for bundle limit in gigabytes [0, 4096] [default: 4096]
```

```sh
Usage: rman-merge [options] outmanifest manifests 

Merges multiple manifests into one

Positional arguments:
outmanifest      	Manifest to write into. [required]
manifests        	Manifest files to read from. [required]

Optional arguments:
-h --help        	shows help message and exits [default: false]
-v --version     	prints version information and exits [default: false]
--no-progress    	Do not print progress. [default: false]
--strip-chunks   	[default: false]
--cache          	Cache file path. [default: ""]
--cache-newonly  	Force create new part regardless of size. [default: false]
--cache-buffer   	Size for cache buffer in megabytes [1, 4096] [default: 32]
--cache-limit    	Size for cache bundle limit in gigabytes [0, 4096] [default: 4096]
-l --filter-lang 	Filter: language(none for international files). [default: <not representable>]
-p --filter-path 	Filter: path with regex match. [default: <not representable>]
```

```sh
Usage: rman-mount [options] output manifests 

Mounts manifests.

Positional arguments:
output               	output directory to mount in. [required]
manifests            	Manifest files to read from. [required]

Optional arguments:
-h --help            	shows help message and exits [default: false]
-v --version         	prints version information and exits [default: false]
--fuse-debug         	FUSE debug [default: false]
--with-prefix        	Prefix file paths with manifest name [default: false]
-l --filter-lang     	Filter by language(none for international files) with regex match. [default: <not representable>]
-p --filter-path     	Filter by path with regex match. [default: <not representable>]
--cache              	Cache file path. [default: ""]
--cache-readonly     	Do not write to cache. [default: false]
--cache-newonly      	Force create new part regardless of size. [default: false]
--cache-buffer       	Size for cache buffer in megabytes [1, 4096] [default: 32]
--cache-limit        	Size for cache bundle limit in gigabytes [0, 4096] [default: 4]
--cdn                	Source url to download files from. [default: "http://lol.secure.dyn.riotcdn.net/channels/public"]
--cdn-lowspeed-time  	Curl seconds that the transfer speed should be below. [default: 0]
--cdn-lowspeed-limit 	Curl average transfer speed in killobytes per second that the transfer should be above. [default: 64]
--cdn-verbose        	Curl: verbose logging. [default: false]
--cdn-buffer         	Curl buffer size in killobytes [1, 512]. [default: 512]
--cdn-proxy          	Curl: proxy. [default: ""]
--cdn-useragent      	Curl: user agent string. [default: ""]
--cdn-cookiefile     	Curl cookie file or '-' to disable cookie engine. [default: ""]
--cdn-cookielist     	Curl: cookie list string. [default: ""]
```

```sh
Usage: rman-rads [options] outmanifest inmanifest inbundle inrelease 

Splits JRMAN .

Positional arguments:
outmanifest  	Manifest to write into. [required]
inmanifest   	Manifest to read from. [required]
inbundle     	Source bundle to read from. [required]
inrelease    	Project or solution path inside bundle. If bundle is empty treat it as regex instead. [default: ""]

Optional arguments:
-h --help    	shows help message and exits [default: false]
-v --version 	prints version information and exits [default: false]
--append     	Append manifest instead of overwriting. [default: false]
```

```sh
Usage: rman-remake [options] outbundle outmanifest inbundle inmanifests 

Remake manifests by rechunking all file data.

Positional arguments:
outbundle            	Bundle file to write into. [required]
outmanifest          	Manifest to write into. [required]
inbundle             	Input bundle to read from [required]
inmanifests          	Input manifests. [default: {}]

Optional arguments:
-h --help            	shows help message and exits [default: false]
-v --version         	prints version information and exits [default: false]
-l --filter-lang     	Filter: language(none for international files). [default: <not representable>]
-p --filter-path     	Filter: path with regex match. [default: <not representable>]
--resume             	Resume file path used to store processed fileIds. [default: ""]
--resume-buffer      	Size for resume buffer before flush to disk in kilobytes [1, 16384] [default: 64]
--append             	Append manifest instead of overwriting. [default: false]
--no-progress        	Do not print progress. [default: false]
--strip-chunks       	[default: false]
--no-ar              	Regex of disable smart chunkers, can be any of: fsb, fsb5, load, mac_exe, mac_fat, pe, wad, wpk, zip [default: ""]
--ar-strict          	Do not fallback to dumb chunking on ar errors. [default: false]
--cdc                	Dumb chunking fallback algorithm fixed, bup [default: "fixed"]
--ar-min             	Smart chunking minimum size in killobytes [1, 4096]. [default: 4]
--chunk-size         	Chunk max size in killobytes [1, 8096]. [default: 1024]
--level              	Compression level for zstd. [default: 6]
--level-high-entropy 	Set compression level for high entropy chunks(0 for no special handling). [default: 0]
--newonly            	Force create new part regardless of size. [default: false]
--buffer             	Size for buffer before flush to disk in megabytes [1, 4096] [default: 32]
--limit              	Size for bundle limit in gigabytes [0, 4096] [default: 4096]
```

