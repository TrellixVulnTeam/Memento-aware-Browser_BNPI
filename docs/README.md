# Memento-aware Browser
A [Chromium](https://www.chromium.org/Home) based Memento-aware web Browser.
## Build and Run
Build instructions for the Memento-aware Browser differ slightly from the [Chromium build instructions](https://www.chromium.org/developers/how-tos/get-the-code).
Chromium uses a package of scripts called [depot_tools](https://dev.chromium.org/developers/how-tos/depottools). To build and run the Memento-aware Browser, you must [install depot_tools](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up).
After installing depot_tools, you can clone the repository and change into the src directory.
```
$ git config http.postBuffer 524288000
$ git clone https://github.com/a-mabe/Memento-aware-Browser.git
$ cd Memento-aware-Browser/src
```

### Linux
After you have installed depot_tools and cloned the repository, assuming you are using Ubuntu, run [install-build-deps.sh](https://chromium.googlesource.com/chromium/src/+/master/build/install-build-deps.sh).
```
$ ./build/install-build-deps.sh
```
Now you can run the Chromium specific hooks, which will then download additional binaries.
```
$ gclient runhooks
```
Now create the build directory by running:
```
$ gn gen out/Default
```
* Ninja updates build files as needed, so you only have to run this once for each new build directory.
* You can replace Default with another name.

Finally, you can build Chromium (the "chrome" target) with Ninja using the command:
```
$ autoninja -C out/Default chrome
```
Once built, run the browser with the following command:
```
$ out/Default/chrome
```
### Windows
After you have installed depot_tools and cloned the repository, you can generate the build directory.
```
$ gn gen out/Default
```
Now you can build Chromium (the "chrome" target) with Ninja using the command:
```
$ autoninja -C out/Default chrome
```
Once built, run the browser with the following command:
```
$ out/Default/chrome
```
### Mac (Not tested)
Create the build directory by running:
```
$ gn gen out/Default
```
* Ninja updates build files as needed, so you only have to run this once for each new build directory.
* You can replace Default with another name.

You can then build Chromium (the "chrome" target) with Ninja using the command:
```
$ autoninja -C out/Default chrome
```
Finally, run the browser:
```
$ out/Default/Chromium.app/Contents/MacOS/Chromium
```
