# icc-brightness

Monitoning brightness change and
control OLED display brightness by applying ICC color profiles

## Compile & install

```sh
# setup compiling enviroment
sudo apt install build-essential liblcms2-dev libcolord-dev
# compile
make
# install
make install
```

## How does this program work

```mermaid
graph TD;
1([monitoring brightness change with inotify])-->
2([when brightness changes])-->
3([connect to colord daemon]) -->
4([find all display device]) -->
5([connect to first device])-->
6([create icc profile wtih vcgt data which can change brightness])-->
7([save icc profile to tmp dir])-->
8([device add profile])-->
10([device set profile default])-->
11([remove previous profile if it is created by us])-->2
```

## Extra

vscode clangd dev

```sh
bear -- gcc -o icc-brightness ./src/icc-brightness.c $(pkg-config --cflags --libs colord lcms2 uuid) -Wall
```

inotify example

- <https://github.com/asadzia/Inotify-API/blob/master/Inotify.c>

get brightness example

- <https://github.com/baskerville/backlight/blob/master/backlight.c>

change brightness by creating icc with vcgt data examples

- <https://github.com/udifuchs/icc-brightness>
- <https://github.com/zb3/gnome-gamma-tool>

colord

- <https://github.com/hughsie/colord>

getopt_long example

- <https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html>
